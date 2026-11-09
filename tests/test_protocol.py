#!/usr/bin/env python3
"""End-to-end protocol tests. Usage: python tests/test_protocol.py [path-to-server]"""
import os
import socket
import subprocess
import sys
import time

PORT = 18080
SERVER = sys.argv[1] if len(sys.argv) > 1 else "./server"
failures = 0


def check(cond, msg):
    global failures
    if not cond:
        failures += 1
        print("FAIL:", msg)


class Client:
    def __init__(self):
        self.s = socket.create_connection(("127.0.0.1", PORT), timeout=5)
        self.buf = b""

    def send(self, text):
        self.s.sendall(text.encode())

    def line(self):
        while b"\n" not in self.buf:
            data = self.s.recv(4096)
            if not data:
                return None
            self.buf += data
        line, self.buf = self.buf.split(b"\n", 1)
        return line.decode()

    def until(self, *wanted):
        """Read lines until one starts with any of `wanted`; returns (hit, all_lines)."""
        seen = []
        while True:
            l = self.line()
            if l is None:
                return None, seen
            seen.append(l)
            for w in wanted:
                if l.startswith(w):
                    return l, seen

    def close(self):
        self.s.close()


def all_cells():
    return [f"{r}{c}" for r in "ABCDEFGHIJ" for c in range(1, 11)]


def test_hello_and_errors():
    c = Client()
    check(c.line() == "HELLO|BATTLESHIP|2", "hello")
    check(c.line() == "STATUS|CHOOSE_MODE", "choose mode")
    c.send("FOO|bar\n")
    check(c.line() == "ERROR|UNKNOWN_COMMAND", "unknown command")
    c.send("SHOOT|A1\n")
    check(c.line() == "ERROR|NO_GAME", "shoot without game")
    c.send("MODE|XYZ\n")
    check(c.line() == "ERROR|INVALID_MODE", "invalid mode")
    c.send("x" * 1000 + "\n")
    check(c.line() == "ERROR|LINE_TOO_LONG", "line too long")
    c.send("\r\n\r\nFOO\r\n")           # blank lines ignored, CRLF accepted
    check(c.line() == "ERROR|UNKNOWN_COMMAND", "crlf")
    c.close()


def test_framing_and_placement():
    c = Client()
    c.until("STATUS|CHOOSE_MODE")
    c.send("MO")                          # split write
    time.sleep(0.1)
    c.send("DE|AI\nPLACE|A1|H\nPLACE|A1|H\n")     # two commands in one segment
    hit, lines = c.until("NEXT_SHIP|Battleship")
    check(hit is not None and "MATCH|AI" in lines and "PLACED|Carrier" in lines, "AI match + first placement")
    hit, lines = c.until("ERROR|")
    check(hit == "ERROR|INVALID_PLACEMENT", "overlapping placement rejected: %s" % lines)
    c.send("PLACE|A1zz|H\n")
    check(c.until("ERROR|")[0] == "ERROR|INVALID_PLACEMENT", "garbage coordinate")
    c.send("PLACE|J10|H\n")
    check(c.until("ERROR|")[0] == "ERROR|INVALID_PLACEMENT", "off the board")
    c.send("SHOOT|B2\n")
    check(c.until("ERROR|")[0] == "ERROR|NOT_PLAYING", "shoot during setup")
    c.send("MODE|AI\n")
    check(c.until("ERROR|")[0] == "ERROR|IN_GAME", "mode while in game")
    c.send("PLACE|RANDOM\n")
    hit, lines = c.until("STATUS|YOUR_TURN")
    check(hit is not None, "random placement starts the game")
    c.close()


def test_ai_game(shoot_batch):
    c = Client()
    c.until("STATUS|CHOOSE_MODE")
    c.send("MODE|AI\nPLACE|RANDOM\n")
    c.until("STATUS|YOUR_TURN")
    c.send("SHOOT|A0\n")
    check(c.until("ERROR|")[0] == "ERROR|INVALID_COORDINATES", "bad coordinate")
    c.send("SHOOT|A1zzz\n")
    check(c.until("ERROR|")[0] == "ERROR|INVALID_COORDINATES", "trailing garbage")

    outcome = None
    for cell in all_cells():
        c.send(f"SHOOT|{cell}\n")
        hit, lines = c.until("STATUS|YOUR_TURN", "STATUS|WIN", "STATUS|LOSE")
        if hit in ("STATUS|WIN", "STATUS|LOSE"):
            outcome = hit
            rest, more = c.until("STATUS|CHOOSE_MODE")
            check(any(l.startswith("REVEAL|") and len(l) == 107 for l in lines + more), "reveal sent")
            break
    check(outcome is not None, "AI game finished")

    # Rematch on the same connection.
    c.send("MODE|AI\n")
    hit, _ = c.until("MATCH|AI")
    check(hit is not None, "rematch after game over")
    c.close()


def play_pvp(a, b):
    """Plays a full PvP game by alternating on whoever has the turn."""
    for x in (a, b):
        x.send("PLACE|RANDOM\n")
    first = {}
    for name, x in (("a", a), ("b", b)):
        hit, _ = x.until("STATUS|YOUR_TURN", "STATUS|OPPONENT_TURN")
        first[name] = hit
    check(sorted(first.values()) == ["STATUS|OPPONENT_TURN", "STATUS|YOUR_TURN"], "exactly one side starts")
    shooter, waiter = (a, b) if first["a"] == "STATUS|YOUR_TURN" else (b, a)

    waiter.send("SHOOT|A1\n")
    check(waiter.until("ERROR|")[0] == "ERROR|NOT_YOUR_TURN", "not your turn enforced")

    cells = {id(a): all_cells(), id(b): all_cells()}
    for _ in range(250):
        cell = cells[id(shooter)].pop(0)
        shooter.send(f"SHOOT|{cell}\n")
        hit, _ = shooter.until("STATUS|OPPONENT_TURN", "STATUS|WIN")
        if hit == "STATUS|WIN":
            check(waiter.until("STATUS|LOSE")[0] is not None, "loser notified")
            return True
        hit, lines = waiter.until("STATUS|YOUR_TURN", "STATUS|LOSE")
        check(any(l.startswith("OPPONENT_SHOT|") for l in lines), "opponent shot reported")
        if hit == "STATUS|LOSE":
            shooter.until("STATUS|WIN")
            return True
        shooter, waiter = waiter, shooter
    return False


def test_pvp():
    a, b = Client(), Client()
    for x in (a, b):
        x.until("STATUS|CHOOSE_MODE")
    a.send("MODE|PVP\n")
    check(a.until("STATUS|WAITING_FOR_OPPONENT")[0] is not None, "first player waits")
    a.send("CHAT|hello\n")
    check(a.until("ERROR|")[0] == "ERROR|NO_OPPONENT", "no chat while waiting")
    b.send("MODE|PVP\n")
    check(b.until("MATCH|PVP")[0] is not None and a.until("MATCH|PVP")[0] is not None, "players matched")
    a.until("STATUS|PLACE_SHIPS"); b.until("STATUS|PLACE_SHIPS")
    a.send("CHAT|hi there\n")
    check(b.until("CHAT|")[0] == "CHAT|hi there", "chat relayed")
    check(play_pvp(a, b), "pvp game completed")
    a.close(); b.close()


def test_forfeit():
    a, b = Client(), Client()
    for x in (a, b):
        x.until("STATUS|CHOOSE_MODE")
        x.send("MODE|PVP\n")
    a.until("STATUS|PLACE_SHIPS"); b.until("STATUS|PLACE_SHIPS")
    a.close()                                   # abrupt disconnect mid-setup
    hit, lines = b.until("STATUS|WIN")
    check(hit is not None and "STATUS|OPPONENT_LEFT" in lines, "forfeit win")
    b.send("MODE|AI\n")
    check(b.until("MATCH|AI")[0] is not None, "can start a new game after forfeit")
    b.close()


def test_server_survives():
    for _ in range(5):                          # connect and vanish while mid-game
        c = Client()
        c.until("STATUS|CHOOSE_MODE")
        c.send("MODE|AI\nPLACE|RANDOM\n")
        c.close()
    time.sleep(0.2)
    c = Client()
    check(c.line() == "HELLO|BATTLESHIP|2", "server still alive")
    c.close()


def main():
    proc = subprocess.Popen([SERVER, str(PORT)], stdout=subprocess.DEVNULL)
    time.sleep(1)
    try:
        for t in (test_hello_and_errors, test_framing_and_placement, lambda: test_ai_game(True),
                  test_pvp, test_forfeit, test_server_survives):
            name = getattr(t, "__name__", "ai_game")
            try:
                t()
                print("ran", name)
            except Exception as e:                # noqa
                global failures
                failures += 1
                print("ERROR in", name, repr(e))
                import traceback
                traceback.print_exc()
        check(proc.poll() is None, "server still running at the end")
    finally:
        proc.terminate()
    print("FAILURES:", failures)
    sys.exit(1 if failures else 0)


main()

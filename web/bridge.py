#!/usr/bin/env python3
"""Tiny web bridge: serves index.html and relays lines between a browser tab and
the Battleship TCP server (browsers cannot open raw TCP sockets).

    python bridge.py [--web-port 8000] [--server 127.0.0.1:8080]

GET  /              the game page
GET  /events        Server-Sent Events: opens one TCP connection to the game server
                    and streams every line it sends ("event: id" first, then data lines)
POST /send?id=<id>  body = one protocol line, forwarded to the game server
"""
import argparse
import os
import socket
import threading
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

HERE = os.path.dirname(os.path.abspath(__file__))
sessions = {}                   # id -> socket
lock = threading.Lock()
SERVER = ("127.0.0.1", 8080)
MAX_LINE = 256


class Handler(BaseHTTPRequestHandler):
    server_version = "BattleshipBridge"

    def log_message(self, *args):       # keep the console quiet
        pass

    def _reply(self, code, body=b"", ctype="text/plain; charset=utf-8"):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path = urlparse(self.path).path
        if path in ("/", "/index.html"):
            with open(os.path.join(HERE, "index.html"), "rb") as f:
                self._reply(200, f.read(), "text/html; charset=utf-8")
        elif path == "/events":
            self._events()
        else:
            self._reply(404, b"not found")

    def do_POST(self):
        url = urlparse(self.path)
        if url.path != "/send":
            return self._reply(404, b"not found")
        sid = parse_qs(url.query).get("id", [""])[0]
        length = min(int(self.headers.get("Content-Length") or 0), MAX_LINE)
        text = self.rfile.read(length).decode("utf-8", "replace")
        text = text.replace("\r", " ").replace("\n", " ").strip()
        with lock:
            sock = sessions.get(sid)
        if sock is None or not text:
            return self._reply(400, b"no such session")
        try:
            sock.sendall(text.encode() + b"\n")
        except OSError:
            return self._reply(502, b"game server connection lost")
        self._reply(204)

    def _events(self):
        try:
            sock = socket.create_connection(SERVER, timeout=5)
            sock.settimeout(None)
        except OSError as e:
            return self._reply(502, f"cannot reach game server {SERVER[0]}:{SERVER[1]}: {e}".encode())

        sid = uuid.uuid4().hex
        with lock:
            sessions[sid] = sock

        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Connection", "close")
        self.end_headers()
        self.close_connection = True

        def emit(data, event=None):
            msg = (f"event: {event}\n" if event else "") + f"data: {data}\n\n"
            self.wfile.write(msg.encode())
            self.wfile.flush()

        buf = b""
        try:
            emit(sid, "id")
            while True:
                data = sock.recv(4096)
                if not data:
                    break
                buf += data
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    emit(line.decode("utf-8", "replace").rstrip("\r"))
            emit("server closed the connection", "closed")
        except (OSError, ValueError):       # browser went away
            pass
        finally:
            with lock:
                sessions.pop(sid, None)
            try:
                sock.sendall(b"QUIT\n")
            except OSError:
                pass
            sock.close()


def main():
    global SERVER
    ap = argparse.ArgumentParser()
    ap.add_argument("--web-port", type=int, default=8000)
    ap.add_argument("--server", default="127.0.0.1:8080", help="host:port of the game server")
    args = ap.parse_args()
    host, _, port = args.server.rpartition(":")
    SERVER = (host or "127.0.0.1", int(port))

    httpd = ThreadingHTTPServer(("127.0.0.1", args.web_port), Handler)
    httpd.daemon_threads = True
    print(f"Battleship web UI: http://127.0.0.1:{args.web_port}  (game server {SERVER[0]}:{SERVER[1]})")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()

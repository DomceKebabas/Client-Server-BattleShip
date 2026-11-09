# Battleship

Battleship over TCP in C: play the computer or another player, from a terminal or a browser.

## Quick start on Windows

Run `build-dist.ps1` once (needs [MSYS2](https://www.msys2.org) with `gcc`), then open the `dist\` folder:

- **`play.bat`** – starts the server and the web UI and opens http://127.0.0.1:8000 (needs Python 3 on PATH).
- **`play-terminal.bat`** – starts the server and the terminal client.

For two-player games, start `server.exe` on one machine and point clients at it
(`client.exe <host> 8080`, or `python web\bridge.py --server <host>:8080`).

## Build (Linux / macOS / WSL)
```bash
make          # ./server and ./client   (CMake also works)
make test     # unit tests + end-to-end protocol tests
./server [port]          # default 8080
./client [host] [port]   # default 127.0.0.1 8080
python3 web/bridge.py    # optional web UI on http://127.0.0.1:8000
```

## How to play
1. Choose **vs computer** (`ai`) or **PvP** (`pvp`, you are paired with the next waiting player).
2. Place the fleet (Carrier 5, Battleship 4, Cruiser 3, Submarine 3, Destroyer 2): click the grid in the browser,
   or `place B3 h|v` / `random` in the terminal.
3. Take turns shooting (`B7`). In PvP the turns alternate; a repeated shot does not cost your turn.
4. Quit with `exit`. Leaving a PvP game forfeits it. After a game you can start another on the same connection.

## Protocol (version 2)
Newline-terminated text lines `KEY|VALUE` (a trailing `\r` is tolerated, lines are at most 256 bytes).
Boards are 100 characters, row by row (`A1..A10`, `B1..`): `.` water, `S` ship, `X` hit, `o` miss.

| Client sends | Meaning |
|---|---|
| `MODE\|AI` / `MODE\|PVP` | start a game (from the lobby) |
| `PLACE\|RANDOM` / `PLACE\|B3\|H` | place the rest of the fleet / the next ship (`H` right, `V` down) |
| `SHOOT\|B7` | fire on your turn |
| `CHAT\|text` | message the PvP opponent |
| `QUIT` | disconnect (forfeits a running game) |

| Server sends | Meaning |
|---|---|
| `HELLO\|BATTLESHIP\|2`, `STATUS\|CHOOSE_MODE` | on connect, and after every game |
| `STATUS\|WAITING_FOR_OPPONENT`, `MATCH\|AI` or `MATCH\|PVP`, `FLEET\|5,4,3,3,2` | matchmaking |
| `STATUS\|PLACE_SHIPS`, `NEXT_SHIP\|<name>\|<size>`, `PLACED\|<name>`, `STATUS\|WAITING_FOR_OPPONENT_SETUP` | setup |
| `YOURBOARD\|<100>`, `TRACKING\|<100>` | your fleet with the enemy's hits / what you know of theirs |
| `STATUS\|YOUR_TURN`, `STATUS\|OPPONENT_TURN` | whose turn |
| `RESULT\|HIT\|MISS\|SUNK\|ALREADY_SHOT`, `SUNK_SHIP\|<name>` | outcome of your shot |
| `OPPONENT_SHOT\|B7\|HIT`, `OPPONENT_SUNK\|<name>` | the opponent's shot at you |
| `SHOTS\|<n>`, `STATUS\|WIN`, `STATUS\|LOSE`, `STATUS\|OPPONENT_LEFT`, `REVEAL\|<100>` | game over (REVEAL shows the enemy fleet) |
| `CHAT\|text` | opponent's message |
| `ERROR\|<code>` | `UNKNOWN_COMMAND`, `INVALID_COORDINATES`, `INVALID_PLACEMENT`, `INVALID_MODE`, `NOT_YOUR_TURN`, `NOT_PLAYING`, `NOT_IN_SETUP`, `NO_GAME`, `IN_GAME`, `ALREADY_WAITING`, `NO_OPPONENT`, `LINE_TOO_LONG`, `IDLE_TIMEOUT`, `SERVER_FULL` |

Server limits: 64 concurrent clients, 10 minutes idle timeout. Ships may touch each other.

## Layout
- `battleship_logic.{h,c}` – game rules, strict coordinate parser, hunt/target AI (no I/O).
- `server.c` – thread per client, lobby and matches. `client.c` – terminal client.
- `web/` – `bridge.py` (HTTP + Server-Sent-Events relay to the TCP server, stdlib only) and `index.html`.
- `tests/` – `test_logic.c` and `test_protocol.py`; CI in `.github/workflows/ci.yml`.

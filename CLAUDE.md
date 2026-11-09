# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build and run

POSIX-only C (`sys/socket.h`, `pthread.h`, `stdatomic.h`). On Windows use WSL, or MSYS2's `gcc` (works; no sanitizers there). `build-dist.ps1` builds `dist\` with the exes, `msys-2.0.dll`, the web UI and `play.bat` launchers.

```bash
make                      # ./server and ./client (CMake has the same targets)
make test                 # tests/test_logic.c, then tests/test_protocol.py against ./server
./server [port]           # default 8080
./client [host] [port]
python3 web/bridge.py     # browser UI at http://127.0.0.1:8000, relays to the server
```

The client also links `battleship_logic.c` (it uses `parse_coordinates`). CI builds with `-Werror` and ASan/UBSan/TSan.

## Architecture

- `battleship_logic.{h,c}`: pure logic, no I/O. `Board`, the fleet (`FLEET_SIZES`/`FLEET_NAMES`), `board_place_ship`/`board_place_random`, `shoot` (returns a `ShotResult` enum, `SHOT_SUNK_ALL` = win), `board_render`, `parse_coordinates` (strict), per-owner `Rng` (no global `rand`), and the hunt/target `Ai`.
- `server.c`: one detached thread per client (`handle_client`). A `Session` is a connection; a `Match` holds both boards and is shared by the two PvP sessions (or one session plus the in-process AI). Locks: `g_lobby_lock` guards `g_pending` and `Session.match/idx`; `Match.lock` guards the match and serialises sends to its players; `Session.send_lock` keeps whole messages atomic. Lock order: lobby lock, then match lock. Only a session's own thread detaches it (`leave_match`), which forfeits a running PvP game.
- `client.c`: terminal client; receive thread reassembles lines, prints boards sent by the server (`YOURBOARD`/`TRACKING`/`REVEAL`).
- `web/bridge.py` + `web/index.html`: browser UI. The bridge opens one TCP connection per `/events` (SSE) stream and forwards `POST /send` bodies as lines; the page is a thin renderer of server messages.

### Wire protocol

Version 2, newline-terminated `KEY|VALUE` lines; the full table is in `README.md`. Changing a message means updating `server.c`, `client.c`, `web/index.html`, the README table and `tests/test_protocol.py`. Both ends reassemble lines from the byte stream (a `recv()` may hold several lines or part of one) and parse coordinates only via `parse_coordinates`.

# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build and run

POSIX-only (uses `sys/socket.h`, `pthread.h`); on Windows build under WSL/MinGW-with-POSIX. There are no tests or linter.

```bash
gcc -o server server.c battleship_logic.c -lpthread
gcc -o client client.c -lpthread
./server   # listens on port 8080
./client   # connects to the server
```

`CMakeLists.txt` is a CLion artifact: it puts `server.c`, `client.c` and the logic into a single executable, which won't link (two `main`s). Use the gcc commands above.

## Architecture

TCP client/server Battleship, single-player: the server generates a hidden board per connection and the client shoots at it.

- `server.c`: accepts connections on port 8080 and spawns a detached pthread per client (`handle_client`). Each thread owns its own `Game`, so there is no shared state or locking.
- `client.c`: a terminal client with a separate receive thread that prints server messages and sets the volatile `game_over` flag when it sees `STATUS|WIN` or the connection closes.
- `battleship_logic.{h,c}`: pure game logic shared by the server (`Game`, `place_ships`, `shoot`, `board_to_string`). `shoot` returns a result string; `"SUNK_ALL"` is the sentinel for a win.

### Wire protocol

Newline-terminated text lines `KEY|VALUE`. The client sends `SHOOT|<row A-J><col 1-10>` (e.g. `SHOOT|B7`). The server replies with `STATUS|START`, `RESULT|<result>` plus `STATUS|YOUR_TURN`, `RESULT|SUNK`, `SHOTS|<n>` plus `STATUS|WIN`, or `ERROR|...`. `RESULT` is one of `HIT`, `MISS`, `SUNK` or `ALREADY_SHOT` (a repeat shot, not counted in `Game.shots`). Changing a message requires updating both `server.c` and `client.c`, since the client matches on strings like `STATUS|WIN`.

Messages are not framed: each `recv()` is treated as one complete message, and the server parses fixed offsets (`buffer[6]` row, `atoi(&buffer[7])` column).

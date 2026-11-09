CC     ?= gcc
CFLAGS ?= -Wall -Wextra -O2
LDLIBS  = -lpthread

all: server client

server: server.c battleship_logic.c battleship_logic.h
	$(CC) $(CFLAGS) -o $@ server.c battleship_logic.c $(LDLIBS)

client: client.c battleship_logic.c battleship_logic.h
	$(CC) $(CFLAGS) -o $@ client.c battleship_logic.c $(LDLIBS)

test_logic: tests/test_logic.c battleship_logic.c battleship_logic.h
	$(CC) $(CFLAGS) -g -o $@ tests/test_logic.c battleship_logic.c

# Unit tests, then end-to-end protocol tests against the real server.
test: server test_logic
	./test_logic
	python3 tests/test_protocol.py ./server

clean:
	rm -f server client test_logic server.exe client.exe test_logic.exe

.PHONY: all test clean

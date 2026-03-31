# Battleship

Terminal Battleship over TCP. Server spawns a new game per client.

## Build
```bash
gcc -o server server.c battleship_logic.c -lpthread
gcc -o client client.c -lpthread
```

## Run
```bash
./server   # terminal 1
./client   # terminal 2
```

## Play
Enter coordinates `A1`–`J10`. Type `exit` to quit.

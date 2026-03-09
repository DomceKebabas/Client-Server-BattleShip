///
/// @Author Dominykas Nemanis on 3/5/26.
///

#ifndef KTINKLU1LAB_BATTLESHIP_LOGIC_H
#define KTINKLU1LAB_BATTLESHIP_LOGIC_H

#include <pthread.h>
#include <time.h>

#define SIZE 10

typedef struct {
    char board[SIZE][SIZE];
    int ships_remaining;
} Game;

void place_ships(Game* game, unsigned int seed);
const char* shoot(Game* game, char row, int col);
void board_to_string(Game* game, char* out);

#endif

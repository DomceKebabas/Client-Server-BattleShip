///
/// @author Dominykas Nemanis on 3/5/26.
///

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "battleship_logic.h"

void place_ships(Game* game, unsigned int seed)
{
    srand(seed);

    for (int i = 0; i < SIZE; i++)
        for (int j = 0; j < SIZE; j++)
            game->board[i][j] = 'O';

    int ship_sizes[] = {4, 3, 3, 2, 2};
    int num_ships = sizeof(ship_sizes)/sizeof(ship_sizes[0]);
    game->ships_remaining = 0;

    for (int s = 0; s < num_ships; s++) {
        int placed = 0;
        while (!placed) {
            int row = rand() % SIZE;
            int col = rand() % SIZE;
            int dir = rand() % 2;

            int fit = 1;
            for (int i = 0; i < ship_sizes[s]; i++) {
                int r = row + (dir==1 ? i : 0);
                int c = col + (dir==0 ? i : 0);
                if (r >= SIZE || c >= SIZE || game->board[r][c] != 'O') {
                    fit = 0;
                    break;
                }
            }

            if (fit) {
                for (int i = 0; i < ship_sizes[s]; i++) {
                    int r = row + (dir==1 ? i : 0);
                    int c = col + (dir==0 ? i : 0);
                    game->board[r][c] = '1' + s;
                    game->ships_remaining++;
                }
                placed = 1;
            }
        }
    }
}

const char* shoot(Game* game, char row, int col)
{
    int r = row - 'A';
    int c = col - 1;
    if (r < 0 || r >= SIZE || c < 0 || c >= SIZE)
        return "MISS";

    char cell = game->board[r][c];

    if (cell >= '1' && cell <= '9') {
        game->board[r][c] = 'H';
        game->ships_remaining--;

        // Check if the ship is sunk.
        int sunk = 1;
        for (int i = 0; i < SIZE; i++)
            for (int j = 0; j < SIZE; j++)
                if (game->board[i][j] == cell) { sunk = 0; }

        if (sunk) {
            if (game->ships_remaining == 0)
                return "SUNK_ALL";
            return "SUNK";
        }
        return "HIT";
    } else if (cell == 'O') {
        game->board[r][c] = 'M';
        return "MISS";
    }
    return "MISS";
}

void board_to_string(Game* game, char* out)
{
    int k = 0;
    for (int i = 0; i < SIZE; i++)
        for (int j = 0; j < SIZE; j++)
            out[k++] = game->board[i][j];
    out[k] = '\0';
}

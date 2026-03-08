///
/// @author Dominykas Nemanis on 3/5/26.
///

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SIZE 10

typedef struct {
    char board[SIZE][SIZE];
    int ships_remaining;
} Game;

typedef struct {
    int socket[2];
    Game board[2];
    int current_turn;
    int active;
    int players_done;
    pthread_mutex_t mutex;
} GameSession;

void place_ships(Game* game)
{
    for (int i = 0; i < SIZE; i++)
        for (int j = 0; j < SIZE; j++)
            game->board[i][j] = 'O';

    int ship_sizes[] = {4, 3, 3, 2, 2};
    int num_ships = sizeof(ship_sizes)/sizeof(ship_sizes[0]);
    game->ships_remaining = 0;

    srand(time(NULL));

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
                if (r >= SIZE || c >= SIZE || game->board[r][c] == 'S') {
                    fit = 0;
                    break;
                }
            }

            if (fit) {
                for (int i = 0; i < ship_sizes[s]; i++) {
                    int r = row + (dir==1 ? i : 0);
                    int c = col + (dir==0 ? i : 0);
                    game->board[r][c] = 'S';
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
    if (r<0 || r>=SIZE || c<0 || c>=SIZE)
        return "MISS";

    if (game->board[r][c] == 'S') {
        game->board[r][c] = 'H';
        game->ships_remaining--;
        if (game->ships_remaining == 0)
            return "SUNK";
        return "HIT";
    } else if (game->board[r][c] == 'O') {
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

///
/// @Author Dominykas Nemanis on 3/5/26.
///

#ifndef KTINKLU1LAB_BATTLESHIP_LOGIC_H
#define KTINKLU1LAB_BATTLESHIP_LOGIC_H

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

void place_ships(Game* game);
const char* shoot(Game* game, char row, int col);
void board_to_string(Game* game, char* out);

#endif //KTINKLU1LAB_BATTLESHIP_LOGIC_H

///
/// @Author Dominykas Nemanis on 3/5/26.
///
/// Pure game logic (no sockets, no threads). Shared by server and client.
///

#ifndef KTINKLU1LAB_BATTLESHIP_LOGIC_H
#define KTINKLU1LAB_BATTLESHIP_LOGIC_H

#include <stddef.h>

#define SIZE 10
#define NUM_SHIPS 5
#define FLEET_CELLS 17                  // 5 + 4 + 3 + 3 + 2

// Minimum buffer size for board_render (SIZE*SIZE cells + NUL).
#define BOARD_STRING_SIZE (SIZE * SIZE + 1)

// The standard fleet, in placement order.
extern const int         FLEET_SIZES[NUM_SHIPS];
extern const char* const FLEET_NAMES[NUM_SHIPS];

// ---------------------------------------------------------------- PRNG
// Small per-owner PRNG (xorshift32). The global srand/rand state is shared
// between threads, so every game keeps its own.
typedef struct { unsigned int state; } Rng;

void         rng_seed(Rng* rng, unsigned int seed);
unsigned int rng_next(Rng* rng);

// --------------------------------------------------------------- board
// Cell values: 'O' water, '1'..'5' ship id (index + '1'), 'H' hit, 'M' miss.
typedef struct {
    char grid[SIZE][SIZE];
    int ships_placed;       // ships placed so far; the next one is FLEET_SIZES[ships_placed]
    int cells_remaining;    // ship cells not yet hit (not the number of ships)
    int shots;              // valid, non-repeated shots received so far
} Board;

typedef enum {
    SHOT_INVALID,           // coordinates out of range
    SHOT_MISS,
    SHOT_ALREADY_SHOT,
    SHOT_HIT,
    SHOT_SUNK,
    SHOT_SUNK_ALL           // last ship sunk: the game is won
} ShotResult;

// Strictly parses "<A-J><1-10>" (e.g. "B7", "j10"). The whole string must match:
// no spaces, signs, leading zeros or trailing characters. Returns 1 on success
// and stores the upper-cased row and the column, otherwise returns 0.
int parse_coordinates(const char* text, char* row, int* col);

void board_init(Board* board);                  // empty board, no ships

// Places the next fleet ship with its first cell at (row, col), 0-based, extending
// right (vertical = 0) or down (vertical = 1). Returns 1 on success, 0 if it would
// leave the board, overlap another ship, or the fleet is already complete.
int  board_place_ship(Board* board, int row, int col, int vertical);

// Randomly places every ship that has not been placed yet.
void board_place_random(Board* board, Rng* rng);

int  board_ready(const Board* board);           // 1 when all NUM_SHIPS are placed

// Fires at (row 'A'-'J', col 1-10). If a ship is sunk, *sunk_ship (may be NULL)
// receives its fleet index, otherwise -1.
ShotResult  shoot(Board* board, char row, int col, int* sunk_ship);
const char* shot_result_name(ShotResult result);    // "HIT", "MISS", "SUNK", ...

// Writes SIZE*SIZE chars, row by row: '.' unknown/water, 'S' intact ship
// (only if reveal_ships), 'X' hit, 'o' miss. out_size must be >= BOARD_STRING_SIZE.
void board_render(const Board* board, int reveal_ships, char* out, size_t out_size);

// ------------------------------------------------------------------ AI
// Hunt/target computer opponent: fires on a checkerboard until it hits,
// then probes the neighbours of every hit.
typedef struct {
    Rng  rng;
    char known[SIZE][SIZE];     // 0 = not fired at yet
    int  stack_r[SIZE * SIZE];
    int  stack_c[SIZE * SIZE];
    int  stack_len;
} Ai;

void ai_init(Ai* ai, unsigned int seed);
void ai_next_shot(Ai* ai, char* row, int* col);                 // never repeats a cell
void ai_report(Ai* ai, char row, int col, ShotResult result);   // feed back the outcome

#endif

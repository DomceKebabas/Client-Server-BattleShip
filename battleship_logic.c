///
/// @author Dominykas Nemanis on 3/5/26.
///

#include <stdlib.h>
#include "battleship_logic.h"

const int         FLEET_SIZES[NUM_SHIPS] = {5, 4, 3, 3, 2};
const char* const FLEET_NAMES[NUM_SHIPS] = {"Carrier", "Battleship", "Cruiser", "Submarine", "Destroyer"};

// ---------------------------------------------------------------- PRNG

void rng_seed(Rng* rng, unsigned int seed)
{
    rng->state = seed ? seed : 0x9E3779B9u;     // xorshift must not start at 0
}

unsigned int rng_next(Rng* rng)
{
    unsigned int x = rng->state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng->state = x;
    return x;
}

// ------------------------------------------------------------ coordinates

int parse_coordinates(const char* text, char* row, int* col)
{
    if (text == NULL)
        return 0;

    char r = text[0];
    if (r >= 'a' && r <= 'z')
        r = (char)(r - 'a' + 'A');
    if (r < 'A' || r >= 'A' + SIZE)
        return 0;

    const char* digits = text + 1;
    if (digits[0] < '1' || digits[0] > '9')         // also rejects leading zeros
        return 0;

    int value = digits[0] - '0';
    if (digits[1] != '\0') {
        if (digits[1] < '0' || digits[1] > '9' || digits[2] != '\0')
            return 0;
        value = value * 10 + (digits[1] - '0');
    }
    if (value > SIZE)
        return 0;

    *row = r;
    *col = value;
    return 1;
}

// ------------------------------------------------------------------ board

void board_init(Board* board)
{
    for (int i = 0; i < SIZE; i++)
        for (int j = 0; j < SIZE; j++)
            board->grid[i][j] = 'O';
    board->ships_placed = 0;
    board->cells_remaining = 0;
    board->shots = 0;
}

int board_place_ship(Board* board, int row, int col, int vertical)
{
    if (board->ships_placed >= NUM_SHIPS)
        return 0;

    int size = FLEET_SIZES[board->ships_placed];
    if (row < 0 || col < 0 || row >= SIZE || col >= SIZE)
        return 0;

    for (int i = 0; i < size; i++) {
        int r = row + (vertical ? i : 0);
        int c = col + (vertical ? 0 : i);
        if (r >= SIZE || c >= SIZE || board->grid[r][c] != 'O')
            return 0;
    }

    for (int i = 0; i < size; i++) {
        int r = row + (vertical ? i : 0);
        int c = col + (vertical ? 0 : i);
        board->grid[r][c] = (char)('1' + board->ships_placed);
    }
    board->cells_remaining += size;
    board->ships_placed++;
    return 1;
}

void board_place_random(Board* board, Rng* rng)
{
    // Rejection sampling always finishes: 17 cells on a 100-cell board.
    while (board->ships_placed < NUM_SHIPS) {
        int row = (int)(rng_next(rng) % SIZE);
        int col = (int)(rng_next(rng) % SIZE);
        int vertical = (int)(rng_next(rng) % 2);
        board_place_ship(board, row, col, vertical);
    }
}

int board_ready(const Board* board)
{
    return board->ships_placed == NUM_SHIPS;
}

ShotResult shoot(Board* board, char row, int col, int* sunk_ship)
{
    if (sunk_ship) *sunk_ship = -1;

    int r = row - 'A';
    int c = col - 1;
    if (r < 0 || r >= SIZE || c < 0 || c >= SIZE)
        return SHOT_INVALID;

    char cell = board->grid[r][c];

    // Repeat shots are reported and don't count towards the shot total.
    if (cell == 'H' || cell == 'M')
        return SHOT_ALREADY_SHOT;

    board->shots++;

    if (cell >= '1' && cell <= '0' + NUM_SHIPS) {
        board->grid[r][c] = 'H';
        board->cells_remaining--;

        // The ship is sunk when none of its cells is left.
        int sunk = 1;
        for (int i = 0; i < SIZE; i++)
            for (int j = 0; j < SIZE; j++)
                if (board->grid[i][j] == cell) sunk = 0;

        if (sunk) {
            if (sunk_ship) *sunk_ship = cell - '1';
            return board->cells_remaining == 0 ? SHOT_SUNK_ALL : SHOT_SUNK;
        }
        return SHOT_HIT;
    }

    board->grid[r][c] = 'M';
    return SHOT_MISS;
}

const char* shot_result_name(ShotResult result)
{
    switch (result) {
        case SHOT_MISS:         return "MISS";
        case SHOT_ALREADY_SHOT: return "ALREADY_SHOT";
        case SHOT_HIT:          return "HIT";
        case SHOT_SUNK:         return "SUNK";
        case SHOT_SUNK_ALL:     return "SUNK";      // the win is reported via STATUS|WIN
        case SHOT_INVALID:      break;
    }
    return "INVALID";
}

void board_render(const Board* board, int reveal_ships, char* out, size_t out_size)
{
    if (out_size == 0)
        return;

    size_t k = 0;
    for (int i = 0; i < SIZE && k + 1 < out_size; i++) {
        for (int j = 0; j < SIZE && k + 1 < out_size; j++) {
            char cell = board->grid[i][j];
            char ch = '.';
            if (cell == 'H')      ch = 'X';
            else if (cell == 'M') ch = 'o';
            else if (cell != 'O' && reveal_ships) ch = 'S';
            out[k++] = ch;
        }
    }
    out[k] = '\0';
}

// ------------------------------------------------------------------- AI

void ai_init(Ai* ai, unsigned int seed)
{
    rng_seed(&ai->rng, seed);
    for (int i = 0; i < SIZE; i++)
        for (int j = 0; j < SIZE; j++)
            ai->known[i][j] = 0;
    ai->stack_len = 0;
}

void ai_next_shot(Ai* ai, char* row, int* col)
{
    // Target mode: follow up on the neighbours of earlier hits.
    while (ai->stack_len > 0) {
        ai->stack_len--;
        int r = ai->stack_r[ai->stack_len];
        int c = ai->stack_c[ai->stack_len];
        if (!ai->known[r][c]) {
            *row = (char)('A' + r);
            *col = c + 1;
            return;
        }
    }

    // Hunt mode: pick a random unknown cell, preferring a checkerboard
    // pattern (every ship is at least 2 long, so it must cross a black square).
    int cand_r[SIZE * SIZE], cand_c[SIZE * SIZE], n = 0;
    for (int pass = 0; pass < 2 && n == 0; pass++)
        for (int i = 0; i < SIZE; i++)
            for (int j = 0; j < SIZE; j++)
                if (!ai->known[i][j] && (pass == 1 || (i + j) % 2 == 0)) {
                    cand_r[n] = i;
                    cand_c[n] = j;
                    n++;
                }

    if (n == 0) {           // board exhausted; cannot happen in a normal game
        *row = 'A';
        *col = 1;
        return;
    }

    int pick = (int)(rng_next(&ai->rng) % (unsigned int)n);
    *row = (char)('A' + cand_r[pick]);
    *col = cand_c[pick] + 1;
}

void ai_report(Ai* ai, char row, int col, ShotResult result)
{
    int r = row - 'A';
    int c = col - 1;
    if (r < 0 || r >= SIZE || c < 0 || c >= SIZE)
        return;

    ai->known[r][c] = 1;

    if (result != SHOT_HIT)
        return;

    static const int dr[4] = {-1, 1, 0, 0};
    static const int dc[4] = {0, 0, -1, 1};
    for (int d = 0; d < 4; d++) {
        int nr = r + dr[d];
        int nc = c + dc[d];
        if (nr >= 0 && nr < SIZE && nc >= 0 && nc < SIZE && !ai->known[nr][nc]
            && ai->stack_len < SIZE * SIZE) {
            ai->stack_r[ai->stack_len] = nr;
            ai->stack_c[ai->stack_len] = nc;
            ai->stack_len++;
        }
    }
}

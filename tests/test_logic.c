// Unit tests for battleship_logic. Build: make test

#include <stdio.h>
#include <string.h>
#include "../battleship_logic.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond) do { \
    checks++; \
    if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

static int count_cells(const Board* b, int (*pred)(char)) {
    int n = 0;
    for (int i = 0; i < SIZE; i++)
        for (int j = 0; j < SIZE; j++)
            if (pred(b->grid[i][j])) n++;
    return n;
}
static int is_ship(char c) { return c >= '1' && c <= '0' + NUM_SHIPS; }

static void test_parse(void) {
    char r; int c;
    CHECK(parse_coordinates("A1", &r, &c) && r == 'A' && c == 1);
    CHECK(parse_coordinates("j10", &r, &c) && r == 'J' && c == 10);
    CHECK(parse_coordinates("b7", &r, &c) && r == 'B' && c == 7);
    const char* bad[] = {"", "A", "A0", "A01", "A11", "K1", "A1z", "A1 ", " A1", "A+5",
                         "A 3", "A100", "1A", "AA", "A-1", NULL};
    for (int i = 0; bad[i]; i++)
        CHECK(!parse_coordinates(bad[i], &r, &c));
    CHECK(!parse_coordinates(NULL, &r, &c));
}

static void test_random_placement(void) {
    for (unsigned seed = 1; seed <= 500; seed++) {
        Board b; Rng rng;
        board_init(&b);
        rng_seed(&rng, seed);
        board_place_random(&b, &rng);
        CHECK(board_ready(&b));
        CHECK(b.cells_remaining == FLEET_CELLS);
        CHECK(count_cells(&b, is_ship) == FLEET_CELLS);
        for (int s = 0; s < NUM_SHIPS; s++) {       // each ship has exactly its size in cells
            int n = 0;
            for (int i = 0; i < SIZE; i++)
                for (int j = 0; j < SIZE; j++)
                    if (b.grid[i][j] == '1' + s) n++;
            CHECK(n == FLEET_SIZES[s]);
        }
    }
}

static void test_manual_placement(void) {
    Board b;
    board_init(&b);
    CHECK(!board_place_ship(&b, 0, 6, 0));      // carrier (5) would leave the board
    CHECK(!board_place_ship(&b, 6, 0, 1));
    CHECK(!board_place_ship(&b, -1, 0, 0));
    CHECK(b.ships_placed == 0);
    CHECK(board_place_ship(&b, 0, 0, 0));       // carrier A1..A5
    CHECK(!board_place_ship(&b, 0, 4, 1));      // battleship overlaps A5
    CHECK(board_place_ship(&b, 1, 0, 0));       // battleship B1..B4
    CHECK(board_place_ship(&b, 2, 0, 1));       // cruiser C1..E1
    CHECK(board_place_ship(&b, 2, 5, 1));       // submarine
    CHECK(!board_ready(&b));
    CHECK(board_place_ship(&b, 9, 8, 0));       // destroyer J9..J10
    CHECK(board_ready(&b));
    CHECK(!board_place_ship(&b, 5, 5, 0));      // fleet complete
    CHECK(b.cells_remaining == FLEET_CELLS);
}

static void test_shooting(void) {
    Board b;
    board_init(&b);
    board_place_ship(&b, 0, 0, 0);              // carrier A1..A5
    board_place_ship(&b, 1, 0, 0);              // battleship B1..B4
    board_place_ship(&b, 2, 0, 0);              // cruiser C1..C3
    board_place_ship(&b, 3, 0, 0);              // submarine D1..D3
    board_place_ship(&b, 4, 0, 0);              // destroyer E1..E2

    int sunk;
    CHECK(shoot(&b, 'F', 1, &sunk) == SHOT_MISS && sunk == -1);
    CHECK(shoot(&b, 'F', 1, &sunk) == SHOT_ALREADY_SHOT);
    CHECK(b.shots == 1);                        // repeat not counted
    CHECK(shoot(&b, 'K', 1, &sunk) == SHOT_INVALID);
    CHECK(shoot(&b, 'A', 11, &sunk) == SHOT_INVALID);
    CHECK(shoot(&b, 'A', 0, NULL) == SHOT_INVALID);
    CHECK(b.shots == 1);

    CHECK(shoot(&b, 'E', 1, &sunk) == SHOT_HIT && sunk == -1);
    CHECK(shoot(&b, 'E', 1, &sunk) == SHOT_ALREADY_SHOT);
    CHECK(shoot(&b, 'E', 2, &sunk) == SHOT_SUNK && sunk == 4);   // destroyer

    // Sink everything else; the very last hit must report the win.
    int wins = 0;
    for (int r = 0; r < 4; r++)
        for (int c = 1; c <= FLEET_SIZES[r]; c++) {
            ShotResult res = shoot(&b, (char)('A' + r), c, &sunk);
            if (res == SHOT_SUNK_ALL) wins++;
            CHECK(res != SHOT_MISS && res != SHOT_ALREADY_SHOT);
        }
    CHECK(wins == 1);
    CHECK(b.cells_remaining == 0);
}

static void test_render(void) {
    Board b; char out[BOARD_STRING_SIZE];
    board_init(&b);
    board_place_ship(&b, 0, 0, 0);
    shoot(&b, 'A', 1, NULL);
    shoot(&b, 'J', 10, NULL);
    board_render(&b, 0, out, sizeof(out));
    CHECK(strlen(out) == SIZE * SIZE);
    CHECK(out[0] == 'X' && out[1] == '.' && out[99] == 'o');
    board_render(&b, 1, out, sizeof(out));
    CHECK(out[0] == 'X' && out[1] == 'S' && out[5] == '.');
    char tiny[4];
    board_render(&b, 1, tiny, sizeof(tiny));    // truncates safely
    CHECK(strlen(tiny) == 3);
}

static void test_ai(void) {
    // The AI must never repeat a shot and must finish any board within 100 shots.
    for (unsigned seed = 1; seed <= 300; seed++) {
        Board b; Rng rng; Ai ai;
        board_init(&b);
        rng_seed(&rng, seed * 7919u);
        board_place_random(&b, &rng);
        ai_init(&ai, seed);

        int fired = 0, won = 0;
        for (; fired < SIZE * SIZE; fired++) {
            char row; int col;
            ai_next_shot(&ai, &row, &col);
            ShotResult res = shoot(&b, row, col, NULL);
            CHECK(res != SHOT_ALREADY_SHOT && res != SHOT_INVALID);
            ai_report(&ai, row, col, res);
            if (res == SHOT_SUNK_ALL) { won = 1; fired++; break; }
        }
        CHECK(won);
        CHECK(b.shots == fired);
    }

    // It should be clearly better than exhaustive scanning on average.
    long total = 0;
    for (unsigned seed = 1; seed <= 300; seed++) {
        Board b; Rng rng; Ai ai;
        board_init(&b);
        rng_seed(&rng, seed * 104729u);
        board_place_random(&b, &rng);
        ai_init(&ai, seed);
        for (;;) {
            char row; int col;
            ai_next_shot(&ai, &row, &col);
            ShotResult res = shoot(&b, row, col, NULL);
            ai_report(&ai, row, col, res);
            if (res == SHOT_SUNK_ALL) break;
        }
        total += b.shots;
    }
    printf("AI average shots to win: %.1f\n", (double)total / 300);
    CHECK(total / 300 < 75);
}

int main(void) {
    test_parse();
    test_random_placement();
    test_manual_placement();
    test_shooting();
    test_render();
    test_ai();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}

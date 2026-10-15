/*
 * test_tetris.c -- rules tests for VOBGTetris, run on the build host.
 *
 * The game core is plain C on purpose so it can be exercised here without a
 * device, a simulator or a Mac. Run with tests/run_tests.sh.
 *
 *   clang -std=c99 -Wall -Wextra -O1 test_tetris.c ../VOBGTetris.c -o test_tetris
 */

#include "VOBGTetris.h"

#include <stdio.h>
#include <string.h>

static int g_checks = 0;
static int g_failures = 0;
static const char *g_group = "";

static void group(const char *name)
{
    g_group = name;
    printf("\n-- %s\n", name);
}

static void check(int cond, const char *what)
{
    g_checks++;
    if (cond) {
        printf("   ok   %s\n", what);
    } else {
        g_failures++;
        printf("   FAIL %s [%s]\n", what, g_group);
    }
}

static void check_eq(long got, long want, const char *what)
{
    g_checks++;
    if (got == want) {
        printf("   ok   %s\n", what);
    } else {
        g_failures++;
        printf("   FAIL %s [%s]: got %ld, want %ld\n", what, g_group, got, want);
    }
}

/* --------------------------------------------------------------- helpers */

/* Fill `row` with `piece`, leaving the columns set in the `gaps` bitmask empty. */
static void fill_row_gaps(VOBGGame *g, int row, int piece, unsigned gaps)
{
    int c;
    for (c = 0; c < VOBG_COLS; c++) {
        g->cells[row][c] = (gaps & (1u << c)) ? 0 : (unsigned char)(piece + 1);
    }
}

/* Completely solid from `first_row` down, hole-free. */
static void fill_board_from(VOBGGame *g, int first_row, int piece)
{
    int r, c;
    for (r = first_row; r < VOBG_ROWS; r++) {
        for (c = 0; c < VOBG_COLS; c++) {
            g->cells[r][c] = (unsigned char)(piece + 1);
        }
    }
}

/* Solid from `first_row` down, but never a *complete* row: leaving one column
 * empty keeps the stack high without making every row scoreable, which is what
 * most of the game-over cases need. */
static void fill_board_holed(VOBGGame *g, int first_row, int piece, int hole_col)
{
    int r, c;
    for (r = first_row; r < VOBG_ROWS; r++) {
        for (c = 0; c < VOBG_COLS; c++) {
            g->cells[r][c] = (c == hole_col) ? 0 : (unsigned char)(piece + 1);
        }
    }
}

static int count_lines(const VOBGGame *g)
{
    int row, col, n = 0;
    for (row = 0; row < VOBG_ROWS; row++) {
        int full = 1;
        for (col = 0; col < VOBG_COLS; col++) {
            if (!g->cells[row][col]) {
                full = 0;
                break;
            }
        }
        n += full;
    }
    return n;
}

/* Put a known piece in play, bypassing the queue. */
static void stage(VOBGGame *g, int piece, int rot, int x, int y)
{
    g->active = (unsigned char)piece;
    g->rot = (signed char)rot;
    g->xf = (float)x;
    g->y = y;
    g->drop_timer = 0.0f;
    g->lock_timer = 0.0f;
}

static void settle(VOBGGame *g)
{
    int i;
    for (i = 0; i < 60 && g->state == VOBG_STATE_CLEARING; i++) {
        vobg_update(g, 0.02f);
    }
}

/* Read out `count` spawns by locking the active piece and wiping the board
 * afterwards, so the stack never grows tall enough to end the game. */
static void spawn_sequence(int count, unsigned long seed, int *out)
{
    VOBGGame g;
    int i;

    vobg_init(&g, seed);
    for (i = 0; i < count; i++) {
        out[i] = g.active;
        stage(&g, g.active, 0, 4, 0);
        vobg_hard_drop(&g);
        settle(&g);
        memset(g.cells, 0, sizeof g.cells);
        g.clear_count = 0;
        g.state = VOBG_STATE_PLAYING;
    }
}

/* ----------------------------------------------------------------- tests */

static void test_bag(void)
{
    enum { N = 70 };
    int seq[N];
    int i, j, k;
    int windows_ok = 1;
    int repeats_only_at_boundary = 1;
    int repeats = 0;
    int other[N];

    group("7-bag randomiser");

    spawn_sequence(N, 12345, seq);

    /* The defining property: each *non-overlapping* run of 7 spawns is a
     * permutation of the seven tetrominoes. It has to be non-overlapping -- a
     * sliding window would straddle a bag boundary and is allowed to repeat. */
    for (i = 0; i + 7 <= N; i += 7) {
        int count[VOBG_PIECE_COUNT];
        memset(count, 0, sizeof count);
        for (j = 0; j < 7; j++) {
            count[seq[i + j]]++;
        }
        for (k = 0; k < VOBG_PIECE_COUNT; k++) {
            if (count[k] != 1) {
                windows_ok = 0;
            }
        }
    }
    check(windows_ok, "every bag of 7 spawns contains all 7 pieces exactly once");

    /* So a repeat can only ever happen where one bag ends and the next begins,
     * i.e. on a multiple of 7. Anything else means the shuffler is broken. */
    for (i = 0; i + 1 < N; i++) {
        if (seq[i] == seq[i + 1]) {
            repeats++;
            if ((i + 1) % 7 != 0) {
                repeats_only_at_boundary = 0;
            }
        }
    }
    check(repeats_only_at_boundary, "back-to-back repeats only occur across a bag boundary");
    check(repeats <= N / 7, "repeats stay within the bag-boundary budget");

    /* Different seeds should produce different streams. */
    spawn_sequence(N, 54321, other);
    check(memcmp(seq, other, sizeof seq) != 0, "a different seed gives a different stream");

    /* Same seed, same stream. */
    spawn_sequence(N, 12345, other);
    check(memcmp(seq, other, sizeof seq) == 0, "the same seed reproduces the stream exactly");
}

static void test_gravity(void)
{
    VOBGGame g;

    group("gravity");

    vobg_init(&g, 7);
    stage(&g, VOBG_PIECE_O, 0, 4, 0);

    vobg_update(&g, 0.5f);
    check_eq(g.y, 0, "no movement before one full interval");

    vobg_update(&g, 0.6f);
    check_eq(g.y, 1, "falls once the level-1 interval elapses");

    check(vobg_gravity_seconds(1) > vobg_gravity_seconds(5), "gravity speeds up as level rises");
    check(vobg_gravity_seconds(1) > 0.99f && vobg_gravity_seconds(1) < 1.01f,
          "level 1 is one row per second");
    check(vobg_gravity_seconds(10) < 0.1f, "level 10 is well under 100ms per row");
    check(vobg_gravity_seconds(99) >= 1.0 / 60.0, "gravity never exceeds one row per frame");
}

static void test_soft_drop(void)
{
    VOBGGame g;
    float before;

    group("soft drop");

    vobg_init(&g, 11);
    stage(&g, VOBG_PIECE_O, 0, 4, 0);
    before = (float)g.score;

    vobg_set_soft_drop(&g, 1);
    vobg_update(&g, 0.05f);
    check_eq(g.y, 1, "one soft-drop tick moves exactly one row");
    check((float)g.score > before, "soft drop scores per cell");

    vobg_init(&g, 11);
    stage(&g, VOBG_PIECE_O, 0, 4, 0);
    vobg_set_soft_drop(&g, 1);
    vobg_update(&g, 0.5f);
    check(g.y > 8, "half a second of soft drop covers many rows");

    /* Releasing it returns the piece to normal gravity. */
    vobg_set_soft_drop(&g, 0);
    {
        int y0 = g.y;
        vobg_update(&g, 0.1f);
        check_eq(g.y, y0, "normal gravity is far slower than soft drop");
    }
}

static void test_movement(void)
{
    VOBGGame g;

    group("horizontal movement");

    vobg_init(&g, 3);
    stage(&g, VOBG_PIECE_O, 0, 4, 5);
    check_eq(vobg_move(&g, -1), 1, "move left succeeds in open space");
    check_eq(g.xf, 3, "position follows the move");
    check_eq(vobg_move(&g, 1), 1, "move right succeeds");
    check_eq(vobg_move(&g, 0), 0, "a zero delta is rejected");

    /* The O lives in box columns 1-2, so box x = -1 is what puts it against the
     * left wall. Staging it at x = 0 would leave column 0 empty. */
    stage(&g, VOBG_PIECE_O, 0, -1, 5); /* cells sit in columns 0 and 1 */
    check_eq(vobg_move(&g, -1), 0, "cannot move through the left wall");
    check_eq(g.xf, -1, "xf unchanged when the move is refused");

    stage(&g, VOBG_PIECE_O, 0, VOBG_COLS - 3, 5); /* cells sit in columns 8 and 9 */
    check_eq(vobg_move(&g, 1), 0, "cannot move through the right wall");
    check_eq(g.xf, VOBG_COLS - 3, "position unchanged at the right wall");

    /* The I piece is 4 wide, so it stops further from the edge than a 3-wide one. */
    stage(&g, VOBG_PIECE_I, 0, 0, 5);
    check_eq(vobg_move(&g, -1), 0, "I respects the left wall");
    stage(&g, VOBG_PIECE_I, 0, VOBG_COLS - 4, 5);
    check_eq(vobg_move(&g, 1), 0, "I respects the right wall");
    stage(&g, VOBG_PIECE_I, 0, 0, 5);
    check_eq(vobg_move(&g, 1), 1, "I can still move right from the left wall");

    /* An I flush against the right edge must not leave the board. */
    stage(&g, VOBG_PIECE_I, 0, VOBG_COLS - 4, 5);
    {
        int n, inside = 1, x, y;
        for (n = 0; n < 4; n++) {
            vobg_active_cell(&g, n, &x, &y);
            if (x < 0 || x >= VOBG_COLS) {
                inside = 0;
            }
        }
        check(inside, "a piece flush to the wall stays inside the board");
    }
}

static void test_rotation(void)
{
    VOBGGame g;
    float x0, y0;

    group("rotation");

    vobg_init(&g, 5);
    stage(&g, VOBG_PIECE_I, 0, 3, 5);
    check_eq(vobg_rotate(&g), 1, "I rotates");
    check_eq(g.rot, 1, "the rotation state advances");
    check_eq(g.xf, 3, "rotation does not shift a piece in open space");

    vobg_rotate(&g);
    vobg_rotate(&g);
    check_eq(g.rot, 3, "three clockwise turns");
    vobg_rotate(&g);
    check_eq(g.rot, 0, "four clockwise turns return to the start");

    /* Vertical I hugging the left wall needs a kick to become horizontal. */
    stage(&g, VOBG_PIECE_I, 1, 0, 5);
    check_eq(vobg_rotate(&g), 1, "rotation against a wall kicks instead of failing");

    /* O is rotationally symmetric and must not move. */
    stage(&g, VOBG_PIECE_O, 0, 4, 5);
    x0 = g.xf;
    y0 = g.y;
    vobg_rotate(&g);
    check(g.xf == x0 && g.y == y0, "O does not shift when rotated");

    /* An I lying flat on the bottom row has to lift to turn upright. */
    vobg_init(&g, 6);
    stage(&g, VOBG_PIECE_I, 0, 3, VOBG_ROWS - 2); /* rests on the floor */
    check_eq(vobg_ghost_row(&g), VOBG_ROWS - 2, "the flat I is on the floor");
    check_eq(vobg_rotate(&g), 1, "an I lying on the floor can still rotate");
    check(g.y + 3 <= VOBG_ROWS - 1, "the upright I stays inside the well");

    /* With every kick blocked, rotation fails cleanly instead of corrupting. */
    vobg_init(&g, 7);
    fill_board_from(&g, 0, VOBG_PIECE_J);
    stage(&g, VOBG_PIECE_I, 0, 3, VOBG_ROWS - 5);
    check_eq(vobg_rotate(&g), 0, "rotation fails when every kick is blocked");
    check_eq(g.rot, 0, "the rotation state is untouched after a failed rotation");

    /* Out-of-range reads are empty rather than garbage. */
    check_eq(vobg_cell(&g, -1, 5), 0, "reading left of the board is empty");
    check_eq(vobg_cell(&g, VOBG_COLS, 5), 0, "reading right of the board is empty");
    check_eq(vobg_cell(&g, 5, -1), 0, "reading above the buffer is empty");
    check_eq(vobg_cell(&g, 5, VOBG_ROWS), 0, "reading below the board is empty");
}

static void test_hard_drop(void)
{
    VOBGGame g;
    int cells;

    group("hard drop");

    vobg_init(&g, 21);
    stage(&g, VOBG_PIECE_O, 0, 4, 0);
    g.score = 0;
    cells = vobg_hard_drop(&g);
    check(cells > 0, "hard drop reports how far the piece travelled");
    check_eq(g.score, cells * 2, "hard drop scores 2 per cell");
    check_eq(g.state, VOBG_STATE_PLAYING, "the piece locks and the next one spawns");
    check(vobg_cell(&g, 5, VOBG_ROWS - 2) != 0, "the piece is resting on the floor");
    check_eq(vobg_cell(&g, 5, VOBG_ROWS - 3), 0, "nothing is stacked above it");

    /* A vertical I occupies four rows of one column. */
    vobg_init(&g, 22);
    stage(&g, VOBG_PIECE_I, 1, 4, 0);
    vobg_hard_drop(&g);
    check(vobg_cell(&g, 6, VOBG_ROWS - 1) != 0, "vertical I reaches the bottom row");
    check(vobg_cell(&g, 6, VOBG_ROWS - 4) != 0, "vertical I spans four rows");
    check_eq(vobg_cell(&g, 6, VOBG_ROWS - 5), 0, "vertical I leaves the row above clear");

    /* The ghost has to agree with where the piece actually stops. The T's arm
     * sits in box column 0, so the blocker in column 4 stops it one row lower
     * than the box itself. */
    vobg_init(&g, 23);
    stage(&g, VOBG_PIECE_T, 0, 4, 0);
    g.cells[VOBG_ROWS - 1][4] = (unsigned char)(VOBG_PIECE_J + 1);
    check_eq(vobg_ghost_row(&g), VOBG_ROWS - 3, "the ghost stops on top of a blocker");
    vobg_hard_drop(&g);
    check(vobg_cell(&g, 4, VOBG_ROWS - 2) != 0, "the piece lands where the ghost promised");
    check_eq(vobg_cell(&g, 4, VOBG_ROWS - 1), VOBG_PIECE_J + 1, "the blocker is untouched");
}

static void test_line_clears(void)
{
    VOBGGame g;
    int cells;

    group("line clears");

    /* A horizontal I covers columns 0-3, so the row has to be empty in all four of
     * them for the piece to reach it at all. */
    vobg_init(&g, 33);
    fill_row_gaps(&g, VOBG_ROWS - 1, VOBG_PIECE_J, 0xFu);
    stage(&g, VOBG_PIECE_I, 0, 0, 0);
    cells = vobg_hard_drop(&g);
    check_eq(g.state, VOBG_STATE_CLEARING, "locking with a full row starts the flash");
    check_eq(g.clear_count, 1, "one row flagged");
    vobg_update(&g, 0.05f);
    check_eq(g.lines, 0, "rows only count once the flash is over");
    settle(&g);
    check_eq(g.lines, 1, "one line counted");
    check_eq(g.score, cells * 2 + 100, "single scores 100 plus the hard-drop bonus");
    check_eq(count_lines(&g), 0, "no full rows remain");

    /* Double. */
    vobg_init(&g, 34);
    fill_row_gaps(&g, VOBG_ROWS - 1, VOBG_PIECE_J, (1u << 4) | (1u << 5));
    fill_row_gaps(&g, VOBG_ROWS - 2, VOBG_PIECE_J, (1u << 4) | (1u << 5));
    stage(&g, VOBG_PIECE_O, 0, 3, 0);
    cells = vobg_hard_drop(&g);
    check_eq(g.clear_count, 2, "two rows flagged");
    settle(&g);
    check_eq(g.lines, 2, "two lines counted");
    check_eq(g.score, cells * 2 + 300, "double scores 300 plus the hard-drop bonus");

    /* Tetris: four rows with a single gap, filled by a vertical I. */
    vobg_init(&g, 35);
    {
        int r;
        for (r = 1; r <= 4; r++) {
            fill_row_gaps(&g, VOBG_ROWS - r, VOBG_PIECE_L, 1u << 6);
        }
    }
    stage(&g, VOBG_PIECE_I, 1, 4, 0);
    cells = vobg_hard_drop(&g);
    check_eq(g.clear_count, 4, "a vertical I flags four rows");
    settle(&g);
    check_eq(g.lines, 4, "four lines counted");
    check_eq(g.score, cells * 2 + 800, "tetris scores 800 plus the hard-drop bonus");
    check_eq(count_lines(&g), 0, "the board is clean after a tetris");

    /* Everything above the cleared rows falls by exactly one. The marker sits in
     * column 5 because the I falling down columns 0-3 would otherwise land on it. */
    vobg_init(&g, 36);
    g.cells[VOBG_ROWS - 4][5] = (unsigned char)(VOBG_PIECE_T + 1);
    fill_row_gaps(&g, VOBG_ROWS - 1, VOBG_PIECE_J, 0xFu);
    stage(&g, VOBG_PIECE_I, 0, 0, 0);
    vobg_hard_drop(&g);
    settle(&g);
    check_eq(vobg_cell(&g, 5, VOBG_ROWS - 3), VOBG_PIECE_T + 1,
            "the row above the clear dropped by one");
    check_eq(vobg_cell(&g, 5, VOBG_ROWS - 4), 0, "and left its old row empty");

    /* Several rows must collapse together, not one at a time. Four rows carry a
     * gap for the vertical I, but the top one is holed as well so only three of
     * them are actually complete. */
    vobg_init(&g, 38);
    {
        int r;
        for (r = VOBG_ROWS - 4; r < VOBG_ROWS; r++) {
            fill_row_gaps(&g, r, VOBG_PIECE_J, 1u << 6);
        }
    }
    g.cells[VOBG_ROWS - 4][0] = 0; /* keeps that row from completing */
    g.cells[VOBG_ROWS - 6][0] = (unsigned char)(VOBG_PIECE_S + 1); /* marker */
    stage(&g, VOBG_PIECE_I, 1, 4, 0); /* vertical I lands in column 6 */
    vobg_hard_drop(&g);
    check_eq(g.clear_count, 3, "exactly three rows complete");
    settle(&g);
    check_eq(g.lines, 3, "three rows clear together");
    check_eq(vobg_cell(&g, 0, VOBG_ROWS - 3), VOBG_PIECE_S + 1,
            "the marker fell three rows");
    check_eq(count_lines(&g), 0, "nothing full is left behind");

    /* Level up at ten lines. */
    vobg_init(&g, 37);
    {
        int batch;
        for (batch = 0; batch < 10; batch++) {
            fill_row_gaps(&g, VOBG_ROWS - 1, VOBG_PIECE_S, 0xFu);
            stage(&g, VOBG_PIECE_I, 0, 0, 0);
            vobg_hard_drop(&g);
            settle(&g);
        }
    }
    check_eq(g.lines, 10, "ten singles counted");
    check_eq(g.level, 2, "level 2 at ten lines");
    check(vobg_gravity_seconds(2) < vobg_gravity_seconds(1), "and gravity reflects it");
}

static void test_lock_delay(void)
{
    VOBGGame g;

    group("lock delay");

    /* A grounded piece may be steered for a moment before it locks. */
    vobg_init(&g, 77);
    stage(&g, VOBG_PIECE_O, 0, 4, VOBG_ROWS - 2);
    check_eq(vobg_ghost_row(&g), VOBG_ROWS - 2, "the piece is grounded");

    vobg_update(&g, 0.2f);
    check_eq(vobg_cell(&g, 5, VOBG_ROWS - 2), 0, "not locked 0.2s in");

    /* Steering resets the countdown, so sliding along the floor is possible. */
    check_eq(vobg_move(&g, -1), 1, "the grounded piece can still slide");
    vobg_update(&g, 0.3f);
    check_eq(vobg_cell(&g, 4, VOBG_ROWS - 2), 0, "sliding reset the lock delay");

    /* Leave it alone and it locks. */
    vobg_update(&g, 0.4f);
    check(vobg_cell(&g, 4, VOBG_ROWS - 2) != 0, "locking happens once the delay expires");
    check_eq(g.state, VOBG_STATE_PLAYING, "and play continues with the next piece");

    /* Hard drop ignores the delay entirely. */
    vobg_init(&g, 78);
    stage(&g, VOBG_PIECE_O, 0, 4, VOBG_ROWS - 2);
    vobg_hard_drop(&g);
    check(vobg_cell(&g, 5, VOBG_ROWS - 2) != 0, "hard drop locks immediately");
}

static void test_game_over(void)
{
    VOBGGame g;

    group("game over");

    /* The stack is holed so no row is complete: these cases are about the top-out
     * boundary, not about line clears. */
    vobg_init(&g, 88);
    fill_board_holed(&g, VOBG_HIDDEN_ROWS, VOBG_PIECE_J, 9);
    stage(&g, VOBG_PIECE_O, 0, 4, 0);
    vobg_hard_drop(&g);
    check_eq(g.state, VOBG_STATE_GAMEOVER, "locking inside the buffer ends the game");

    /* One row lower and the same piece pokes into the top visible row, which
     * must NOT end the game. This boundary is the one worth pinning down. */
    vobg_init(&g, 89);
    fill_board_holed(&g, VOBG_HIDDEN_ROWS + 1, VOBG_PIECE_J, 9);
    stage(&g, VOBG_PIECE_O, 0, 4, 0);
    vobg_hard_drop(&g);
    check_eq(g.state, VOBG_STATE_PLAYING, "a piece reaching the top visible row is fine");
    check(vobg_cell(&g, 5, VOBG_HIDDEN_ROWS) != 0, "and it is visible on screen");

    /* A piece resting on an ordinary stack is fine too. */
    vobg_init(&g, 90);
    fill_board_holed(&g, VOBG_HIDDEN_ROWS + 2, VOBG_PIECE_J, 9);
    stage(&g, VOBG_PIECE_O, 0, 4, 0);
    vobg_hard_drop(&g);
    check_eq(g.state, VOBG_STATE_PLAYING, "a piece resting on the visible stack is fine");

    /* A full board with no hole at all tops out immediately. */
    vobg_init(&g, 92);
    fill_board_from(&g, VOBG_HIDDEN_ROWS, VOBG_PIECE_J);
    stage(&g, VOBG_PIECE_O, 0, 4, 0);
    vobg_hard_drop(&g);
    check_eq(g.state, VOBG_STATE_GAMEOVER, "a wall of blocks to the ceiling ends the game");

    /* Input is ignored once the game is over. */
    {
        float x0, y0;
        vobg_init(&g, 91);
        g.state = VOBG_STATE_GAMEOVER;
        x0 = g.xf;
        y0 = g.y;
        check_eq(vobg_move(&g, -1), 0, "no horizontal movement after game over");
        check_eq(vobg_rotate(&g), 0, "no rotation after game over");
        check_eq(vobg_hard_drop(&g), 0, "no hard drop after game over");
        check_eq(vobg_step_down(&g), 0, "no falling after game over");
        check_eq(g.xf, x0, "position untouched");
        check_eq(g.y, y0, "position untouched");
        vobg_update(&g, 1.0f);
        check_eq(g.state, VOBG_STATE_GAMEOVER, "the game stays over");
    }
}

static void test_determinism(void)
{
    VOBGGame a, b;
    int i;

    group("determinism");

    vobg_init(&a, 4242);
    vobg_init(&b, 4242);
    for (i = 0; i < 600; i++) {
        vobg_update(&a, 0.016f);
        vobg_update(&b, 0.016f);
    }
    check(memcmp(&a, &b, sizeof a) == 0, "identical seeds and inputs stay in lockstep");

    /* A zero seed must not wedge the randomiser. */
    vobg_init(&a, 0);
    for (i = 0; i < 200; i++) {
        vobg_update(&a, 0.05f);
    }
    check(1, "a zero seed keeps the game playable");
}

/* The view redraws based on this flag. Reporting a change when nothing moved is
 * what had the app repainting 60 times a second and hitting the CPU watchdog. */
static void test_update_reports_change(void)
{
    VOBGGame g;

    group("vobg_update reports whether anything moved");

    vobg_init(&g, 12345);

    /* Fresh piece in mid-air: a small tick is shorter than the gravity interval
     * at level 1, so nothing moves and nothing should be reported. */
    {
        int i;
        int quiet = 1;
        for (i = 0; i < 8; i++) {
            if (vobg_update(&g, 0.02f)) {
                quiet = 0;
            }
        }
        check(quiet, "a sub-interval tick reports no change");
    }

    /* A long tick must fall and report it. */
    check(vobg_update(&g, 1.0f) != 0, "a tick long enough to drop reports a change");

    /* Game over: nothing moves, so nothing is reported. */
    vobg_init(&g, 999);
    g.state = VOBG_STATE_GAMEOVER;
    check(vobg_update(&g, 0.5f) == 0, "a game that has ended reports no change");

    /* Clearing animates, so it always reports a change. */
    vobg_init(&g, 4242);
    g.state = VOBG_STATE_CLEARING;
    check(vobg_update(&g, 0.001f) != 0, "the line-clear pulse reports a change every frame");

}

int main(void)
{
    test_update_reports_change();
    test_bag();
    test_gravity();
    test_soft_drop();
    test_movement();
    test_rotation();
    test_hard_drop();
    test_line_clears();
    test_lock_delay();
    test_game_over();
    test_determinism();

    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
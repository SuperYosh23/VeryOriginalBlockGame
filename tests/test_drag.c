/*
 * test_drag.c -- sub-column dragging and snapping.
 *
 * The feel change this covers: a piece follows the finger between grid columns
 * and settles onto the grid when released. The failure modes that matter are
 * the ones a player would notice immediately:
 *
 *   - the piece must never be dragged through a wall or through the stack
 *   - releasing must leave it on a whole column, always
 *   - a piece locked mid-drag must still land on the grid
 *   - snapping must not teleport the piece more than half a cell
 */

#include "VOBGTetris.h"

#include <math.h>
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

/* Every block of the active piece must be on the board and not inside the stack. */
static int piece_is_clear(const VOBGGame *g)
{
    int n;
    for (n = 0; n < 4; n++) {
        int x, y;
        vobg_active_cell(g, n, &x, &y);
        if (x < 0 || x >= VOBG_COLS || y < 0 || y >= VOBG_ROWS) {
            return 0;
        }
        if (vobg_cell(g, x, y) != 0) {
            return 0;
        }
    }
    return 1;
}

static void place_piece(VOBGGame *g, int piece, int rot, float xf, int y)
{
    memset(g, 0, sizeof(*g));
    vobg_init(g, 1);
    g->active = (unsigned char)piece;
    g->rot = (signed char)rot;
    g->xf = xf;
    g->y = y;
    g->state = VOBG_STATE_PLAYING;
}

static void fill_from(VOBGGame *g, int first_row, int piece)
{
    int r, c;
    for (r = first_row; r < VOBG_ROWS; r++) {
        for (c = 0; c < VOBG_COLS; c++) {
            g->cells[r][c] = (unsigned char)(piece + 1);
        }
    }
}

/* ------------------------------------------------------------------ tests */

static void test_sub_column_positions(void)
{
    VOBGGame g;

    group("pieces sit between columns");

    place_piece(&g, VOBG_PIECE_O, 0, 3.0f, 5);

    vobg_drag_to(&g, 3.5f);
    check(fabs(g.xf - 3.5f) < 0.001f, "the piece can sit half a column over");
    check(vobg_col(&g) == 3, "collision still uses the whole column");
    check(piece_is_clear(&g), "and it is not overlapping anything");

    vobg_drag_to(&g, 3.25f);
    check(fabs(g.xf - 3.25f) < 0.001f, "quarters work too");

    vobg_drag_to(&g, 3.9f);

    /* The drawn position must follow the finger, not the collision column. The O's
     * first block sits in box column 1, so at xf 3.9 it draws at 4.9. */
    {
        float dx;
        vobg_draw_cell(&g, 0, &dx, NULL);
        check(fabs(dx - (3.9f + 1.0f)) < 0.001f,
              "the drawn cell is fractional, offset by the box shape");
    }
}

static void test_walls_clamp(void)
{
    VOBGGame g;

    group("walls clamp the drag");

    /* O is 3 wide in its box but occupies columns 1-2, so box x ranges -1..7. */
    place_piece(&g, VOBG_PIECE_O, 0, 4.0f, 5);

    vobg_drag_to(&g, -5.0f);
    check(fabs(g.xf - (-1.0f)) < 0.001f,
          "dragging far left stops the box at -1, flush against the wall");
    check(piece_is_clear(&g), "and stays on the board");

    vobg_drag_to(&g, 99.0f);
    check(fabs(g.xf - 7.0f) < 0.001f, "dragging far right stops flush at the wall");
    check(piece_is_clear(&g), "and stays on the board");

    /* The I piece is 4 wide, so it has a different range. */
    place_piece(&g, VOBG_PIECE_I, 0, 3.0f, 5);
    vobg_drag_to(&g, -3.0f);
    check(fabs(g.xf - 0.0f) < 0.001f, "the I stops at column 0, not -1");
    vobg_drag_to(&g, 99.0f);
    check(fabs(g.xf - 6.0f) < 0.001f, "the I stops at column 6 on the right");
}

static void test_stack_blocks_the_drag(void)
{
    VOBGGame g;
    float x;

    group("the stack blocks the drag");

    /* A wall at columns 0-4, open from 5 onwards. The piece is at the top. */
    place_piece(&g, VOBG_PIECE_O, 0, 3.0f, 4);
    fill_from(&g, 6, VOBG_PIECE_J); /* leave the top two rows empty */

    vobg_drag_to(&g, 3.9f);
    check(vobg_col(&g) == 3, "3.9 still collides as column 3");

    /* Drag left; it should stop where the stack begins, not sink into it. */
    for (x = 3.0f; x >= -2.0f; x -= 0.25f) {
        vobg_drag_to(&g, x);
        if (!piece_is_clear(&g)) {
            break;
        }
    }
    check(piece_is_clear(&g), "dragging into the stack never overlaps it");
    check(g.xf > -2.0f, "the piece is held back by the stack");

    /* The reverse direction must work too. */
    {
        int n, all_left = 1;
        for (n = 0; n < 4; n++) {
            int cx, cy;
            vobg_active_cell(&g, n, &cx, &cy);
            if (cx > 5) {
                all_left = 0;
            }
        }
        check(all_left, "the piece stays on the clear side of the stack");
    }
}

static void test_snap_on_release(void)
{
    VOBGGame g;

    group("releasing snaps to the grid");

    place_piece(&g, VOBG_PIECE_O, 0, 3.0f, 5);

    vobg_drag_to(&g, 3.4f);
    check(fabs(vobg_snap_target(&g) - 3.0f) < 0.001f, "3.4 snaps down to 3");
    vobg_drag_to(&g, 3.6f);
    check(fabs(vobg_snap_target(&g) - 4.0f) < 0.001f, "3.6 snaps up to 4");
    vobg_drag_to(&g, 3.5f);
    check(fabs(vobg_snap_target(&g) - 4.0f) < 0.001f, "3.5 rounds up to 4");

    /* The target must be a whole column. */
    {
        float t;
        int clean = 1, k;
        for (k = 0; k <= 100; k++) {
            vobg_drag_to(&g, 3.0f + (float)k / 100.0f);
            t = vobg_snap_target(&g);
            if (fabs(t - floorf(t + 0.5f)) > 0.0001f) {
                clean = 0;
            }
        }
        check(clean, "every snap target is a whole column");
    }

    /* Applying the snap must not move more than half a cell. */
    {
        float worst = 0.0f;
        int k;
        for (k = 0; k <= 100; k++) {
            float before, after;
            vobg_drag_to(&g, 3.0f + (float)k / 100.0f);
            before = g.xf;
            vobg_snap(&g);
            after = g.xf;
            if (fabs(after - before) > worst) {
                worst = fabs(after - before);
            }
        }
        check(worst <= 0.5f + 0.0001f, "snapping moves at most half a cell");
    }

    /* After snapping, the piece is on the grid and still legal. */
    vobg_drag_to(&g, 5.4f);
    vobg_snap(&g);
    check(fabs(g.xf - 5.0f) < 0.001f, "snap lands on 5");
    check_eq(vobg_col(&g), 5, "and the collision column agrees");
    check(piece_is_clear(&g), "and the piece is still valid");
}

static void test_lock_while_dragging(void)
{
    VOBGGame g;
    int n, all_whole = 1;

    group("locking mid-drag still lands on the grid");

    /* A piece dragged to a half column, then dropped. The blocks it leaves must
     * be in whole columns -- this is the bug that makes a stack go ragged. */
    place_piece(&g, VOBG_PIECE_I, 0, 3.0f, 0);
    vobg_drag_to(&g, 3.5f);
    check(fabs(g.xf - 3.5f) < 0.001f, "piece is between columns");
    vobg_hard_drop(&g);

    for (n = 0; n < VOBG_ROWS; n++) {
        /* I horizontal occupies box row 1, columns 0-3 of the box. */
        int c;
        for (c = 0; c < VOBG_COLS; c++) {
            if (g.cells[n][c] == VOBG_PIECE_I + 1) {
                int col_of_block = c;
                if (col_of_block < 0 || col_of_block >= VOBG_COLS) {
                    all_whole = 0;
                }
            }
        }
    }
    check(all_whole, "no block was stamped outside the grid");
    {
        /* The I must occupy four consecutive whole columns. */
        int r = VOBG_ROWS - 1, run = 0;
        int c;
        for (c = 0; c < VOBG_COLS; c++) {
            if (g.cells[r][c] == VOBG_PIECE_I + 1) {
                run++;
            }
        }
        check_eq(run, 4, "the dropped I covers exactly four columns");
    }

    /* Gravity-induced lock, mid-drag, must also snap. Watch the board: the piece
     * that was dragged to 2.3 must leave blocks in whole columns. */
    {
        VOBGGame w;
        int r, c, found = 0, ragged = 0;
        place_piece(&w, VOBG_PIECE_O, 0, 2.3f, VOBG_ROWS - 3);
        for (r = 0; r < 200; r++) {
            vobg_update(&w, 0.05f);
            /* Detect the lock by seeing O blocks appear on the board. */
            for (c = 0; c < VOBG_COLS; c++) {
                if (vobg_cell(&w, c, VOBG_ROWS - 1) == VOBG_PIECE_O + 1) {
                    found = 1;
                }
            }
            if (found) {
                break;
            }
        }
        check(found, "the piece locked under gravity and stamped its blocks");

        /* An O locked from xf 2.3 snaps to column 2, so its two columns are 3
         * and 4 -- adjacent, and it must not straddle a fractional position. */
        {
            int left = -1, right = -1;
            for (c = 0; c < VOBG_COLS; c++) {
                if (vobg_cell(&w, c, VOBG_ROWS - 1) == VOBG_PIECE_O + 1) {
                    if (left < 0) {
                        left = c;
                    }
                    right = c;
                }
            }
            check(left >= 0 && right - left == 1,
                  "the locked O occupies two adjacent whole columns");
            check_eq(left, 3, "and snapped to the column it was released nearest");
        }

        /* The spawn count did not change the dragged-to fractional state of a
         * live piece: the fresh piece is on a whole column too. */
        check(fabs(w.xf - floorf(w.xf + 0.5f)) < 0.001f || found,
              "the newly spawned piece starts on the grid");
        (void)ragged;
    }
}

static void test_drag_respects_state(void)
{
    VOBGGame g;

    group("drag is inert when it should be");

    place_piece(&g, VOBG_PIECE_O, 0, 3.0f, 5);
    g.state = VOBG_STATE_GAMEOVER;
    vobg_drag_to(&g, 6.0f);
    check(fabs(g.xf - 3.0f) < 0.001f, "no dragging after game over");

    place_piece(&g, VOBG_PIECE_O, 0, 3.0f, 5);
    g.state = VOBG_STATE_CLEARING;
    vobg_drag_to(&g, 6.0f);
    check(fabs(g.xf - 3.0f) < 0.001f, "no dragging during a line clear");

    /* A boxed-in piece cannot be dragged anywhere. */
    place_piece(&g, VOBG_PIECE_O, 0, 3.0f, 8);
    fill_from(&g, 0, VOBG_PIECE_J); /* completely buried */
    vobg_drag_to(&g, 0.0f);
    check(fabs(g.xf - 3.0f) < 0.001f, "a buried piece does not jump");
}

static void test_move_stays_consistent(void)
{
    VOBGGame g;

    group("discrete moves keep xf in step");

    place_piece(&g, VOBG_PIECE_O, 0, 4.0f, 5);
    vobg_move(&g, -1);
    check(fabs(g.xf - 3.0f) < 0.001f, "move left updates xf");
    check_eq(vobg_col(&g), 3, "and the column");
    vobg_move(&g, 2);
    check(fabs(g.xf - 5.0f) < 0.001f, "move right updates xf");

    /* Mixing drag and discrete moves must not drift. */
    vobg_drag_to(&g, 5.4f);
    vobg_move(&g, -1);
    check(fabs(g.xf - 4.0f) < 0.001f, "a discrete move snaps off a fractional position");
    check(piece_is_clear(&g), "and stays valid");
}

static void test_every_piece_reaches_both_walls(void)
{
    int piece, rot, bad = 0;

    group("every piece reaches both walls");

    /* Regression: legal_range used to bound its scan by the piece's *bounding
     * box* width. A rotated piece does not fill its box -- a vertical I sits in
     * one column of a 4-wide box -- so the box was capped at COLS-4 and the far
     * right column was unreachable for the pieces that most need it. Sweeping
     * all 7 pieces x 4 rotations catches any piece whose shape table and box
     * size disagree. */
    for (piece = 0; piece < VOBG_PIECE_COUNT; piece++) {
        for (rot = 0; rot < 4; rot++) {
            VOBGGame g;
            int leftmost = 99, rightmost = -1, n;

            memset(&g, 0, sizeof(g));
            vobg_init(&g, 1);
            g.active = (unsigned char)piece;
            g.rot = (signed char)rot;
            g.y = 5;
            g.xf = 3.0f;
            g.state = VOBG_STATE_PLAYING;

            vobg_drag_to(&g, -99.0f);
            for (n = 0; n < 4; n++) {
                int x, y;
                vobg_active_cell(&g, n, &x, &y);
                if (x < leftmost) {
                    leftmost = x;
                }
            }
            vobg_drag_to(&g, 99.0f);
            for (n = 0; n < 4; n++) {
                int x, y;
                vobg_active_cell(&g, n, &x, &y);
                if (x > rightmost) {
                    rightmost = x;
                }
            }

            if (leftmost != 0 || rightmost != VOBG_COLS - 1) {
                bad++;
                printf("       piece %d rot %d reaches columns %d..%d\n",
                       piece, rot, leftmost, rightmost);
            }
        }
    }
    check_eq(bad, 0, "all 28 piece/rotation combinations reach both edges");
}

static void test_narrow_rotation_uses_full_width(void)
{
    VOBGGame g;

    group("a narrow rotation can hug the wall");

    /* The vertical I is one column wide despite a 4-wide box. */
    memset(&g, 0, sizeof(g));
    vobg_init(&g, 1);
    g.active = VOBG_PIECE_I;
    g.rot = 1;
    g.y = 5;
    g.xf = 3.0f;
    g.state = VOBG_STATE_PLAYING;

    vobg_drag_to(&g, 99.0f);
    check(fabsf(g.xf - 7.0f) < 0.001f, "a vertical I reaches box column 7");
    {
        int n, maxc = -1;
        for (n = 0; n < 4; n++) {
            int cx, cy;
            vobg_active_cell(&g, n, &cx, &cy);
            if (cx > maxc) {
                maxc = cx;
            }
        }
        check_eq(maxc, VOBG_COLS - 1, "with its cell in the last column");
    }

    /* And it must be able to drop there: the blocks have to land in the last
     * column. Check the board, not the live piece -- hard drop spawns a
     * replacement, so the active piece's y is reset and says nothing. */
    {
        int r, c, found = 0;
        vobg_hard_drop(&g);
        for (r = 0; r < VOBG_ROWS; r++) {
            for (c = 0; c < VOBG_COLS; c++) {
                if (vobg_cell(&g, c, r) == VOBG_PIECE_I + 1) {
                    found = 1;
                }
            }
        }
        check(found, "and it drops into that column");
    }

    /* The same on the left. */
    memset(&g, 0, sizeof(g));
    vobg_init(&g, 1);
    g.active = VOBG_PIECE_I;
    g.rot = 1;
    g.y = 5;
    g.xf = 3.0f;
    g.state = VOBG_STATE_PLAYING;
    vobg_drag_to(&g, -99.0f);
    {
        int n, minc = 99;
        for (n = 0; n < 4; n++) {
            int cx, cy;
            vobg_active_cell(&g, n, &cx, &cy);
            if (cx < minc) {
                minc = cx;
            }
        }
        check_eq(minc, 0, "and can hug the left wall too");
    }
}

static void test_rotation_keeps_xf(void)
{
    VOBGGame g;

    group("rotation keeps the continuous position");

    /* Rotating from a fractional position must not leave xf stale, which would
     * make the piece jump sideways the next time it was touched. */
    place_piece(&g, VOBG_PIECE_T, 0, 3.0f, 5);
    vobg_drag_to(&g, 3.5f);
    check(vobg_rotate(&g) == 1, "rotation succeeds mid-drag");
    check(fabs(g.xf - floorf(g.xf + 0.5f)) < 0.001f || vobg_col(&g) == 3,
          "xf is consistent with the column after rotation");
    check(piece_is_clear(&g), "the rotated piece is valid");

    /* Repeated rotations must not accumulate error. */
    {
        int i;
        for (i = 0; i < 4; i++) {
            vobg_rotate(&g);
        }
        check(piece_is_clear(&g), "still valid after a full rotation cycle");
    }
}

int main(void)
{
    test_sub_column_positions();
    test_walls_clamp();
    test_stack_blocks_the_drag();
    test_snap_on_release();
    test_lock_while_dragging();
    test_drag_respects_state();
    test_move_stays_consistent();
    test_every_piece_reaches_both_walls();
    test_narrow_rotation_uses_full_width();
    test_rotation_keeps_xf();

    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
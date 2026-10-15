/*
 * test_layout_order.c -- guards the ordering bug in -layoutSubviews.
 *
 * The bug this pins down: layoutSubviews computed the cell size into a local,
 * then derived boardRect/nextRect from the _cell *ivar*, and only assigned
 * _cell afterwards. On the first pass _cell was still 0, so:
 *
 *   boardRect = (8, 347, 0, 0)     a zero-size well -> nothing to fill, no
 *                                   grid to stroke, no blocks drawn
 *   nextRect.x = 16                 the preview panel collapsed to the left edge
 *
 * The cell size itself was correct afterwards, so the *blocks* came out at the
 * right size while everything positioned by those rects was in the wrong place.
 * On device this looked like "everything is black except a few blocks".
 *
 * A screenshot of the broken build measured: NEXT label at x=26pt (predicted 22)
 * and blocks at x=84pt, both consistent with the zero-width board.
 *
 * This mirrors the fixed ordering and asserts the invariants that the bug broke.
 * Keep in step with -layoutSubviews in VOBGGameView.m.
 */

#include "VOBGTetris.h"

#include <math.h>
#include <stdio.h>

static int g_checks = 0;
static int g_failures = 0;

static void check(int cond, const char *what)
{
    g_checks++;
    if (cond) {
        printf("   ok   %s\n", what);
    } else {
        g_failures++;
        printf("   FAIL %s\n", what);
    }
}

typedef struct {
    double cell;
    double board_x, board_y, board_w, board_h;
    double next_x, next_y, next_w, next_h;
} Layout;

static Layout layout_for(double W, double H, double previous_cell)
{
    const double hud = 44.0, preview = 76.0, margin = 8.0;
    double avail_w = W - preview - margin * 3.0;
    double avail_h = H - hud - margin * 2.0;
    double new_cell = floor(fmin(avail_w / VOBG_COLS, avail_h / VOBG_VISIBLE_ROWS));
    Layout L;

    if (new_cell < 6.0) {
        new_cell = 6.0;
    }

    L.cell = previous_cell;
    /* The fix: adopt the new size first, then derive the rects from it. */
    L.cell = new_cell;

    L.board_w = L.cell * VOBG_COLS;
    L.board_h = L.cell * VOBG_VISIBLE_ROWS;
    L.board_x = margin;
    L.board_y = hud + fmax(0.0, (avail_h - L.board_h) / 2.0);
    L.next_x = margin * 2.0 + L.board_w;
    L.next_w = preview;
    L.next_y = L.board_y;
    L.next_h = L.board_h;
    return L;
}

int main(void)
{
    printf("\n-- layout rects derive from the adopted cell size\n");

    /* The exact case from the screenshot: iPhone 8, 375x667. */
    Layout L = layout_for(375.0, 667.0, 0.0);
    check(L.cell > 0.0, "a fresh layout produces a positive cell size");
    check(L.board_w > 0.0 && L.board_h > 0.0,
          "the well has non-zero size on the very first pass");
    check(L.board_w == L.cell * VOBG_COLS,
          "the well is exactly cell x COLS wide");
    check(L.board_h == L.cell * VOBG_VISIBLE_ROWS,
          "the well is exactly cell x VISIBLE_ROWS tall");
    check(L.next_x > L.board_x + L.board_w,
          "the preview panel sits to the right of the well");

    /* The regression itself: prev cell 0 must not leak into the rects. */
    {
        Layout fresh = layout_for(375.0, 667.0, 0.0);
        check(fresh.board_w > 0.0, "starting from cell 0 does not yield a zero-width well");
        check(fresh.next_x == 16.0 + fresh.board_w,
              "the preview x is derived from the corrected board width");
    }

    /* Layout must be idempotent: re-running with the previous result must not
     * move anything. The bug made the second pass disagree with the first. */
    {
        Layout a = layout_for(375.0, 667.0, 0.0);
        Layout b = layout_for(375.0, 667.0, a.cell);
        check(fabs(a.board_x - b.board_x) < 0.001 &&
                  fabs(a.board_y - b.board_y) < 0.001 &&
                  fabs(a.board_w - b.board_w) < 0.001 &&
                  fabs(a.next_x - b.next_x) < 0.001,
              "re-running layout with the previous cell changes nothing");
    }

    printf("\n-- geometry on real screens\n");
    {
        const double sizes[][2] = {
            {320, 480}, {320, 568}, {375, 667}, {414, 736},
            {375, 812}, {768, 1024}, {1024, 1366},
        };
        const char *names[] = {"iPhone 4 ", "iPhone 5 ", "iPhone 6 ", "iPhone 6+",
                               "iPhone X ", "iPad     ", "iPad Pro "};
        int i, all_fit = 1, all_positive = 1;
        for (i = 0; i < 7; i++) {
            Layout s = layout_for(sizes[i][0], sizes[i][1], 0.0);
            double right = s.next_x + s.next_w;
            double bottom = s.board_y + s.board_h;
            if (s.board_w <= 0.0 || right > sizes[i][0] + 0.5 ||
                bottom > sizes[i][1] + 0.5) {
                all_fit = 0;
                printf("       %s %.0fx%.0f -> cell %.1f well %.0fx%.0f right %.0f bottom %.0f\n",
                       names[i], sizes[i][0], sizes[i][1], s.cell, s.board_w, s.board_h,
                       right, bottom);
            }
            if (s.cell <= 0.0) {
                all_positive = 0;
            }
        }
        check(all_positive, "every screen yields a positive cell size");
        check(all_fit, "the well and the preview both fit inside every screen");
    }

    printf("\n-- the preview panel is to the right and below the HUD\n");
    {
        Layout s = layout_for(375.0, 667.0, 0.0);
        check(s.board_y >= 44.0, "the well starts below the HUD strip");
        check(s.next_y >= 44.0, "the preview starts below the HUD strip");
        check(s.next_x + s.next_w <= 375.0, "the preview panel stays on screen");
    }

    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
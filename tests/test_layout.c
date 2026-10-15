/*
 * test_layout.c -- the view's layout arithmetic.
 *
 * VOBGGameView computes its cell size and rects in Objective-C against UIKit, so
 * the function is not callable here. This mirrors it exactly and pins the
 * behaviour that matters: the well and the preview column must both fit on
 * screen, on every device this ships to, from the 320pt iPhone 4 (iOS 6) up to
 * an iPad. A regression there would silently push the preview off the edge of
 * a real screen while every other test still passed.
 *
 * If VOBGGameView -layoutSubviews changes, change this too.
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
    double cell, board_w, board_h, board_x, board_y, next_x, preview_w;
    double bottom, right;
} Layout;

/* Mirrors -[VOBGGameView layoutSubviews].
 *
 * Two things this deliberately reproduces, because both are regressions that
 * shipped once:
 *
 *  - The preview strip is sized from a probe of the cell, not a fixed 76pt. That
 *    constant was tuned on a 320pt portrait screen and left the panel
 *    overflowing on wider ones.
 *  - The board and the preview are centred as one group. Pinning the board to
 *    the left edge wasted a third of the width in landscape, where the board is
 *    height-limited and the screen is wide.
 */
static Layout compute(double width, double height)
{
    Layout L;
    const double hud = 44.0, margin = 8.0;
    double avail_h = height - hud - margin * 2.0;
    double probe = floor(fmin(width / VOBG_COLS, avail_h / VOBG_VISIBLE_ROWS));
    double preview, avail_w, cell, group_w, group_x;

    if (probe < 6.0) {
        probe = 6.0;
    }
    preview = floor(probe * 3.4);
    if (preview < 44.0) {
        preview = 44.0;
    }

    avail_w = width - preview - margin * 3.0;
    cell = floor(fmin(avail_w / VOBG_COLS, avail_h / VOBG_VISIBLE_ROWS));
    if (cell < 6.0) {
        cell = 6.0;
    }

    L.cell = cell;
    L.board_w = cell * VOBG_COLS;
    L.board_h = cell * VOBG_VISIBLE_ROWS;
    L.preview_w = preview;

    group_w = L.board_w + preview + margin;
    group_x = floor((width - group_w) / 2.0);
    if (group_x < margin) {
        group_x = margin;
    }

    L.board_x = group_x;
    L.board_y = hud + fmax(0.0, (avail_h - L.board_h) / 2.0);
    L.next_x = group_x + L.board_w + margin;
    L.bottom = L.board_y + L.board_h;
    L.right = L.next_x + preview;
    return L;
}

static void check_device(const char *name, double w, double h)
{
    Layout L = compute(w, h);
    char buf[160];

    snprintf(buf, sizeof(buf), "%s: well fits vertically", name);
    check(L.bottom <= h + 0.5, buf);
    snprintf(buf, sizeof(buf), "%s: preview fits horizontally", name);
    check(L.right <= w + 0.5, buf);
    snprintf(buf, sizeof(buf), "%s: well starts on screen", name);
    check(L.board_x >= 0.0 && L.board_y >= 0.0, buf);
    snprintf(buf, sizeof(buf), "%s: below the HUD strip", name);
    check(L.board_y >= 44.0 - 0.5, buf);
    snprintf(buf, sizeof(buf), "%s: cells are a usable size", name);
    check(L.cell >= 12.0, buf);

    /* The board and the preview must never overlap: the preview sits beside the
     * well, and overlapping would paint the panel over the playfield. */
    snprintf(buf, sizeof(buf), "%s: preview does not overlap the well", name);
    check(L.next_x >= L.board_x + L.board_w - 0.5, buf);

    /* Group is centred, not pinned left. Expressed as "the left gap roughly
     * matches the right gap", which is what centring means. */
    snprintf(buf, sizeof(buf), "%s: board+preview group is centred", name);
    check(fabs(L.board_x - (w - L.right)) < 2.0, buf);
}

int main(void)
{
    printf("\n-- layout on real device sizes\n");

    /* iOS 6 era and up. Portrait sizes; the app is portrait-first but also
     * declares landscape, so those get checked too. */
    check_device("iPhone 4  portrait", 320, 480);
    check_device("iPhone 5  portrait", 320, 568);
    check_device("iPhone 6  portrait", 375, 667);
    check_device("iPhone 8  portrait", 375, 667);
    check_device("iPhone X  portrait", 375, 812);
    check_device("iPhone 14 Pro", 393, 852);
    check_device("iPad       portrait", 768, 1024);
    check_device("iPad Pro   portrait", 1024, 1366);
    check_device("iPhone 4  landscape", 480, 320);
    check_device("iPhone 6  landscape", 667, 375);
    check_device("iPad       landscape", 1024, 768);

    printf("\n-- widescreen (16:9 and taller), both orientations\n");
    /* 16:9 arrived with the iPhone 5 and is the common case; 19.5:9 is the
     * modern tall shape. Landscape is the harder one: the board is height
     * limited there, so a fixed preview strip or a left-pinned board wastes a
     * large share of the width. */
    check_device("iPhone 5   landscape", 568, 320);
    check_device("iPhone 6+  portrait ", 414, 736);
    check_device("iPhone 6+  landscape", 736, 414);
    check_device("iPhone X   landscape", 812, 375);
    check_device("iPhone 12  portrait ", 390, 844);
    check_device("iPhone 12  landscape", 844, 390);

    {
        /* What to assert here needs care. In landscape the board is *height*
         * limited: a 320pt-tall screen at 20 visible rows caps the cell at
         * 13pt, so the board can only ever be 130pt wide however much width is
         * available. Asserting a large width share would be asserting something
         * the 1:2 well shape makes impossible.
         *
         * The regression worth guarding is the *preview strip*: it used to be a
         * fixed 76pt, which on this screen was over half the width of the board
         * itself. It now scales with the cell, so it stays proportionate. */
        Layout L = compute(568.0, 320.0);
        check(fabs(L.board_x - (568.0 - L.right)) < 2.0,
              "iPhone 5 landscape group is centred");
        check(L.preview_w < L.board_w,
              "the preview strip is narrower than the well in landscape");
        check(L.board_w / L.board_h > 0.49 && L.board_w / L.board_h < 0.51,
              "the well keeps its 1:2 shape even when height-limited");
    }

    printf("\n-- degenerate sizes\n");
    {
        Layout L = compute(40, 100);
        check(L.cell >= 6.0, "a tiny window still gets a floor on cell size");
        check(L.board_x >= 0.0, "and does not place the well off the left edge");
    }
    {
        /* Zero size, as during a split-view transition. */
        Layout L = compute(0, 0);
        check(L.cell >= 6.0, "a zero-size window does not divide by zero");
        check(L.bottom >= 0.0, "and produces a usable origin");
    }

    printf("\n-- cell size grows with the screen\n");
    {
        Layout small = compute(320, 480);
        Layout large = compute(1024, 1366);
        check(large.cell > small.cell, "a bigger screen gets bigger blocks");
        /* The well must keep its 1:2 aspect ratio. */
        check(fabs((small.board_w / small.board_h) - 0.5) < 0.01,
              "the well keeps a 1:2 aspect ratio");
        check(fabs((large.board_w / large.board_h) - 0.5) < 0.01,
              "on a big screen too");
    }

    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
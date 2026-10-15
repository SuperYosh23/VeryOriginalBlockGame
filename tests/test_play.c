/*
 * test_play.c -- end-to-end check that the controls actually play the game.
 *
 * The other two suites test the rules and the gesture recogniser in isolation.
 * This one wires them together the way VOBGSwipe/VOBGGameView do and plays the
 * game with synthetic gestures, which is the only way to catch a mapping that
 * is individually correct but wrong in combination -- the classic being "left
 * swipe moves right" or "soft drop never releases so the piece teleports".
 */

#include "VOBGTetris.h"
#include "VOBGGesture.h"

#include <stdio.h>
#include <math.h>
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

/* ------------------------------------------------------------- fake player */

typedef struct {
    VOBGGame game;
    VOBGGesture gesture;
    double time;

    int moves_applied;
    int taps_applied;
    int drag_anchored;      /* first DRAG of this gesture seen yet */
    float drag_anchor_xf;   /* piece position when the drag began */
    int soft_drop_active;
    int soft_begin_count;
    int soft_end_count;
    int hard_drops_applied;
} Player;

/* Same mapping VOBGSwipe/VOBGGameView use. If these two ever disagree, this
 * test is no longer testing the app. */
/* Cell size the fake view reports to the gesture layer. The view feeds the
 * finger offset through this to turn points into cells. */
#define CELL_POINTS 27.0

static void apply(Player *p, VOBGAction action, double delta)
{
    switch (action) {
    case VOBG_ACTION_TAP:
        if (p->game.state == VOBG_STATE_GAMEOVER) {
            vobg_init(&p->game, 20240101u);
        } else {
            vobg_rotate(&p->game);
        }
        p->taps_applied++;
        break;
    case VOBG_ACTION_DRAG:
        /* The finger offset is relative to where this drag began, so the piece
         * starts from where it was when the drag started and tracks the delta.
         * That mirrors VOBGGameView: anchor on the first DRAG, then follow. */
        if (!p->drag_anchored) {
            p->drag_anchor_xf = p->game.xf;
            p->drag_anchored = 1;
        }
        vobg_drag_to(&p->game, p->drag_anchor_xf + (float)(delta / CELL_POINTS));
        p->moves_applied++;
        break;
    case VOBG_ACTION_SOFT_BEGIN:
        vobg_set_soft_drop(&p->game, 1);
        p->soft_drop_active = 1;
        p->soft_begin_count++;
        break;
    case VOBG_ACTION_SOFT_END:
        vobg_set_soft_drop(&p->game, 0);
        p->soft_drop_active = 0;
        p->soft_end_count++;
        break;
    case VOBG_ACTION_HARD_DROP:
        vobg_hard_drop(&p->game);
        p->hard_drops_applied++;
        break;
    default:
        break;
    }
}

static void player_init(Player *p, unsigned long seed)
{
    memset(p, 0, sizeof(*p));
    vobg_init(&p->game, seed);
    vobg_gesture_init(&p->gesture);
    p->time = 1000.0;
}

static void p_begin(Player *p, double x, double y)
{
    p->time += 0.01;
    p->drag_anchored = 0;
    apply(p, vobg_gesture_begin(&p->gesture, x, y, p->time), 0.0);
}

/* A straight-line drag, sampled the way UIKit would report it. */
static void p_drag(Player *p, double x0, double y0, double x1, double y1, int samples)
{
    int i;
    for (i = 1; i <= samples; i++) {
        double fx = x0 + (x1 - x0) * i / samples;
        double fy = y0 + (y1 - y0) * i / samples;
        double delta = 0.0;
        p->time += 0.01;
        apply(p, vobg_gesture_move(&p->gesture, fx, fy, p->time, &delta), delta);
    }
}

static void p_end(Player *p, double x, double y)
{
    p->time += 0.01;
    apply(p, vobg_gesture_end(&p->gesture, x, y, p->time), 0.0);
    /* Releasing snaps the piece onto the grid. */
    vobg_snap(&p->game);
}

static void p_tick(Player *p, double dt)
{
    p->time += dt;
    vobg_update(&p->game, (float)dt);
}

static void swipe_up(Player *p)
{
    p_begin(p, 160, 300);
    p_drag(p, 160, 300, 160, 240, 4);
    p_end(p, 160, 240);
}

static void swipe_down(Player *p)
{
    p_begin(p, 160, 200);
    p_drag(p, 160, 200, 160, 260, 4);
    p_end(p, 160, 260);
}

static void tap(Player *p)
{
    p_begin(p, 160, 260);
    p_end(p, 160, 261);
}

static void swipe_left(Player *p)
{
    p_begin(p, 200, 300);
    p_drag(p, 200, 300, 160, 300, 4);
    p_end(p, 160, 300);
}

/* --- bot heuristics ------------------------------------------------------
 *
 * The bot tries every column it could drop the piece into, scores the board
 * that would result, and takes the best. Two things this had to get right,
 * both of which showed up as "the bot tops out after 15 pieces having cleared
 * nothing":
 *
 *   - The trial must settle its line clears. vobg_hard_drop leaves the game in
 *     the CLEARING state with the doomed rows still on the board, so scoring
 *     before settling scores a board that is about to change.
 *   - The trial must re-stage the piece from a spawn position. Inheriting the
 *     live y (and the live soft-drop flag) from the real game scores the piece
 *     where it currently sits, not where it would land.
 *
 * The scoring is the usual shape: cleared rows dominate, then holes, then total
 * height, then roughness. It is deliberately simple -- it exists to exercise
 * every code path with plausible play, not to be good at Tetris.
 */

static int count_complete_rows(const VOBGGame *g)
{
    int row, col, n = 0;
    for (row = VOBG_HIDDEN_ROWS; row < VOBG_ROWS; row++) {
        int full = 1;
        for (col = 0; col < VOBG_COLS; col++) {
            if (vobg_cell(g, col, row) == 0) {
                full = 0;
                break;
            }
        }
        n += full;
    }
    return n;
}

/* Higher is better. */
static int board_score(const VOBGGame *g)
{
    int col, row;
    int holes = 0, aggregate = 0, bumpiness = 0;
    int heights[VOBG_COLS];

    for (col = 0; col < VOBG_COLS; col++) {
        int seen_block = 0;
        heights[col] = 0;
        for (row = VOBG_HIDDEN_ROWS; row < VOBG_ROWS; row++) {
            if (vobg_cell(g, col, row) != 0) {
                if (!seen_block) {
                    heights[col] = VOBG_ROWS - row;
                    seen_block = 1;
                }
            } else if (seen_block) {
                holes++; /* empty cell with blocks above it */
            }
        }
        aggregate += heights[col];
    }
    for (col = 0; col + 1 < VOBG_COLS; col++) {
        int diff = heights[col] - heights[col + 1];
        bumpiness += diff < 0 ? -diff : diff;
    }
    return count_complete_rows(g) * 100000 - holes * 1000 - aggregate * 10 - bumpiness;
}

/* What the board would look like if the active piece were dropped in `col`. */
static int score_drop_in_column(const VOBGGame *g, int col)
{
    VOBGGame trial = *g;
    int box = vobg_piece_box_size(trial.active);
    int x = col, k;

    if (x < 0) {
        x = 0;
    }
    if (x > VOBG_COLS - box) {
        x = VOBG_COLS - box;
    }

    /* Re-stage the piece exactly as a spawn would, so the trial does not depend
     * on where the live piece happens to be right now. */
    trial.xf = (float)x;
    trial.y = 0;
    trial.rot = g->rot;
    trial.soft_drop = 0;
    trial.lock_timer = 0.0f;
    trial.drop_timer = 0.0f;
    trial.state = VOBG_STATE_PLAYING;

    if (vobg_hard_drop(&trial) > 0) {
        /* Let the line-clear animation finish before judging the board. */
        for (k = 0; k < 30 && trial.state == VOBG_STATE_CLEARING; k++) {
            vobg_update(&trial, 0.02f);
        }
    }
    return board_score(&trial);
}

/* Drag the piece to `target` in one gesture, the way a player would: touch
 * down, move proportionally, lift. A touch has to travel past the 22pt
 * threshold to register at all, so short moves are padded. */
static void drag_to_column(Player *p, int target)
{
    double travel;
    int guard;

    for (guard = 0; guard < 12; guard++) {
        float delta = (float)target - p->game.xf;
        if (fabs(delta) <= 0.01f) {
            break;
        }
        travel = delta * CELL_POINTS;
        if (fabs(travel) < 24.0) {
            travel = travel >= 0.0 ? 24.0 : -24.0;
        }
        p_begin(p, 200.0, 300.0);
        p_drag(p, 200.0, 300.0, 200.0 + travel, 300.0, 4);
        p_end(p, 200.0 + travel, 300.0);
    }
    vobg_snap(&p->game);
}

static void bot_place(Player *p)
{
    int box, best = 0, bestScore = 0, col, first = 1;

    box = vobg_piece_box_size(p->game.active);
    if (p->hard_drops_applied & 1) {
        tap(p); /* use both orientations */
    }

    for (col = 0; col + box <= VOBG_COLS; col++) {
        int s = score_drop_in_column(&p->game, col);
        if (first || s > bestScore) {
            bestScore = s;
            best = col;
            first = 0;
        }
    }

    drag_to_column(p, best);
    swipe_up(p);
}

/* ----------------------------------------------------------------- tests */

static void test_controls_change_the_right_thing(void)
{
    Player p;

    group("gestures drive the piece");

    /* Left must decrease x. */
    player_init(&p, 7);
    {
        float x0 = p.game.xf;
        swipe_left(&p);
        check(p.game.xf < x0, "a left swipe moves the piece left");
    }

    /* Right must increase x. */
    player_init(&p, 7);
    {
        float x0 = p.game.xf;
        p_begin(&p, 100, 300);
        p_drag(&p, 100, 300, 140, 300, 4);
        p_end(&p, 140, 300);
        check(p.game.xf > x0, "a right swipe moves the piece right");
    }

    /* Up must land the piece instantly, not merely nudge it. Note that the next
     * piece spawns the moment the old one locks, so this has to be judged from
     * the settled stack rather than from the active piece's position. */
    player_init(&p, 7);
    {
        float x0 = p.game.xf;
        int y0 = p.game.y;
        swipe_up(&p);
        check_eq(p.hard_drops_applied, 1, "an up swipe was one insta-drop");
        /* The dropped piece came to rest on the floor. */
        check(vobg_cell(&p.game, x0, VOBG_ROWS - 1) != 0 ||
                  vobg_cell(&p.game, x0, VOBG_ROWS - 2) != 0,
              "the piece landed on the floor");
        /* And a fresh one is in play at the top, not the same piece sliding. */
        check(p.game.y < y0 + 2, "the next piece spawned rather than the old one moving");
    }

    /* A tap must rotate. */
    player_init(&p, 7);
    {
        int rot0 = p.game.rot;
        tap(&p);
        check(p.game.rot != rot0, "a tap rotates the piece");
        check_eq(p.taps_applied, 1, "and registers as a single tap");
    }
}

static void test_soft_drop_is_released(void)
{
    Player p;

    group("soft drop does not stick");

    player_init(&p, 7);
    swipe_down(&p);
    check_eq(p.soft_drop_active, 0, "lifting the finger releases soft drop");
    check_eq(p.soft_begin_count, p.soft_end_count, "begin and end stay balanced");

    /* A cancelled touch must release it too, or gravity stays fast forever. */
    player_init(&p, 7);
    p_begin(&p, 160, 200);
    p_drag(&p, 160, 200, 160, 260, 4);
    apply(&p, vobg_gesture_cancel(&p.gesture), 0.0);
    check_eq(p.soft_drop_active, 0, "a cancelled touch releases soft drop");

    /* And the flag in the game itself must match. */
    check_eq(p.game.soft_drop, 0, "the game is no longer in soft drop");
}

static void test_soft_drop_is_faster(void)
{
    Player a, b;

    group("soft drop accelerates gravity");

    /* Same piece, same elapsed time; one held down, one not. */
    player_init(&a, 7);
    vobg_set_soft_drop(&a.game, 1);
    {
        int i;
        for (i = 0; i < 30; i++) {
            vobg_update(&a.game, 0.016f);
        }
    }
    player_init(&b, 7);
    {
        int i;
        for (i = 0; i < 30; i++) {
            vobg_update(&b.game, 0.016f);
        }
    }
    check(a.game.y > b.game.y, "holding down falls much further than letting go");
}

static void test_a_full_game_runs(void)
{
    Player p;
    int pieces = 0;
    int frames = 0;

    group("a whole game can be played");

    player_init(&p, 20240101);

    /* Play the game with the heuristic bot until it tops out. Every placement
     * goes through the real gesture recogniser, so this exercises the whole
     * input path rather than just the rules. */
    while (p.game.state != VOBG_STATE_GAMEOVER && frames < 4000) {
        bot_place(&p);
        pieces++;
        /* Let the line-clear animation play out. */
        p_tick(&p, 0.20);
        frames++;
    }

    check(pieces > 20, "the bot got through many pieces before topping out");
    check_eq(p.hard_drops_applied, pieces, "every piece was placed by an up swipe");
    check_eq(p.soft_begin_count, p.soft_end_count, "soft drop stayed balanced all game");
    check(p.game.lines > 0, "the bot cleared lines by playing properly");
    check(p.game.score > 0, "and scored");
    check(p.game.level >= 1, "the level is sane");
    check_eq(p.game.state, VOBG_STATE_GAMEOVER, "the game ended by topping out, not a crash");

    printf("   --   %d pieces, %d lines, %d points, level %d\n",
           pieces, p.game.lines, p.game.score, p.game.level);

    /* Soft drop has to be exercised somewhere too, so mix it in. */
    player_init(&p, 7);
    {
        int i;
        for (i = 0; i < 20; i++) {
            swipe_down(&p);
            swipe_up(&p);
            p_tick(&p, 0.20);
        }
    }
    check_eq(p.soft_begin_count, 20, "twenty soft drops were issued");
    check_eq(p.soft_end_count, 20, "and all twenty were released");
    check_eq(p.game.soft_drop, 0, "the game is not left in soft drop");
}

static void test_game_over_and_restart(void)
{
    Player p;

    group("game over and restart");

    player_init(&p, 7);
    /* Pile the board up to the ceiling. */
    {
        int row, col;
        for (row = VOBG_HIDDEN_ROWS; row < VOBG_ROWS; row++) {
            for (col = 0; col < VOBG_COLS; col++) {
                p.game.cells[row][col] = (unsigned char)(VOBG_PIECE_J + 1);
            }
        }
    }
    swipe_up(&p);
    check_eq(p.game.state, VOBG_STATE_GAMEOVER, "topping out ends the game");

    /* Input during game over must be inert, not crash. */
    tap(&p);
    check_eq(p.taps_applied, 1, "a tap during game over is handled");
    check_eq(p.game.state, VOBG_STATE_PLAYING, "and that tap restarted the game");
    check_eq(p.game.lines, 0, "the restart cleared the board");
    check_eq(p.game.score, 0, "and the score");

    /* And the restarted game is playable. */
    swipe_up(&p);
    check(p.game.score > 0, "the restarted game accepts input again");
}

static void test_frame_dt_clamping_survives(void)
{
    Player p;
    int y_before;

    group("a long stall does not teleport the piece");

    player_init(&p, 7);
    y_before = p.game.y;

    /* Simulate a resume from the background: one enormous dt. The view clamps
     * this to 0.05s; feed that clamped value and confirm nothing explodes. */
    p_tick(&p, 0.05);
    check(p.game.y <= y_before + 4, "a 50ms frame moves the piece at most a few rows");

    /* Even a raw huge dt should not corrupt the board. */
    vobg_update(&p.game, 100.0f);
    {
        int row, col, sane = 1;
        for (row = 0; row < VOBG_ROWS; row++) {
            for (col = 0; col < VOBG_COLS; col++) {
                if (p.game.cells[row][col] > VOBG_PIECE_COUNT) {
                    sane = 0;
                }
            }
        }
        check(sane, "the board holds only valid pieces after a huge dt");
    }
}

int main(void)
{
    test_controls_change_the_right_thing();
    test_soft_drop_is_released();
    test_soft_drop_is_faster();
    test_a_full_game_runs();
    test_game_over_and_restart();
    test_frame_dt_clamping_survives();

    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
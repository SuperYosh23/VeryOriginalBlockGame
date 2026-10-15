/*
 * test_gesture.c -- tests for the swipe state machine.
 *
 * Horizontal movement is now a continuous drag: the recogniser reports how far
 * the finger has travelled and the piece follows it, snapping to the grid on
 * release. So the thing worth testing here is no longer "did it move one
 * column" but "did it report the right offset, and is everything else still
 * unambiguous".
 *
 * The risks this guards against:
 *   - committing mid-swipe jumping the piece (the delta must be measured from
 *     where the touch began, not from where it committed)
 *   - a drag turning back into a tap
 *   - up/down still latching and staying exclusive
 *   - a second finger hijacking the gesture
 */

#include "VOBGGesture.h"

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

static void check_near(double got, double want, const char *what)
{
    g_checks++;
    if (fabs(got - want) < 0.001) {
        printf("   ok   %s\n", what);
    } else {
        g_failures++;
        printf("   FAIL %s [%s]: got %.3f, want %.3f\n", what, g_group, got, want);
    }
}

#define ORIGIN_X 200.0
#define ORIGIN_Y 300.0

/* What the recogniser reported over a whole gesture. */
typedef struct {
    int taps;
    int drags;          /* how many DRAG actions */
    double last_delta;  /* the most recent reported offset */
    double first_delta; /* the offset on the first DRAG */
    int soft_begin;
    int soft_end;
    int hard_drops;
} Tally;

static void tally_reset(Tally *t)
{
    memset(t, 0, sizeof(*t));
}

static void note(VOBGAction action, double delta, Tally *t)
{
    switch (action) {
    case VOBG_ACTION_TAP:
        t->taps++;
        break;
    case VOBG_ACTION_DRAG:
        if (t->drags == 0) {
            t->first_delta = delta;
        }
        t->drags++;
        t->last_delta = delta;
        break;
    case VOBG_ACTION_SOFT_BEGIN:
        t->soft_begin++;
        break;
    case VOBG_ACTION_SOFT_END:
        t->soft_end++;
        break;
    case VOBG_ACTION_HARD_DROP:
        t->hard_drops++;
        break;
    default:
        break;
    }
}

/* ----------------------------------------------------------------- tests */

static void test_tap(void)
{
    VOBGGesture g;
    Tally t;

    group("tap to rotate");

    tally_reset(&t);
    vobg_gesture_init(&g);
    vobg_gesture_begin(&g, ORIGIN_X, ORIGIN_Y, 100.0);
    note(vobg_gesture_end(&g, ORIGIN_X + 2, ORIGIN_Y + 1, 100.1), 0, &t);
    check(t.taps == 1, "a still, quick touch rotates once");
    check(t.drags == 0, "and does not also drag");

    tally_reset(&t);
    vobg_gesture_init(&g);
    vobg_gesture_begin(&g, ORIGIN_X, ORIGIN_Y, 100.0);
    note(vobg_gesture_move(&g, ORIGIN_X, ORIGIN_Y + 3, 100.1, &t.last_delta),
         t.last_delta, &t);
    note(vobg_gesture_end(&g, ORIGIN_X, ORIGIN_Y + 3, 100.2), 0, &t);
    check(t.taps == 1, "a wobble inside the tap slop is still a tap");
    check(t.drags == 0, "with no drag reported");
}

static void test_drag_reports_offset(void)
{
    VOBGGesture g;
    Tally t;
    double delta;

    group("horizontal drag reports the finger offset");

    tally_reset(&t);
    vobg_gesture_init(&g);
    /* A 3GS in portrait: 21pt cells, so a 6pt slop and an 18.9pt commit. */
    vobg_gesture_set_cell(&g, 21.0);
    vobg_gesture_begin(&g, ORIGIN_X, ORIGIN_Y, 100.0);

    /* Inside the tap slop: nothing yet, it is still a tap candidate. */
    note(vobg_gesture_move(&g, ORIGIN_X - 3, ORIGIN_Y, 100.1, &delta), delta, &t);
    check(t.drags == 0, "no drag inside the tap slop");

    /* Past it the piece previews sideways movement straight away, without
     * locking the axis -- this is the nudge that used to need a big swipe
     * first. */
    note(vobg_gesture_move(&g, ORIGIN_X - 9, ORIGIN_Y, 100.15, &delta), delta, &t);
    check(t.drags == 1, "a small sideways nudge drags immediately");
    check_near(t.first_delta, -9.0, "and reports the offset from the touch origin");

    /* Crossing the full threshold commits and still reports the offset so far,
     * measured from where the touch began -- not from where it committed.
     * Committing here at -25 must report -25, not -10. */
    note(vobg_gesture_move(&g, ORIGIN_X - 25, ORIGIN_Y, 100.2, &delta), delta, &t);
    check(t.drags == 2, "crossing the threshold commits the drag");
    check_near(t.last_delta, -25.0, "the offset is still measured from the touch origin");
    /* The first DRAG is now the preview at -15, not the commit at -25. Both are
     * offsets from the touch origin; nothing is ever measured from wherever the
     * gesture happened to lock its axis. */
    check_near(t.first_delta, -9.0, "the first offset is the preview, not the commit");

    /* Subsequent moves report the running offset. */
    note(vobg_gesture_move(&g, ORIGIN_X - 40, ORIGIN_Y, 100.3, &delta), delta, &t);
    check_near(t.last_delta, -40.0, "the offset tracks the finger");
    note(vobg_gesture_move(&g, ORIGIN_X - 12.5, ORIGIN_Y, 100.4, &delta), delta, &t);
    check_near(t.last_delta, -12.5, "sub-column positions are reported, not rounded");
    note(vobg_gesture_end(&g, ORIGIN_X - 12.5, ORIGIN_Y, 100.5), 0, &t);
    check(t.taps == 0, "a drag that ends never becomes a tap");

    /* Rightwards. */
    tally_reset(&t);
    vobg_gesture_init(&g);
    vobg_gesture_begin(&g, ORIGIN_X, ORIGIN_Y, 100.0);
    note(vobg_gesture_move(&g, ORIGIN_X + 33.3, ORIGIN_Y, 100.1, &delta), delta, &t);
    check_near(t.last_delta, 33.3, "rightward drags report a positive offset");
}

static void test_drag_past_origin(void)
{
    VOBGGesture g;
    Tally t;
    double delta;

    group("dragging back past the origin");

    tally_reset(&t);
    vobg_gesture_init(&g);
    vobg_gesture_begin(&g, ORIGIN_X, ORIGIN_Y, 100.0);
    note(vobg_gesture_move(&g, ORIGIN_X + 40, ORIGIN_Y, 100.1, &delta), delta, &t);
    note(vobg_gesture_move(&g, ORIGIN_X - 30, ORIGIN_Y, 100.2, &delta), delta, &t);
    check_near(t.last_delta, -30.0, "the offset goes negative when the finger comes back");
    note(vobg_gesture_end(&g, ORIGIN_X - 30, ORIGIN_Y, 100.3), 0, &t);
    check(t.taps == 0, "returning across the origin does not become a tap");
    check(t.drags >= 2, "both directions reported drags");
}

static void test_vertical_stays_exclusive(void)
{
    VOBGGesture g;
    Tally t;
    double delta;

    group("vertical gestures stay exclusive");

    /* Down is a soft drop and must not drag. */
    tally_reset(&t);
    vobg_gesture_init(&g);
    vobg_gesture_begin(&g, ORIGIN_X, ORIGIN_Y, 100.0);
    note(vobg_gesture_move(&g, ORIGIN_X, ORIGIN_Y + 40, 100.1, &delta), delta, &t);
    check(t.soft_begin == 1, "a downward swipe accelerates gravity once");
    check(t.drags == 0, "and never emits a drag");
    note(vobg_gesture_move(&g, ORIGIN_X - 40, ORIGIN_Y + 60, 100.2, &delta), delta, &t);
    check(t.drags == 0, "moving sideways during a soft drop still does not drag");
    note(vobg_gesture_end(&g, ORIGIN_X - 40, ORIGIN_Y + 60, 100.3), 0, &t);
    check(t.soft_end == 1, "lifting releases soft drop");
    check(t.soft_begin == t.soft_end, "soft drop begin/end stay balanced");

    /* Up is an insta-drop and latches. */
    tally_reset(&t);
    vobg_gesture_init(&g);
    vobg_gesture_begin(&g, ORIGIN_X, ORIGIN_Y, 100.0);
    note(vobg_gesture_move(&g, ORIGIN_X, ORIGIN_Y - 30, 100.1, &delta), delta, &t);
    check(t.hard_drops == 1, "an upward swipe insta-drops once");
    check(t.drags == 0, "and does not drag");
    note(vobg_gesture_move(&g, ORIGIN_X - 30, ORIGIN_Y - 60, 100.2, &delta), delta, &t);
    check(t.hard_drops == 1, "continuing up does not re-drop");
    check(t.drags == 0, "and dragging afterwards does nothing");
    note(vobg_gesture_end(&g, ORIGIN_X - 30, ORIGIN_Y - 60, 100.3), 0, &t);

    /* Cancel must still release soft drop. */
    tally_reset(&t);
    vobg_gesture_init(&g);
    vobg_gesture_begin(&g, ORIGIN_X, ORIGIN_Y, 100.0);
    note(vobg_gesture_move(&g, ORIGIN_X, ORIGIN_Y + 40, 100.1, &delta), delta, &t);
    note(vobg_gesture_cancel(&g), 0, &t);
    check(t.soft_begin == 1, "a cancelled soft drop began");
    check(t.soft_end == 1, "and was released on cancel");
}

static void test_ties_go_horizontal(void)
{
    VOBGGesture g;
    Tally t;
    double delta;

    group("diagonals");

    tally_reset(&t);
    vobg_gesture_init(&g);
    vobg_gesture_begin(&g, ORIGIN_X, ORIGIN_Y, 100.0);
    note(vobg_gesture_move(&g, ORIGIN_X + 30, ORIGIN_Y + 30, 100.1, &delta), delta, &t);
    check(t.drags == 1, "a perfect diagonal commits to horizontal");
    check(t.soft_begin == 0, "not to a soft drop");

    /* Once committed, a larger vertical movement must not take over. */
    note(vobg_gesture_move(&g, ORIGIN_X + 35, ORIGIN_Y + 200, 100.2, &delta), delta, &t);
    check(t.drags > 1, "a steep drift during a drag stays a drag");
    check(t.soft_begin == 0, "and never starts a soft drop");
}

static void test_multitouch_and_lifecycle(void)
{
    VOBGGesture g;
    Tally t;
    double delta;

    group("multi-touch and lifecycle");

    tally_reset(&t);
    vobg_gesture_init(&g);
    vobg_gesture_begin(&g, ORIGIN_X, ORIGIN_Y, 100.0);
    check(vobg_gesture_begin(&g, 50, 50, 100.5) == VOBG_ACTION_NONE,
          "a second finger is ignored while tracking");
    check(vobg_gesture_is_tracking(&g), "the first gesture is still tracked");
    note(vobg_gesture_move(&g, ORIGIN_X - 30, ORIGIN_Y, 100.6, &delta), delta, &t);
    check(t.drags >= 1, "the original finger still steers the piece");
    note(vobg_gesture_end(&g, ORIGIN_X - 30, ORIGIN_Y, 100.7), 0, &t);
    check(vobg_gesture_is_tracking(&g) == 0, "the gesture ends on lift");

    /* A fresh gesture after the previous one ended. */
    tally_reset(&t);
    vobg_gesture_init(&g);
    vobg_gesture_begin(&g, ORIGIN_X, ORIGIN_Y, 100.0);
    note(vobg_gesture_end(&g, ORIGIN_X + 1, ORIGIN_Y + 1, 100.1), 0, &t);
    check(t.taps == 1, "the next tap is recognised as a fresh tap");

    /* Moves with no touch in flight are harmless. */
    check(vobg_gesture_move(&g, 10, 10, 200.0, &delta) == VOBG_ACTION_NONE,
          "moving with no gesture in flight is ignored");
    check(vobg_gesture_end(&g, 10, 10, 200.1) == VOBG_ACTION_NONE,
          "ending with no gesture in flight is ignored");
}

static void test_threshold_edges(void)
{
    VOBGGesture g;
    Tally t;
    double delta;

    group("threshold edges");

    /* Just under the tap slop: still a tap. */
    tally_reset(&t);
    vobg_gesture_init(&g);
    vobg_gesture_begin(&g, ORIGIN_X, ORIGIN_Y, 100.0);
    note(vobg_gesture_move(&g, ORIGIN_X - 3.9, ORIGIN_Y, 100.1, &delta), delta, &t);
    note(vobg_gesture_end(&g, ORIGIN_X - 3.9, ORIGIN_Y, 100.2), 0, &t);
    check(t.taps == 1, "just under the tap slop is a tap");
    check(t.drags == 0, "with no drag");

    /* Just over the tap slop: a drag, even though the axis is not locked yet. */
    tally_reset(&t);
    vobg_gesture_init(&g);
    vobg_gesture_begin(&g, ORIGIN_X, ORIGIN_Y, 100.0);
    note(vobg_gesture_move(&g, ORIGIN_X - 22.1, ORIGIN_Y, 100.1, &delta), delta, &t);
    note(vobg_gesture_end(&g, ORIGIN_X - 22.1, ORIGIN_Y, 100.2), 0, &t);
    check(t.drags == 1, "just over the threshold is a drag");
    check(t.taps == 0, "not a tap");

    /* A held drag keeps reporting the same offset; it must not accumulate. */
    tally_reset(&t);
    vobg_gesture_init(&g);
    vobg_gesture_begin(&g, ORIGIN_X, ORIGIN_Y, 100.0);
    note(vobg_gesture_move(&g, ORIGIN_X - 40, ORIGIN_Y, 100.1, &delta), delta, &t);
    note(vobg_gesture_move(&g, ORIGIN_X - 40, ORIGIN_Y, 100.2, &delta), delta, &t);
    note(vobg_gesture_move(&g, ORIGIN_X - 40, ORIGIN_Y, 100.3, &delta), delta, &t);
    check_near(t.last_delta, -40.0, "a stationary finger keeps reporting the same offset");
}

/* The reported bug: a small sideways nudge did nothing until one large swipe had
 * committed the gesture, and letting go during that window rotated the piece. */
static void test_small_nudge_immediately(void)
{
    VOBGGesture g;
    Tally t;
    double delta;

    group("a small nudge works on the very first touch");

    /* Nudge right, well under move_threshold, then lift. */
    tally_reset(&t);
    vobg_gesture_init(&g);
    vobg_gesture_begin(&g, ORIGIN_X, ORIGIN_Y, 100.0);
    note(vobg_gesture_move(&g, ORIGIN_X + 14, ORIGIN_Y, 100.1, &delta), delta, &t);
    check(t.drags == 1, "a 14pt nudge drags with no prior swipe to unlock it");
    check_near(t.last_delta, 14.0, "the offset is reported from the touch origin");
    note(vobg_gesture_end(&g, ORIGIN_X + 14, ORIGIN_Y, 100.2), 0, &t);
    check(t.taps == 0, "lifting after a nudge does not rotate the piece");

    /* And the same to the left, on a fresh touch. */
    tally_reset(&t);
    vobg_gesture_init(&g);
    vobg_gesture_begin(&g, ORIGIN_X, ORIGIN_Y, 100.0);
    note(vobg_gesture_move(&g, ORIGIN_X - 14, ORIGIN_Y, 100.1, &delta), delta, &t);
    check(t.drags == 1, "a leftward nudge works too, without unlocking first");
    note(vobg_gesture_end(&g, ORIGIN_X - 14, ORIGIN_Y, 100.2), 0, &t);
    check(t.taps == 0, "and still does not rotate");

    /* A sequence of separate small nudges must each work. This is the "you have
     * to do this every time you place your finger down" part. */
    {
        int i;
        int all_dragged = 1;
        for (i = 0; i < 5; i++) {
            tally_reset(&t);
            vobg_gesture_init(&g);
            vobg_gesture_begin(&g, ORIGIN_X, ORIGIN_Y, 100.0 + i * 10.0);
            note(vobg_gesture_move(&g, ORIGIN_X + 13, ORIGIN_Y, 100.5 + i * 10.0, &delta),
                 delta, &t);
            note(vobg_gesture_end(&g, ORIGIN_X + 13, ORIGIN_Y, 100.6 + i * 10.0), 0, &t);
            if (t.drags != 1 || t.taps != 0) {
                all_dragged = 0;
            }
        }
        check(all_dragged, "five separate small nudges all drag, none rotate");
    }
}

/* A wobble inside the slop is still a rotate, so the preview band has to start
 * above it. */
static void test_tap_slop_boundary(void)
{
    VOBGGesture g;
    Tally t;
    double delta;

    group("tap slop boundary");

    tally_reset(&t);
    vobg_gesture_init(&g);
    vobg_gesture_begin(&g, ORIGIN_X, ORIGIN_Y, 100.0);
    note(vobg_gesture_move(&g, ORIGIN_X + 3.9, ORIGIN_Y, 100.1, &delta), delta, &t);
    note(vobg_gesture_end(&g, ORIGIN_X + 3.9, ORIGIN_Y, 100.2), 0, &t);
    check(t.taps == 1, "3.9pt of drift still rotates");
    check(t.drags == 0, "and reports no drag");

    tally_reset(&t);
    vobg_gesture_init(&g);
    vobg_gesture_begin(&g, ORIGIN_X, ORIGIN_Y, 100.0);
    note(vobg_gesture_move(&g, ORIGIN_X + 4.1, ORIGIN_Y, 100.1, &delta), delta, &t);
    note(vobg_gesture_end(&g, ORIGIN_X + 4.1, ORIGIN_Y, 100.2), 0, &t);
    check(t.drags == 1, "4.1pt is past the slop, so it drags");
    check(t.taps == 0, "and does not rotate");
}

/* The reason the preview band does not commit the axis: an upward swipe that
 * begins with a little sideways drift must still hard-drop. */
static void test_upward_swipe_survives_drift(void)
{
    VOBGGesture g;
    Tally t;
    double delta;

    group("an upward swipe still wins after sideways drift");

    tally_reset(&t);
    vobg_gesture_init(&g);
    vobg_gesture_set_cell(&g, 21.0);
    vobg_gesture_begin(&g, ORIGIN_X, ORIGIN_Y, 100.0);
    /* Drift right into the preview band... */
    note(vobg_gesture_move(&g, ORIGIN_X + 18, ORIGIN_Y + 2, 100.1, &delta), delta, &t);
    check(t.drags == 1, "the drift previews sideways movement");
    check(t.hard_drops == 0, "but commits nothing");
    /* ...then keep going up past the real threshold. */
    note(vobg_gesture_move(&g, ORIGIN_X + 20, ORIGIN_Y - 30, 100.2, &delta), delta, &t);
    check(t.hard_drops == 1, "the upward flick still hard-drops");
    check(t.soft_begin == 0, "and does not also soft-drop");
    note(vobg_gesture_end(&g, ORIGIN_X + 20, ORIGIN_Y - 30, 100.3), 0, &t);
    check(t.hard_drops == 1, "the hard drop fires exactly once");
}

/* A tiny upward wobble must not soft-drop the piece. */
static void test_small_vertical_does_not_soft_drop(void)
{
    VOBGGesture g;
    Tally t;
    double delta;

    group("a small vertical wobble is harmless");

    tally_reset(&t);
    vobg_gesture_init(&g);
    vobg_gesture_set_cell(&g, 21.0);
    vobg_gesture_begin(&g, ORIGIN_X, ORIGIN_Y, 100.0);
    note(vobg_gesture_move(&g, ORIGIN_X, ORIGIN_Y + 18, 100.1, &delta), delta, &t);
    check(t.soft_begin == 0, "18pt down does not start a soft drop");
    check(t.drags == 0, "and does not drag sideways either");
    note(vobg_gesture_end(&g, ORIGIN_X, ORIGIN_Y + 18, 100.2), 0, &t);
    check(t.taps == 0, "lifting here does not rotate");
}

/* Standard tap semantics: once the finger has left the slop, returning to the
 * origin does not make it a tap again. */
static void test_return_to_origin_is_not_a_tap(void)
{
    VOBGGesture g;
    Tally t;
    double delta;

    group("returning to the origin is not a tap");

    tally_reset(&t);
    vobg_gesture_init(&g);
    vobg_gesture_begin(&g, ORIGIN_X, ORIGIN_Y, 100.0);
    note(vobg_gesture_move(&g, ORIGIN_X + 16, ORIGIN_Y, 100.1, &delta), delta, &t);
    note(vobg_gesture_move(&g, ORIGIN_X + 1, ORIGIN_Y, 100.2, &delta), delta, &t);
    note(vobg_gesture_end(&g, ORIGIN_X + 1, ORIGIN_Y, 100.3), 0, &t);
    check(t.taps == 0, "a touch that wandered out and came back does not rotate");
    check(t.drags >= 1, "it was treated as a drag");
}

/* The thresholds are chosen in cells because a cell is 21pt on a 3GS and 23pt
 * on a 6 -- a fixed point value is half a cell on one and a third on the other. */
static void test_thresholds_scale_with_cell(void)
{
    VOBGGesture g;
    Tally t;

    group("thresholds follow the cell size");

    /* 3GS portrait: 21pt cells. */
    vobg_gesture_init(&g);
    vobg_gesture_set_cell(&g, 21.0);
    check_near(g.tap_slop, 4.0, "3GS portrait slop is the 4pt minimum");
    check(g.tap_slop < 21.0 * 0.25, "and is well under a quarter of a cell");
    check_near(g.move_threshold, 18.9, "3GS portrait commits just under a cell");
    check(g.move_threshold > g.tap_slop * 2.0, "leaving a wide preview band");
    check(g.move_threshold > g.tap_slop, "the preview band has room");

    /* 3GS landscape: 14pt cells. This is the case the fixed thresholds got
     * worst -- a 12pt slop was 0.86 of a cell, so nearly a whole cell of the
     * board was dead to small movements. */
    vobg_gesture_init(&g);
    vobg_gesture_set_cell(&g, 14.0);
    check(g.tap_slop < 14.0 * 0.35, "3GS landscape slop is under a third of a cell");
    check(g.move_threshold > g.tap_slop, "and still leaves a preview band");

    /* A degenerate size must not produce a zero or negative slop. */
    vobg_gesture_init(&g);
    vobg_gesture_set_cell(&g, 0.0);
    check(g.tap_slop > 0.0, "a zero cell leaves the provisional slop alone");
    check(g.move_threshold > g.tap_slop, "and still leaves a preview band");

    vobg_gesture_init(&g);
    vobg_gesture_set_cell(&g, -5.0);
    check(g.tap_slop > 0.0, "a negative cell is rejected outright");

    /* The practical regression test: on a 3GS, a movement that used to be
     * swallowed now drags, and releasing after it does not rotate. */
    vobg_gesture_init(&g);
    vobg_gesture_set_cell(&g, 21.0);
    tally_reset(&t);
    vobg_gesture_begin(&g, ORIGIN_X, ORIGIN_Y, 100.0);
    note(vobg_gesture_move(&g, ORIGIN_X + 8, ORIGIN_Y, 100.1, &t.last_delta), t.last_delta, &t);
    check(t.drags == 1, "an 8pt nudge on a 3GS drags (was inside the old 12pt dead zone)");
    note(vobg_gesture_end(&g, ORIGIN_X + 8, ORIGIN_Y, 100.2), 0, &t);
    check(t.taps == 0, "and releasing after it does not rotate");

    /* A genuine tap must still rotate on the small screen. */
    tally_reset(&t);
    vobg_gesture_begin(&g, ORIGIN_X, ORIGIN_Y, 200.0);
    note(vobg_gesture_move(&g, ORIGIN_X + 2, ORIGIN_Y + 1, 200.1, &t.last_delta), t.last_delta, &t);
    note(vobg_gesture_end(&g, ORIGIN_X + 2, ORIGIN_Y + 1, 200.2), 0, &t);
    check(t.taps == 1, "a 2pt tap still rotates on a 3GS");
    check(t.drags == 0, "and does not drag");

    /* And a 5pt movement, which is over the slop but well under the commit. */
    tally_reset(&t);
    vobg_gesture_begin(&g, ORIGIN_X, ORIGIN_Y, 300.0);
    note(vobg_gesture_move(&g, ORIGIN_X + 5, ORIGIN_Y, 300.1, &t.last_delta), t.last_delta, &t);
    check(t.drags == 1, "a 5pt movement now drags on a 3GS");
}

int main(void)
{
    test_tap();
    test_drag_reports_offset();
    test_drag_past_origin();
    test_vertical_stays_exclusive();
    test_ties_go_horizontal();
    test_multitouch_and_lifecycle();
    test_threshold_edges();
    test_small_nudge_immediately();
    test_tap_slop_boundary();
    test_upward_swipe_survives_drift();
    test_small_vertical_does_not_soft_drop();
    test_return_to_origin_is_not_a_tap();
    test_thresholds_scale_with_cell();

    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
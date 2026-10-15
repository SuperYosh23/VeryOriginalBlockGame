/*
 * VOBGGesture.c -- see VOBGGesture.h.
 *
 * The rules, in one place:
 *
 *   - A touch starts UNDECIDED. Until it travels past tap_slop it is still a
 *     candidate tap; only if it ends there does it count as one. That is what
 *     separates "rotate" from "swipe" without a mode switch.
 *   - Between tap_slop and move_threshold the piece previews sideways movement
 *     without the axis being locked. Past move_threshold the axis commits.
 *   - Committing to an axis is one-way. A swipe that wanders back to its origin
 *     must not become a tap, because the piece has already moved.
 *   - Horizontal beats vertical on a tie: sideways is the most frequent action
 *     and a vertical wobble in the middle of a swipe is common on a phone.
 *   - Up latches. One hard drop per upward swipe, however far the finger keeps
 *     travelling.
 *   - Horizontal movement is a continuous drag. The recogniser reports the raw
 *     finger offset and the piece follows it between columns, snapping to the
 *     grid on release. There is deliberately no auto-repeat: holding still now
 *     means "stay here", and repeating would fight both the finger and the snap.
 */

#include "VOBGGesture.h"

#include <math.h>

void vobg_gesture_init(VOBGGesture *g)
{
    g->phase = VOBG_GESTURE_IDLE;
    g->origin_x = 0.0;
    g->origin_y = 0.0;
    g->last_event_time = 0.0;
    g->moved = 0;
    /* Provisional. The view replaces these with cell-derived values as soon as
     * the layout runs; these only matter for the first frame or two. */
    g->tap_slop = VOBG_SLOP_MIN;
    g->move_threshold = VOBG_COMMIT_MIN;
}

static double clampd(double v, double lo, double hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

void vobg_gesture_set_cell(VOBGGesture *g, double cell)
{
    if (cell <= 0.0) {
        return;
    }
    /* The slop floor is the important half. 0.25 cells is about 5pt on a 3GS,
     * still more than a resting finger's tremor but far less than the old fixed
     * 12pt -- that was over half a cell, so every small movement was swallowed
     * and then read as a tap, which rotated the piece. */
    g->tap_slop = clampd(cell * VOBG_SLOP_CELLS, VOBG_SLOP_MIN, VOBG_SLOP_MAX);
    /* Just under a cell: enough that a deliberate sideways swipe commits, but
     * small enough that the preview band always has room to work in. */
    g->move_threshold = clampd(cell * VOBG_COMMIT_CELLS,
                               VOBG_COMMIT_MIN, VOBG_COMMIT_MAX);
}

int vobg_gesture_is_tracking(const VOBGGesture *g)
{
    return g->phase != VOBG_GESTURE_IDLE;
}

VOBGAction vobg_gesture_begin(VOBGGesture *g, double x, double y, double time)
{
    if (g->phase != VOBG_GESTURE_IDLE) {
        return VOBG_ACTION_NONE; /* already tracking; extra fingers ignored */
    }
    g->phase = VOBG_GESTURE_UNDECIDED;
    g->origin_x = x;
    g->origin_y = y;
    g->last_event_time = time;
    g->moved = 0;
    return VOBG_ACTION_NONE;
}

/* Pick the axis for a touch that has travelled far enough, and emit whatever
 * that first crossing implies. */
static VOBGAction commit(VOBGGesture *g, double dx, double dy, double time)
{
    g->last_event_time = time;

    if (fabs(dx) >= fabs(dy)) {
        /* Sideways. Report the offset on this same event so the piece starts
         * tracking the finger immediately instead of waiting for it to move
         * again. The first delta is measured from where the touch began, so
         * committing partway through a swipe does not jump the piece. */
        g->phase = VOBG_GESTURE_HORIZONTAL;
        return VOBG_ACTION_DRAG;
    }
    if (dy > 0.0) {
        g->phase = VOBG_GESTURE_SOFT_DROP;
        return VOBG_ACTION_SOFT_BEGIN;
    }
    g->phase = VOBG_GESTURE_DONE;
    return VOBG_ACTION_HARD_DROP;
}

VOBGAction vobg_gesture_move(VOBGGesture *g, double x, double y, double time,
                             double *out_delta_x)
{
    double dx, dy;
    VOBGAction action;

    if (out_delta_x) {
        *out_delta_x = 0.0;
    }

    if (!vobg_gesture_is_tracking(g)) {
        return VOBG_ACTION_NONE;
    }

    dx = x - g->origin_x;
    dy = y - g->origin_y;

    if (g->phase == VOBG_GESTURE_UNDECIDED) {
        if (fabs(dx) < g->move_threshold && fabs(dy) < g->move_threshold) {
            /* Preview band. Sideways movement past the tap slop drags the piece
             * straight away, without committing the axis: the finger can still
             * turn into an upward swipe for hard drop. Without this the piece
             * ignored every movement under the threshold, so nudging it one
             * column required a big swipe first, every single touch.
             *
             * The phase deliberately stays UNDECIDED, so the end handler can
             * still see that this was not a tap. */
            if (fabs(dx) >= g->tap_slop) {
                g->moved = 1;
                if (out_delta_x) {
                    *out_delta_x = dx;
                }
                return VOBG_ACTION_DRAG;
            }
            return VOBG_ACTION_NONE; /* still might be a tap */
        }
        g->moved = 1;
        action = commit(g, dx, dy, time);
        if (action == VOBG_ACTION_DRAG && out_delta_x) {
            *out_delta_x = dx;
        }
        return action;
    }

    if (g->phase != VOBG_GESTURE_HORIZONTAL) {
        /* Soft drop and hard drop both act on commit alone. */
        return VOBG_ACTION_NONE;
    }

    /* Horizontal: report the raw finger offset every time. The piece follows the
     * finger continuously, so there is no step counting and no auto-repeat --
     * holding still now means "stay here", which is what dragging should do.
     * Auto-repeating would fight the finger and fight the release-snap. */
    if (out_delta_x) {
        *out_delta_x = dx;
    }
    return VOBG_ACTION_DRAG;
}

VOBGAction vobg_gesture_end(VOBGGesture *g, double x, double y, double time)
{
    VOBGAction action = VOBG_ACTION_NONE;

    if (!vobg_gesture_is_tracking(g)) {
        return VOBG_ACTION_NONE;
    }

    if (g->phase == VOBG_GESTURE_SOFT_DROP) {
        action = VOBG_ACTION_SOFT_END;
    } else if (g->phase == VOBG_GESTURE_UNDECIDED) {
        double dx = x - g->origin_x;
        double dy = y - g->origin_y;
        /* A tap is a touch that never left the slop at any point, matching what
         * UITapGestureRecognizer does. Testing only the release position would
         * let a finger wander out and come back rotate the piece after having
         * already dragged it there. */
        if (!g->moved &&
            fabs(dx) < g->tap_slop && fabs(dy) < g->tap_slop) {
            action = VOBG_ACTION_TAP;
        }
    }

    (void)time;
    g->phase = VOBG_GESTURE_IDLE;
    return action;
}

VOBGAction vobg_gesture_cancel(VOBGGesture *g)
{
    VOBGAction action = VOBG_ACTION_NONE;
    if (g->phase == VOBG_GESTURE_SOFT_DROP) {
        /* A cancelled touch must still release soft drop, or gravity stays
         * accelerated forever. */
        action = VOBG_ACTION_SOFT_END;
    }
    g->phase = VOBG_GESTURE_IDLE;
    return action;
}

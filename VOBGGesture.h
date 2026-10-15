/*
 * VOBGGesture.h -- the swipe state machine, in portable C.
 *
 * This is the part of the controls that is easy to get subtly wrong (a swipe
 * that turns into a tap when the finger slides back, an insta-drop that
 * re-fires while the finger is still moving up, a held swipe that stutters), so
 * it lives here in plain C where the host test suite can exercise it. The
 * Objective-C layer in VOBGSwipe.m is a thin adapter over this.
 *
 * Usage per touch cycle:
 *
 *   vobg_gesture_begin(&g, x, y, time);
 *   vobg_gesture_move(&g, x, y, time, &count, &dir);  // once per UIKit move event
 *   vobg_gesture_end(&g, x, y, time);
 *   vobg_gesture_tick(&g, time, &dir);                // once per frame
 *
 * Each call returns a VOBGAction. MOVE additionally reports how many columns to
 * shift and in which direction, because a fast flick can cross several
 * threshold-widths in a single event.
 */

#ifndef VOBG_GESTURE_H
#define VOBG_GESTURE_H

typedef enum {
    VOBG_GESTURE_IDLE = 0,
    VOBG_GESTURE_UNDECIDED, /* down, could still become a tap */
    VOBG_GESTURE_HORIZONTAL,
    VOBG_GESTURE_SOFT_DROP,
    VOBG_GESTURE_DONE /* hard drop already fired; ignore the rest */
} VOBGGesturePhase;

typedef enum {
    VOBG_ACTION_NONE = 0,
    VOBG_ACTION_TAP,        /* rotate */
    VOBG_ACTION_DRAG,       /* horizontal position update; see out_delta_x */
    VOBG_ACTION_SOFT_BEGIN, /* gravity accelerated */
    VOBG_ACTION_SOFT_END,   /* finger lifted */
    VOBG_ACTION_HARD_DROP   /* insta-drop */
} VOBGAction;

typedef struct {
    VOBGGesturePhase phase;
    double origin_x, origin_y;
    double last_event_time;
    int moved;  /* the finger has left the tap slop at some point */

    /* Tunable. vobg_gesture_init sets provisional values; the view calls
     * vobg_gesture_set_cell to derive them from the actual playfield.
     *
     * Two thresholds, because they answer different questions:
     *
     *   tap_slop       how far a finger may wander and still be a tap.
     *   move_threshold how far it must travel before the *axis* is locked in
     *                  and a vertical swipe commits to soft or hard drop.
     *
     * The gap between them is the preview band: horizontal movement in the band
     * drags the piece but has not yet committed, so an upward swipe that starts
     * with a little sideways drift is still a hard drop rather than being
     * hijacked into a sideways drag.
     *
     * Both are in POINTS at run time, but they are chosen in CELLS. See
     * vobg_gesture_set_cell for why that distinction is the whole ball game. */
    double tap_slop;
    double move_threshold;
} VOBGGesture;

/* Fractions of a cell, and the absolute limits they are clamped to.
 *
 * Expressed in cells because a cell is 21pt on a 3GS in portrait and 14pt in
 * landscape, but 23pt on a 6. A fixed 12pt slop is over half a cell on the
 * small screens and well under half on the large ones, which made one gesture
 * feel broken on one device and twitchy on another. */
#define VOBG_SLOP_CELLS 0.16
#define VOBG_COMMIT_CELLS 0.90
/* The minimum is what actually decides the slop on every screen this ships to:
 * 0.16 cells is 3.4pt on a 3GS, well under the 4pt floor. Raising the floor is
 * therefore the lever that matters, not the fraction. */
#define VOBG_SLOP_MIN 4.0
#define VOBG_SLOP_MAX 8.0
#define VOBG_COMMIT_MIN 14.0
#define VOBG_COMMIT_MAX 26.0

void vobg_gesture_init(VOBGGesture *g);

/* Re-derive both thresholds from the playfield's cell size. Call whenever the
 * layout changes. `cell` is the cell edge in points. */
void vobg_gesture_set_cell(VOBGGesture *g, double cell);

VOBGAction vobg_gesture_begin(VOBGGesture *g, double x, double y, double time);

/* For VOBG_ACTION_DRAG, *out_delta_x is how far the finger has travelled from
 * where the touch began, in points. It is continuous, not a step count: the
 * piece follows the finger between columns and the view decides how many cells
 * that is. Reporting a step count here would throw away the sub-column
 * resolution the drag exists to provide. */
VOBGAction vobg_gesture_move(VOBGGesture *g, double x, double y, double time,
                             double *out_delta_x);

VOBGAction vobg_gesture_end(VOBGGesture *g, double x, double y, double time);
VOBGAction vobg_gesture_cancel(VOBGGesture *g);

int vobg_gesture_is_tracking(const VOBGGesture *g);

#endif /* VOBG_GESTURE_H */

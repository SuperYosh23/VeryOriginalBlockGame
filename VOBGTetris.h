/*
 * VOBGTetris.h -- game rules for VeryOriginalBlockGame.
 *
 * Deliberately plain C: no Foundation, no UIKit, no Objective-C. That keeps the
 * rules testable on the build host (see tests/) and keeps the whole thing free
 * of anything that might not exist on iOS 6.
 *
 * Coordinate system: columns 0..VOBG_COLS-1 left to right, rows 0..VOBG_ROWS-1
 * top to bottom. Rows 0..VOBG_HIDDEN_ROWS-1 are above the visible well: pieces
 * spawn there so they do not pop into existence in front of you. The view only
 * draws rows >= VOBG_HIDDEN_ROWS.
 */

#ifndef VOBG_TETRIS_H
#define VOBG_TETRIS_H

#define VOBG_COLS 10
#define VOBG_ROWS 22
#define VOBG_HIDDEN_ROWS 2
#define VOBG_VISIBLE_ROWS (VOBG_ROWS - VOBG_HIDDEN_ROWS) /* 20 */
#define VOBG_NEXT_COUNT 3
#define VOBG_MAX_LEVEL 20

/* How long the cleared rows flash before they collapse. Exposed so the view can
 * animate the flash in step with the rules instead of guessing. */
#define VOBG_CLEAR_DELAY 0.18f
/* How long a grounded piece can still be steered before it locks. */
#define VOBG_LOCK_DELAY 0.35f

enum {
    VOBG_PIECE_I = 0,
    VOBG_PIECE_J = 1,
    VOBG_PIECE_L = 2,
    VOBG_PIECE_O = 3,
    VOBG_PIECE_S = 4,
    VOBG_PIECE_Z = 5,
    VOBG_PIECE_T = 6,
    VOBG_PIECE_COUNT = 7
};

enum {
    VOBG_ROT_0 = 0,
    VOBG_ROT_1 = 1,
    VOBG_ROT_2 = 2,
    VOBG_ROT_3 = 3
};

typedef enum {
    VOBG_STATE_PLAYING = 0,
    VOBG_STATE_CLEARING = 1,
    VOBG_STATE_GAMEOVER = 2
} VOBGState;

typedef struct {
    /* 0 = empty, otherwise VOBG_PIECE_x + 1. */
    unsigned char cells[VOBG_ROWS][VOBG_COLS];

    unsigned char active;   /* piece id of the falling piece */
    signed char rot;        /* 0..3 */
    int y;                  /* top row of the piece's bounding box */

    /* Left edge of the piece in cells.
     *
     * Fractional on purpose: a dragged piece sits between columns and is drawn
     * there, so the position has to be able to be continuous. Everything that
     * needs a whole column -- collision, rotation, locking -- uses
     * vobg_col(), which is floor(xf).
     *
     * There is deliberately no separate integer copy. Keeping one was the source
     * of a bug where a piece could be "placed" by writing the integer while the
     * fractional position stayed behind, and the next operation would silently
     * drag the piece back to where it used to be.
     */
    float xf;

    unsigned char next[VOBG_NEXT_COUNT];
    unsigned char bag[VOBG_PIECE_COUNT];
    int bag_len;
    unsigned long rng;

    int state;
    int score;
    int lines;
    int level;

    float drop_timer;   /* accumulated gravity */
    float lock_timer;   /* lock delay, only counts while grounded */
    float clear_timer;  /* line-clear flash */

    unsigned char clear_rows[VOBG_ROWS]; /* flash flags, valid while CLEARING */
    int clear_count;

    int soft_drop;        /* soft drop is currently held */
    int last_drop_cells;  /* cells travelled by the last hard drop, for scoring */
} VOBGGame;

/* Whole column the piece collides and locks against: floor(xf). */
int vobg_col(const VOBGGame *g);

/* Seconds per row of gravity at `level` (1-based). */
float vobg_gravity_seconds(int level);

void vobg_init(VOBGGame *g, unsigned long seed);

/* Advances gravity, lock delay and the line-clear animation by dt seconds. */
/* Advance the simulation by dt seconds. Returns non-zero if anything the player
 * can see changed, so the view can skip a redraw when nothing did.
 *
 * This matters: redrawing the well is ~200 block images plus the HUD, and doing
 * that 60 times a second whether or not the game moved is what drove the app
 * into the CPU watchdog (EXC_RESOURCE, 61% over 180s). */
int vobg_update(VOBGGame *g, float dt);

/* dx is -1 or +1, snapping to whole columns. Returns 1 if the piece moved.
 * The view uses the drag API below instead; this stays for tests and for any
 * one-shot nudge. */
int vobg_move(VOBGGame *g, int dx);

/* Continuous drag. `xf` is the desired left edge in cells; it is clamped to the
 * columns this piece can legally occupy at its current row, so a piece can slide
 * up against a wall or an overhang but never through one. */
void vobg_drag_to(VOBGGame *g, float xf);

/* Whole column the piece would settle into on release (nearest, then clamped to
 * the legal range). Returned without changing the game, so the caller can animate
 * towards it. */
float vobg_snap_target(const VOBGGame *g);

/* Snap `xf` to vobg_snap_target immediately. */
void vobg_snap(VOBGGame *g);

/* Rotates one step clockwise, kicking off walls and floors. Returns 1 on success. */
int vobg_rotate(VOBGGame *g);

/* Hold/release soft drop. While held, gravity runs ~20x. */
void vobg_set_soft_drop(VOBGGame *g, int on);

/* Drops the piece to the floor and locks it. Returns cells travelled. */
int vobg_hard_drop(VOBGGame *g);

/* One row down right now; scores 1 point per row while soft dropping. */
int vobg_step_down(VOBGGame *g);

/* Row the piece's box would occupy if dropped. Used by vobg_hard_drop to find
 * the landing spot; the view no longer draws a ghost, but this stays because it
 * is the rules' definition of where a piece comes to rest. */
int vobg_ghost_row(const VOBGGame *g);

/* Cell value at an absolute row, or 0. Rows above the well are still valid. */
unsigned char vobg_cell(const VOBGGame *g, int col, int row);

/* Piece id queued at `index` of the next-piece preview. */
int vobg_next_piece(const VOBGGame *g, int index);

/* Cell offset of block `n` (0..3) of the falling piece, in absolute coords.
 * Integer column: floor(xf), i.e. what the piece collides against. */
void vobg_active_cell(const VOBGGame *g, int n, int *out_x, int *out_y);

/* Same block, but with the continuous column so a dragged piece can be drawn
 * between the grid lines. out_x is fractional. */
void vobg_draw_cell(const VOBGGame *g, int n, float *out_x, float *out_y);

/* Side length of the piece's bounding box, in cells (3, or 4 for the I piece). */
int vobg_piece_box_size(int piece);

#endif /* VOBG_TETRIS_H */
/*
 * VOBGTetris.c -- see VOBGTetris.h.
 *
 * Rules: 7-bag randomiser, clockwise rotation with simple wall/floor kicks,
 * lock delay, single/double/triple/tetris scoring, guideline-ish gravity curve,
 * level up every 10 lines. No hold piece, no T-spins, no combos -- that is the
 * "classic essentials" scope this game was built to.
 */

#include "VOBGTetris.h"

#include <math.h>
#include <string.h>

#define VOBG_SOFT_DROP_MULT 20  /* gravity multiplier while soft dropping */

/* Offsets of block `n` of `piece` in rotation state `rot`, inside the piece's
 * bounding box. Every state lists exactly 4 cells. */
typedef struct {
    signed char x, y;
} VOBGOffset;

static const VOBGOffset kShapes[VOBG_PIECE_COUNT][4][4] = {
    { /* I, 4x4 box */
      {{0, 1}, {1, 1}, {2, 1}, {3, 1}},
      {{2, 0}, {2, 1}, {2, 2}, {2, 3}},
      {{0, 2}, {1, 2}, {2, 2}, {3, 2}},
      {{1, 0}, {1, 1}, {1, 2}, {1, 3}}
    },
    { /* J, 3x3 box */
      {{0, 0}, {0, 1}, {1, 1}, {2, 1}},
      {{1, 0}, {2, 0}, {1, 1}, {1, 2}},
      {{0, 1}, {1, 1}, {2, 1}, {2, 2}},
      {{1, 0}, {1, 1}, {0, 2}, {1, 2}}
    },
    { /* L, 3x3 box */
      {{2, 0}, {0, 1}, {1, 1}, {2, 1}},
      {{1, 0}, {1, 1}, {1, 2}, {2, 2}},
      {{0, 1}, {1, 1}, {2, 1}, {0, 2}},
      {{0, 0}, {1, 0}, {1, 1}, {1, 2}}
    },
    { /* O, 3x3 box, identical in every state so rotating never shifts it */
      {{1, 0}, {2, 0}, {1, 1}, {2, 1}},
      {{1, 0}, {2, 0}, {1, 1}, {2, 1}},
      {{1, 0}, {2, 0}, {1, 1}, {2, 1}},
      {{1, 0}, {2, 0}, {1, 1}, {2, 1}}
    },
    { /* S, 3x3 box */
      {{1, 0}, {2, 0}, {0, 1}, {1, 1}},
      {{1, 0}, {1, 1}, {2, 1}, {2, 2}},
      {{1, 1}, {2, 1}, {0, 2}, {1, 2}},
      {{0, 0}, {0, 1}, {1, 1}, {1, 2}}
    },
    { /* Z, 3x3 box */
      {{0, 0}, {1, 0}, {1, 1}, {2, 1}},
      {{2, 0}, {1, 1}, {2, 1}, {1, 2}},
      {{0, 1}, {1, 1}, {1, 2}, {2, 2}},
      {{1, 0}, {0, 1}, {1, 1}, {0, 2}}
    },
    { /* T, 3x3 box */
      {{1, 0}, {0, 1}, {1, 1}, {2, 1}},
      {{1, 0}, {1, 1}, {2, 1}, {1, 2}},
      {{0, 1}, {1, 1}, {2, 1}, {1, 2}},
      {{1, 0}, {0, 1}, {1, 1}, {1, 2}}
    }
};

static int box_size(int piece) { return piece == VOBG_PIECE_I ? 4 : 3; }

int vobg_piece_box_size(int piece) { return box_size(piece); }

/* ------------------------------------------------------------------ random */

static unsigned long rng_next(VOBGGame *g)
{
    /* xorshift32: tiny, fast, and fully reproducible from the seed, which the
     * unit tests rely on. */
    unsigned long x = g->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g->rng = x;
    return x;
}

static int rng_below(VOBGGame *g, int n)
{
    return (int)(rng_next(g) % (unsigned long)n);
}

static void bag_refill(VOBGGame *g)
{
    int i;
    for (i = 0; i < VOBG_PIECE_COUNT; i++) {
        g->bag[i] = (unsigned char)i;
    }
    for (i = VOBG_PIECE_COUNT - 1; i > 0; i--) {
        int j = rng_below(g, i + 1);
        unsigned char tmp = g->bag[i];
        g->bag[i] = g->bag[j];
        g->bag[j] = tmp;
    }
    g->bag_len = VOBG_PIECE_COUNT;
}

static int bag_pull(VOBGGame *g)
{
    if (g->bag_len <= 0) {
        bag_refill(g);
    }
    g->bag_len--;
    return g->bag[g->bag_len];
}

/* ------------------------------------------------------------------ timing */

int vobg_col(const VOBGGame *g)
{
    /* Plain floor, deliberately unclamped. A piece's *box* left edge can sit at
     * -1 (the O occupies box columns 1-2, so it rests against the left wall with
     * the box at -1) and floor() is exactly what that needs. Anything out of
     * range is rejected by fits(), so no clamping is wanted here -- clamping to
     * 0 would silently move a wall-hugging piece one column inward. */
    return (int)floor(g->xf);
}

float vobg_gravity_seconds(int level)
{
    /* Guideline curve: 1.0s per row at level 1, roughly 0.06s by level 10. Past
     * that it is already faster than the display refresh, so clamp to one row
     * per frame -- otherwise gravity would skip rows through the `while` loop in
     * vobg_update and tunnelling artefacts become visible. */
    double base, seconds;
    const double min_interval = 1.0 / 60.0;

    if (level < 1) {
        level = 1;
    }
    if (level > VOBG_MAX_LEVEL) {
        level = VOBG_MAX_LEVEL;
    }
    base = 0.8 - (level - 1) * 0.007;
    if (base < 0.0) {
        base = 0.0;
    }
    seconds = pow(base, (double)(level - 1));
    if (seconds < min_interval) {
        seconds = min_interval;
    }
    return (float)seconds;
}

/* ------------------------------------------------------------------ board */

unsigned char vobg_cell(const VOBGGame *g, int col, int row)
{
    if (col < 0 || col >= VOBG_COLS || row < 0 || row >= VOBG_ROWS) {
        return 0;
    }
    return g->cells[row][col];
}

void vobg_active_cell(const VOBGGame *g, int n, int *out_x, int *out_y)
{
    const VOBGOffset *o = &kShapes[g->active][g->rot][n];
    if (out_x) {
        *out_x = vobg_col(g) + o->x;
    }
    if (out_y) {
        *out_y = g->y + o->y;
    }
}

void vobg_draw_cell(const VOBGGame *g, int n, float *out_x, float *out_y)
{
    const VOBGOffset *o = &kShapes[g->active][g->rot][n];
    if (out_x) {
        *out_x = g->xf + (float)o->x;
    }
    if (out_y) {
        *out_y = (float)(g->y + o->y);
    }
}

int vobg_next_piece(const VOBGGame *g, int index)
{
    if (index < 0 || index >= VOBG_NEXT_COUNT) {
        return -1;
    }
    return g->next[index];
}

/* Would the active piece fit with its box at (x, y) in rotation state `rot`? */
static int fits(const VOBGGame *g, int x, int y, int rot)
{
    int n;
    for (n = 0; n < 4; n++) {
        const VOBGOffset *o = &kShapes[g->active][rot][n];
        int cx = x + o->x;
        int cy = y + o->y;
        if (cx < 0 || cx >= VOBG_COLS || cy >= VOBG_ROWS) {
            return 0;
        }
        /* cy < 0 means the piece is above the buffer; that is still legal, it
         * only happens for the tallest rotations right after a spawn. */
        if (cy >= 0 && g->cells[cy][cx] != 0) {
            return 0;
        }
    }
    return 1;
}

static int grounded(const VOBGGame *g)
{
    return !fits(g, vobg_col(g), g->y + 1, g->rot);
}

/* Range of whole columns this piece may occupy at its current row. Returns 0 if
 * there is no legal column at all.
 *
 * The scan bounds are the important part, and getting them from the bounding box
 * is wrong. A piece's box is a fixed size for every rotation, but a rotated
 * piece does not fill it: a vertical I occupies a single column of its 4-wide
 * box, so it can sit with the box at column 7 and its cell at column 9. Bounding
 * the scan by the box width capped it at COLS-4 and made the far right column
 * unreachable for exactly the pieces that need it most.
 *
 * So the bounds come from where the cells of *this rotation* actually are, with
 * one column of slack either side so fits() gets the final say.
 */
static int legal_range(const VOBGGame *g, int *out_lo, int *out_hi)
{
    int min_x = 99, max_x = -99;
    int lo_box, hi_box;
    int lo = 0, hi = 0;
    int col, n, found = 0;

    for (n = 0; n < 4; n++) {
        const VOBGOffset *o = &kShapes[g->active][g->rot][n];
        if (o->x < min_x) {
            min_x = o->x;
        }
        if (o->x > max_x) {
            max_x = o->x;
        }
    }
    /* The box may sit far enough left for the leftmost cell to land on column 0. */
    lo_box = -min_x;
    /* ...and far enough right for the rightmost cell to land on the last column. */
    hi_box = VOBG_COLS - 1 - max_x;

    for (col = lo_box; col <= hi_box; col++) {
        if (fits(g, col, g->y, g->rot)) {
            if (!found) {
                lo = col;
                found = 1;
            }
            hi = col;
        }
    }
    if (!found) {
        return 0;
    }
    if (out_lo) {
        *out_lo = lo;
    }
    if (out_hi) {
        *out_hi = hi;
    }
    return 1;
}

void vobg_drag_to(VOBGGame *g, float xf)
{
    int lo, hi;

    if (g->state != VOBG_STATE_PLAYING) {
        return;
    }
    if (!legal_range(g, &lo, &hi)) {
        return; /* boxed in; leave it where it is */
    }
    if (xf < (float)lo) {
        xf = (float)lo;
    }
    if (xf > (float)hi) {
        xf = (float)hi;
    }
    g->xf = xf;

    /* Steering a grounded piece resets the lock delay, same as vobg_move. */
    if (grounded(g)) {
        g->lock_timer = 0.0f;
    }
}

float vobg_snap_target(const VOBGGame *g)
{
    int lo, hi;
    float target;

    if (!legal_range(g, &lo, &hi)) {
        return g->xf;
    }
    /* Round to nearest, then clamp. Since xf is already inside [lo,hi] the
     * clamp only matters at the exact edges. */
    target = (float)floor(g->xf + 0.5f);
    if (target < (float)lo) {
        target = (float)lo;
    }
    if (target > (float)hi) {
        target = (float)hi;
    }
    return target;
}

void vobg_snap(VOBGGame *g)
{
    g->xf = vobg_snap_target(g);
}

static void place(const VOBGGame *g, VOBGGame *out, int n)
{
    const VOBGOffset *o = &kShapes[g->active][g->rot][n];
    int cx = vobg_col(g) + o->x;
    int cy = g->y + o->y;
    if (cx >= 0 && cx < VOBG_COLS && cy >= 0 && cy < VOBG_ROWS) {
        out->cells[cy][cx] = (unsigned char)(g->active + 1);
    }
}

int vobg_ghost_row(const VOBGGame *g)
{
    int y = g->y;
    while (fits(g, vobg_col(g), y + 1, g->rot)) {
        y++;
    }
    return y;
}

/* -------------------------------------------------------------- life cycle */

static void spawn(VOBGGame *g)
{
    int i;

    /* The head of the queue becomes the active piece and the queue refills from
     * the bag, so `next` always shows the pieces after the one in play. */
    g->active = g->next[0];
    for (i = 0; i < VOBG_NEXT_COUNT - 1; i++) {
        g->next[i] = g->next[i + 1];
    }
    g->next[VOBG_NEXT_COUNT - 1] = (unsigned char)bag_pull(g);

    g->rot = VOBG_ROT_0;
    g->xf = (float)((VOBG_COLS - box_size(g->active)) / 2);
    g->y = 0;
    g->drop_timer = 0.0f;
    g->lock_timer = 0.0f;
    g->last_drop_cells = 0;
    g->soft_drop = 0;

    if (!fits(g, vobg_col(g), g->y, g->rot)) {
        g->state = VOBG_STATE_GAMEOVER;
    }
}

void vobg_init(VOBGGame *g, unsigned long seed)
{
    int i;

    memset(g, 0, sizeof(*g));
    /* xorshift32 dies on a zero state, and seed 0 is a legal thing to pass. */
    g->rng = seed ? seed : 0x1234567ul;
    for (i = 0; i < 4; i++) {
        rng_next(g); /* warm up: cheap insurance against a poor first bag */
    }

    g->score = 0;
    g->lines = 0;
    g->level = 1;
    g->state = VOBG_STATE_PLAYING;
    g->soft_drop = 0;

    bag_refill(g);
    for (i = 0; i < VOBG_NEXT_COUNT; i++) {
        g->next[i] = (unsigned char)bag_pull(g);
    }
    spawn(g);
}

/* --------------------------------------------------------------- mechanics */

int vobg_move(VOBGGame *g, int dx)
{
    if (g->state != VOBG_STATE_PLAYING) {
        return 0;
    }
    if (dx == 0 || !fits(g, vobg_col(g) + dx, g->y, g->rot)) {
        return 0;
    }
    g->xf = (float)(vobg_col(g) + dx);
    /* Steering a grounded piece resets the lock delay, which is what makes
     * sliding a piece along the floor possible at all. */
    if (grounded(g)) {
        g->lock_timer = 0.0f;
    }
    return 1;
}

int vobg_rotate(VOBGGame *g)
{
    /* Kicks are tried in order and the first that fits wins. A full SRS table is
     * overkill for a swipe game; this is enough to keep rotations off the walls
     * and out of the floor. The two-deep entries at the end matter: without them
     * an I piece lying flat on the bottom row has nowhere to go, since turning it
     * upright needs two rows of headroom. They are last so a sideways kick is
     * still preferred when one is available. */
    static const VOBGOffset kicks[11] = {
        {0, 0},  {-1, 0},  {1, 0},   {0, -1}, {-2, 0},
        {2, 0},  {-1, -1}, {1, -1},  {0, -2}, {-1, -2},
        {1, -2}
    };
    int nrot = (g->rot + 1) & 3;
    int i;

    if (g->state != VOBG_STATE_PLAYING) {
        return 0;
    }
    for (i = 0; i < (int)(sizeof(kicks) / sizeof(kicks[0])); i++) {
        int nx = vobg_col(g) + kicks[i].x;
        int ny = g->y + kicks[i].y;
        if (fits(g, nx, ny, nrot)) {
            g->rot = (signed char)nrot;
            g->xf = (float)nx;
            g->y = ny;
            if (grounded(g)) {
                g->lock_timer = 0.0f;
            }
            return 1;
        }
    }
    return 0;
}

void vobg_set_soft_drop(VOBGGame *g, int on)
{
    g->soft_drop = on ? 1 : 0;
    if (on) {
        g->drop_timer = 0.0f;
    }
}

/* Lock the piece where it stands: stamp it, flag full rows, start the flash. */
static void lock_piece(VOBGGame *g)
{
    int n, row, col;
    int full = 0;
    int lowest = 0;

    /* A piece can be locked mid-drag, with `xf` between two columns. Settle it
     * onto the grid before stamping, or blocks would bake into the board at
     * fractional columns and the stack would come out ragged. */
    vobg_snap(g);

    for (n = 0; n < 4; n++) {
        const VOBGOffset *o = &kShapes[g->active][g->rot][n];
        int cy = g->y + o->y;
        if (cy > lowest) {
            lowest = cy;
        }
        place(g, g, n);
    }

    for (row = 0; row < VOBG_ROWS; row++) {
        int complete = 1;
        for (col = 0; col < VOBG_COLS; col++) {
            if (g->cells[row][col] == 0) {
                complete = 0;
                break;
            }
        }
        g->clear_rows[row] = (unsigned char)(complete ? 1 : 0);
        if (complete) {
            full++;
        }
    }

    /* The piece locked entirely inside the buffer: the stack reached the top. */
    if (lowest < VOBG_HIDDEN_ROWS) {
        g->state = VOBG_STATE_GAMEOVER;
        return;
    }

    g->clear_count = full;
    g->clear_timer = 0.0f;
    g->state = full ? VOBG_STATE_CLEARING : VOBG_STATE_PLAYING;
    if (!full) {
        spawn(g);
    }
}

/* Remove the flagged rows, shift everything above them down, score. */
static void collapse_rows(VOBGGame *g)
{
    static const int points[5] = {0, 100, 300, 500, 800};
    int row, write;
    int cleared = g->clear_count;

    write = VOBG_ROWS - 1;
    for (row = VOBG_ROWS - 1; row >= 0; row--) {
        if (g->clear_rows[row]) {
            continue;
        }
        if (write != row) {
            memcpy(g->cells[write], g->cells[row], VOBG_COLS);
        }
        write--;
    }
    while (write >= 0) {
        memset(g->cells[write], 0, VOBG_COLS);
        write--;
    }

    g->lines += cleared;
    g->score += points[cleared > 4 ? 4 : cleared] * g->level;
    g->level = g->lines / 10 + 1;
    if (g->level > VOBG_MAX_LEVEL) {
        g->level = VOBG_MAX_LEVEL;
    }
    g->clear_count = 0;
    g->state = VOBG_STATE_PLAYING;
    spawn(g);
}

int vobg_hard_drop(VOBGGame *g)
{
    int target, cells;

    if (g->state != VOBG_STATE_PLAYING) {
        return 0;
    }
    /* Settle onto the grid *before* finding the landing row. Snapping afterwards
     * would recompute the legal range at the row the piece landed in, where the
     * stack may block a column that was perfectly legal up here, and the piece
     * would jump sideways as it fell. */
    vobg_snap(g);
    target = vobg_ghost_row(g);
    cells = target - g->y;
    g->y = target;
    g->score += cells * 2;
    lock_piece(g);
    return cells;
}

int vobg_step_down(VOBGGame *g)
{
    if (g->state != VOBG_STATE_PLAYING) {
        return 0;
    }
    if (!fits(g, vobg_col(g), g->y + 1, g->rot)) {
        return 0;
    }
    g->y++;
    if (g->soft_drop) {
        g->score += 1;
    }
    g->lock_timer = 0.0f;
    return 1;
}

int vobg_update(VOBGGame *g, float dt)
{
    int changed = 0;

    if (dt < 0.0f) {
        dt = 0.0f;
    }

    if (g->state == VOBG_STATE_CLEARING) {
        /* The doomed rows pulse white, so this phase genuinely animates and has
         * to report a change every frame. */
        g->clear_timer += dt;
        if (g->clear_timer >= VOBG_CLEAR_DELAY) {
            collapse_rows(g);
        }
        return 1;
    }
    if (g->state != VOBG_STATE_PLAYING) {
        return 0;
    }

    if (grounded(g)) {
        float before = g->xf;
        g->drop_timer = 0.0f;
        /* Settle onto the grid the moment the piece touches down, while the
         * player can still see it. Deferring this to lock_piece would recompute
         * the legal range here, where the stack underneath can block a column
         * that was clear up above, and the piece would shift as it landed. */
        vobg_snap(g);
        if (g->xf != before) {
            changed = 1;
        }
        g->lock_timer += dt;
        if (g->lock_timer >= VOBG_LOCK_DELAY) {
            lock_piece(g);
            changed = 1;
        }
        /* Otherwise the piece is sitting still on the stack and nothing on screen
         * moves until the lock delay expires. That is most of the time in a
         * normal game, and it is where the wasted frames were. */
        return changed;
    }

    g->drop_timer += dt * (g->soft_drop ? VOBG_SOFT_DROP_MULT : 1.0f);
    {
        float interval = vobg_gravity_seconds(g->level);
        int guard = 0;
        while (g->drop_timer >= interval && guard++ < 32) {
            g->drop_timer -= interval;
            if (!vobg_step_down(g)) {
                break;
            }
            changed = 1;
            if (!grounded(g)) {
                continue;
            }
            break; /* touched down: the lock delay takes over next frame */
        }
    }
    return changed;
}
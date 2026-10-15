/*
 * VOBGGameView.m -- the playfield: layout, drawing, input, game loop.
 *
 * Deliberately one view rather than a view per block. Everything is drawn in a
 * single -drawRect: from cached block images, which is far cheaper on iOS 6
 * hardware than maintaining hundreds of UIViews, and it keeps the rotation and
 * resize paths trivial.
 *
 * The update loop is driven by CADisplayLink. dt is clamped so that returning
 * from a long background pause does not teleport the piece dozens of rows.
 */

#import "VOBGGameView.h"

#include "VOBGTetris.h"
#import "VOBGArt.h"
#import "VOBGSwipe.h"
#import "VOBGOverlay.h"
#import "VOBGSettings.h"

#include <math.h>

/*
 * Colour handling.
 *
 * CGContextSetFillColorWithColor / CGContextSetStrokeColorWithColor only accept
 * a CGColor in an RGB colour space. +[UIColor colorWithWhite:alpha:] does NOT
 * produce one -- it yields a colour in the generic gray space -- and passing it
 * there fails *silently*: the context keeps whatever colour it had, so the
 * fill or stroke never happens.
 *
 * That is why the well, the grid, the preview panel, the borders and the ghost
 * outline were all invisible while the blocks and the HUD text were not: the
 * blocks are drawn as images (an RGB gradient built by CGGradient, never
 * touching this path) and the text is drawn by UIKit's own text renderer. Every
 * element that disappeared went through these two functions.
 *
 * Extracting the components and handing them to CGContextSetRGBFillColor /
 * CGContextSetRGBStrokeColor avoids the colour space question entirely, which
 * is also why the earlier "just make it brighter" fix changed nothing.
 */

static void VOBGGetRGBA(UIColor *color, CGFloat *rgba)
{
    CGColorRef cg = color.CGColor;
    if (cg == NULL) {
        rgba[0] = rgba[1] = rgba[2] = 0.0;
        rgba[3] = 1.0;
        return;
    }
    size_t count = CGColorGetNumberOfComponents(cg);
    const CGFloat *c = CGColorGetComponents(cg);
    if (c == NULL) {
        rgba[0] = rgba[1] = rgba[2] = 0.0;
        rgba[3] = 1.0;
        return;
    }
    if (count >= 4) {
        rgba[0] = c[0];
        rgba[1] = c[1];
        rgba[2] = c[2];
        rgba[3] = c[3];
    } else if (count >= 2) {
        /* Gray: components are (white, alpha). */
        rgba[0] = rgba[1] = rgba[2] = c[0];
        rgba[3] = c[1];
    } else {
        rgba[0] = rgba[1] = rgba[2] = 0.0;
        rgba[3] = 1.0;
    }
}

static void VOBGSetFill(CGContextRef ctx, UIColor *color)
{
    CGFloat c[4];
    VOBGGetRGBA(color, c);
    CGContextSetRGBFillColor(ctx, c[0], c[1], c[2], c[3]);
}

static void VOBGSetStroke(CGContextRef ctx, UIColor *color)
{
    CGFloat c[4];
    VOBGGetRGBA(color, c);
    CGContextSetRGBStrokeColor(ctx, c[0], c[1], c[2], c[3]);
}

@interface VOBGGameView () <VOBGSwipeDelegate, VOBGOverlayDelegate>
@end

@implementation VOBGGameView {
    VOBGGame _game;
    VOBGSwipe *_swipe;
    CADisplayLink *_link;
    CFTimeInterval _lastFrame;

    CGFloat _cell;      /* points per cell */
    CGRect _boardRect;  /* where the well is drawn, in view coordinates */
    CGRect _nextRect;   /* the preview column */
    CGRect _hudRect;    /* the top strip holding the score fields */
    CGRect _pauseButtonRect;

    VOBGOverlay *_overlay;
    BOOL _paused;
    BOOL _pressedPause;   /* pause button is held down */

    BOOL _running;

    /* Snap animation, so releasing the piece eases onto the grid instead of
     * jumping to it. */
    BOOL _snapping;
    float _snapFrom;
    float _snapTo;
    NSTimeInterval _snapElapsed;

    /* Drag anchoring.
     *
     * The gesture recogniser reports how far the finger has travelled from
     * where the touch *began*, but a swipe only commits after it has moved past
     * the threshold -- by then the finger is already a threshold-width away. If
     * the piece were placed relative to that, it would jump sideways the moment
     * a swipe turned into a drag. So the first DRAG of a gesture records where
     * the piece was, and later ones are measured from there.
     */
    BOOL _dragAnchored;
    float _dragAnchorColumn;
}

- (id)initWithFrame:(CGRect)frame
{
    self = [super initWithFrame:frame];
    if (self) {
        self.backgroundColor = [UIColor blackColor];
        self.opaque = YES;
        self.multipleTouchEnabled = NO;

        _swipe = [[VOBGSwipe alloc] init];
        _swipe.delegate = self;

        _overlay = [[VOBGOverlay alloc] initWithFrame:self.bounds];
        _overlay.delegate = self;
        _overlay.glossyBlocks = [VOBGSettings sharedSettings].glossyBlocks;
        _overlay.dpadEnabled = [VOBGSettings sharedSettings].dpadControls;
        [self addSubview:_overlay];

        [self newGame];

        /* Stop the clock when backgrounded and restart it on return. A display
         * link left running in the background burns battery and, worse, hands
         * one enormous dt to the first frame back. */
        NSNotificationCenter *nc = [NSNotificationCenter defaultCenter];
        [nc addObserver:self
               selector:@selector(handleAppBackground)
                   name:UIApplicationDidEnterBackgroundNotification
                 object:nil];
        [nc addObserver:self
               selector:@selector(handleAppForeground)
                   name:UIApplicationWillEnterForegroundNotification
                 object:nil];
        [nc addObserver:self
               selector:@selector(handleSettingsChanged)
                   name:VOBGSettingsChangedNotification
                 object:nil];
    }
    return self;
}

/* The pause button lives in the HUD row. Drawn rather than a UIButton so it
 * matches the neon look; a UIButton would bring its own system chrome. */
- (void)applyPauseButton
{
    _pauseButtonRect = CGRectZero;
    if (_overlay != nil) {
        _overlay.passthroughRect = CGRectZero;
    }
    if (_hudRect.size.width <= 0.0) {
        return; /* laid out before the HUD has a width */
    }
    /* 38pt rather than the usual 44 minimum: the HUD strip is only 44 tall, and
     * a target that overflows it would eat touches meant for the top of the
     * playfield. Still comfortably larger than the icon drawn inside it. */
    CGFloat s = MIN(38.0, _hudRect.size.height - 4.0);
    _pauseButtonRect = CGRectMake(CGRectGetMaxX(_hudRect) - s,
                                  CGRectGetMidY(_hudRect) - s / 2.0, s, s);
    /* Tell the overlay to keep its hands off this rect. */
    _overlay.passthroughRect = _pauseButtonRect;
}

/* While paused the clock must not run: gravity, lock delay and the line-clear
 * animation are all driven from the display link. */
- (void)setPaused:(BOOL)paused
{
    if (_paused == paused) {
        return;
    }
    _paused = paused;
    if (paused) {
        [self stopLoop];
        [_swipe cancelTouches];
        vobg_set_soft_drop(&_game, 0);
    } else {
        [self startLoop];
    }
    [self setNeedsDisplay];
}

/* Fresh board, fresh random stream. */
- (void)newGame
{
    unsigned long seed = (unsigned long)(arc4random_uniform(0xFFFFFFFFu) ^ 0x5DEECE66u);
    vobg_init(&_game, seed);
    [_swipe cancelTouches];
    vobg_set_soft_drop(&_game, 0);
    _lastFrame = 0;
    /* A new game is always in play, so pick up the current control scheme here
     * rather than relying on the caller. That covers launch (init calls this
     * directly) as well as the pause menu's Restart button. */
    self.paused = NO;
    [self applyControlMode];
    [self setNeedsDisplay];
}

- (void)dealloc
{
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    [_link invalidate];
}

/* ---------------------------------------------------------------- lifecycle */

- (void)didMoveToWindow
{
    [super didMoveToWindow];
    if (self.window != nil) {
        [self startLoop];
    } else {
        [self stopLoop];
    }
}

- (void)startLoop
{
    /* A new game resets the paused flag from init, before the view has a
     * window. Starting the link that early would run it against a view that is
     * not on screen; didMoveToWindow starts it for real. */
    if (_link != nil || _running || self.window == nil) {
        return;
    }
    _running = YES;
    _lastFrame = 0;
    _link = [CADisplayLink displayLinkWithTarget:self selector:@selector(step:)];
    /* preferredFramesPerSecond only exists from iOS 10. Setting it unguarded is
     * a crash on iOS 6-9 at launch, so ask the runtime whether the selector is
     * there rather than testing the OS version. Without it the link simply
     * follows the panel's native rate, which is what we want anyway. */
    SEL setter = NSSelectorFromString(@"setPreferredFramesPerSecond:");
    if ([_link respondsToSelector:setter]) {
        /* Invoke dynamically: the 9.3 SDK header does not declare the property,
         * so a direct [link setPreferredFramesPerSecond:60] would not compile
         * against the slice that actually has to run on iOS 6. */
        NSMethodSignature *sig = [_link methodSignatureForSelector:setter];
        NSInvocation *call = [NSInvocation invocationWithMethodSignature:sig];
        [call setTarget:_link];
        [call setSelector:setter];
        NSInteger fps = 60;
        [call setArgument:&fps atIndex:2];
        [call invoke];
    }
    [_link addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
}

- (void)stopLoop
{
    _running = NO;
    [_link invalidate];
    _link = nil;
}

- (void)handleAppBackground
{
    /* Also release soft drop, or the game comes back from the background
     * falling twenty times too fast with no finger on the screen. */
    [self stopLoop];
    [_swipe cancelTouches];
    vobg_set_soft_drop(&_game, 0);
}

- (void)handleAppForeground
{
    if (self.window != nil) {
        [self startLoop];
    }
}

- (void)restart
{
    [self newGame];
}

/* ------------------------------------------------------------------ update */

- (void)step:(CADisplayLink *)link
{
    CFTimeInterval now = link.timestamp;
    BOOL changed = NO;
    if (_lastFrame == 0) {
        _lastFrame = now;
        return;
    }
    /* Clamp: without this, a resume from the background hands the game one
     * enormous dt and the piece falls off the board. */
    CFTimeInterval dt = now - _lastFrame;
    _lastFrame = now;
    if (dt > 0.05) {
        dt = 0.05;
    }
    if (dt < 0) {
        dt = 0;
    }

    changed = vobg_update(&_game, (float)dt);

    /* Ease the piece onto the grid after a drag ends. */
    changed = [self advanceSnap:(NSTimeInterval)dt] || changed;

    /* Redraw only when the game actually moved. Calling setNeedsDisplay
     * unconditionally here meant a full repaint -- the whole well, the preview
     * and three freshly built HUD strings -- on all 60 frames a second, even
     * while the piece sat still waiting out its lock delay. iOS killed the app
     * for it: EXC_RESOURCE CPU, 61% over 180 seconds. */
    if (changed) {
        [self setNeedsDisplay];
    }
}

/* ------------------------------------------------------------------ layout */

- (void)layoutSubviews
{
    [super layoutSubviews];

    CGFloat width = self.bounds.size.width;
    CGFloat height = self.bounds.size.height;

    /* Room for the HUD strip on top. */
    CGFloat hudHeight = 44.0;
    CGFloat margin = 8.0;
    _hudRect = CGRectMake(margin, 0.0, width - margin * 2.0, hudHeight);

    /* The preview strip is sized relative to the board rather than fixed at 76pt.
     * A fixed width was tuned for a 320pt portrait screen and left the panel
     * overflowing or the board needlessly cramped on wider ones. Scaling it with
     * the cell keeps the same proportion everywhere, which matters most in
     * landscape where the screen is short and the board is height-limited: a
     * fixed 76pt strip was eating a third of the width. */
    CGFloat availH = height - hudHeight - margin * 2.0;
    CGFloat probeCell = floor(MIN(width / VOBG_COLS, availH / VOBG_VISIBLE_ROWS));
    if (probeCell < 6.0) {
        probeCell = 6.0;
    }
    CGFloat previewWidth = floor(probeCell * 3.4);
    if (previewWidth < 44.0) {
        previewWidth = 44.0;
    }

    CGFloat availW = width - previewWidth - margin * 3.0;

    CGFloat newCell = floor(MIN(availW / VOBG_COLS, availH / VOBG_VISIBLE_ROWS));
    if (newCell < 6.0) {
        newCell = 6.0;
    }

    /* Adopt the new cell size BEFORE deriving the rects from it. Reading _cell
     * first and assigning afterwards uses the previous size -- zero on the first
     * pass -- so the well collapses to nothing (nothing is filled, nothing is
     * stroked) and the preview column is placed from a zero-width board. That
     * is why the playfield and panel were invisible while the HUD, which does
     * not depend on these rects, drew correctly. */
    if (newCell != _cell) {
        _cell = newCell;
        [[VOBGArt sharedArt] invalidate];
        /* The gesture thresholds are measured in cells, so they have to follow
         * the cell size. Without this they stayed at their provisional values
         * and a 12pt slop was over half a cell on a 3GS. */
        [_swipe setCellSize:_cell];
    }

    CGFloat boardW = _cell * VOBG_COLS;
    CGFloat boardH = _cell * VOBG_VISIBLE_ROWS;

    /* Centre the board *and* the preview strip as one group. Pinning the board to
     * the left edge was fine on a 4:3 screen where the two together filled the
     * width, but on a 16:9 phone in landscape the group occupies barely a third
     * of the screen and the board ends up jammed against the bezel with a huge
     * dead gap beside it. */
    CGFloat groupW = boardW + previewWidth + margin;
    CGFloat groupX = floor((width - groupW) / 2.0);
    if (groupX < margin) {
        groupX = margin;
    }

    /* Vertically centre in whatever space is left under the HUD, rather than
     * pinning to the top and leaving a gap at the bottom on a tall screen. */
    CGFloat boardY = hudHeight + MAX(0.0, (availH - boardH) / 2.0);

    _boardRect = CGRectMake(groupX, boardY, boardW, boardH);
    _nextRect = CGRectMake(groupX + boardW + margin, boardY, previewWidth, boardH);

    [self applyPauseButton];
    _overlay.frame = self.bounds;
}

/* ------------------------------------------------------------------ drawing */

/* The background is black, which made letterboxing impossible to spot by eye
 * once (the bars are black too, on a black app). VOBG_DIAGNOSTIC_BG exists to
 * temporarily fill the view white so the bars would show; it is 0 now that the
 * full-screen launch storyboard has settled it. */
#ifndef VOBG_DIAGNOSTIC_BG
#define VOBG_DIAGNOSTIC_BG 0
#endif

- (void)drawRect:(CGRect)rect
{
    CGContextRef ctx = UIGraphicsGetCurrentContext();

    #if VOBG_DIAGNOSTIC_BG
    /* Diagnostic only: see the note above. */
    VOBGSetFill(ctx, [UIColor whiteColor]);
#else
    VOBGSetFill(ctx, [UIColor blackColor]);
#endif
    CGContextFillRect(ctx, self.bounds);

    [self drawWellInContext:ctx];
    [self drawStackInContext:ctx];
    [self drawActivePieceInContext:ctx];
    [self drawHUDInContext:ctx];
    [self drawPreviewInContext:ctx];
    [self drawOverlaysInContext:ctx];
}

- (void)drawWellInContext:(CGContextRef)ctx
{
    /* The well is a visible panel, not a subtle tint. An earlier pass used 6%
     * white, which on an OLED panel is indistinguishable from the black
     * background -- the well simply looked like it was not drawn at all. */
    VOBGSetFill(ctx, [UIColor colorWithWhite:0.10f alpha:1.0f]);
    CGContextFillRect(ctx, _boardRect);

    /* Grid on both axes, bright enough to read. */
    VOBGSetStroke(ctx, [UIColor colorWithWhite:1.0f alpha:0.16f]);
    CGContextSetLineWidth(ctx, 1.0);

    int c, r;
    for (c = 0; c <= VOBG_COLS; c++) {
        CGFloat x = floor(_boardRect.origin.x + c * _cell) + 0.5;
        CGContextMoveToPoint(ctx, x, _boardRect.origin.y);
        CGContextAddLineToPoint(ctx, x, CGRectGetMaxY(_boardRect));
    }
    for (r = 0; r <= VOBG_VISIBLE_ROWS; r++) {
        CGFloat y = floor(_boardRect.origin.y + r * _cell) + 0.5;
        CGContextMoveToPoint(ctx, _boardRect.origin.x, y);
        CGContextAddLineToPoint(ctx, CGRectGetMaxX(_boardRect), y);
    }
    CGContextStrokePath(ctx);

    /* Border around the well, so its extent is unmistakable. */
    VOBGSetStroke(ctx, [UIColor colorWithWhite:1.0f alpha:0.45f]);
    CGContextSetLineWidth(ctx, 1.5);
    CGContextStrokeRect(ctx, CGRectInset(_boardRect, 0.5, 0.5));
}

- (void)drawStackInContext:(CGContextRef)ctx
{
    VOBGArt *art = [VOBGArt sharedArt];
    int row, col;

    for (row = 0; row < VOBG_ROWS; row++) {
        if (row < VOBG_HIDDEN_ROWS) {
            continue; /* above the visible well */
        }
        int y = row - VOBG_HIDDEN_ROWS;
        for (col = 0; col < VOBG_COLS; col++) {
            unsigned char v = vobg_cell(&_game, col, row);
            if (v == 0) {
                continue;
            }
            UIImage *img = [art blockImageForPiece:(v - 1) size:_cell];
            if (img == nil) {
                continue;
            }
            CGRect target = CGRectMake(_boardRect.origin.x + col * _cell,
                                       _boardRect.origin.y + y * _cell,
                                       _cell, _cell);
            CGContextDrawImage(ctx, target, img.CGImage);
        }
    }
}

- (void)drawActivePieceInContext:(CGContextRef)ctx
{
    VOBGArt *art = [VOBGArt sharedArt];
    int n;

    if (_game.state == VOBG_STATE_GAMEOVER) {
        return;
    }

    if (_game.state == VOBG_STATE_CLEARING) {
        /* The rows about to go pulse white. The active piece is not drawn here
         * during a clear: it has already locked into the stack, so drawing it
         * again would stamp it over the top of its own blocks at full
         * brightness and make it look like it never merged. */
        CGFloat progress = _game.clear_timer / (CGFloat)VOBG_CLEAR_DELAY;
        if (progress < 0.0f) {
            progress = 0.0f;
        }
        if (progress > 1.0f) {
            progress = 1.0f;
        }
        /* Ramp up and back down so the flash reads as a flash. */
        CGFloat pulse = (CGFloat)sin(progress * (CGFloat)M_PI);
        CGFloat alpha = 0.25f + 0.70f * pulse;
        int row;

        VOBGSetFill(ctx, [UIColor colorWithWhite:1.0f alpha:alpha]);
        for (row = VOBG_HIDDEN_ROWS; row < VOBG_ROWS; row++) {
            if (!_game.clear_rows[row]) {
                continue;
            }
            CGRect r = CGRectMake(_boardRect.origin.x,
                                  _boardRect.origin.y + (row - VOBG_HIDDEN_ROWS) * _cell,
                                  _cell * VOBG_COLS, _cell);
            CGContextFillRect(ctx, r);
        }
        return;
    }

    /* The active piece. Deliberately no ghost outline of where it will land: on a
     * small screen the hollow markers read as a second, real piece and make the
     * stack harder to read at a glance. */
    for (n = 0; n < 4; n++) {
        float x;
        float y;
        /* Fractional column: this is what lets a dragged piece sit between the
         * grid lines while it is being moved. */
        vobg_draw_cell(&_game, n, &x, &y);
        if (y < (float)VOBG_HIDDEN_ROWS) {
            continue;
        }
        UIImage *img = [art blockImageForPiece:_game.active size:_cell];
        if (img == nil) {
            continue;
        }
        CGRect target = CGRectMake(_boardRect.origin.x + x * _cell,
                                   _boardRect.origin.y + (y - (float)VOBG_HIDDEN_ROWS) * _cell,
                                   _cell, _cell);
        CGContextDrawImage(ctx, target, img.CGImage);
    }
}

- (void)drawHUDInContext:(CGContextRef)ctx
{
    /* NSString's -drawAtPoint:withAttributes: and -sizeWithAttributes: are iOS
     * 7.0+. Building an NSAttributedString and drawing *that* is the iOS 6 path
     * and keeps the same font/colour control. Calling the NSString form would
     * compile fine against the 9.3 SDK and crash on iOS 6 at run time. */
    UIColor *valueColor = [UIColor colorWithWhite:1.0f alpha:1.0f];
    UIColor *labelColor = [UIColor colorWithWhite:1.0f alpha:0.60f];

    /* Chain the fields: each returns the x just past itself. Discarding the
     * first return value stacks all three labels on top of each other at the
     * left edge. */
    CGFloat hudX = CGRectGetMinX(_hudRect);
    if (_hudRect.size.width <= 0.0) {
        hudX = 10.0; /* layout has not run yet */
    }
    CGFloat x = [self drawHUDField:[NSString stringWithFormat:@"%d", _game.score]
                             label:@"SCORE"
                                x:hudX
                       valueColor:valueColor
                       labelColor:labelColor];
    x = [self drawHUDField:[NSString stringWithFormat:@"%d", _game.lines]
                     label:@"LINES"
                        x:x
               valueColor:valueColor
               labelColor:labelColor];
    (void)[self drawHUDField:[NSString stringWithFormat:@"%d", _game.level]
                      label:@"LEVEL"
                         x:x
                valueColor:valueColor
                labelColor:labelColor];

    /* Pause button last, so it sits above anything the fields drew. */
    if (!CGRectIsEmpty(_pauseButtonRect)) {
        CGRect target = _pauseButtonRect;
        CGFloat bw = target.size.width * 0.11;
        CGFloat bh = target.size.height * 0.34;
        CGFloat gap = target.size.width * 0.09;
        CGFloat bx = CGRectGetMidX(target) - bw - gap / 2.0;
        CGFloat by = CGRectGetMidY(target) - bh / 2.0;

        /* The hit target is larger than the drawn outline, so show the press by
         * filling the whole target rather than by moving anything. */
        if (_pressedPause) {
            UIBezierPath *held =
                [UIBezierPath bezierPathWithRoundedRect:target
                                          cornerRadius:target.size.height * 0.25];
            VOBGSetFill(ctx, [UIColor colorWithWhite:1.0f alpha:0.22f]);
            [held fill];
        }

        /* A soft outline on every draw, so the target is discoverable -- an
         * unadorned corner of the screen is not something anyone will try. */
        CGRect outline = CGRectInset(target, 2.0, 2.0);
        UIBezierPath *p =
            [UIBezierPath bezierPathWithRoundedRect:outline
                                      cornerRadius:outline.size.height * 0.25];
        VOBGSetStroke(ctx, [UIColor colorWithWhite:1.0f alpha:_pressedPause ? 0.95f : 0.45f]);
        p.lineWidth = 1.5;
        [p stroke];

        /* Two bars: the universally read "pause" mark. */
        VOBGSetFill(ctx, [UIColor colorWithWhite:1.0f alpha:0.85f]);
        CGContextFillRect(ctx, CGRectMake(bx, by, bw, bh));
        CGContextFillRect(ctx, CGRectMake(bx + bw + gap, by, bw, bh));
    }
}

/* Draws "<value> <label>" and returns the x just past it. */
- (CGFloat)drawHUDField:(NSString *)value
                  label:(NSString *)label
                     x:(CGFloat)x
            valueColor:(UIColor *)valueColor
            labelColor:(UIColor *)labelColor
{
    UIFont *valueFont = [UIFont boldSystemFontOfSize:13.0];
    UIFont *labelFont = [UIFont systemFontOfSize:9.0];

    NSMutableAttributedString *s = [[NSMutableAttributedString alloc]
        initWithString:[value stringByAppendingString:
                             [@" " stringByAppendingString:label]]];
    [s addAttribute:NSFontAttributeName
               value:valueFont
               range:NSMakeRange(0, value.length)];
    [s addAttribute:NSForegroundColorAttributeName
               value:valueColor
               range:NSMakeRange(0, value.length)];
    [s addAttribute:NSFontAttributeName
               value:labelFont
               range:NSMakeRange(value.length, s.length - value.length)];
    [s addAttribute:NSForegroundColorAttributeName
               value:labelColor
               range:NSMakeRange(value.length, s.length - value.length)];
    [s drawAtPoint:CGPointMake(x, CGRectGetMinY(_hudRect) + 12.0)];
    return x + s.size.width + 14.0;
}

- (void)drawPreviewInContext:(CGContextRef)ctx
{
    VOBGArt *art = [VOBGArt sharedArt];
    int i;

    VOBGSetFill(ctx, [UIColor colorWithWhite:0.10f alpha:1.0f]);
    CGContextFillRect(ctx, _nextRect);
    VOBGSetStroke(ctx, [UIColor colorWithWhite:1.0f alpha:0.30f]);
    CGContextSetLineWidth(ctx, 1.0);
    CGContextStrokeRect(ctx, CGRectInset(_nextRect, 0.5, 0.5));

    /* Real attributed-string attribute keys. A dictionary keyed by UIColor is
     * the old NSString-drawing convention; handing it to NSAttributedString is
     * silently ignored, which renders the label in default black-on-black. */
    NSDictionary *labelAttrs = @{
        NSForegroundColorAttributeName: [UIColor colorWithWhite:1.0f alpha:0.60f],
        NSFontAttributeName: [UIFont boldSystemFontOfSize:9.0f]
    };
    [[[NSAttributedString alloc] initWithString:@"NEXT" attributes:labelAttrs]
        drawAtPoint:CGPointMake(_nextRect.origin.x + 6.0, _nextRect.origin.y + 4.0)];

    /* Sized so the widest preview (the I piece, four columns) still fits the
     * column instead of spilling over the edge of the screen. */
    CGFloat size = _cell * 0.72;
    CGFloat maxByWidth = (CGRectGetWidth(_nextRect) - 8.0) / 4.0;
    if (size > maxByWidth) {
        size = maxByWidth;
    }
    if (size < 4.0) {
        size = 4.0;
    }

    CGFloat slotH = (CGRectGetHeight(_nextRect) - 24.0) / VOBG_NEXT_COUNT;
    if (slotH <= 0) {
        return;
    }

    for (i = 0; i < VOBG_NEXT_COUNT; i++) {
        int piece = vobg_next_piece(&_game, i);
        if (piece < 0) {
            continue;
        }

        /* Lay the piece out from the real shape table. One scratch game struct
         * for the whole column rather than one per cell: this runs every frame
         * and VOBGGame is ~500 bytes to zero out. */
        VOBGGame tmp;
        memset(&tmp, 0, sizeof(tmp));
        tmp.active = (unsigned char)piece;
        tmp.rot = 0;

        int minX = 99, maxX = -99, minY = 99, maxY = -99, n;
        for (n = 0; n < 4; n++) {
            int px, py;
            vobg_active_cell(&tmp, n, &px, &py);
            if (px < minX) { minX = px; }
            if (px > maxX) { maxX = px; }
            if (py < minY) { minY = py; }
            if (py > maxY) { maxY = py; }
        }

        CGFloat pieceW = (maxX - minX + 1) * size;
        CGFloat pieceH = (maxY - minY + 1) * size;
        CGFloat originX = _nextRect.origin.x + (CGRectGetWidth(_nextRect) - pieceW) / 2.0;
        CGFloat slotTop = _nextRect.origin.y + 20.0 + i * slotH;
        CGFloat originY = slotTop + (slotH - pieceH) / 2.0;

        UIImage *img = [art blockImageForPiece:piece size:size];
        if (img == nil) {
            continue;
        }
        for (n = 0; n < 4; n++) {
            int px, py;
            vobg_active_cell(&tmp, n, &px, &py);
            CGRect target = CGRectMake(originX + (px - minX) * size,
                                       originY + (py - minY) * size,
                                       size, size);
            CGContextDrawImage(ctx, target, img.CGImage);
        }
    }
}

- (void)drawOverlaysInContext:(CGContextRef)ctx
{
    if (_game.state != VOBG_STATE_GAMEOVER) {
        return;
    }
    VOBGSetFill(ctx, [UIColor colorWithWhite:0.0f alpha:0.72f]);
    CGContextFillRect(ctx, self.bounds);

    CGFloat cx = CGRectGetMidX(self.bounds);
    CGFloat cy = CGRectGetMidY(self.bounds);

    NSAttributedString *over =
        [[NSAttributedString alloc] initWithString:@"GAME OVER"
                                        attributes:@{NSFontAttributeName:
                                                         [UIFont boldSystemFontOfSize:28.0],
                                                     NSForegroundColorAttributeName:
                                                         [UIColor whiteColor]}];
    [over drawAtPoint:CGPointMake(cx - over.size.width / 2.0, cy - over.size.height)];

    NSAttributedString *again =
        [[NSAttributedString alloc] initWithString:@"tap to play again"
                                        attributes:@{NSFontAttributeName:
                                                         [UIFont systemFontOfSize:13.0],
                                                     NSForegroundColorAttributeName:
                                                         [UIColor colorWithWhite:1.0f alpha:0.7f]}];
    [again drawAtPoint:CGPointMake(cx - again.size.width / 2.0, cy + 14.0)];
}

/* ------------------------------------------------------------------- input */

- (void)touchesBegan:(NSSet *)touches withEvent:(UIEvent *)event
{
    UITouch *touch = [touches anyObject];
    CGPoint point;

    if (touch == nil) {
        return;
    }
    point = [touch locationInView:self];

    /* The pause button takes precedence over everything else. While the menus
     * are up the overlay claims the whole screen, so this only ever fires during
     * play -- the overlay steps aside for the button's rect (see
     * -hitTest:withEvent: in VOBGOverlay.m). */
    if (!CGRectIsEmpty(_pauseButtonRect) && CGRectContainsPoint(_pauseButtonRect, point)) {
        _pressedPause = YES;
        [self setNeedsDisplay];
        return;
    }

    /* Each new touch re-anchors the drag, so a second gesture never inherits the
     * previous one's offset. */
    _dragAnchored = NO;
    _snapping = NO;
    [_swipe beginTouches:touches atPoint:point time:[event timestamp]];
}

- (void)touchesMoved:(NSSet *)touches withEvent:(UIEvent *)event
{
    UITouch *touch = [touches anyObject];
    if (touch == nil) {
        return;
    }
    [_swipe moveTouches:touches
               atPoint:[touch locationInView:self]
                   time:[event timestamp]];
}

- (void)touchesEnded:(NSSet *)touches withEvent:(UIEvent *)event
{
    UITouch *touch = [touches anyObject];
    if (touch == nil) {
        return;
    }
    if (_pressedPause) {
        _pressedPause = NO;
        [self togglePause];
        return;
    }
    [_swipe endTouches:touches
              atPoint:[touch locationInView:self]
                  time:[event timestamp]];
}

- (void)touchesCancelled:(NSSet *)touches withEvent:(UIEvent *)event
{
    _pressedPause = NO;
    [_swipe cancelTouches];
}

- (void)togglePause
{
    self.paused = !_paused;
    if (_paused) {
        [_overlay showPause];
    } else {
        /* Back to play. */
        [self applyControlMode];
    }
}

#pragma mark - VOBGSwipeDelegate

- (void)handleTap
{
    if (_paused) {
        return;
    }
    /* A tap on the game-over screen restarts; otherwise it rotates. */
    if (_game.state == VOBG_STATE_GAMEOVER) {
        [self restart];
        return;
    }
    vobg_rotate(&_game);
    [self setNeedsDisplay];
}

/* Show the d-pad during play when that is the chosen control scheme, otherwise
 * take the overlay out of the way entirely. */
- (void)applyControlMode
{
    if ([VOBGSettings sharedSettings].dpadControls) {
        [_overlay showDpadForPlay];
    } else {
        [_overlay hideAll];
    }
}

/* Play input is accepted only while a game is actually running. The D-pad keys
 * hold soft drop, so a topped-out or paused game has to reject new input without
 * swallowing the release -- see -overlayDidEndSoftDrop:. */
- (BOOL)acceptsPlayInput
{
    return !_paused && _game.state != VOBG_STATE_GAMEOVER;
}

/* Swipe input is ignored while paused, while a d-pad key is held (the two
 * control schemes must not fight over the same touch), and once the game is
 * over -- a tap there restarts instead, which -handleTap deals with. */
- (BOOL)swipesEnabled
{
    return !_paused && ![_overlay isDPadActive];
}

- (void)handleDragBy:(double)dx
{
    if (_cell <= 0.0 || ![self swipesEnabled]) {
        return; /* layout has not run yet, or the d-pad has the touch */
    }
    if (!_dragAnchored) {
        /* First movement of this gesture. The finger is already past the
         * threshold, so anchoring here is what stops the piece jumping sideways
         * the instant a swipe turns into a drag. */
        _dragAnchored = YES;
        _dragAnchorColumn = _game.xf;
    }
    _snapping = NO;
    vobg_drag_to(&_game, _dragAnchorColumn + (float)(dx / (double)_cell));
    [self setNeedsDisplay];
}

- (void)handleDragEnd
{
    if (_paused) {
        return;
    }
    /* Easing onto the grid rather than teleporting: the snap is at most half a
     * cell, so a short tween reads as the piece settling instead of glitching. */
    _snapFrom = _game.xf;
    _snapTo = vobg_snap_target(&_game);
    if (fabsf(_snapTo - _snapFrom) < 0.001f) {
        vobg_snap(&_game);
        _snapping = NO;
    } else {
        _snapping = YES;
        _snapElapsed = 0.0;
    }
}

/* Put the piece back on the grid immediately, with no tween.
 *
 * A vertical gesture can commit after a sideways preview has already left the
 * piece between columns -- a short sideways nudge followed by a flick up. The
 * drop itself is fine, because vobg_col rounds the position down, but without
 * this the piece would spend the rest of its life drawn half a cell off. */
- (void)snapPieceNow
{
    _snapping = NO;
    vobg_snap(&_game);
}

- (void)handleSoftBegin
{
    if (![self acceptsPlayInput]) {
        return;
    }
    [self snapPieceNow];
    vobg_set_soft_drop(&_game, 1);
}

- (void)handleSoftEnd
{
    vobg_set_soft_drop(&_game, 0);
}

- (void)handleHardDrop
{
    if (![self acceptsPlayInput]) {
        return;
    }
    [self snapPieceNow];
    vobg_hard_drop(&_game);
    [self setNeedsDisplay];
}

/* Advance the snap tween. Called once per frame from -step:. Returns whether it
 * drew, so the caller can fold it into the single redraw decision. */
- (BOOL)advanceSnap:(NSTimeInterval)dt
{
    const NSTimeInterval duration = 0.07;
    double t;

    if (!_snapping) {
        return NO;
    }
    _snapElapsed += dt;
    t = _snapElapsed / duration;
    if (t >= 1.0) {
        _game.xf = _snapTo;
        _snapping = NO;
        return YES;
    }
    /* Ease-out: fast off the mark, settling into the grid. */
    t = 1.0 - (1.0 - t) * (1.0 - t);
    _game.xf = (float)(_snapFrom + (_snapTo - _snapFrom) * t);
    return YES;
}

/* ------------------------------------------------------- VOBGOverlayDelegate */

- (void)overlayDidRequestResume:(VOBGOverlay *)overlay
{
    (void)overlay;
    self.paused = NO;
    [self applyControlMode];
}

- (void)overlayDidRequestRestart:(VOBGOverlay *)overlay
{
    (void)overlay;
    [self restart];
}

- (void)overlayDidRequestOptions:(VOBGOverlay *)overlay
{
    (void)overlay;
    [_overlay showOptions];
}

- (void)overlayDidRequestCloseOptions:(VOBGOverlay *)overlay
{
    (void)overlay;
    /* Options is only reachable from the pause menu, so Back returns there. */
    [_overlay showPause];
}

- (void)overlayDidToggleGlossy:(VOBGOverlay *)overlay
{
    (void)overlay;
    [[VOBGSettings sharedSettings] toggleGlossyBlocks];
    /* Settings change handler does the rest; keep the overlay's copy in sync in
     * case the notification is ever posted from elsewhere. */
    _overlay.glossyBlocks = [VOBGSettings sharedSettings].glossyBlocks;
    [_overlay showOptions];
}

- (void)overlayDidToggleDpad:(VOBGOverlay *)overlay
{
    (void)overlay;
    [[VOBGSettings sharedSettings] toggleDpadControls];
    _overlay.dpadEnabled = [VOBGSettings sharedSettings].dpadControls;
    [_overlay showOptions];
}

/* D-pad input. Each of these arrives on touch-down rather than on release, and
 * left/right keep arriving while held, so they respect the same "game must be
 * running" rule the swipe handlers use. */
- (void)overlay:(VOBGOverlay *)overlay didPressDPad:(int)dir
{
    (void)overlay;
    if (dir == 0 || ![self acceptsPlayInput]) {
        return;
    }
    vobg_move(&_game, dir);
    [self setNeedsDisplay];
}

- (void)overlayDidRotate:(VOBGOverlay *)overlay
{
    (void)overlay;
    if (_paused) {
        return;
    }
    if (_game.state == VOBG_STATE_GAMEOVER) {
        [self restart];
        return;
    }
    vobg_rotate(&_game);
    [self setNeedsDisplay];
}

- (void)overlayDidHardDrop:(VOBGOverlay *)overlay
{
    (void)overlay;
    if (![self acceptsPlayInput]) {
        return;
    }
    vobg_hard_drop(&_game);
    [self setNeedsDisplay];
}

- (void)overlayDidBeginSoftDrop:(VOBGOverlay *)overlay
{
    (void)overlay;
    if (![self acceptsPlayInput]) {
        return;
    }
    vobg_set_soft_drop(&_game, 1);
}

- (void)overlayDidEndSoftDrop:(VOBGOverlay *)overlay
{
    (void)overlay;
    /* Deliberately not gated on acceptsPlayInput: the game may have been paused
     * or topped out while the key was down, and leaving soft drop engaged would
     * make the next piece fall at 20x with no key held. */
    vobg_set_soft_drop(&_game, 0);
}

/* The block style changed: the cached art is keyed on style, so throw it away
 * and redraw. */
- (void)handleSettingsChanged
{
    _overlay.glossyBlocks = [VOBGSettings sharedSettings].glossyBlocks;
    _overlay.dpadEnabled = [VOBGSettings sharedSettings].dpadControls;
    [[VOBGArt sharedArt] invalidate];
    if (!_paused) {
        [self applyControlMode];
    }
    [self setNeedsDisplay];
}

/* The protocol methods below just forward to the handlers above. */
- (void)swipeDidTap:(VOBGSwipe *)swipe { (void)swipe; [self handleTap]; }
- (void)swipe:(VOBGSwipe *)swipe didDragBy:(double)dx { (void)swipe; [self handleDragBy:dx]; }
- (void)swipe:(VOBGSwipe *)swipe didEndDrag:(VOBGSwipe *)unused { (void)swipe; (void)unused; [self handleDragEnd]; }
- (void)swipeDidBeginSoftDrop:(VOBGSwipe *)swipe { (void)swipe; [self handleSoftBegin]; }
- (void)swipeDidEndSoftDrop:(VOBGSwipe *)swipe { (void)swipe; [self handleSoftEnd]; }
- (void)swipeDidHardDrop:(VOBGSwipe *)swipe { (void)swipe; [self handleHardDrop]; }

@end
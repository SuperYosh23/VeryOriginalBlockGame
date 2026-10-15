/*
 * VOBGOverlay.m -- see VOBGOverlay.h.
 *
 * Colour handling goes through VOBGSetFill / VOBGSetStroke rather than the plain
 * CGContext setters, because those silently ignore a gray CGColor. That trap is
 * documented in VOBGGameView.m; it is why this file has its own colour helpers
 * instead of just using UIColor setFill.
 */

#import "VOBGOverlay.h"

typedef NS_ENUM(NSUInteger, VOBGOverlayMode) {
    VOBGOverlayModeHidden = 0,
    VOBGOverlayModePause,
    VOBGOverlayModeOptions,
    VOBGOverlayModePlay  /* only the d-pad is visible */
};

/* Buttons. Kept as ints rather than an enum type so the widget array can hold
 * them without dragging the enum into every signature. */
enum {
    kBtnResume = 1,
    kBtnRestart,
    kBtnOptions,
    kBtnCloseOptions,
    kBtnGlossy,
    kBtnDpad,
    kBtnDPadLeft,
    kBtnDPadRight,
    kBtnDPadDown,
    kBtnDPadUp,
    kBtnDPadRotate
};

/* One tappable thing. Geometry and drawing both read from this array, which is
 * the whole point: hit-testing must not depend on a draw having happened yet. */
typedef struct {
    CGRect rect;
    int id;
    BOOL filled;  /* accent-filled menu button, as opposed to outlined */
    BOOL dpad;    /* drawn as a d-pad key with a glyph */
} VOBGWidget;

#define VOBG_MAX_WIDGETS 8

/* Held-key auto-repeat for left/right. Long enough to register as a press,
 * short enough that steering across the board does not feel like a chore. */
#define VOBG_REPEAT_DELAY 0.30
#define VOBG_REPEAT_RATE  0.07

@implementation VOBGOverlay {
    VOBGOverlayMode _mode;
    VOBGWidget _widgets[VOBG_MAX_WIDGETS];
    int _widgetCount;
    int _pressedWidget;   /* widget id held down, or 0 */
    int _activeDPad;      /* d-pad key currently held, or 0 */
    CGRect _plateRect;    /* backing plate behind the directional cross */
    NSTimer *_repeatTimer;
}

/* ------------------------------------------------------------------ colour */

static void VOBGGetRGBA(UIColor *color, CGFloat *rgba)
{
    CGColorRef cg = color.CGColor;
    size_t count;
    const CGFloat *c;

    if (cg == NULL) {
        rgba[0] = rgba[1] = rgba[2] = 0.0;
        rgba[3] = 1.0;
        return;
    }
    count = CGColorGetNumberOfComponents(cg);
    c = CGColorGetComponents(cg);
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
        rgba[0] = c[0];
        rgba[1] = c[0];
        rgba[2] = c[0];
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

static UIColor *VOBGTextColor(void)
{
    return [UIColor colorWithWhite:1.0f alpha:0.92f];
}

static UIColor *VOBGAccentColor(void)
{
    return [UIColor colorWithRed:0.20f green:0.90f blue:0.96f alpha:1.0f];
}

/* Menu button captions. The two option rows embed live state, so the title has
 * to be built at layout time rather than stored. */
static NSString *VOBGTitleForWidget(const VOBGWidget *widget, BOOL glossy, BOOL dpad)
{
    switch (widget->id) {
    case kBtnResume:      return @"Resume";
    case kBtnRestart:     return @"Restart";
    case kBtnOptions:     return @"Options";
    case kBtnCloseOptions:return @"Back";
    case kBtnGlossy:
        return glossy ? @"Blocks: Glossy" : @"Blocks: Flat";
    case kBtnDpad:
        return dpad ? @"Controls: D-pad" : @"Controls: Swipe";
    default:
        return @"";
    }
}

static BOOL VOBGIsDPadWidget(int id)
{
    return id >= kBtnDPadLeft && id <= kBtnDPadRotate;
}

static int VOBGDirForWidget(int id)
{
    if (id == kBtnDPadLeft) {
        return -1;
    }
    if (id == kBtnDPadRight) {
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------- state */

- (id)initWithFrame:(CGRect)frame
{
    self = [super initWithFrame:frame];
    if (self) {
        self.backgroundColor = [UIColor clearColor];
        self.opaque = NO;
        self.multipleTouchEnabled = NO;
        _mode = VOBGOverlayModeHidden;
        _widgetCount = 0;
        _pressedWidget = 0;
        _activeDPad = 0;
        /* Critical. This view covers the whole game view, and UIKit hit-tests
         * subviews purely by geometry: it does not care whether a view is
         * drawing anything. Left interactive while hidden it swallows every
         * touch before the playfield ever sees one, and the game goes
         * completely unresponsive. Hidden mode has to disable interaction, not
         * merely draw nothing. */
        self.userInteractionEnabled = NO;
    }
    return self;
}

- (void)dealloc
{
    [_repeatTimer invalidate];
    _repeatTimer = nil;
}

- (void)showPause
{
    _mode = VOBGOverlayModePause;
    self.userInteractionEnabled = YES;
    [self setNeedsDisplay];
}

- (void)showOptions
{
    _mode = VOBGOverlayModeOptions;
    self.userInteractionEnabled = YES;
    [self setNeedsDisplay];
}

- (void)showDpadForPlay
{
    _mode = VOBGOverlayModePlay;
    self.userInteractionEnabled = YES;
    [self setNeedsDisplay];
}

- (void)hideAll
{
    [self releaseDPad];
    _mode = VOBGOverlayModeHidden;
    self.userInteractionEnabled = NO;
    [self setNeedsDisplay];
}

- (BOOL)isVisible
{
    return _mode != VOBGOverlayModeHidden;
}

- (BOOL)isDPadActive
{
    return _activeDPad != 0;
}

/* ----------------------------------------------------------------- geometry */

/* Recomputes every widget rect for the current bounds and mode. Called from both
 * -drawRect and -hitTest:withEvent:, so it has to be pure: no drawing, no
 * state beyond the array it fills. */
- (void)computeGeometry
{
    CGFloat w = CGRectGetWidth(self.bounds);
    CGFloat h = CGRectGetHeight(self.bounds);
    CGFloat bw = MIN(260.0, w - 40.0);
    CGFloat cx = w / 2.0;
    CGFloat y, top, bh, gap;

    _widgetCount = 0;
    _plateRect = CGRectZero;
    if (_mode == VOBGOverlayModeHidden) {
        return;
    }

    if (_mode == VOBGOverlayModePlay) {
        /* The four directions are one bundled cross in the bottom-left: a key
         * each side of a central hub, with a plate drawn behind all of them so
         * they read as a single object rather than four floating squares. The
         * key size scales with width so the cross still fits a 320pt screen. */
        CGFloat s = MIN(46.0, w / 7.5);
        CGFloat pad = 14.0;
        CGFloat cx = pad + s * 1.5;
        CGFloat cy = h - pad - s * 1.5;
        CGFloat plateInset = 6.0;
        CGFloat rotateD = s * 1.45;
        VOBGWidget defs[5];
        int i;

        defs[0].rect = CGRectMake(cx - s * 0.5, cy - s * 1.5, s, s);  /* up */
        defs[0].id = kBtnDPadUp;     defs[0].filled = NO; defs[0].dpad = YES;
        defs[1].rect = CGRectMake(cx - s * 1.5, cy - s * 0.5, s, s);  /* left */
        defs[1].id = kBtnDPadLeft;   defs[1].filled = NO; defs[1].dpad = YES;
        defs[2].rect = CGRectMake(cx + s * 0.5, cy - s * 0.5, s, s);  /* right */
        defs[2].id = kBtnDPadRight;  defs[2].filled = NO; defs[2].dpad = YES;
        defs[3].rect = CGRectMake(cx - s * 0.5, cy + s * 0.5, s, s);  /* down */
        defs[3].id = kBtnDPadDown;   defs[3].filled = NO; defs[3].dpad = YES;

        /* Rotate sits alone in the opposite corner, so the other thumb reaches
         * it without disturbing the steering thumb. */
        defs[4].rect = CGRectMake(w - pad - rotateD, h - pad - rotateD,
                                  rotateD, rotateD);
        defs[4].id = kBtnDPadRotate; defs[4].filled = NO; defs[4].dpad = YES;

        for (i = 0; i < 5 && _widgetCount < VOBG_MAX_WIDGETS; i++) {
            _widgets[_widgetCount++] = defs[i];
        }

        _plateRect = CGRectInset(CGRectMake(cx - s * 1.5, cy - s * 1.5, s * 3.0, s * 3.0),
                                 -plateInset, -plateInset);
        return;
    }

    top = MAX(20.0, h * 0.5 - 150.0);

    if (_mode == VOBGOverlayModePause) {
        y = top + 58.0;
        bh = 46.0;
        gap = 12.0;
    } else {
        y = top + 54.0;
        bh = 52.0;
        gap = 14.0;
    }

    /* Menu buttons are listed top to bottom; -hitTest:withEvent: walks this
     * array backwards, so later entries win an overlap. */
    {
        VOBGWidget defs[4];
        int n = 0;
        int ids[4];
        BOOL filled[4];
        int i;

        if (_mode == VOBGOverlayModePause) {
            ids[0] = kBtnResume;   filled[0] = YES;
            ids[1] = kBtnRestart;  filled[1] = NO;
            ids[2] = kBtnOptions;  filled[2] = NO;
        } else {
            ids[0] = kBtnGlossy;   filled[0] = NO;
            ids[1] = kBtnDpad;     filled[1] = NO;
            ids[2] = kBtnCloseOptions; filled[2] = YES;
        }

        for (i = 0; i < 3; i++) {
            defs[n].rect = CGRectMake(cx - bw / 2.0, y, bw, bh);
            defs[n].id = ids[i];
            defs[n].filled = filled[i];
            defs[n].dpad = NO;
            n++;
            y += bh + gap;
        }
        /* The Back button is a touch shorter than the option rows above it. */
        if (_mode == VOBGOverlayModeOptions) {
            defs[n - 1].rect = CGRectMake(cx - bw / 2.0, y, bw, 46.0);
        }
        for (i = 0; i < n && _widgetCount < VOBG_MAX_WIDGETS; i++) {
            _widgets[_widgetCount++] = defs[i];
        }
    }
}

/* Recomputes first rather than trusting the array. -drawRect fills it, but a
 * touch can arrive before the first draw or after a bounds change that has not
 * been drawn yet, and a stale rect means a dead button. */
- (int)widgetAtPoint:(CGPoint)point
{
    int i;
    [self computeGeometry];
    for (i = _widgetCount - 1; i >= 0; i--) {
        if (CGRectContainsPoint(_widgets[i].rect, point)) {
            return _widgets[i].id;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ drawing */

- (void)drawText:(NSString *)text
            font:(UIFont *)font
           color:(UIColor *)color
        centered:(CGRect)rect
{
    NSAttributedString *s =
        [[NSAttributedString alloc] initWithString:text
                                        attributes:@{
            NSFontAttributeName: font,
            NSForegroundColorAttributeName: color
        }];
    CGSize size = s.size;
    /* NSString's -drawAtPoint:withAttributes: is iOS 7+; drawing an
     * NSAttributedString directly is the iOS 6 path with the same control. */
    [s drawAtPoint:CGPointMake(CGRectGetMidX(rect) - size.width / 2.0,
                               CGRectGetMidY(rect) - size.height / 2.0)];
}

/* A solid triangle pointing the way the key moves.
 *
 * Drawn as a path rather than typed as a glyph: the arrow characters that read
 * fine on a desktop font render inconsistently across iOS system fonts and
 * point sizes, and on the 320pt-wide devices this ships to they came out visibly
 * mismatched in weight against the neon outlines. A path is exact at any scale. */
static void VOBGDrawArrowInContext(CGContextRef ctx, int id, CGRect rect)
{
    CGFloat cx = CGRectGetMidX(rect);
    CGFloat cy = CGRectGetMidY(rect);
    CGFloat hw = rect.size.width * 0.24;
    CGFloat hh = rect.size.height * 0.26;

    /* UIView coordinates put +y downwards, so "up" is the smaller y. The base of
     * each triangle is laid out first and the apex added last, which keeps the
     * winding consistent and the direction unambiguous. */
    switch (id) {
    case kBtnDPadUp:            /* apex at cy - hh */
        CGContextMoveToPoint(ctx, cx - hw, cy + hh);
        CGContextAddLineToPoint(ctx, cx + hw, cy + hh);
        CGContextAddLineToPoint(ctx, cx, cy - hh);
        break;
    case kBtnDPadDown:          /* apex at cy + hh */
        CGContextMoveToPoint(ctx, cx - hw, cy - hh);
        CGContextAddLineToPoint(ctx, cx + hw, cy - hh);
        CGContextAddLineToPoint(ctx, cx, cy + hh);
        break;
    case kBtnDPadLeft:          /* apex at cx - hw */
        CGContextMoveToPoint(ctx, cx + hw, cy - hh);
        CGContextAddLineToPoint(ctx, cx + hw, cy + hh);
        CGContextAddLineToPoint(ctx, cx - hw, cy);
        break;
    case kBtnDPadRight:         /* apex at cx + hw */
        CGContextMoveToPoint(ctx, cx - hw, cy - hh);
        CGContextAddLineToPoint(ctx, cx - hw, cy + hh);
        CGContextAddLineToPoint(ctx, cx + hw, cy);
        break;
    default:
        return;
    }
    CGContextClosePath(ctx);
    CGContextFillPath(ctx);
}

/* The plate behind the cross. One translucent rounded rectangle under all four
 * keys is what makes them read as one bundled control rather than four separate
 * ones. It carries no outline of its own. */
- (void)drawDPadPlateInContext:(CGContextRef)ctx
{
    UIBezierPath *plate;

    if (CGRectIsEmpty(_plateRect)) {
        return;
    }
    plate = [UIBezierPath bezierPathWithRoundedRect:_plateRect
                                     cornerRadius:_plateRect.size.width * 0.16];
    /* Fill only, no outline. The fill is what groups the four keys into one
     * control; a border around it just adds a second competing edge next to the
     * four key outlines. */
    VOBGSetFill(ctx, [UIColor colorWithWhite:1.0f alpha:0.09f]);
    [plate fill];
}

- (void)drawWidget:(const VOBGWidget *)widget
{
    CGContextRef ctx = UIGraphicsGetCurrentContext();
    CGRect rect = widget->rect;
    BOOL held = (widget->id == _pressedWidget) ||
                (widget->dpad && widget->id == _activeDPad);

    if (widget->dpad) {
        BOOL rotate = (widget->id == kBtnDPadRotate);

        /* Rotate is a true circle; the directional keys are rounded squares,
         * matching the menu buttons. */
        UIBezierPath *key =
            [UIBezierPath bezierPathWithRoundedRect:rect
                                      cornerRadius:rotate ? rect.size.height * 0.5
                                                          : rect.size.height * 0.24];
        VOBGSetFill(ctx, [UIColor colorWithWhite:1.0f
                                           alpha:held ? 0.30f : 0.11f]);
        [key fill];
        VOBGSetStroke(ctx, [UIColor colorWithWhite:1.0f alpha:0.60f]);
        key.lineWidth = 1.5;
        [key stroke];

        VOBGSetFill(ctx, [UIColor colorWithWhite:1.0f alpha:0.90f]);
        if (rotate) {
            /* A plain dot. Rotation is the one action that needs no direction,
             * so it gets no arrow. */
            CGFloat d = rect.size.height * 0.30;
            CGContextFillEllipseInRect(ctx,
                CGRectMake(CGRectGetMidX(rect) - d / 2.0,
                           CGRectGetMidY(rect) - d / 2.0, d, d));
        } else {
            VOBGDrawArrowInContext(ctx, widget->id, rect);
        }
        return;
    }

    UIBezierPath *path =
        [UIBezierPath bezierPathWithRoundedRect:rect cornerRadius:rect.size.height * 0.24];
    if (widget->filled) {
        VOBGSetFill(ctx, held ? VOBGAccentColor()
                              : [UIColor colorWithWhite:1.0f alpha:0.14f]);
        [path fill];
    }
    VOBGSetStroke(ctx, (widget->filled || held) ? VOBGAccentColor()
                                                : [UIColor colorWithWhite:1.0f alpha:0.75f]);
    path.lineWidth = held ? 2.5 : 1.5;
    [path stroke];

    [self drawText:VOBGTitleForWidget(widget, _glossyBlocks, _dpadEnabled)
              font:[UIFont boldSystemFontOfSize:MIN(19.0, rect.size.height * 0.42)]
             color:widget->filled ? [UIColor whiteColor] : VOBGTextColor()
          centered:rect];
}

- (void)drawRect:(CGRect)rect
{
    CGContextRef ctx = UIGraphicsGetCurrentContext();
    int i;

    if (_mode == VOBGOverlayModeHidden) {
        return;
    }

    if (_mode != VOBGOverlayModePlay) {
        /* Dim the playfield behind the menu. */
        VOBGSetFill(ctx, [UIColor colorWithWhite:0.0f alpha:0.78f]);
        CGContextFillRect(ctx, self.bounds);
    }

    [self computeGeometry];

    if (_mode == VOBGOverlayModePlay) {
        [self drawDPadPlateInContext:ctx];
    }

    if (_mode != VOBGOverlayModePlay) {
        CGFloat w = CGRectGetWidth(self.bounds);
        CGFloat h = CGRectGetHeight(self.bounds);
        CGFloat bw = MIN(260.0, w - 40.0);
        CGFloat cx = w / 2.0;
        CGFloat top = MAX(20.0, h * 0.5 - 150.0);
        BOOL pause = (_mode == VOBGOverlayModePause);

        [self drawText:pause ? @"PAUSED" : @"OPTIONS"
                  font:[UIFont boldSystemFontOfSize:pause ? 30.0 : 28.0]
                 color:VOBGTextColor()
              centered:CGRectMake(cx - bw / 2.0, top, bw, 40.0)];
    }

    for (i = 0; i < _widgetCount; i++) {
        [self drawWidget:&_widgets[i]];
    }
}

/* -------------------------------------------------------------- hit-testing */

/* This is the load-bearing method.
 *
 * UIKit routes a touch to the topmost view whose -hitTest:withEvent: returns
 * non-nil. This view spans the whole screen, so while it is showing it claims
 * essentially everything, and returning nil is the only way a touch reaches the
 * playfield underneath.
 *
 * In D-pad mode that is the point: the d-pad is an alternative to swiping, not
 * an addition to them, so the two must not both respond to the same thumb.
 *
 * One exception is -passthroughRect, the pause button. The button is drawn as
 * part of the game view's HUD and handled there, so the overlay steps aside for
 * it. Without this the overlay would sit on top of the very control used to
 * reach the pause menu. */
- (UIView *)hitTest:(CGPoint)point withEvent:(UIEvent *)event
{
    if (![self isVisible] || !self.userInteractionEnabled) {
        return nil;
    }
    if (!CGRectContainsPoint(self.bounds, point)) {
        return nil;
    }
    if (_mode == VOBGOverlayModePlay &&
        !CGRectIsEmpty(_passthroughRect) &&
        CGRectContainsPoint(_passthroughRect, point)) {
        return nil;
    }
    return self;
}

/* ---------------------------------------------------------------- D-pad act */

- (void)pressDPadWidget:(int)widgetId
{
    switch (widgetId) {
    case kBtnDPadLeft:
    case kBtnDPadRight:
        [self.delegate overlay:self didPressDPad:VOBGDirForWidget(widgetId)];
        [self startRepeat];
        break;
    case kBtnDPadDown:
        [self.delegate overlayDidBeginSoftDrop:self];
        break;
    case kBtnDPadUp:
        [self.delegate overlayDidHardDrop:self];
        break;
    case kBtnDPadRotate:
        [self.delegate overlayDidRotate:self];
        break;
    default:
        break;
    }
}

- (void)startRepeat
{
    if (_repeatTimer != nil) {
        return;
    }
    /* The long first pause is the timer's initial delay; later ticks re-arm at
     * the fast rate. Not a repeating timer, so stopping is just an invalidate. */
    _repeatTimer = [NSTimer scheduledTimerWithTimeInterval:VOBG_REPEAT_DELAY
                                                    target:self
                                                  selector:@selector(repeatTick:)
                                                  userInfo:nil
                                                   repeats:NO];
}

- (void)repeatTick:(NSTimer *)timer
{
    int dir;

    (void)timer;
    dir = VOBGDirForWidget(_activeDPad);
    if (dir == 0) {
        [self stopRepeat];
        return;
    }
    [self.delegate overlay:self didPressDPad:dir];
    _repeatTimer = [NSTimer scheduledTimerWithTimeInterval:VOBG_REPEAT_RATE
                                                    target:self
                                                  selector:@selector(repeatTick:)
                                                  userInfo:nil
                                                   repeats:NO];
}

- (void)stopRepeat
{
    [_repeatTimer invalidate];
    _repeatTimer = nil;
}

/* Lets go of whatever D-pad key is down, telling the game to stop soft dropping
 * if that is what was held. Safe to call when nothing is held. */
- (void)releaseDPad
{
    if (_activeDPad == kBtnDPadDown) {
        [self.delegate overlayDidEndSoftDrop:self];
    }
    _activeDPad = 0;
    [self stopRepeat];
}

- (void)touchesBegan:(NSSet *)touches withEvent:(UIEvent *)event
{
    UITouch *touch = [touches anyObject];
    int widgetId;

    if (touch == nil || _mode == VOBGOverlayModeHidden) {
        return;
    }
    widgetId = [self widgetAtPoint:[touch locationInView:self]];

    if (VOBGIsDPadWidget(widgetId)) {
        /* D-pad: act on touch-down, and hold for soft drop / auto-repeat. */
        _activeDPad = widgetId;
        [self pressDPadWidget:widgetId];
    } else {
        _pressedWidget = widgetId;
    }
    [self setNeedsDisplay];
}

- (void)touchesMoved:(NSSet *)touches withEvent:(UIEvent *)event
{
    UITouch *touch = [touches anyObject];
    int widgetId;

    if (touch == nil || _mode == VOBGOverlayModeHidden) {
        return;
    }
    widgetId = [self widgetAtPoint:[touch locationInView:self]];

    if (_activeDPad != 0) {
        if (widgetId != _activeDPad) {
            /* Slid off the key, or across to another one: let go of the old
             * before picking up the new, so soft drop never sticks on. */
            [self releaseDPad];
            if (VOBGIsDPadWidget(widgetId)) {
                _activeDPad = widgetId;
                [self pressDPadWidget:widgetId];
            }
        }
    } else if (widgetId == 0) {
        /* Dragged off a menu button: cancel it rather than firing on release. */
        _pressedWidget = 0;
    }
    [self setNeedsDisplay];
}

- (void)touchesEnded:(NSSet *)touches withEvent:(UIEvent *)event
{
    UITouch *touch = [touches anyObject];
    int widgetId = 0;
    int pressed = _pressedWidget;

    if (touch == nil || _mode == VOBGOverlayModeHidden) {
        [self releaseDPad];
        _pressedWidget = 0;
        return;
    }
    widgetId = [self widgetAtPoint:[touch locationInView:self]];

    [self releaseDPad];

    /* Menu buttons fire on lift, and only if the finger is still on the button
     * it started on. That is what stops a swipe beginning on Resume from also
     * pressing whatever it lands on. */
    if (pressed != 0 && widgetId == pressed) {
        switch (pressed) {
        case kBtnResume:       [self.delegate overlayDidRequestResume:self]; break;
        case kBtnRestart:      [self.delegate overlayDidRequestRestart:self]; break;
        case kBtnOptions:      [self.delegate overlayDidRequestOptions:self]; break;
        case kBtnCloseOptions: [self.delegate overlayDidRequestCloseOptions:self]; break;
        case kBtnGlossy:       [self.delegate overlayDidToggleGlossy:self]; break;
        case kBtnDpad:         [self.delegate overlayDidToggleDpad:self]; break;
        default: break;
        }
    }
    _pressedWidget = 0;
    [self setNeedsDisplay];
}

- (void)touchesCancelled:(NSSet *)touches withEvent:(UIEvent *)event
{
    [self releaseDPad];
    _pressedWidget = 0;
    [self setNeedsDisplay];
}

@end
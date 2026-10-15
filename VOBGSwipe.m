/*
 * VOBGSwipe.m -- see VOBGSwipe.h.
 *
 * Note the timestamps: UIKit gives a *relative* time that resets with the
 * process. Do not pass a wall-clock value here.
 */

#import "VOBGSwipe.h"

@implementation VOBGSwipe {
    VOBGGesture _gesture;
    BOOL _didDrag;
}

- (instancetype)init
{
    self = [super init];
    if (self) {
        vobg_gesture_init(&_gesture);
    }
    return self;
}

- (void)setCellSize:(CGFloat)cell
{
    vobg_gesture_set_cell(&_gesture, (double)cell);
}

- (BOOL)isTrackingTouch
{
    return vobg_gesture_is_tracking(&_gesture) ? YES : NO;
}

/* Fan an action out to the delegate. */
- (void)emit:(VOBGAction)action delta:(double)delta
{
    switch (action) {
    case VOBG_ACTION_TAP:
        [self.delegate swipeDidTap:self];
        break;
    case VOBG_ACTION_DRAG:
        _didDrag = YES;
        [self.delegate swipe:self didDragBy:delta];
        break;
    case VOBG_ACTION_SOFT_BEGIN:
        [self.delegate swipeDidBeginSoftDrop:self];
        break;
    case VOBG_ACTION_SOFT_END:
        [self.delegate swipeDidEndSoftDrop:self];
        break;
    case VOBG_ACTION_HARD_DROP:
        [self.delegate swipeDidHardDrop:self];
        break;
    default:
        break;
    }
}

- (void)beginTouches:(NSSet *)touches atPoint:(CGPoint)point time:(NSTimeInterval)time
{
    _didDrag = NO;
    [self emit:vobg_gesture_begin(&_gesture, point.x, point.y, time) delta:0.0];
}

- (void)moveTouches:(NSSet *)touches atPoint:(CGPoint)point time:(NSTimeInterval)time
{
    double delta = 0.0;
    VOBGAction action = vobg_gesture_move(&_gesture, point.x, point.y, time, &delta);
    [self emit:action delta:delta];
}

- (void)endTouches:(NSSet *)touches atPoint:(CGPoint)point time:(NSTimeInterval)time
{
    VOBGAction action = vobg_gesture_end(&_gesture, point.x, point.y, time);
    BOOL wasDrag = _didDrag;
    _didDrag = NO;
    [self emit:action delta:0.0];
    if (wasDrag) {
        [self.delegate swipe:self didEndDrag:self];
    }
}

- (void)cancelTouches
{
    BOOL wasDrag = _didDrag;
    _didDrag = NO;
    [self emit:vobg_gesture_cancel(&_gesture) delta:0.0];
    if (wasDrag) {
        [self.delegate swipe:self didEndDrag:self];
    }
}

@end
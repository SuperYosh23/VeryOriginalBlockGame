/*
 * VOBGSwipe.h -- UIKit adapter for the swipe state machine.
 *
 * Owns the touch lifecycle and forwards what the state machine reports to a
 * delegate. The decisions themselves live in VOBGGesture.c, which is plain C and
 * covered by tests/run_tests.sh; this file is only plumbing.
 */

#ifndef VOBG_SWIPE_H
#define VOBG_SWIPE_H

#import <UIKit/UIKit.h>

#include "VOBGGesture.h"

@class VOBGSwipe;

@protocol VOBGSwipeDelegate <NSObject>
- (void)swipeDidTap:(VOBGSwipe *)swipe;            /* rotate */
- (void)swipe:(VOBGSwipe *)swipe didDragBy:(double)dx; /* finger offset, points */
- (void)swipe:(VOBGSwipe *)swipe didEndDrag:(VOBGSwipe *)unused; /* snap now */
- (void)swipeDidBeginSoftDrop:(VOBGSwipe *)swipe;
- (void)swipeDidEndSoftDrop:(VOBGSwipe *)swipe;
- (void)swipeDidHardDrop:(VOBGSwipe *)swipe;       /* insta-drop */
@end

@interface VOBGSwipe : NSObject

/* Tell the recogniser how big a playfield cell is, so the tap slop and the axis
 * lock are measured in cells rather than in fixed points. Called from
 * -layoutSubviews; see vobg_gesture_set_cell. */
- (void)setCellSize:(CGFloat)cell;

@property (nonatomic, weak) id<VOBGSwipeDelegate> delegate;

- (instancetype)init;

- (void)beginTouches:(NSSet *)touches atPoint:(CGPoint)point time:(NSTimeInterval)time;
- (void)moveTouches:(NSSet *)touches atPoint:(CGPoint)point time:(NSTimeInterval)time;
- (void)endTouches:(NSSet *)touches atPoint:(CGPoint)point time:(NSTimeInterval)time;
- (void)cancelTouches;

- (BOOL)isTrackingTouch;

@end

#endif /* VOBG_SWIPE_H */
/*
 * VOBGOverlay.h -- pause menu, options, and the D-pad.
 *
 * A plain UIView drawn in -drawRect with hit-testing done by hand, rather than a
 * stack of UIButtons. Two reasons: the neon look has to match the playfield
 * exactly (a UIButton draws system chrome that would not), and hit-testing by
 * rect is simpler than managing a control hierarchy for a menu that changes
 * shape on rotation.
 *
 * Touch routing is the subtle part, and it is why this is a UIView subclass
 * overriding -hitTest:withEvent: rather than just implementing the touch
 * methods. See the note above -hitTest:withEvent: in the .m.
 */

#ifndef VOBG_OVERLAY_H
#define VOBG_OVERLAY_H

#import <UIKit/UIKit.h>

@class VOBGOverlay;

@protocol VOBGOverlayDelegate <NSObject>
- (void)overlayDidRequestResume:(VOBGOverlay *)overlay;
- (void)overlayDidRequestRestart:(VOBGOverlay *)overlay;
- (void)overlayDidRequestOptions:(VOBGOverlay *)overlay;
- (void)overlayDidRequestCloseOptions:(VOBGOverlay *)overlay;
- (void)overlayDidToggleGlossy:(VOBGOverlay *)overlay;
- (void)overlayDidToggleDpad:(VOBGOverlay *)overlay;

/* D-pad. Pressed on touch-down rather than on release: a button that only
 * acts when you lift your thumb feels broken. */
- (void)overlay:(VOBGOverlay *)overlay didPressDPad:(int)dir;   /* dir: -1 / +1 */
- (void)overlayDidRotate:(VOBGOverlay *)overlay;
- (void)overlayDidHardDrop:(VOBGOverlay *)overlay;
- (void)overlayDidBeginSoftDrop:(VOBGOverlay *)overlay;
- (void)overlayDidEndSoftDrop:(VOBGOverlay *)overlay;
@end

@interface VOBGOverlay : UIView

@property (nonatomic, weak) id<VOBGOverlayDelegate> delegate;

/* Which screen is showing. */
- (void)showPause;
- (void)showOptions;
- (void)showDpadForPlay;
- (void)hideAll;
- (BOOL)isVisible;

/* Copied from VOBGSettings so the option rows can render their current value. */
@property (nonatomic, assign) BOOL glossyBlocks;
@property (nonatomic, assign) BOOL dpadEnabled;

/* A rect the overlay declines to claim, letting the touch fall through to the
 * view beneath. The pause button lives there: the overlay is drawn and hit
 * tested above the whole screen, but the button is drawn as part of the game
 * view's HUD and handled there. Only consulted while the d-pad is showing. */
@property (nonatomic, assign) CGRect passthroughRect;

/* True while a D-pad key is held, so the game view can suppress swipes and
 * knows not to also treat the same touch as a gesture. */
- (BOOL)isDPadActive;

@end

#endif /* VOBG_OVERLAY_H */
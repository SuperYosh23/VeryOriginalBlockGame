/*
 * VOBGSettings.h -- user preferences.
 *
 * Two settings so far, both persisted in NSUserDefaults:
 *
 *   glossyBlocks  drawn as lit slabs with a gradient and a bevel (default), or
 *                 flat squares. Affects only how blocks look.
 *   dpadControls  on-screen buttons instead of swipes. Affects input only.
 *
 * Changes are announced so the view can rebuild cached art and move its touch
 * handling; nothing polls.
 */

#ifndef VOBG_SETTINGS_H
#define VOBG_SETTINGS_H

#import <Foundation/Foundation.h>

extern NSString *const VOBGSettingsChangedNotification;

@interface VOBGSettings : NSObject

+ (VOBGSettings *)sharedSettings;

/* glossyBlocks defaults to YES, dpadControls to NO (swipe is the default
 * control scheme and the d-pad is opt-in). */
@property (nonatomic, assign) BOOL glossyBlocks;
@property (nonatomic, assign) BOOL dpadControls;

/* Flip a setting, persist it, and post the change notification. */
- (void)toggleGlossyBlocks;
- (void)toggleDpadControls;

@end

#endif /* VOBG_SETTINGS_H */
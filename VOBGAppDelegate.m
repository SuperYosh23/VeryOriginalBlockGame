#import "VOBGAppDelegate.h"
#import "VOBGGameView.h"

/* See the matching note in VOBGGameView.m: 0 now that the app runs full screen,
 * so the window background is black again. */
#ifndef VOBG_DIAGNOSTIC_BG
#define VOBG_DIAGNOSTIC_BG 0
#endif

/*
 * Plain window + root view controller. No storyboard and no Auto Layout: both
 * would need a minimum iOS version newer than 6, and the whole layout is a
 * handful of rects computed in -layoutSubviews anyway.
 */
@implementation VOBGAppDelegate

- (BOOL)application:(UIApplication *)application
        didFinishLaunchingWithOptions:(NSDictionary *)launchOptions
{
    self.window = [[UIWindow alloc] initWithFrame:[[UIScreen mainScreen] bounds]];
#if VOBG_DIAGNOSTIC_BG
    /* Diagnostic only: a white window background makes any letterbox bars left
     * by iOS visible against it. Matches the game view's diagnostic fill. */
    self.window.backgroundColor = [UIColor whiteColor];
#else
    self.window.backgroundColor = [UIColor blackColor];
#endif

    VOBGGameView *game = [[VOBGGameView alloc] initWithFrame:self.window.bounds];
    self.window.rootViewController = [[UIViewController alloc] init];
    self.window.rootViewController.view = game;

    /* Full screen: every pixel belongs to the well on a 320pt display. */
    [application setStatusBarHidden:YES withAnimation:UIStatusBarAnimationNone];
    application.idleTimerDisabled = YES; /* no sleeping mid-game */

    [self.window makeKeyAndVisible];
    return YES;
}

- (void)applicationDidEnterBackground:(UIApplication *)application
{
    /* The view's own display link stops on the background notification. */
}

@end
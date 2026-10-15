/*
 * VOBGSettings.m -- see VOBGSettings.h.
 *
 * Defaults are applied on first read rather than being written out, so a fresh
 * install and an existing one behave the same without needing a migration.
 */

#import "VOBGSettings.h"

NSString *const VOBGSettingsChangedNotification = @"VOBGSettingsChanged";

static NSString *const kGlossyBlocksKey = @"VOBGGlossyBlocks";
static NSString *const kDpadControlsKey = @"VOBGDpadControls";

@implementation VOBGSettings {
    BOOL _glossyBlocks;
    BOOL _dpadControls;
}

+ (VOBGSettings *)sharedSettings
{
    static VOBGSettings *shared = nil;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        shared = [[VOBGSettings alloc] init];
    });
    return shared;
}

- (id)init
{
    self = [super init];
    if (self) {
        NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
        /* registerDefaults supplies the fallback without persisting it, so a
         * user who has never touched a setting still gets these. Swipe is the
         * default control scheme; the d-pad is opt-in from the options menu. */
        [defaults registerDefaults:@{
            kGlossyBlocksKey: @YES,
            kDpadControlsKey: @NO
        }];
        _glossyBlocks = [defaults boolForKey:kGlossyBlocksKey];
        _dpadControls = [defaults boolForKey:kDpadControlsKey];
    }
    return self;
}

- (BOOL)glossyBlocks
{
    return _glossyBlocks;
}

- (void)setGlossyBlocks:(BOOL)value
{
    if (_glossyBlocks == value) {
        return;
    }
    _glossyBlocks = value;
    [[NSUserDefaults standardUserDefaults] setBool:value forKey:kGlossyBlocksKey];
    [self postChange];
}

- (BOOL)dpadControls
{
    return _dpadControls;
}

- (void)setDpadControls:(BOOL)value
{
    if (_dpadControls == value) {
        return;
    }
    _dpadControls = value;
    [[NSUserDefaults standardUserDefaults] setBool:value forKey:kDpadControlsKey];
    [self postChange];
}

- (void)toggleGlossyBlocks
{
    self.glossyBlocks = !_glossyBlocks;
}

- (void)toggleDpadControls
{
    self.dpadControls = !_dpadControls;
}

- (void)postChange
{
    [[NSNotificationCenter defaultCenter]
        postNotificationName:VOBGSettingsChangedNotification
                      object:self];
}

@end
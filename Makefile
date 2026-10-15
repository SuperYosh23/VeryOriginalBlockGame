# Build settings for VeryOriginalBlockGame.
#
# The armv7 slice is the important one: it is what runs on iOS 6-10 hardware,
# so it is pinned to the iOS 9.3 SDK. Newer SDKs auto-link frameworks and symbols
# that do not exist on iOS 6, and the app would fail to launch before main()
# rather than fail to build. The arm64 slice rides the iOS 11.2 SDK so the same
# binary also runs on a current device.
TARGET = iphone:clang
ARCHS = armv7 arm64
SDKVERSION_armv7 = 9.3
SDKVERSION_arm64 = 11.2
TARGET_OS_DEPLOYMENT_VERSION_armv7 = 6.0
TARGET_OS_DEPLOYMENT_VERSION_arm64 = 11.0

include $(THEOS)/makefiles/common.mk

APPLICATION_NAME = VeryOriginalBlockGame

VeryOriginalBlockGame_FILES = main.m \
	VOBGAppDelegate.m \
	VOBGGameView.m \
	VOBGOverlay.m \
	VOBGSettings.m \
	VOBGSwipe.m \
	VOBGArt.m \
	VOBGTetris.c \
	VOBGGesture.c

VeryOriginalBlockGame_RESOURCE_DIRS = Resources
VeryOriginalBlockGame_FRAMEWORKS = UIKit Foundation CoreGraphics QuartzCore
VeryOriginalBlockGame_CFLAGS = -fobjc-arc -Wno-deprecated-declarations
VeryOriginalBlockGame_LDFLAGS = -Wl,-U,_memcpy -Wl,-U,__Unwind_Resume

include $(THEOS_MAKE_PATH)/application.mk
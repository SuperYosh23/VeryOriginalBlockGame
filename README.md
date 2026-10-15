# VeryOriginalBlockGame

A falling-blocks game for iOS 6 and later. Appears on the home screen as
**VOBG**.

Swipe to move, tap to rotate. Swipe controls are the default; an on-screen D-pad
is available in the options menu.

| Gesture | Action |
|---|---|
| Drag left / right | the piece follows your finger between columns, snapping to the grid when you let go |
| Swipe down | soft drop, hold to accelerate |
| Swipe up | insta-drop, lands the piece immediately |
| Tap | rotate clockwise |

### On screen

- **Pause** button in the top right of the HUD. Pausing stops the clock
  outright — gravity, lock delay and the line-clear animation all halt — rather
  than just drawing a menu over a game that keeps running.
- **Pause menu** — Resume, Restart, Options.
- **Options** — block style (glossy or flat) and control scheme (swipe or
  D-pad). Both persist in `NSUserDefaults`.
- **D-pad** — the four directions bundled into a single cross in the bottom
  left, with rotate as a separate dot in the opposite corner for the other
  thumb. Directions auto-repeat while held.

## Building

Needs [Theos](https://theos.dev) (`THEOS` set), and one of the iOS SDKs already
present in `~/theos/sdks`.

```sh
make package                 # fat armv7 + arm64 .deb
./make_legacy_ipa.py         # armv7-only .ipa for old devices
```

Both slices build from the same sources:

- **armv7**, iOS 9.3 SDK, deployment target **6.0** — the slice that runs on
  iOS 6-10 hardware.
- **arm64**, iOS 11.2 SDK, deployment target 11.0 — so the same binary also
  runs on a current device.

The split is deliberate. Newer SDKs auto-link frameworks and symbols that do
not exist on iOS 6, so pointing the old slice at a new SDK produces a binary
that launches fine on a modern phone and crashes immediately on an old one.

### Deploying to a device

```sh
SSHPASS=<password> tools/deploy.sh [host ...]
```

Installs the built `.deb` over SSH, pseudo-signs it with `ldid`, refreshes the
icon cache, and verifies the on-device binary's md5 against the local build.
It does not respring — `uicache` is enough for an existing bundle identifier,
and respringing would close everything else you have open.

The toolchain has no `ibtool`, so `Resources/LaunchScreen.storyboardc` is
committed pre-compiled and reused as-is. It is what gives the app the full
screen on 16:9 hardware; `UILaunchImages` alone does not, because iOS 6
letterboxes anything that does not declare a launch storyboard.

## Testing

```sh
./tests/run_tests.sh         # rules, gestures, and a scripted game
./ios6_compat_check.py --binary
```

`run_tests.sh` compiles the pure-C layers with the host compiler and runs them
under AddressSanitizer and UBSan. No simulator, device or Mac required.

| Suite | Checks | Covers |
|---|---|---|
| `test_tetris.c` | 101 | 7-bag randomiser, gravity, soft drop, movement, rotation kicks, hard drop, line clears and scoring, lock delay, game over |
| `test_gesture.c` | 79 | tap-vs-swipe disambiguation, the preview band, cell-relative thresholds, direction latching, multi-touch |
| `test_play.c` | 30 | the controls driving a real game: a bot plays to game over using only gestures |
| `test_layout.c` | 129 | playfield and preview geometry across orientations, including 16:9 and degenerate sizes |
| `test_drag.c` | 47 | drag offset, clamping and snapping |
| `test_layout_order.c` | 13 | layout pass ordering, guarding the zero-cell regression |

`ios6_compat_check.py` is a build gate for the legacy target. It scans the
sources for iOS 7+ API and the armv7 slice for symbols missing on iOS 6, and
fails the build if it finds any. Both checks caught real crashes during
development: `-drawAtPoint:withAttributes:` (iOS 7) and
`CADisplayLink.preferredFramesPerSecond` (iOS 10) both compile cleanly against
the 9.3 SDK and both would have killed the app on launch.

## Layout

```
VOBGTetris.c/.h    game rules. Plain C, no Foundation or UIKit.
VOBGGesture.c/.h   swipe state machine. Plain C.
VOBGSwipe.m/.h     UIKit adapter over the gesture state machine.
VOBGOverlay.m/.h   pause menu, options, and the D-pad.
VOBGSettings.m/.h  persisted preferences.
VOBGArt.m/.h       colours and cached block rendering.
VOBGGameView.m     playfield: layout, drawing, input, game loop.
VOBGAppDelegate.m  window and root view controller.
```

The rules and the gesture recogniser are deliberately plain C. Both are the
parts most likely to be subtly wrong and neither needs a screen to test, so
keeping them free of UIKit means the host test suite can exercise them
directly.

## Notes on the implementation

**Controls.** A touch starts out undecided. There are two thresholds, not one,
because they answer different questions: `tap_slop` decides whether a touch is
still a tap, and `move_threshold` decides when the *axis* locks and a vertical
swipe commits to soft or hard drop. Sideways movement between them previews the
drag without locking the axis, so an upward swipe that begins with a little
sideways drift still insta-drops instead of being hijacked into a sideways drag.
A tap is a touch that never left the slop at any point, matching
`UITapGestureRecognizer`, so wandering out and back does not rotate a piece that
has already been dragged.

**Both thresholds are derived from the cell size**, not fixed in points. A cell
is 21pt on a 3GS in portrait, 23pt on a 6, and only 14pt in landscape, so a
fixed slop is a different fraction of the board on every screen — which is what
made small movements feel dead on the small hardware while feeling twitchy on
the large.

**There is deliberately no auto-repeat on a horizontal drag.** The piece is
under direct manipulation: it goes where your finger goes and stays there.
Repeating would fight both the finger and the snap.

**Rendering.** Every block is drawn once per (piece, size, style) into an
offscreen bitmap and then blitted. Redrawing gradient-filled blocks for all
~200 cells every frame is too slow for the 32-bit hardware this ships to.
Frames are driven by `CADisplayLink` with `dt` clamped, so returning from a long
background pause does not teleport the piece off the board.

The loop also redraws only when the model actually changed. `vobg_update`
returns whether anything moved, which matters because the alternative — calling
`setNeedsDisplay` every frame — meant a full repaint while the piece sat still
waiting out its lock delay, which is most of a normal game. iOS killed the app
for it: `EXC_RESOURCE` CPU, 61% over 180 seconds.

**Touch routing.** The pause menu and D-pad live in one overlay view spanning
the whole screen. Two details in it are load-bearing: it must disable
`userInteractionEnabled` when hidden, because UIKit hit-tests subviews by
geometry and does not care that a view is drawing nothing — left interactive it
swallows every touch and the game goes completely unresponsive; and while the
D-pad is showing it must claim the whole screen, or both control schemes
respond to the same thumb. The one exception is a passthrough rect for the
pause button, which is drawn and handled by the game view underneath.

**Rules.** 7-bag randomiser, rotation with wall and floor kicks, lock delay,
100/300/500/800 line scoring, and a guideline-style gravity curve. No hold
piece, no T-spins, no combos.

## Disclaimer

The code in this repository was written entirely by an AI coding assistant.

The requirements, feature decisions, design choices and bug reports came from the
maintainer; the implementation, tests and tooling were produced by the assistant
iteratively in response to that direction. No lines were written by hand.

It was developed and tested against real hardware — an iPhone 3GS on iOS 6.1.6
(armv7) and an iPhone 6 on iOS 9.2 (arm64) — but that testing was driven by the
maintainer, who has been the only person to hold the device. Read the code
before trusting it.
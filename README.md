# VeryOriginalBlockGame

A falling-blocks game for iOS 6 and later, controlled entirely with swipes.

| Gesture | Action |
|---|---|
| Swipe left / right | move the piece one column (auto-repeats while held) |
| Swipe down | soft drop, hold to accelerate |
| Swipe up | insta-drop, lands the piece immediately |
| Tap | rotate clockwise |

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

## Testing

```sh
./tests/run_tests.sh         # rules, gestures, and a scripted game
./ios6_compat_check.py --binary
```

`run_tests.sh` compiles the pure-C layers with the host compiler and runs them
under AddressSanitizer and UBSan. No simulator, device or Mac required.

| Suite | Covers |
|---|---|
| `test_tetris.c` | 7-bag randomiser, gravity, soft drop, movement, rotation kicks, hard drop, line clears and scoring, lock delay, game over |
| `test_gesture.c` | tap-vs-swipe disambiguation, swipe directions, flick travel, auto-repeat, hold behaviour, multi-touch |
| `test_play.c` | the controls driving a real game: a bot plays to game over using only gestures |

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
VOBGArt.m/.h       colours and cached block rendering.
VOBGGameView.m     playfield: layout, drawing, input, game loop.
VOBGAppDelegate.m  window and root view controller.
```

The rules and the gesture recogniser are deliberately plain C. Both are the
parts most likely to be subtly wrong and neither needs a screen to test, so
keeping them free of UIKit means the host test suite can exercise them
directly.

## Notes on the implementation

**Controls.** A touch starts out undecided: until it travels past a threshold
it could still be a tap. Committing to a direction is one-way, so a swipe that
drifts back to where it started does not rotate the piece as it lifts. Left and
right auto-repeat after a short delay, and a fast flick crosses as many columns
as the distance warrants rather than being clipped to one. An upward swipe
latches, so keeping the finger moving upward does not re-drop.

**Rendering.** Every block is drawn once per (piece, size) into an offscreen
bitmap and then blitted. Redrawing gradient-filled blocks for all ~200 cells
every frame is too slow for the 32-bit hardware this ships to. Frames are driven
by `CADisplayLink` with `dt` clamped, so returning from a long background pause
does not teleport the piece off the board.

**Rules.** 7-bag randomiser, rotation with wall and floor kicks, lock delay,
100/300/500/800 line scoring, and a guideline-style gravity curve. No hold
piece, no T-spins, no combos.
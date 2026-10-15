#!/usr/bin/env python3
"""Build-time gate for iOS 6 compatibility.

Why this exists
---------------
The app compiles against the iOS 9.3 SDK but ships to devices running iOS 6.
The compiler is perfectly happy with an iOS 7+ API -- the symbol exists in the
SDK and the declaration is visible. Nothing complains at build time. On the
device it shows up as

    -[NSString drawAtPoint:withAttributes:]: unrecognized selector

which on iOS 6 is an immediate crash before the player has seen a frame. There
is no crash log on those devices and no way to reproduce without hardware, so
the only practical place to catch it is here, before the build ships.

Two checks run:

  1. Source scan: every UIKit/Foundation selector the sources use, checked
     against a table of API that postdates iOS 6.
  2. Binary scan: the armv7 slice's undefined symbols, checked against a deny
     list of symbols that do not exist on iOS 6. The arm64 slice is not checked:
     it targets iOS 11 and cannot run on anything older.

The source scan is the important one. It caught -drawAtPoint:withAttributes:
(iOS 7) and CADisplayLink.preferredFramesPerSecond (iOS 10) during development,
both of which compiled cleanly and both of which would have crashed on the
target hardware.

Usage:
    ./ios6_compat_check.py            # check sources only
    ./ios6_compat_check.py --binary   # also check the built armv7 slice
"""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent

SOURCE_FILES = [
    "main.m",
    "VOBGAppDelegate.m",
    "VOBGGameView.m",
    "VOBGSwipe.m",
    "VOBGArt.m",
]
SOURCE_HEADERS = ["VOBGTetris.h", "VOBGGesture.h", "VOBGSwipe.h", "VOBGArt.h"]

THEOS = Path("/home/superyosh23/theos")
TOOLCHAIN = THEOS / "toolchain/linux/iphone/bin"
LIPO = TOOLCHAIN / "lipo"
NM = TOOLCHAIN / "nm"

# Selector spellings that do not exist on iOS 6, and when they appeared.
# Matched as "methodName:" or a bare property/method name. Keep this list
# specific: a loose pattern would false-positive on our own identifiers and
# would train us to ignore the check.
IOS7_PLUS_SELECTORS = [
    (r"\bdrawAtPoint:.*withAttributes:", "iOS 7; use NSAttributedString drawAtPoint:"),
    (r"\bsizeWithAttributes:", "iOS 7; use NSAttributedString .size"),
    (r"\bdrawInRect:.*withAttributes:", "iOS 7; use NSAttributedString drawInRect:"),
    (r"\bboundingRectWithSize:.*withAttributes:", "iOS 7; use NSAttributedString"),
    (r"\bcontainsString:", "iOS 8; use rangeOfString:"),
    (r"\bcomponentsMatchedByPredicate", "iOS 8"),
    (r"\bbase64EncodedStringWithOptions", "iOS 7"),
    (r"\binitWithBase64EncodedString", "iOS 7"),
    (r"\bNSURLSession\b", "iOS 7; use NSURLConnection"),
    (r"\barcs4random\b", "not a symbol; check arcs4random availability"),
    (r"\bpreferredFramesPerSecond\b(?!:)", "iOS 10; guard with respondsToSelector"),
    (r"\bUIAccessibilityIsVoiceOverRunning\b", "iOS 8"),
    (r"\btraitCollection\b", "iOS 8"),
    (r"\bdarkerSystemColors\b", "iOS 8"),
]

# Wrong-in-a-way-that-is-invisible bugs: CoreGraphics colour-space traps.
#
# CGContextSetFillColorWithColor and CGContextSetStrokeColorWithColor only accept
# a CGColor in an RGB colour space. +[UIColor colorWithWhite:alpha:] produces one
# in the generic gray space, and passing it there fails SILENTLY -- the context
# keeps its previous colour and nothing is drawn. This shipped once and made the
# whole well, grid, preview panel and ghost outline invisible on a real device
# while every other test still passed. Banned outright; VOBGGameView has
# VOBGSetFill / VOBGSetStroke helpers that extract components and use the RGB
# setters instead.
SILENT_COLOR_SPACE_BUGS = [
    (r"CGContextSetFillColorWithColor\s*\([^;]*colorWithWhite",
     "CG color trap: colorWithWhite is generic gray and is silently ignored here; use VOBGSetFill"),
    (r"CGContextSetStrokeColorWithColor\s*\([^;]*colorWithWhite",
     "CG color trap: colorWithWhite is generic gray and is silently ignored here; use VOBGSetStroke"),
]

# Undefined symbols that must not appear in the armv7 slice.
IOS6_MISSING_SYMBOLS = {
    "_NSURLAuthenticationMethodServerTrust": "added in iOS 7",
    "_base64EncodedStringWithOptions:": "added in iOS 7",
    "_initWithBase64EncodedString:options:": "added in iOS 7",
    "_SecTrustEvaluateWithError": "added in iOS 12",
}

# Weak refs are an iOS 5 feature, so they are fine. Listed here only to make the
# assumption explicit rather than accidental.
ALLOWED_ON_IOS6 = {
    "_objc_storeWeak",
    "_objc_loadWeakRetained",
    "_objc_destroyWeak",
    "_objc_storeStrong",
    "_objc_retain",
    "_objc_release",
    "_objc_retainAutorelease",
    "_objc_retainAutoreleasedReturnValue",
    "_objc_retainAutoreleaseReturnValue",
    "_objc_autoreleasePoolPush",
    "_objc_autoreleasePoolPop",
    "_objc_autoreleaseReturnValue",
    "_objc_msgSend_stret",
    "_objc_msgSendSuper2",
    "_memcpy_chk",
    "_memset_chk",
    "_stack_chk_guard",
    "_stack_chk_fail",
    "_divsi3",
    "_umodsi3",
    "_pow",
    "_floor",
    "_memset",
    "_arc4random_uniform",
    "_dispatch_once",
}


def strip_comments(line: str, state: dict) -> str:
    """Remove C comments so prose about an API is not scanned as a use of it.

    Needs a small state machine because the file is scanned line by line and a
    block comment can start and end on the same line as code.
    """
    out = []
    i = 0
    n = len(line)
    in_block = state.get("block", False)
    in_string = state.get("string", False)
    while i < n:
        c = line[i]
        if in_block:
            if line.startswith("*/", i):
                in_block = False
                i += 2
                continue
            i += 1
            continue
        if in_string:
            if c == "\\":
                i += 2
                continue
            if c == '"':
                in_string = False
            i += 1
            continue
        if line.startswith("/*", i):
            in_block = True
            i += 2
            continue
        if line.startswith("//", i):
            break
        if c == '"':
            in_string = True
            out.append(" ")
            i += 1
            continue
        out.append(c)
        i += 1
    state["block"] = in_block
    state["string"] = in_string
    return "".join(out)


def check_sources() -> list[str]:
    problems = []
    for name in SOURCE_FILES + SOURCE_HEADERS:
        path = ROOT / name
        if not path.is_file():
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        state = {"block": False, "string": False}
        for lineno, raw in enumerate(text.splitlines(), 1):
            code = strip_comments(raw, state)
            if not code.strip():
                continue
            for pattern, why in IOS7_PLUS_SELECTORS + SILENT_COLOR_SPACE_BUGS:
                if re.search(pattern, code):
                    problems.append(
                        f"{name}:{lineno}: {code.strip()}\n      -> {why}"
                    )
    return problems


def find_binary() -> Path | None:
    """Locate the built app binary.

    Theos nests the fat binary differently depending on the build type: a debug
    build lands in obj/debug/<app>/, a release build straight in obj/. Check all
    of them rather than guessing.
    """
    obj = ROOT / ".theos/obj"
    rel = Path("VeryOriginalBlockGame.app/VeryOriginalBlockGame")
    candidates = [
        obj / "release" / rel,
        obj / "debug" / rel,
        obj / rel,
        obj / "armv7" / rel,  # single-arch fallback
    ]
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    return None


def check_binary() -> list[str]:
    if not LIPO.exists() or not NM.exists():
        return ["lipo/nm not found; cannot check the binary"]
    binary = find_binary()
    if binary is None:
        return ["no built binary under .theos/obj; run `make package` first"]

    import tempfile

    problems = []
    with tempfile.TemporaryDirectory() as tmp:
        thin = Path(tmp) / "armv7"
        subprocess.run(
            [str(LIPO), str(binary), "-thin", "armv7", "-output", str(thin)],
            check=True,
        )
        out = subprocess.run(
            [str(NM), "-u", str(thin)], capture_output=True, text=True, check=True
        ).stdout
        for line in out.splitlines():
            sym = line.strip()
            if not sym.startswith("_"):
                continue
            if sym in ALLOWED_ON_IOS6:
                continue
            if sym in IOS6_MISSING_SYMBOLS:
                problems.append(f"{sym}: {IOS6_MISSING_SYMBOLS[sym]}")
    return problems


def main() -> int:
    args = sys.argv[1:]
    if "--help" in args or "-h" in args:
        print(__doc__)
        return 0

    print("iOS 6 compatibility gate")
    print("=" * 60)

    source_problems = check_sources()
    print(f"\n[1/2] source scan ({len(SOURCE_FILES)} files)")
    if source_problems:
        for p in source_problems:
            print(f"  FAIL {p}")
    else:
        print("  ok   no iOS 7+ API found in the sources")

    binary_problems = []
    if "--binary" in args or "--all" in args:
        print(f"\n[2/2] binary scan (armv7 slice)")
        binary_problems = check_binary()
        if binary_problems:
            for p in binary_problems:
                print(f"  FAIL {p}")
        else:
            print("  ok   no iOS 6-missing symbols in the armv7 slice")
    else:
        print("\n[2/2] binary scan  (skipped; pass --binary to run)")

    total = len(source_problems) + len(binary_problems)
    print("\n" + "=" * 60)
    if total == 0:
        print("PASS: nothing newer than iOS 6 detected")
        return 0
    print(f"FAIL: {total} problem(s) would break iOS 6 at run time")
    return 1


if __name__ == "__main__":
    sys.exit(main())
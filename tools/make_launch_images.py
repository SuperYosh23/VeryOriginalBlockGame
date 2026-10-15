#!/usr/bin/env python3
"""Generate the launch images.

This exists because of a concrete widescreen bug: Info.plist declared four
UILaunchImages entries (320x480, 320x568, 375x667, 414x736) but no such files
were ever in Resources/. iOS decides whether an app runs native or letterboxed
from the launch image it can find -- with no 568pt-high image, a 4-inch iPhone 5
(16:9, 320x568) has nothing to match and falls back to 480p letterboxing.

So the images have to exist, and they have to cover the widescreen sizes:

    Default.png       320x 480   3.5"  4:3   (iPhone 4S and earlier)
    Default-568h.png  320x 568   4"    16:9  (iPhone 5 / 5s / SE)
    Default-667h.png  375x 667   4.7"  16:9  (iPhone 6/7/8/SE2/13 mini)
    Default-736h.png  414x 736   5.5"  16:9  (iPhone 6+/7+/8+)

Art: the game logo on the same near-black the playfield uses, so the transition
into the running app is not a flash of white.

Note these are drawn at 1x (point size), not device pixels. UILaunchImages
entries carry the point size in UILaunchImageSize, and iOS scales the artwork,
which is why the classic filenames are unsuffixed point dimensions.

Usage:
    python3 tools/make_launch_images.py
"""

from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RES = ROOT / "Resources"
SOURCE = ROOT / "newicon.png"

# filename -> (point width, point height)
LAUNCH_IMAGES = {
    "Default": (320, 480),
    "Default-568h": (320, 568),
    "Default-667h": (375, 667),
    "Default-736h": (414, 736),
}

# The playfield background. Matching it exactly means no visible flash at launch.
BG = "#0d1024"

# Logo width as a fraction of the screen width.
LOGO_SCALE = 0.52


def require_tools() -> None:
    if not shutil.which("convert"):
        raise SystemExit("ImageMagick not found (need `convert`).")


def build(base: Path, w: int, h: int, dest: Path, workdir: Path) -> None:
    logo_w = int(w * LOGO_SCALE)
    logo_h = logo_w  # the source art is square

    logo = workdir / f"logo-{w}x{h}.png"
    subprocess.run(
        ["convert", str(base), "-filter", "Lanczos",
         "-resize", f"{logo_w}x{logo_h}!", "-strip", str(logo)],
        check=True, capture_output=True,
    )

    # Centred logo on the playfield background.
    x = (w - logo_w) // 2
    y = (h - logo_h) // 2
    subprocess.run(
        ["convert", "-size", f"{w}x{h}", f"xc:{BG}",
         str(logo), "-geometry", f"+{x}+{y}", "-composite",
         "-strip", str(dest)],
        check=True, capture_output=True,
    )


def main() -> int:
    require_tools()
    if not SOURCE.is_file():
        print(f"source not found: {SOURCE}", file=sys.stderr)
        return 1
    RES.mkdir(exist_ok=True)

    with tempfile.TemporaryDirectory() as tmp:
        workdir = Path(tmp)
        base = workdir / "base.png"
        # Flatten to RGB and strip any alpha: a launch image must be opaque, or
        # iOS treats it oddly and some devices flash.
        subprocess.run(
            ["convert", str(SOURCE), "-alpha", "off", "-background", "white",
             "-alpha", "remove", "-alpha", "off", "-colorspace", "sRGB",
             "-strip", str(base)],
            check=True, capture_output=True,
        )
        for name, (w, h) in LAUNCH_IMAGES.items():
            dest = RES / f"{name}.png"
            build(base, w, h, dest, workdir)
            print(f"wrote {name}.png ({w}x{h})")

    print(f"done -> {RES}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
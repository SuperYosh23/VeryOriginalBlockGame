#!/usr/bin/env python3
"""Generate the app icon set from newicon.png.

iOS needs the same artwork at ten sizes, and there is one shape decision here.

The source art is a full-bleed square. iOS 7 and later mask an icon's corners
themselves, but iOS 6 does no masking at all -- it draws the square PNG exactly
as given, so a full-bleed icon looks unfinished there.

So every icon is pre-masked with the superellipse ("squircle") that iOS 7+ would
have applied anyway. That is correct on iOS 6, and on iOS 7+ the system masks
corners that already match the shape, which is a no-op.

Shipping one pre-rounded set, rather than a square one for iOS 7+ and a rounded
one for iOS 6, is deliberate: iOS 6 resolves icons by conventional filename and
ignores a custom list, so two sets only worked for the OS that did not need them.

The mask is a true superellipse (exponent 5), not a circular crop, so the icon
matches the shape of the others on the home screen. It is built per output size
rather than scaled down from one big mask, which keeps the antialiased corner
edge clean at 57px.

Usage:
    python3 tools/make_icons.py [--source newicon.png]
"""

from __future__ import annotations

import argparse
import math
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RES = ROOT / "Resources"

DEFAULT_SOURCE = ROOT / "newicon.png"

# One set, under the conventional names iOS resolves by filename. Every icon is
# pre-masked with the superellipse: iOS 6 does no masking of its own and would
# show a bare square, while iOS 7+ masks corners that already match the shape it
# would have applied anyway. Shipping one rounded set avoids maintaining two.
SIZES = {
    "Icon": 57,         # iPhone 1st gen / iOS 6 home screen, @1x
    "Icon@2x": 114,
    "Icon-72": 72,      # iPad
    "Icon-72@2x": 144,
    "Icon-76": 76,      # iPhone 2/3/4, non-retina
    "Icon-76@2x": 152,
    "Icon-120": 120,    # iPhone 4/5 retina
    "Icon-120@2x": 240, # iPad @2x
    "Icon-167": 167,    # iPad Pro
    "Icon-180": 180,    # iPhone 6+
}

# Apple's icon superellipse. |x/a|^n + |y/b|^n = 1, with n around 5. iOS 6 icons
# were drawn to roughly this shape.
SUPERELLIPSE_N = 5.0


def require_tools() -> None:
    for tool in ("convert", "magick"):
        if shutil.which(tool):
            return
    raise SystemExit(
        "ImageMagick not found. Install imagemagick, or drop the generated icons "
        "into Resources/ by hand."
    )


def run(args: list[str]) -> None:
    subprocess.run(args, check=True, capture_output=True)


def resize(source: Path, size: int, dest: Path) -> None:
    """High-quality downscale to an exact square size."""
    # -filter Lanczos for downscaling, then an exact resize to the target box.
    run([
        "convert",
        str(source),
        "-filter", "Lanczos",
        "-resize", f"{size}x{size}!",
        "-strip",
        str(dest),
    ])


def superellipse_points(size: int, n: float) -> str:
    """A closed polygon approximating the superellipse |x|^n+|y|^n=1.

    Drawn as an explicit polygon rather than via -fx because ImageMagick 7
    dropped the `v` coordinate from -fx, and a rounded-rectangle primitive would
    only approximate the shape. The parametric form is
    x = a * sign(cos t) * |cos t|^(2/n), which traces the curve exactly.
    """
    r = size / 2.0
    pts = []
    steps = 720
    for i in range(steps + 1):
        t = 2.0 * math.pi * i / steps
        c, s = math.cos(t), math.sin(t)
        x = r * math.copysign(abs(c) ** (2.0 / n), c)
        y = r * math.copysign(abs(s) ** (2.0 / n), s)
        pts.append((x + r, y + r))
    return " ".join(f"{x:.3f},{y:.3f}" for x, y in pts)


def rounded_mask(size: int, dest: Path) -> None:
    """A superellipse mask at the target resolution, drawn at final size.

    Building the mask per output size keeps the corner edge crisp; scaling one
    big mask down would soften or double up the antialiased border.
    """
    poly = superellipse_points(size, SUPERELLIPSE_N)
    run([
        "convert",
        "-size", f"{size}x{size}",
        "xc:black",
        "-fill", "white",
        "-draw", f"polygon {poly}",
        "-alpha", "off",
        str(dest),
    ])


def apply_mask(source: Path, size: int, dest: Path, mask_dir: Path) -> None:
    """Round `source` to a superellipse at exactly `size` pixels."""
    mask = mask_dir / f"mask-{size}.png"
    rounded_mask(size, mask)
    run([
        "convert",
        str(source),
        str(mask),
        "-alpha", "off",
        "-compose", "CopyOpacity",
        "-composite",
        "-alpha", "set",
        "-background", "none",
        str(dest),
    ])
    mask.unlink(missing_ok=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", default=str(DEFAULT_SOURCE),
                        help="source image (default: newicon.png)")
    args = parser.parse_args()

    source = Path(args.source).resolve()
    if not source.is_file():
        print(f"Source image not found: {source}", file=sys.stderr)
        return 1
    require_tools()
    RES.mkdir(exist_ok=True)

    with tempfile.TemporaryDirectory() as tmp:
        tmpdir = Path(tmp)
        base = tmpdir / f"base-{source.stem}.png"
        # Normalise once: strip metadata, drop any alpha, flatten to RGB.
        run([
            "convert", str(source),
            "-alpha", "off",
            "-background", "white",
            "-alpha", "remove",
            "-alpha", "off",
            "-colorspace", "sRGB",
            "-strip",
            str(base),
        ])

        for name, size in SIZES.items():
            dest = RES / f"{name}.png"
            resize(base, size, dest)
            apply_mask(dest, size, dest, tmpdir)
        print(f"wrote {len(SIZES)} pre-rounded icons")

    print(f"done -> {RES}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
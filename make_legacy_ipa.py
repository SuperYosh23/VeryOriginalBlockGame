#!/usr/bin/env python3
"""Produce the legacy 32-bit (armv7-only) .ipa for old iOS devices.

Mirrors the approach used by LegacyMusic/make_legacy_ipa.py: take the packaged
.deb, keep only the armv7 slice, fix up the Info.plist for a pre-notch device,
and repack.

The armv7 slice already reports minos 6.0 / sdk 9.3, which is exactly right for
these devices, so unlike the modern build this deliberately does NOT touch the
Mach-O load commands. It also keeps the ad-hoc signature Theos applied, so it
installs on jailbroken devices via ipainstaller/AppSync, while sideloading tools
are free to re-sign it.

Usage:
    ./make_legacy_ipa.py                 # newest deb in packages/
    ./make_legacy_ipa.py path/to.deb
"""

from __future__ import annotations

import argparse
import plistlib
import shutil
import subprocess
import sys
import tarfile
import tempfile
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent

THEOS = Path("/home/superyosh23/theos")
LIPO = THEOS / "toolchain/linux/iphone/bin/lipo"

EXECUTABLE = "VeryOriginalBlockGame"
MIN_OS = "6.0"
SDK_VERSION = "9.3"


def default_deb() -> Path:
    debs = sorted((HERE / "packages").glob("*.deb"), key=lambda p: p.stat().st_mtime)
    if not debs:
        raise SystemExit("No .deb found in packages/; run `make package` first.")
    return debs[-1]


def legacy_ipa_name(deb: Path) -> str:
    """com.superyosh23.veryoriginalblockgame_1.0-4+debug_iphoneos-arm.deb
    -> VeryOriginalBlockGame-1.0-legacy.ipa"""
    stem = deb.stem.split("_", 1)[-1]
    version = stem.split("+", 1)[0].split("-", 1)[0]
    return f"{EXECUTABLE}-{version}-legacy.ipa"


def extract_deb(deb: Path, dest: Path) -> None:
    """Unpack a .deb without needing dpkg (which is not on this host)."""
    with tempfile.TemporaryDirectory() as tmp:
        tmpdir = Path(tmp)
        subprocess.run(["ar", "x", str(deb.resolve())], cwd=tmpdir, check=True)
        data = next(tmpdir.glob("data.tar*"))
        with tarfile.open(data) as tf:
            tf.extractall(dest)


def build_legacy_ipa(deb: Path, out: Path) -> Path:
    deb = Path(deb).resolve()
    if not deb.is_file():
        raise SystemExit(f"Input .deb not found: {deb}")
    if not LIPO.exists():
        raise SystemExit(f"Missing tool: {LIPO}")

    out = Path(out).resolve()
    with tempfile.TemporaryDirectory(prefix="legacy_ipa_") as tmp:
        tmpdir = Path(tmp)
        root = tmpdir / "root"
        root.mkdir()
        extract_deb(deb, root)

        apps = list(root.glob("Applications/*.app")) or list(root.glob("*.app"))
        if not apps:
            raise SystemExit("No .app found inside the .deb")
        app = apps[0]
        print(f"[legacy] Patching {app.name}")

        # 1. Keep only the 32-bit slice. lipo carries the ad-hoc signature over.
        exe = app / EXECUTABLE
        thin = app / (EXECUTABLE + ".thin")
        print("  -> lipo -thin armv7")
        subprocess.run([str(LIPO), str(exe), "-thin", "armv7", "-output", str(thin)],
                       check=True)
        thin.replace(exe)
        exe.chmod(0o755)

        # 2. Info.plist for a 32-bit, non-notched device.
        plist_path = app / "Info.plist"
        with plist_path.open("rb") as fh:
            info = plistlib.load(fh)
        info["MinimumOSVersion"] = MIN_OS
        info["DTSDKName"] = f"iphoneos{SDK_VERSION}"
        info["DTPlatformVersion"] = SDK_VERSION
        info["UIRequiredDeviceCapabilities"] = ["armv7"]
        info.pop("UILaunchStoryboardName", None)
        with plist_path.open("wb") as fh:
            plistlib.dump(info, fh, fmt=plistlib.FMT_BINARY)

        # 3. Drop signing leftovers; the sideloader re-signs.
        shutil.rmtree(app / "_CodeSignature", ignore_errors=True)
        (app / "embedded.mobileprovision").unlink(missing_ok=True)

        # 4. Repack as Payload/.
        with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as zf:
            for path in sorted(app.rglob("*")):
                zf.write(path, Path("Payload") / app.name / path.relative_to(app))

    return out


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("deb", nargs="?", help="input .deb (default: newest in packages/)")
    parser.add_argument("-o", "--output", help="output .ipa path")
    args = parser.parse_args()

    deb = Path(args.deb) if args.deb else default_deb()
    out = Path(args.output) if args.output else HERE / legacy_ipa_name(deb)
    result = build_legacy_ipa(deb, out)
    print(f"\nDone: {result}  ({result.stat().st_size / 1024:.0f} KB, armv7 only, min iOS {MIN_OS})")


if __name__ == "__main__":
    main()
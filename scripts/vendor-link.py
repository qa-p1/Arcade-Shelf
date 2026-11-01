#!/usr/bin/env python3
"""Re-vendor Arcade Link from a local Link checkout at one tag or commit.

    python3 scripts/vendor-link.py --link ../Arcade-Link --revision v0.4.15

Copies the Qt module, the QML design kit and Inter with Link's own
tools/vendor_qt.py (pinned in vendor/arcade-link/VENDORED.json), then the
shared Windows packaging includes and shortcut schemas from the same commit
(pinned in vendor/arcade-link/shared-provenance.json). Files the previous pins
owned and the new commit no longer has are removed. Check the result with
scripts/check-vendor.py.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
VENDOR = ROOT / "vendor/arcade-link"
# Link path -> Shelf path, outside what vendor_qt.py copies.
SHARED = {
    "packaging/windows/README.md": "vendor/arcade-link/shared/packaging/windows/README.md",
    "packaging/windows/arcade.iss": "vendor/arcade-link/shared/packaging/windows/arcade.iss",
    "packaging/windows/arcade.nsh": "vendor/arcade-link/shared/packaging/windows/arcade.nsh",
    "packaging/windows/arcade-integration.ps1": "vendor/arcade-link/shared/packaging/windows/arcade-integration.ps1",
    "tools/arcade-release.py": "scripts/arcade-release.py",
    "tools/test_arcade_release.py": "scripts/test_arcade_release.py",
    "tools/validate_shortcuts.py": "scripts/validate_shortcuts.py",
    "spec/arcade-release.schema.json": "spec/arcade-release.schema.json",
    "spec/shortcuts.schema.json": "vendor/arcade-link/shared/spec/shortcuts.schema.json",
    "spec/shortcut-sheet.schema.json": "vendor/arcade-link/shared/spec/shortcut-sheet.schema.json",
    "spec/vectors/accelerators.json": "vendor/arcade-link/shared/spec/vectors/accelerators.json",
    "spec/vectors/shortcuts.json": "vendor/arcade-link/shared/spec/vectors/shortcuts.json",
    # The kit's launcher icon set (STANDARD §8) and Shelf's monochrome glyph (tray).
    "design/app-icons/shelf/icon.svg": "packaging/icons/icon.svg",
    **{f"design/app-icons/shelf/png/{size}.png": f"packaging/icons/png/{size}.png"
       for size in (16, 24, 32, 48, 64, 128, 256, 512, 1024)},
    "design/app-icons/shelf/shelf.ico": "packaging/icons/arcade-shelf.ico",
    "design/app-icons/shelf/macos/icon.icns": "packaging/icons/arcade-shelf.icns",
    "design/glyphs/arcade.shelf.svg": "assets/glyph.svg",
}


def owned_by_old_pins():
    """Every vendored path the previous pins list, relative to the repo."""
    paths = set()
    legacy = VENDOR / "provenance.json"  # the 0.3 Qt pin, before VENDORED.json
    if legacy.exists():
        paths |= {f["destination"] for f in json.loads(legacy.read_text())["files"]}
    pin = VENDOR / "VENDORED.json"
    if pin.exists():
        paths |= {f"vendor/arcade-link/{name}" for name in json.loads(pin.read_text())["files"]}
    return paths


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--link", type=Path, required=True, help="a Link checkout")
    parser.add_argument("--revision", required=True, help="the Link tag (or commit) to pin")
    args = parser.parse_args()
    link = args.link.resolve()

    def git(*a):
        return subprocess.check_output(["git", "-C", str(link), *a])

    commit = git("rev-parse", "--verify", f"{args.revision}^{{commit}}").decode().strip()
    old = owned_by_old_pins()
    for rel in old:
        (ROOT / rel).unlink(missing_ok=True)
    (VENDOR / "provenance.json").unlink(missing_ok=True)
    (VENDOR / "VENDORED.json").unlink(missing_ok=True)
    subprocess.run([sys.executable, str(link / "tools/vendor_qt.py"), "--destination", str(VENDOR),
                    "--revision", commit], check=True)
    files = []
    for source, destination in SHARED.items():
        data = git("show", f"{commit}:{source}")
        target = ROOT / destination
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        files.append({"source": source, "destination": destination, "sha256": hashlib.sha256(data).hexdigest()})
    (VENDOR / "shared-provenance.json").write_text(json.dumps(
        {"repository": "https://github.com/qa-p1/Arcade-Link", "commit": commit, "reference": args.revision,
         "files": files}, indent=2) + "\n", encoding="utf-8")
    # Remove directories the old pins left empty.
    for directory in sorted({(ROOT / rel).parent for rel in old}, key=lambda p: len(p.parts), reverse=True):
        while directory != VENDOR and directory.is_dir() and not any(directory.iterdir()):
            directory.rmdir()
            directory = directory.parent
    print(f"Arcade Link {args.revision} ({commit}) vendored")


if __name__ == "__main__":
    main()

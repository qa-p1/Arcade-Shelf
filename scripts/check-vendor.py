#!/usr/bin/env python3
"""Check the vendored Arcade Link files against their pins, offline.

vendor/arcade-link/VENDORED.json pins the Qt module, the QML design kit and
Inter (written by Link's tools/vendor_qt.py); shared-provenance.json pins the
Windows packaging includes, schemas and release tools. With --upstream (a Link
checkout), every file is also compared with the pinned commit.
Re-vendor with scripts/vendor-link.py.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VENDOR = ROOT / "vendor/arcade-link"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--upstream", type=Path, help="optional Link checkout to compare with the pinned commit")
    args = parser.parse_args()
    failures = []

    def digest(path: Path) -> str:
        return hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else "missing"

    def upstream(commit: str, source: str) -> str:
        result = subprocess.run(["git", "-C", str(args.upstream), "show", f"{commit}:{source}"], capture_output=True)
        return hashlib.sha256(result.stdout).hexdigest() if result.returncode == 0 else "missing"

    qt = json.loads((VENDOR / "VENDORED.json").read_text(encoding="utf-8"))
    for name, expected in qt["files"].items():
        if Path(name).is_absolute() or ".." in Path(name).parts:
            failures.append(f"{name}: invalid vendor path")
            continue
        if digest(VENDOR / name) != expected:
            failures.append(f"vendor/arcade-link/{name}: differs from VENDORED.json")
        if args.upstream and upstream(qt["commit"], name) != expected:
            failures.append(f"upstream {name}: does not match the pinned commit")
    shared = json.loads((VENDOR / "shared-provenance.json").read_text(encoding="utf-8"))
    for entry in shared["files"]:
        if digest(ROOT / entry["destination"]) != entry["sha256"]:
            failures.append(f"{entry['destination']}: differs from shared-provenance.json")
        if args.upstream and upstream(shared["commit"], entry["source"]) != entry["sha256"]:
            failures.append(f"upstream {entry['source']}: does not match the pinned commit")
    if qt["commit"] != shared["commit"]:
        failures.append("VENDORED.json and shared-provenance.json pin different Link commits")
    for failure in failures:
        print(failure)
    if not failures:
        print(f"Arcade Link {shared.get('reference', '')} ({qt['commit']}): "
              f"{len(qt['files'])} Qt and kit files, {len(shared['files'])} shared files match")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())

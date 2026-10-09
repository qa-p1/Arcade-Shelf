#!/usr/bin/env python3
"""Check the exact Arcade Link source pin, without network access."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--upstream", type=Path, help="optional checkout at the pinned code commit")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    metadata = json.loads((root / "vendor/arcade-link/provenance.json").read_text())
    failures = []
    for entry in metadata["files"]:
        local = root / entry["destination"]
        digest = hashlib.sha256(local.read_bytes()).hexdigest() if local.is_file() else "missing"
        if digest != entry["sha256"]:
            failures.append(f"{entry['destination']}: expected {entry['sha256']}, got {digest}")
        if args.upstream:
            upstream = args.upstream / entry["source"]
            upstream_digest = hashlib.sha256(upstream.read_bytes()).hexdigest() if upstream.is_file() else "missing"
            if upstream_digest != entry["sha256"]:
                failures.append(f"upstream {entry['source']}: does not match the recorded pin")
    for failure in failures:
        print(failure)
    if failures:
        return 1
    print(f"Arcade Link {metadata['tag']} ({metadata['commit']}): {len(metadata['files'])} files match")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

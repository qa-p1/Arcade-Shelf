#!/usr/bin/env python3
"""Check vendored Arcade Link Qt and shared integration assets offline."""
from __future__ import annotations
import argparse
import hashlib
import json
import subprocess
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--upstream", type=Path, help="optional Link checkout for the Qt source pin")
    parser.add_argument("--shared-upstream", type=Path, help="optional Link checkout for shared assets")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    failures = []

    def check(metadata_path: Path, upstream_root: Path | None, label: str) -> int:
        metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
        start_failures = len(failures)
        for entry in metadata["files"]:
            local = root / entry["destination"]
            digest = hashlib.sha256(local.read_bytes()).hexdigest() if local.is_file() else "missing"
            if digest != entry["sha256"]:
                failures.append(f"{entry['destination']}: expected {entry['sha256']}, got {digest}")
            if upstream_root:
                revision = metadata.get("commit")
                if not revision:
                    revision = subprocess.check_output(["git", "-C", str(upstream_root), "rev-parse", metadata["tag"]], text=True).strip()
                result = subprocess.run(["git", "-C", str(upstream_root), "show", f"{revision}:{entry['source']}"], capture_output=True)
                upstream_digest = hashlib.sha256(result.stdout).hexdigest() if result.returncode == 0 else "missing"
                if upstream_digest != entry["sha256"]:
                    failures.append(f"upstream {entry['source']}: does not match the recorded {label} pin")
        if len(failures) == start_failures:
            revision = metadata.get("commit", metadata.get("tag", "unknown"))
            print(f"Arcade Link {label} ({revision}): {len(metadata['files'])} files match")
        return len(metadata["files"])

    check(root / "vendor/arcade-link/provenance.json", args.upstream, "Qt")
    check(root / "vendor/arcade-link/shared-provenance.json", args.shared_upstream, "shared-assets")
    for failure in failures:
        print(failure)
    if failures:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

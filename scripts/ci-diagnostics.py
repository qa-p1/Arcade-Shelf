#!/usr/bin/env python3
"""Publish bounded failed-test diagnostics as GitHub check annotations.

This reads only CTest/QtTest logs and the pinned dependency files. It never prints
environment variables, credentials, endpoint files, or entire build logs.
It is intended for an Actions ``if: failure()`` step where artifact/log downloads
may be unavailable to the reviewer. The original failed check remains failed.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re

MAX_LOG_BYTES = 1024 * 1024
MAX_MESSAGE_BYTES = 6000
MAX_ANNOTATIONS = 20
FAILURE = re.compile(r'FAIL!|QFATAL|Test Failed\.|Test timed out|\*\*\*Exception|Segmentation fault|Traceback \(most recent call last\)|<Incident type="(?:fail|fatal)"')
TEST_START = re.compile(r"(?m)^\d+/\d+ Testing: (.+)$")


def safe_text(value: str) -> str:
    """Bound output and mask token-shaped values before publishing it."""
    value = re.sub(r"(?i)([\"']?(?:token|authorization|password|secret)[\"']?\s*[:=]\s*)[^\s,}]+", r"\1[redacted]", value)
    value = re.sub(r"\b[0-9a-fA-F]{32,}\b", "[redacted hex]", value)
    # Assertions can contain control bytes; workflow commands must stay textual.
    value = "".join(character if character in "\r\n\t" or ord(character) >= 32 else "?" for character in value)
    return value.encode("utf-8")[:MAX_MESSAGE_BYTES].decode("utf-8", errors="ignore")


def escape(value: str, *, property_value: bool = False) -> str:
    value = value.replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")
    if property_value:
        value = value.replace(",", "%2C").replace(":", "%3A")
    return value


def annotation(title: str, body: str, path: str = "CMakeLists.txt") -> None:
    print(f"::error file={escape(path, property_value=True)},line=1,title={escape(title, property_value=True)}::{escape(safe_text(body))}")


def failure_context(block: str) -> str:
    lines = block.splitlines()
    interesting: set[int] = set()
    for index, line in enumerate(lines):
        if FAILURE.search(line):
            interesting.update(range(max(0, index - 3), min(len(lines), index + 15)))
    if not interesting:
        interesting.update(range(max(0, len(lines) - 35), len(lines)))
    # Preserve actual-versus-expected lines next to each failing assertion.
    selected = [line for index, line in enumerate(lines) if index in interesting]
    return "\n".join(selected)


def read_log(log: Path) -> str:
    if not log.is_file():
        return ""
    with log.open("rb") as source:
        source.seek(0, 2)
        size = source.tell()
        source.seek(max(0, size - MAX_LOG_BYTES))
        return source.read(MAX_LOG_BYTES).decode("utf-8", errors="replace")


def ctest_failures(build: Path) -> list[tuple[str, str]]:
    contents = read_log(build / "Testing/Temporary/LastTest.log")
    starts = list(TEST_START.finditer(contents))
    results = []
    for index, start in enumerate(starts):
        end = starts[index + 1].start() if index + 1 < len(starts) else len(contents)
        block = contents[start.start():end]
        if FAILURE.search(block):
            name = start.group(1).strip()
            if name == "Desktop":
                # A Windows GUI executable can lose its redirected stdout.
                # QtTest's independent file logger still records assertions.
                block += read_log(build / "Desktop.qt-test.xml")
            results.append((name, failure_context(block)))
    # A runner/CTest crash can leave a diagnostic without a normal test block.
    if not results and FAILURE.search(contents):
        results.append(("runner", failure_context(contents)))
    return results


def vendor_failures(root: Path) -> list[tuple[str, str]]:
    metadata_path = root / "vendor/arcade-link/provenance.json"
    if not metadata_path.is_file():
        return []
    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    results = []
    for entry in metadata["files"]:
        relative = Path(entry["destination"])
        if relative.is_absolute() or ".." in relative.parts:
            continue
        local = root / relative
        actual = hashlib.sha256(local.read_bytes()).hexdigest() if local.is_file() else "missing"
        if actual != entry["sha256"]:
            # Do not dump dependency contents; mention likely CRLF conversion
            # only when undoing it actually restores the frozen release hash.
            hint = ""
            if local.is_file():
                normalized = local.read_bytes().replace(b"\r\n", b"\n")
                if hashlib.sha256(normalized).hexdigest() == entry["sha256"]:
                    hint = " Checkout changed the pinned LF source to CRLF. Preserve vendor/** bytes with -text attributes."
            results.append((relative.as_posix(), "Frozen Arcade Link bytes do not match the pinned release." + hint))
    return results


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=Path("build"))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    total = 0
    for name, context in ctest_failures(args.build):
        if total >= MAX_ANNOTATIONS:
            break
        annotation(f"CTest {name} failed", context)
        total += 1
    for path, message in vendor_failures(root):
        if total >= MAX_ANNOTATIONS:
            break
        annotation("Immutable Arcade Link source mismatch", message, path)
        total += 1
    if not total:
        print("No failed CTest block or immutable vendor mismatch found; inspect the failed build/package step.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

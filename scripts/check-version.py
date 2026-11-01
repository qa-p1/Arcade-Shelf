#!/usr/bin/env python3
"""Prints Shelf's version. CMakeLists.txt is the one place it is written."""
import re
from pathlib import Path

text = (Path(__file__).resolve().parents[1] / "CMakeLists.txt").read_text(encoding="utf-8")
match = re.search(r"^project\(ArcadeShelf VERSION (\d+\.\d+\.\d+)", text, re.M)
if not match:
    raise SystemExit("CMakeLists.txt has no project(ArcadeShelf VERSION x.y.z)")
print(match.group(1))

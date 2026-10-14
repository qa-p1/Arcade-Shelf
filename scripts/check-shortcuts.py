#!/usr/bin/env python3
"""Validate Shelf shortcuts and ensure the generated Markdown is current."""
from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
source = ROOT / "docs/user/shortcuts.json"
rendered = ROOT / "docs/user/shortcuts.md"
validator = ROOT / "scripts/validate_shortcuts.py"

check = subprocess.run([sys.executable, str(validator), str(source)], encoding="utf-8", capture_output=True, env={**os.environ, "PYTHONUTF8": "1"})
if check.returncode:
    sys.stderr.write(check.stderr or check.stdout)
    raise SystemExit(check.returncode)

result = subprocess.run([sys.executable, str(validator), str(source), "--markdown"], encoding="utf-8", capture_output=True, env={**os.environ, "PYTHONUTF8": "1"})
if result.returncode:
    sys.stderr.write(result.stderr or result.stdout)
    raise SystemExit(result.returncode)
expected = result.stdout.rstrip() + "\n"
if not rendered.is_file() or rendered.read_text(encoding="utf-8") != expected:
    print("docs/user/shortcuts.md is stale; regenerate it with scripts/validate_shortcuts.py --markdown", file=sys.stderr)
    raise SystemExit(1)
print("Shelf shortcuts are valid and docs/user/shortcuts.md is current")

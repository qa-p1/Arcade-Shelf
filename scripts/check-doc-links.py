#!/usr/bin/env python3
"""Check local Markdown links and images without network access."""
from pathlib import Path
import re
from urllib.parse import unquote, urlsplit

root = Path(__file__).resolve().parents[1]
failures = []
for source in [root / "README.md", *sorted((root / "docs").rglob("*.md"))]:
    for match in re.finditer(r"!?\[[^\]]*\]\(([^)]+)\)", source.read_text(encoding="utf-8")):
        target = match.group(1).strip().split(' "', 1)[0].strip("<>")
        parsed = urlsplit(target)
        if parsed.scheme or parsed.netloc or not parsed.path:
            continue
        if not (source.parent / unquote(parsed.path)).exists():
            failures.append(f"{source.relative_to(root)}: missing {target}")
for failure in failures:
    print(failure)
if failures:
    raise SystemExit(1)
print("Documentation links and images exist")

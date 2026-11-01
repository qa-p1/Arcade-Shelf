#!/usr/bin/env python3
"""What CI checks, in one command (STANDARD §10).

    scripts/check [--build DIR] [--no-tests]

Configures DIR (default build/) with compiler warnings as errors when it is
not configured yet, builds it, then checks the C++ format (clang-format 18,
on Linux), the QML (qmllint at zero warnings), the vendored Link files, the
shortcut data, the documentation links, the release tools and every CTest
test. Point CMAKE_PREFIX_PATH at Qt 6.8 first.
"""
import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def run(*command, env=None):
    print("+", " ".join(map(str, command)), flush=True)
    subprocess.run([str(part) for part in command], cwd=ROOT, check=True, env=env)


def check_format():
    tool = os.environ.get("CLANG_FORMAT") or shutil.which("clang-format-18") or shutil.which("clang-format")
    if not tool:
        raise SystemExit("clang-format 18 is required for the format check (or set CLANG_FORMAT).")
    version = subprocess.run([tool, "--version"], capture_output=True, text=True, check=True).stdout
    if " 18." not in version:
        raise SystemExit(f"The format check needs clang-format 18, as CI uses; {tool} is: {version.strip()}")
    files = subprocess.run(["git", "ls-files", "src/*.cpp", "src/*.h", "src/*.mm", "tests/*.cpp", "tests/*.h"],
                           cwd=ROOT, capture_output=True, text=True, check=True).stdout.split()
    run(tool, "--dry-run", "--Werror", *files)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build", default="build", help="the CMake build directory (default: build)")
    parser.add_argument("--no-tests", action="store_true", help="build and check, but skip CTest")
    args = parser.parse_args()
    build = (ROOT / args.build).resolve()
    if not (build / "CMakeCache.txt").exists():
        run("cmake", "-S", ROOT, "-B", build, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
            "-DBUILD_TESTING=ON", "-DSHELF_WARNINGS_AS_ERRORS=ON")
    # Ninja keeps going after an error, so one run reports every warning.
    keep_going = ["--", "-k", "0"] if "Ninja" in (build / "CMakeCache.txt").read_text(errors="replace") else []
    run("cmake", "--build", build, "--parallel", *keep_going)
    if sys.platform.startswith("linux"):
        check_format()
    run("cmake", "--build", build, "--target", "qmllint_gate")
    for script in ("check-vendor.py", "check-shortcuts.py", "check-doc-links.py", "test_arcade_release.py"):
        run(sys.executable, "scripts/" + script)
    if not args.no_tests:
        env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_QUICK_BACKEND="software")
        run("ctest", "--test-dir", build, "--output-on-failure", env=env)
    print("All checks passed.")


if __name__ == "__main__":
    main()

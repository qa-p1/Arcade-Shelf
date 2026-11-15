#!/usr/bin/env python3
"""Start the packaged AppImage as a resident and follow the resident, not the launcher.

A start that keeps Shelf resident (U1) copies the image once to
<XDG_DATA_HOME>/arcade/runtime/shelf-<version>-<16 hex digits>/, starts the
same command line from there and exits 0 at once. The process that stays is the
copy, so this finds it through its Link endpoint, checks where it runs from and
that the launcher left nothing behind, lets it run for a few seconds with the
real Qt Quick backend (run it under xvfb-run, with no QT_QUICK_BACKEND), then
quits it and waits for it to go. Everything lives in a temporary folder outside
any checkout.
"""
import argparse
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import tempfile
import time


def processes_running(image: Path):
    """PIDs whose executable is the AppImage file itself: its runtime or FUSE helper."""
    found = []
    for proc in Path("/proc").iterdir():
        if not proc.name.isdigit():
            continue
        try:
            if Path(os.readlink(proc / "exe")) == image:
                found.append(int(proc.name))
        except OSError:
            continue
    return found


def alive(pid: int) -> bool:
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    try:
        # A zombie is gone for this purpose; nothing reaps a daemon's orphan here.
        return Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()[0] != "Z"
    except OSError:
        return False


def wait_for(probe, description: str, seconds: float):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        value = probe()
        if value:
            return value
        time.sleep(0.1)
    raise AssertionError(f"Timed out after {seconds:g}s: {description}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--appimage", type=Path, required=True)
    image = parser.parse_args().appimage.resolve(strict=True)
    resident = None
    with tempfile.TemporaryDirectory(prefix="shelf-resident-", dir=os.environ.get("ARCADE_E2E_ROOT") or os.environ.get("RUNNER_TEMP")) as temporary:
        root = Path(temporary).resolve()
        env = dict(os.environ)
        for key in ("QT_QUICK_BACKEND", "QSG_RHI_BACKEND", "LD_LIBRARY_PATH", "APPIMAGE", "APPDIR", "OWD",
                    "ARGV0", "ARCADE_UNPACKED", "ARCADE_DEV_BUILD"):
            env.pop(key, None)
        for key, name in (("HOME", "home"), ("XDG_DATA_HOME", "data"), ("XDG_CONFIG_HOME", "config"),
                          ("XDG_CACHE_HOME", "cache"), ("ARCADE_SHELF_HOME", "profile"), ("ARCADE_HOME", "arcade")):
            (root / name).mkdir(mode=0o700)
            env[key] = str(root / name)
        env.update(APPIMAGE_EXTRACT_AND_RUN="1", ARCADE_NO_INSTALL_PROMPT="1", ARCADE_SHELF_ALLOW_DESKTOP_IN_TESTS="1")
        endpoint = root / "profile/instance/run/arcade.shelf.endpoint"
        copies = root / "data/arcade/runtime"

        def run(*args, timeout=120):
            return subprocess.run([str(image), *args], env=env, capture_output=True, text=True, timeout=timeout)

        version = run("--version").stdout.strip().split()[-1]
        launcher = None
        try:
            launcher = subprocess.Popen([str(image), "--show"], env=env, stdin=subprocess.DEVNULL,
                                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
            # The first start extracts the image into the runtime folder, so allow for the copy.
            try:
                code = launcher.wait(timeout=120)
            except subprocess.TimeoutExpired:
                raise AssertionError("The launcher is still running after 120 s; it must hand over to the unpacked copy and exit 0") from None
            assert code == 0, f"The launcher exited {code}; it must hand over to the unpacked copy and exit 0"
            resident = wait_for(lambda: json.loads(endpoint.read_text())["pid"] if endpoint.is_file() else 0,
                                "the resident's Link endpoint", 60)
            assert alive(resident), "The launcher exited and took the resident with it"
            executable = Path(os.readlink(f"/proc/{resident}/exe"))
            pattern = re.compile(re.escape(str(copies)) + r"/shelf-" + re.escape(version) + r"-[0-9a-f]{16}/.*arcade-shelf$")
            assert pattern.fullmatch(str(executable)), \
                f"The resident runs from {executable}, not from {copies}/shelf-{version}-<16 hex digits>/"
            environment = dict(entry.split(b"=", 1) for entry in Path(f"/proc/{resident}/environ").read_bytes().split(b"\0") if b"=" in entry)
            assert environment.get(b"ARCADE_UNPACKED") == b"1", "The copy was not told it is the unpacked copy"
            assert environment.get(b"APPIMAGE") == str(image).encode(), "The copy lost $APPIMAGE (installs and restarts use it)"
            assert not processes_running(image), f"The AppImage's own runtime still runs: {processes_running(image)}"
            # The real GL path: the window is up and stays up.
            time.sleep(4)
            assert alive(resident), "The resident exited by itself"
            status = json.loads(run("--status", timeout=30).stdout)
            assert status["running"] and status["pid"] == resident and status["version"] == version, status
            result = run("--quit", timeout=30)
            assert result.returncode == 0, f"--quit exited {result.returncode}: {result.stdout}{result.stderr}"
            wait_for(lambda: not alive(resident), "the resident to quit", 15)
            assert not endpoint.exists(), "A stopped Shelf left its endpoint behind"
            assert not processes_running(image), f"A runtime process is left: {processes_running(image)}"
        except (AssertionError, subprocess.TimeoutExpired, KeyError, ValueError) as error:
            print(f"FAIL: {type(error).__name__}: {error}", file=sys.stderr)
            return 1
        finally:
            if launcher is not None and launcher.poll() is None:
                os.killpg(launcher.pid, signal.SIGKILL)
            if resident and alive(resident):
                os.kill(resident, signal.SIGKILL)
    print(f"Resident Shelf {version} ran from its unpacked copy; the launcher exited 0; --status and --quit followed it")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

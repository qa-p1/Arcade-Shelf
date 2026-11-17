#!/usr/bin/env python3
"""Tier 1 (headless Hyprland, Link's desktop harness) for Arcade Shelf's windows.

    shelf_desktop.py <AppImage>

In a private profile, from the AppImage the way a user starts it (it hands over to its unpacked copy):
- ID-1: the panel's and Settings' class is `dev.arcade.Shelf`, told apart by title.
- --show maps the panel at its open size; --status reports the resident.
- --add stages a file: it is in the store (shelf.sqlite3) and the panel stays open.
- Escape on the open panel closes it to the capsule (the same window, at the capsule's fixed size); --toggle
  opens it again.
- --settings opens Settings as its own window and Ctrl+W closes it, leaving the panel.
- --quit stops the resident and no Shelf window is left.
Exit 0 when every check passes, 1 otherwise.
"""
import json
import os
import shutil
import sqlite3
import subprocess
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, ".arcade-link/tools/desktop")
from arcade_desktop import Desktop, DesktopError  # noqa: E402

CLASS = "dev.arcade.Shelf"
PANEL, SETTINGS = "Arcade Shelf", "Settings — Arcade Shelf"
# Main.qml: the open panel is at least 440x350; the capsule is 216x76 (a docked tab is smaller still).
OPEN_MIN, CAPSULE_MAX = (440, 350), (240, 100)
d = Desktop()
home = tempfile.mkdtemp(prefix="shelf-desktop-")
downloads = Path(home, "Downloads")
downloads.mkdir()
app = str(downloads / "Arcade-Shelf.AppImage")
shutil.copy2(sys.argv[1], app)
os.chmod(app, 0o755)
profile = Path(home, "profile")
env = {**os.environ, "HOME": home, "XDG_CONFIG_HOME": f"{home}/.config", "XDG_DATA_HOME": f"{home}/.local/share",
       "XDG_CACHE_HOME": f"{home}/.cache", "ARCADE_HOME": f"{home}/arcade-link", "ARCADE_SHELF_HOME": str(profile),
       "ARCADE_NO_INSTALL_PROMPT": "1",
       # ARCADE_HOME alone turns off Shelf's desktop integration (hyprctl placement, which floats the panel), as in
       # the unit tests; this session is private, so it stays on, as for a user.
       "ARCADE_SHELF_ALLOW_DESKTOP_IN_TESTS": "1"}
# The harness marks AppImages "already unpacked" so tests keep the process they start; Shelf is followed through
# --status and the compositor instead, so it runs as a user's would.
env.pop("ARCADE_UNPACKED", None)
log = open(d.artifact("shelf.log"), "wb")
results = []


def check(name, ok, detail=""):
    results.append(("PASS" if ok else "FAIL", name, detail))
    print(f"{'PASS' if ok else 'FAIL'}: {name}{' (' + detail + ')' if detail and not ok else ''}", flush=True)
    return ok


def run(*args, timeout=60):
    return subprocess.run([app, *args], env=env, stdout=log, stderr=subprocess.STDOUT, timeout=timeout)


def status():
    out = subprocess.run([app, "--status"], env=env, capture_output=True, text=True, timeout=30)
    try:
        return json.loads(out.stdout.strip().splitlines()[-1])
    except (IndexError, ValueError):
        return {}


def panel():
    # The panel's title is exactly "Arcade Shelf"; Settings' title also contains it.
    return next((c for c in d.clients() if c.get("class") == CLASS and c.get("title") == PANEL), None)


def sized(test, timeout=10, what="panel size"):
    try:
        return d.wait(lambda: (lambda w: w if w and test(w["size"]) else None)(panel()), timeout, what)
    except DesktopError:
        return None


def wait_for(probe, what, timeout=15):
    try:
        return d.wait(probe, timeout, what)
    except DesktopError:
        return None


def is_open(size):
    return size[0] >= OPEN_MIN[0] and size[1] >= OPEN_MIN[1]


def is_capsule(size):
    return size[0] <= CAPSULE_MAX[0] and size[1] <= CAPSULE_MAX[1]


def stored(name):
    db = profile / "shelf.sqlite3"
    if not db.exists():
        return False
    try:
        with sqlite3.connect(f"file:{db}?mode=ro", uri=True, timeout=2) as con:
            return con.execute("SELECT count(*) FROM items WHERE name = ?", (name,)).fetchone()[0] > 0
    except sqlite3.Error:
        return False


try:
    # --- the panel: identity, size, resident ---------------------------------------------------------------------
    run("--show")
    try:
        window = d.wait_window(cls=CLASS, title=PANEL, timeout=30)
    except DesktopError:
        window = None
    if not check("--show maps the panel", window is not None):
        raise SystemExit(1)
    check("ID-1: the panel's class is Shelf's desktop id", window.get("class") == CLASS, window.get("class", ""))
    window = sized(is_open, what="the open panel")
    check("the panel opens at its open size", window is not None, str(panel() and panel()["size"]))
    check("the panel floats (Hyprland would otherwise tile it)",
          wait_for(lambda: (panel() or {}).get("floating"), "the panel to float", 10) is not None,
          json.dumps(panel() and {k: panel().get(k) for k in ("floating", "size", "at")}))
    resident = status()
    check("--status reports the running Shelf", bool(resident.get("running")), json.dumps(resident))

    # --- --add stages a file into the store ----------------------------------------------------------------------
    note = Path(home, "shelf-desktop-note.txt")
    note.write_text("collected by the desktop test\n")
    added = run("--add", str(note))
    check("--add exits 0", added.returncode == 0, f"exit {added.returncode}")
    try:
        d.wait(lambda: stored(note.name), 10, "the added file in the store")
        check("--add puts the file in the store", True)
    except DesktopError:
        check("--add puts the file in the store", False)
    check("the panel is still open after --add", sized(is_open, 5) is not None)

    # --- Escape closes to the capsule; --toggle opens again ------------------------------------------------------
    if panel():
        d.focus(panel()["address"])
        time.sleep(0.3)
        d.escape(panel()["address"])
    capsule = sized(is_capsule, what="the capsule")
    check("Escape closes the panel to its capsule", capsule is not None, str(panel() and panel()["size"]))
    check("the capsule is the same window", capsule is not None and capsule["address"] == window["address"])
    run("--toggle")
    check("--toggle opens the panel again", sized(is_open, what="the reopened panel") is not None)

    # --- Settings: its own window, Ctrl+W closes it --------------------------------------------------------------
    run("--settings")
    try:
        settings = d.wait_window(cls=CLASS, title=SETTINGS, timeout=15)
    except DesktopError:
        settings = None
    check("--settings opens Settings", settings is not None)
    if settings:
        check("ID-1: Settings' class is Shelf's desktop id", settings.get("class") == CLASS, settings.get("class", ""))
        check("Settings is a separate window from the panel", panel() is not None
              and settings["address"] != panel()["address"])
        d.focus(settings["address"])
        time.sleep(0.3)
        d.send_shortcut(settings["address"], "CTRL", "w")
        try:
            d.wait_gone(cls=CLASS, title=SETTINGS, timeout=5)
            check("Ctrl+W closes Settings", True)
        except DesktopError:
            check("Ctrl+W closes Settings", False)
        check("closing Settings leaves the panel", panel() is not None)

    # --- quit ----------------------------------------------------------------------------------------------------
    run("--quit")
    try:
        d.wait(lambda: not status().get("running"), 20, "Shelf to quit")
        check("--quit stops Shelf", True)
    except DesktopError:
        check("--quit stops Shelf", False)
    try:
        d.wait(lambda: d.find_window(cls=CLASS) is None, 10, "Shelf's windows to go")
        check("no Shelf window is left after --quit", True)
    except DesktopError:
        check("no Shelf window is left after --quit", False)
finally:
    subprocess.run([app, "--quit"], env=env, capture_output=True, timeout=30)
    log.flush()
    failed = [r for r in results if r[0] == "FAIL"]
    print(f"\n{len(results) - len(failed)}/{len(results)} checks passed", flush=True)
    if failed:  # Also when a check stopped the run early.
        print(Path(d.artifact("shelf.log")).read_text(errors="replace")[-6000:], flush=True)

sys.exit(1 if failed else 0)

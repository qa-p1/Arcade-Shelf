#!/usr/bin/env python3
"""Upgrade from a published Shelf release to a new one, keeping the user's data (PLAN exit gate "Upgrade test").

    dbus-run-session -- xvfb-run -a python3 scripts/verify-upgrade.py OLD.AppImage NEW.AppImage

OLD is the previous release's published file (check it against its SHA256SUMS.txt first), NEW the candidate's. In a
private profile: OLD is installed from a download folder (`--install --silent --no-launch`, which also adds the login
entry), started with `--background`, and given the data a user of it has:
  * a text file and a folder, added with OLD's own `--add` (Shelf stores them as references on the Quick Shelf);
  * a second, pinned shelf and two items Shelf owns, with their staged content files in `objects/`, plus three
    non-default settings. Shelf has no command line for these (staged items come from Link handoffs and the
    clipboard, settings from its Settings page), so with OLD stopped they are written into shelf.sqlite3 and
    `objects/` in the shapes Store.cpp writes (`items` rows with owned=1, `meta.settings` as compact JSON), and OLD is
    started again and must load them (its own `--status` counts the items) and keep them.
All rows of every table of shelf.sqlite3 and every staged file (SHA-256) are then snapshotted. NEW is downloaded and
installed over OLD while OLD runs, the way a user updates by hand, and started. NEW must replace the installed copy
in place, stop OLD, report its own version, keep every row (a newer Shelf may add tables, columns and rows), every
staged file and the settings, keep exactly one launcher and at most one login entry, both starting the installed
file, and stop on --quit.

A dry run against a local build of the same version sets VERIFY_UPGRADE_SAME_VERSION=1: OLD and NEW may then be the
same bytes (or the same version number); everything else is still checked.
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import sqlite3
import subprocess
import sys
import tempfile
import time

APP_ID = "arcade.shelf"
LAUNCHER = "dev.arcade.Shelf.desktop"
FILENAME = "Arcade-Shelf.AppImage"
SAME_VERSION = os.environ.get("VERIFY_UPGRADE_SAME_VERSION") == "1"

# Non-default settings (defaults in Store::settings(): theme system, retentionDays 30, alwaysOnTop true,
# reducedMotion false, quotaBytes DefaultQuota). Stored as one compact JSON object in meta(key='settings').
SETTINGS = {"theme": "dark", "retentionDays": 90, "alwaysOnTop": False, "reducedMotion": True}
NOTES = {  # id suffix -> (shelf, file name, content)
    "a1" * 16: ("quick", "Meeting notes.txt", "Agenda\n- ship the upgrade test\n- keep the user's shelf\n"),
    "b2" * 16: ("sh_" + "c3" * 16, "Links.txt", "https://example.invalid/one\nhttps://example.invalid/two\n"),
}


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def alive(pid):
    """Running, not a zombie."""
    try:
        return Path(f"/proc/{pid}/stat").read_text().rpartition(") ")[2].split()[0] != "Z"
    except OSError:
        return False


def shelf_processes(root):
    """Processes of this profile: running a file under `root`, or started with its ARCADE_HOME."""
    found = {}
    marker = b"ARCADE_HOME=" + str(root / "arcade").encode()
    for proc in Path("/proc").iterdir():
        if not proc.name.isdigit() or int(proc.name) == os.getpid():
            continue
        try:
            exe = os.readlink(proc / "exe")
            environment = (proc / "environ").read_bytes().split(b"\0")
        except OSError:
            continue
        if (exe.startswith(str(root) + "/") or marker in environment) and alive(proc.name):
            found[int(proc.name)] = exe
    return found


def read_database(path):
    """{table: (columns, [row, ...])} for every table of the database, opened read-only."""
    connection = sqlite3.connect(f"file:{path}?mode=ro", uri=True, timeout=10)
    try:
        tables = [row[0] for row in connection.execute(
            "SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%' ORDER BY name")]
        result = {}
        for table in tables:
            cursor = connection.execute(f'SELECT * FROM "{table}"')
            result[table] = ([column[0] for column in cursor.description], cursor.fetchall())
        return result
    finally:
        connection.close()


def staged_files(data):
    """Every file under objects/: {relative path: sha256}."""
    objects = data / "objects"
    return {str(path.relative_to(objects)): digest(path) for path in sorted(objects.rglob("*")) if path.is_file()}


def check_rows(before, after, label):
    """Every row OLD had is in `after` unchanged, compared on the columns OLD had (NEW may add tables, columns, rows)."""
    for table, (columns, rows) in before.items():
        assert table in after, f"{label}: table {table} is gone (has {sorted(after)})"
        names, now = after[table]
        missing_columns = [name for name in columns if name not in names]
        assert not missing_columns, f"{label}: table {table} lost columns {missing_columns}"
        index = [names.index(name) for name in columns]
        kept = {tuple(row[i] for i in index) for row in now}
        for row in rows:
            assert row in kept, f"{label}: {table} row {dict(zip(columns, row))} is missing or changed"


def seed_owned_data(data):
    """Writes what Shelf itself would store for owned items: a pinned shelf, two staged text items and the settings."""
    now = int(time.time() * 1000)
    connection = sqlite3.connect(data / "shelf.sqlite3", timeout=10)
    try:
        with connection:
            connection.execute("PRAGMA foreign_keys=ON")
            connection.execute("INSERT INTO shelves(id,name,temporary,pinned,position,created) VALUES(?,?,0,1,"
                               "(SELECT COALESCE(MAX(position),-1)+1 FROM shelves),?)", ("sh_" + "c3" * 16, "Projects", now))
            for suffix, (shelf, name, content) in NOTES.items():
                item = "it_" + suffix
                folder = data / "objects" / item
                folder.mkdir(mode=0o700)
                path = folder / name.replace(" ", "-")
                path.write_text(content)
                path.chmod(0o600)
                position = connection.execute("SELECT COALESCE(MAX(position),-1)+1 FROM items WHERE shelf=?",
                                              (shelf,)).fetchone()[0]
                connection.execute("INSERT INTO items(id,shelf,type,name,path,text,html,size,owned,position,created,refkey)"
                                   " VALUES(?,?,?,?,?,?,?,?,1,?,?,NULL)",
                                   (item, shelf, "text/plain", name, str(path), content, "", len(content.encode()),
                                    position, now))
            connection.execute("INSERT OR REPLACE INTO meta(key,value) VALUES('settings',?)",
                               (json.dumps(SETTINGS, separators=(",", ":")),))
    finally:
        connection.close()


def main():
    old_image, new_image = (Path(arg).resolve(strict=True) for arg in sys.argv[1:3])
    if not SAME_VERSION:
        assert digest(old_image) != digest(new_image), "OLD and NEW are the same file"
    with tempfile.TemporaryDirectory(prefix="shelf-upgrade-", dir=os.environ.get("RUNNER_TEMP")) as directory:
        root = Path(directory).resolve()
        for parent in (root, *root.parents):
            if (parent / ".git").exists():
                raise RuntimeError("Upgrade verification must run outside the checkout")
        env = {k: v for k, v in os.environ.items() if not k.startswith("ARCADE_") and k not in (
            "APPIMAGE", "APPDIR", "OWD", "ARGV0", "LD_LIBRARY_PATH", "QT_PLUGIN_PATH", "WAYLAND_DISPLAY",
            "HYPRLAND_INSTANCE_SIGNATURE", "SWAYSOCK")}
        env.update(ARCADE_HOME=str(root / "arcade"), HOME=str(root / "home"), XDG_CONFIG_HOME=str(root / "config"),
                   XDG_DATA_HOME=str(root / "data"), XDG_CACHE_HOME=str(root / "cache"),
                   XDG_RUNTIME_DIR=str(root / "run"), TMPDIR=str(root / "tmp"), ARCADE_NO_INSTALL_PROMPT="1",
                   GDK_BACKEND="x11", APPIMAGE_EXTRACT_AND_RUN="1", LC_ALL="C.UTF-8")
        for name in ("home", "config", "data", "cache", "run", "tmp", "arcade"):
            (root / name).mkdir(mode=0o700)
        downloads = root / "home/Downloads"
        downloads.mkdir()
        arcade = root / "arcade"
        installed = arcade / "Applications/Arcade" / FILENAME
        # With ARCADE_HOME set only the install lands there; Shelf's data stays at $XDG_DATA_HOME/arcade/shelf.
        data = root / "data/arcade/shelf"
        launcher = arcade / ".local/share/applications" / LAUNCHER
        autostart_folder = arcade / ".config/autostart"
        log = (root / "shelf.log").open("w+b")
        launched = []

        def download(image):
            copy = downloads / image.name
            shutil.copy2(image, copy)
            copy.chmod(0o755)
            return copy

        def run(executable, *args, check=True, timeout=120):
            try:
                result = subprocess.run([str(executable), *args], env=env, capture_output=True, text=True,
                                        timeout=timeout)
            except subprocess.TimeoutExpired:
                raise AssertionError(f"{Path(executable).name} {' '.join(args)} did not finish in {timeout}s")
            if check:
                assert result.returncode == 0, (
                    f"{Path(executable).name} {' '.join(args)} exited {result.returncode}: {result.stderr[-2000:]}")
            return result.stdout

        def status():
            return json.loads(run(installed, "--status", timeout=30))

        def wait(probe, what, seconds=30):
            end = time.monotonic() + seconds
            while time.monotonic() < end:
                if result := probe():
                    return result
                time.sleep(0.2)
            raise AssertionError(f"Timed out waiting for {what}")

        def running():
            try:
                info = status()
            except (AssertionError, ValueError):
                return None
            return info if info.get("running") else None

        def start():
            launched.append(subprocess.Popen([str(installed), "--background"], env=env, stdout=log,
                                             stderr=subprocess.STDOUT, start_new_session=True))
            return wait(running, "Shelf to report running in --status", 40)

        def stop():
            run(installed, "--quit", timeout=30)
            wait(lambda: not running() and not shelf_processes(root) and all(p.poll() is not None for p in launched),
                 "every Shelf process to exit after --quit", 20)

        try:
            # OLD is installed from a download.
            old_version = run(download(old_image), "--version").strip().split()[-1]
            receipt = json.loads(run(downloads / old_image.name, "--install", "--silent", "--no-launch"))
            assert Path(receipt["path"]) == installed, receipt
            assert receipt["version"] == old_version, receipt
            assert digest(installed) == digest(old_image), "The installed copy is not OLD's bytes"
            old_login = Path(receipt["integration"]["autostart"])
            assert old_login.is_file() and old_login.parent == autostart_folder, f"OLD set up no login entry: {receipt}"
            assert Path(receipt["integration"]["desktopEntry"]) == launcher, receipt["integration"]
            assert not shelf_processes(root), "--no-launch started Shelf"

            first = start()
            assert first["version"] == old_version and first["id"] == APP_ID, first

            # The user adds a file and a folder with OLD's own --add; Shelf stores them as references.
            user = root / "home/Documents"
            (user / "Project").mkdir(parents=True)
            note = user / "todo.txt"
            note.write_text("buy milk\nwrite the upgrade test\n")
            (user / "Project/readme.md").write_text("# Project\n")
            user_files = {path: digest(path) for path in (note, user / "Project/readme.md")}
            run(installed, "--add", str(note), timeout=30)
            wait(lambda: status()["status"]["items"] >= 1, "OLD to store the added file")
            run(installed, "--add", str(user / "Project"), timeout=30)
            wait(lambda: status()["status"]["items"] >= 2, "OLD to store the added folder")
            stop()
            added = read_database(data / "shelf.sqlite3")
            types = sorted(row[added["items"][0].index("type")] for row in added["items"][1])
            assert types == ["file/text", "folder/reference"], f"OLD stored the --add items as {types}"

            # What --add cannot make (owned items, a second shelf, settings): written with OLD stopped, then OLD loads it.
            seed_owned_data(data)
            on_quick = len(added["items"][1]) + sum(1 for shelf, _, _ in NOTES.values() if shelf == "quick")
            old_resident = start()
            assert old_resident["status"]["items"] == on_quick, (
                f"{old_version} reports {old_resident['status']['items']} items on the Quick Shelf, expected {on_quick}")
            old_pid = old_resident["pid"]
            time.sleep(3)  # OLD's startup clean-up (orphaned objects, expired items) has run by now
            before = read_database(data / "shelf.sqlite3")
            files_before = staged_files(data)
            assert len(files_before) == len(NOTES), f"{old_version} dropped or added staged files: {files_before}"
            names = before["items"][0]
            titles = {row[names.index("name")] for row in before["items"][1]}
            assert {"todo.txt", "Project", "Meeting notes.txt", "Links.txt"} <= titles, f"{old_version} lost items: {titles}"
            settings_row = dict(before["meta"][1])
            assert json.loads(settings_row["settings"]) == SETTINGS, (
                f"{old_version} changed the settings it was given: {settings_row['settings']}")
            assert {"shelves", "items", "meta"} <= set(before), sorted(before)
            print(f"{old_version} installed at {installed} and running (PID {old_pid}) with the user's data: "
                  f"{len(before['items'][1])} items, {len(before['shelves'][1])} shelves, {len(files_before)} staged "
                  f"files, settings {sorted(SETTINGS)}", flush=True)

            # The update is by hand while OLD runs: download NEW, double click (--install, then start it).
            new_download = download(new_image)
            new_version = run(new_download, "--version").strip().split()[-1]
            assert SAME_VERSION or new_version != old_version, f"NEW is {new_version}, the same release as OLD"
            # 0.4.0 observed: `--install --no-launch` over a running Shelf replaces the file but leaves the old
            # resident running (only the launching install asks it to quit), and `--background` against a resident
            # just returns. So, like a double click, the update install is not --no-launch: it stops OLD, installs
            # and starts NEW with --background. Output goes to a file, not a pipe, the started Shelf inherits it.
            with (root / "update.log").open("w+b") as update:
                try:
                    result = subprocess.run([str(new_download), "--install", "--silent"], env=env, stdout=update,
                                            stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, timeout=120)
                except subprocess.TimeoutExpired:
                    raise AssertionError("NEW's --install did not finish in 120s")
                update.seek(0)
                output = update.read().decode(errors="replace")
            assert result.returncode == 0, f"NEW --install exited {result.returncode}: {output[-2000:]}"
            upgraded = json.JSONDecoder().raw_decode(output[output.index("{"):])[0]
            wait(lambda: not alive(old_pid), "OLD to exit during the install", 20)
            assert digest(installed) == digest(new_image), "The installed copy is not NEW's bytes"
            if not SAME_VERSION:
                assert digest(installed) != digest(old_image), "The installed copy is still OLD's bytes"
            assert upgraded["version"] == new_version and Path(upgraded["path"]) == installed, upgraded
            assert upgraded["installedAt"] == receipt["installedAt"], "The upgrade reset the original install date"
            assert upgraded.get("previous", {}).get("version") == old_version, (
                f"The receipt does not record the replaced {old_version}: {upgraded.get('previous')}")

            new_resident = wait(lambda: (i if (i := running()) and i["pid"] != old_pid else None),
                                "NEW to be started by the install", 40)
            assert new_resident["version"] == new_version, f"--status reports {new_resident['version']}, not {new_version}"
            assert new_resident["pid"] != old_pid and not alive(old_pid), (new_resident["pid"], old_pid)
            assert old_pid not in shelf_processes(root), "OLD's process is still there"
            assert new_resident["status"]["items"] == old_resident["status"]["items"], (
                f"NEW sees {new_resident['status']['items']} items on the Quick Shelf, OLD saw "
                f"{old_resident['status']['items']}")
            time.sleep(3)  # NEW's startup clean-up has run

            # The integration is NEW's: one launcher, at most one login entry, both starting the installed file.
            integration = json.loads(run(installed, "--integration-status", timeout=60))
            assert integration["receipt"]["version"] == new_version, integration["receipt"]
            assert Path(integration["receipt"]["path"]) == installed, integration["receipt"]
            missing = [entry["path"] for entry in integration["entries"] if not entry["exists"]]
            assert not missing, f"Integration entries missing after the upgrade: {missing}"
            entries = upgraded["integration"]
            assert Path(entries["desktopEntry"]) == launcher, f"The launcher moved: {entries['desktopEntry']}"
            launchers = [path for path in launcher.parent.glob("*.desktop")
                         if f"X-Arcade-Id={APP_ID}" in path.read_text() or path.name == LAUNCHER
                         or "arcade.shelf" in path.name.lower()]
            assert launchers == [launcher], f"Expected one Shelf launcher, found {launchers}"
            assert f'Exec="{installed}"\n' in launcher.read_text(), launcher.read_text()
            logins = [path for path in autostart_folder.glob("*.desktop")
                      if f"X-Arcade-Id={APP_ID}" in path.read_text() or "shelf" in path.name.lower()]
            assert len(logins) <= 1, f"More than one Shelf login entry: {logins}"
            for login in logins:
                text = login.read_text()
                assert f'Exec="{installed}" "--background"' in text, f"{login} does not start the installed file:\n{text}"
                assert "Hidden=true" not in text and "X-GNOME-Autostart-enabled=false" not in text, text
            assert logins == [old_login], f"The login entry changed: {old_login} -> {logins}"
            cli = Path(entries["cli"])
            assert cli.resolve() == installed, f"{cli} -> {cli.resolve()}"
            for icon in entries["icons"]:
                assert Path(icon).is_file(), f"Icon {icon} is missing"
            beside = sorted(path.name for path in installed.parent.iterdir() if path.name.endswith(".AppImage"))
            assert beside == [FILENAME], f"Other packages beside the installed copy: {beside}"

            stop()
            assert not running(), "NEW still runs after --quit"
            assert b"panicked at" not in (root / "shelf.log").read_bytes(), "Shelf logged a panic"

            # The data: every row OLD had, every staged file, the settings, read after NEW ran and stopped.
            after = read_database(data / "shelf.sqlite3")
            check_rows(before, after, f"{new_version} after the upgrade")
            files_after = staged_files(data)
            for name, value in files_before.items():
                assert files_after.get(name) == value, (
                    f"Staged file {name}: {files_after.get(name)}, expected {value}")
            for suffix, (shelf, name, content) in NOTES.items():
                assert any(path.read_text() == content for path in (data / "objects" / f"it_{suffix}").iterdir()), (
                    f"The staged text of {name} changed")
            settings = json.loads(dict(after["meta"][1])["settings"])
            for key, value in SETTINGS.items():
                assert settings.get(key) == value, f"Setting {key} is {settings.get(key)!r}, expected {value!r}"
            for path, value in user_files.items():
                assert digest(path) == value, f"The user's own file {path} was changed"
            print(f"{new_version} replaced {old_version} in place (PID {new_resident['pid']}): "
                  f"{sum(len(rows) for _, rows in before.values())} rows in {len(before)} tables, "
                  f"{len(files_before)} staged files and {len(SETTINGS)} settings kept; one launcher, "
                  f"{len(logins)} login entry, both start the installed file", flush=True)
        except Exception:
            log.seek(0)
            print(log.read().decode(errors="replace")[-6000:], file=sys.stderr)
            raise
        finally:
            if installed.exists():
                try:
                    subprocess.run([str(installed), "--quit"], env=env, capture_output=True, timeout=30)
                    time.sleep(1)
                except (subprocess.TimeoutExpired, OSError):
                    pass
            for pid in shelf_processes(root):
                try:
                    os.kill(pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
            for process in launched:
                if process.poll() is None:
                    process.kill()
                try:
                    process.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    pass
    print(f"PASS: {old_version} -> {new_version} keeps the user's data and integration")


if __name__ == "__main__":
    main()

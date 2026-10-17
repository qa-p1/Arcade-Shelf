#!/usr/bin/env python3
"""Exercise Shelf's Linux installer with a copied binary or the real AppImage.

Local runs belong inside Link's tools/e2e.py run --. Both modes create their
own temporary HOME/XDG/profile, outside any checkout, and retain no installs.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


def digest(path: Path) -> str:
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


class AppImageLifecycleTests(unittest.TestCase):
    def test_arcade_home(self):
        self.lifecycle(arcade_home=True)

    def test_home_and_xdg(self):
        self.lifecycle(arcade_home=False)

    def lifecycle(self, *, arcade_home: bool):
        with tempfile.TemporaryDirectory(prefix="shelf-appimage-", dir=os.environ.get("ARCADE_E2E_ROOT") or os.environ.get("RUNNER_TEMP")) as temporary:
            root = Path(temporary).resolve()
            for parent in (root, *root.parents):
                self.assertFalse((parent / ".git").exists(), "Installer fixtures must be outside a checkout")
            env = dict(os.environ)
            for key in ("APPIMAGE", "APPDIR", "APPIMAGE_EXTRACT_AND_RUN", "OWD", "ARGV0",
                        "ARCADE_DEV_BUILD", "ARCADE_HOME", "ARCADE_SHELF_HOME",
                        "ARCADE_NO_INSTALL_PROMPT", "DISPLAY", "WAYLAND_DISPLAY",
                        "HYPRLAND_INSTANCE_SIGNATURE", "SWAYSOCK",
                        "LD_LIBRARY_PATH", "QT_PLUGIN_PATH", "QML_IMPORT_PATH", "QML2_IMPORT_PATH"):
                env.pop(key, None)
            for key, name in (("HOME", "home"), ("XDG_CONFIG_HOME", "config"),
                              ("XDG_DATA_HOME", "data"), ("XDG_CACHE_HOME", "cache"),
                              ("XDG_RUNTIME_DIR", "run"), ("ARCADE_SHELF_HOME", "profile"),
                              ("TMPDIR", "temporary")):
                directory = root / name
                directory.mkdir(mode=0o700)
                env[key] = str(directory)
            env["QT_QPA_PLATFORM"] = "shelf-test-no-gui-plugin"
            if arcade_home:
                env["ARCADE_HOME"] = str(root / "arcade")
            install_home = root / ("arcade" if arcade_home else "home")
            data_home = install_home / ".local/share" if arcade_home else root / "data"
            receipt_path = (install_home / "installs" if arcade_home else data_home / "arcade/installs") / "arcade.shelf.json"
            installed = install_home / "Applications/Arcade/Arcade-Shelf.AppImage"
            cli = install_home / ".local/bin/arcade-shelf"
            desktop = data_home / "applications/arcade.shelf.desktop"
            env["PATH"] = str(cli.parent) + os.pathsep + env.get("PATH", "")

            # The runtime normally sets APPIMAGE to the package and APPDIR to
            # its extracted/mounted payload, not to the package's parent.
            # In Downloads: --silent removes the downloaded file only there.
            download = install_home / "Downloads/Arcade Shelf.AppImage"
            download.parent.mkdir(parents=True)
            if REAL_IMAGE:
                env["APPIMAGE_EXTRACT_AND_RUN"] = "1"
                payload = download
            else:
                appdir = root / "AppDir"
                payload = appdir / "usr/bin/arcade-shelf"
                payload.parent.mkdir(parents=True)
                shutil.copy2(SOURCE, payload)

            def run(executable, *args, image: Path | None = None):
                command_env = dict(env)
                if image is not None and not REAL_IMAGE:
                    command_env.update(APPIMAGE=str(image), APPDIR=str(appdir))
                    executable = payload
                result = subprocess.run([str(executable), *args], env=command_env, cwd=root,
                                        capture_output=True, text=True, encoding="utf-8", timeout=180)
                self.assertEqual(result.returncode, 0, f"{args}: {result.stdout}\n{result.stderr}")
                return result.stdout

            def copy_download():
                shutil.copy2(SOURCE, download)
                download.chmod(0o755)

            def read_receipt():
                return json.loads(receipt_path.read_text(encoding="utf-8"))

            def assert_status():
                status = json.loads(run(cli, "--integration-status"))
                self.assertEqual(status["receipt"], read_receipt())
                self.assertTrue(status["entries"])
                self.assertTrue(all(entry["exists"] for entry in status["entries"]), status)
                self.assertTrue(status["binOnPath"], status)

            def assert_desktop():
                text = desktop.read_text(encoding="utf-8")
                self.assertIn("X-Arcade-Id=arcade.shelf\n", text)
                self.assertIn("Actions=Settings;Quit;Uninstall;\n", text)
                for action, argument in (("Settings", "--settings"), ("Quit", "--quit"), ("Uninstall", "--uninstall")):
                    self.assertIn(f"[Desktop Action {action}]", text)
                    self.assertIn(f'Exec="{installed}" "{argument}"', text)
                self.assertIn(f'Exec="{installed}"\n', text)
                self.assertNotIn(str(root / "AppDir"), text)
                self.assertNotIn(str(download), text)

            # Preserve data beside the payload and within the integration
            # directories, as well as Shelf's profile and original user files.
            sentinels = [root / "profile/keep-user-data.txt", root / "original.txt",
                         installed.parent / "user-data.txt", desktop.parent / "unrelated.desktop",
                         cli.parent / "unrelated-command"]
            for path in sentinels:
                path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
                path.write_text("keep user data\n", encoding="utf-8")

            copy_download()
            version = run(payload, "--version").strip().split()[-1]
            receipt = json.loads(run(payload, "--install", "--silent", "--no-launch", image=download))
            self.assertEqual(receipt, read_receipt())
            self.assertEqual((receipt["schema"], receipt["id"], receipt["method"], receipt["managedBy"]),
                             (1, "arcade.shelf", "appimage", "self"))
            self.assertEqual((receipt["version"], receipt["channel"]), (version, "stable"))
            self.assertEqual(Path(receipt["path"]), installed)
            self.assertTrue(os.access(installed, os.X_OK))
            self.assertEqual(digest(installed), digest(SOURCE), "Installed file differs from the package")
            self.assertFalse(download.exists(), "The default remove-download option was ignored")
            integration = receipt["integration"]
            self.assertEqual(Path(integration["desktopEntry"]), desktop)
            self.assertEqual(Path(integration["cli"]), cli)
            self.assertTrue(cli.is_symlink())
            self.assertEqual(cli.resolve(strict=True), installed)
            icons = [Path(path) for path in integration["icons"]]
            self.assertEqual(len(icons), 9, "Expected SVG and eight raster icons")
            for path in [installed, desktop, cli, *icons]:
                self.assertTrue(path.is_relative_to(root), path)
                self.assertTrue(path.is_file(), path)
            for pixels in (16, 24, 32, 48, 64, 128, 256, 512):
                icon = data_home / f"icons/hicolor/{pixels}x{pixels}/apps/arcade.shelf.png"
                self.assertIn(icon, icons)
                self.assertTrue(icon.read_bytes().startswith(b"\x89PNG\r\n\x1a\n"))
            self.assertIn(data_home / "icons/hicolor/scalable/apps/arcade.shelf.svg", icons)
            self.assertEqual(run(cli, "--version").strip().split()[-1], version)
            assert_desktop()
            assert_status()
            self.assertFalse((root / "profile/instance/run/arcade.shelf.endpoint").exists(), "--no-launch started Shelf")

            desktop.unlink()
            repaired = json.loads(run(installed, "--repair"))
            self.assertEqual(repaired, read_receipt())
            self.assertEqual(Path(repaired["path"]), installed)
            assert_desktop()
            assert_status()

            # Simulate an older installed release without building another
            # artifact. A fresh download must report and retain that predecessor.
            old = read_receipt()
            old["version"] = "0.0.0"
            receipt_path.write_text(json.dumps(old), encoding="utf-8")
            copy_download()
            updated = json.loads(run(payload, "--install", "--silent", "--no-launch", image=download))
            self.assertEqual(updated, read_receipt())
            self.assertEqual(updated["version"], version)
            self.assertEqual(updated["installedAt"], old["installedAt"])
            self.assertEqual(updated["previous"]["version"], "0.0.0")
            previous = Path(updated["previous"]["path"])
            self.assertTrue(previous.is_relative_to(installed.parent / ".previous"), previous)
            self.assertNotEqual(previous, installed)
            self.assertEqual(digest(previous), digest(SOURCE))
            assert_desktop()
            assert_status()

            # With --no-launch omitted, the wrapper re-execs the installed copy.
            # --status exits headlessly, so this tests re-exec without a resident.
            output = run(installed, "--install", "--silent", "--status", image=installed)
            decoder = json.JSONDecoder()
            relaunched_receipt, end = decoder.raw_decode(output.lstrip())
            relaunched_status = json.loads(output.lstrip()[end:])
            self.assertEqual(relaunched_receipt, read_receipt())
            self.assertEqual(relaunched_status, {"running": False})
            self.assertEqual(relaunched_receipt["previous"], updated["previous"])

            removed = json.loads(run(installed, "--uninstall", "--silent"))
            self.assertTrue(removed["uninstalled"])
            for path in [receipt_path, installed, previous, desktop, cli, *icons]:
                self.assertFalse(path.exists() or path.is_symlink(), f"Uninstall retained {path}")
            if integration.get("autostart"):
                self.assertFalse(Path(integration["autostart"]).exists())
            for path in sentinels:
                self.assertEqual(path.read_text(encoding="utf-8"), "keep user data\n", path)
            for name in (".profile", ".bash_profile", ".bashrc", ".zprofile", ".zshrc"):
                self.assertFalse((root / "home" / name).exists(), f"Installer created {name}")


def main():
    global SOURCE, REAL_IMAGE
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--binary", type=Path, help="copy the built binary to simulate APPIMAGE/APPDIR")
    source.add_argument("--appimage", type=Path, help="run the packaged AppImage with its own runtime")
    args = parser.parse_args()
    SOURCE = (args.appimage or args.binary).resolve(strict=True)
    REAL_IMAGE = args.appimage is not None
    suite = unittest.TestLoader().loadTestsFromTestCase(AppImageLifecycleTests)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())

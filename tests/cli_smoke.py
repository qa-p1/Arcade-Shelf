#!/usr/bin/env python3
"""Headless integration CLI checks; no installation or live profile is used."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    binary = parser.parse_args().binary.resolve()
    with tempfile.TemporaryDirectory(prefix="shelf-cli-") as temporary:
        root = Path(temporary)
        env = dict(os.environ)
        for key in ("DISPLAY", "WAYLAND_DISPLAY", "HYPRLAND_INSTANCE_SIGNATURE", "APPIMAGE", "APPDIR"):
            env.pop(key, None)
        for name in ("home", "data", "config", "cache", "runtime"):
            (root / name).mkdir(mode=0o700)
        env.update(HOME=str(root / "home"), ARCADE_HOME=str(root / "arcade"),
                   ARCADE_SHELF_HOME=str(root / "shelf"), ARCADE_DEV_BUILD="1",
                   XDG_DATA_HOME=str(root / "data"), XDG_CONFIG_HOME=str(root / "config"),
                   XDG_CACHE_HOME=str(root / "cache"), XDG_RUNTIME_DIR=str(root / "runtime"),
                   QT_QPA_PLATFORM="shelf-test-no-gui-plugin", ARCADE_NO_INSTALL_PROMPT="1")
        before = sorted(str(p.relative_to(root)) for p in root.rglob("*"))

        def command(args, expected):
            result = subprocess.run([str(binary), *args], env=env, capture_output=True, text=True, timeout=10)
            assert result.returncode == expected, (args, result.returncode, result.stdout, result.stderr)
            return result

        status = json.loads(command(["--integration-status"], 0).stdout)
        assert not status.get("receipt"), status
        manifest = json.loads(command(["--arcade-manifest"], 0).stdout)
        assert manifest["install"] == "dev", manifest
        assert manifest["docs"]["version"] and len(manifest["menu"]) == 2, manifest
        command(["--help"], 0)
        for args in (["--install", "--silent", "--no-launch"],
                     ["--install", "--silent", "--channel=nightly", "--no-launch"], ["--repair"]):
            command(args, 1)
        for args in (["--silent"], ["--install", "--repair", "--silent"],
                     ["--install", "--silent", "--channel=invalid"],
                     ["--integration-status", "--remove-data"], ["--integration-status", "--no-launch"]):
            command(args, 2)
        after = sorted(str(p.relative_to(root)) for p in root.rglob("*"))
        assert before == after, (before, after)
    print("Headless CLI: status, manifest, help, dev install rejection and option validation passed")


if __name__ == "__main__":
    main()

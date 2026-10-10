#!/usr/bin/env python3
"""Build Shelf's Linux AppImage (or a relocatable tarball) from a built Qt 6 Shelf.

No profile registrations or system modifications. The only download is the
pinned, SHA-256 checked appimagetool. Run on the oldest supported distribution.

Libraries that integrate with the host (C++ runtime, graphics, fonts, display
protocols, D-Bus) stay OS supplied, following the AppImage project's exclude
list: a bundled older libstdc++ or libwayland-client breaks the host's GL
driver, and Qt Quick then aborts without a graphics backend.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
import platform
import re
import shutil
import subprocess
import tarfile
import tempfile
import time
from pathlib import Path

BASE_LIBS = re.compile(
    r"^(linux-vdso|ld-linux"
    r"|lib(c|m|pthread|dl|rt|resolv|util|nss_.*)\.so"
    r"|lib(stdc\+\+|gcc_s)\.so"
    r"|lib(GL|EGL|GLX|GLESv2|OpenGL|GLdispatch|gbm|drm|vulkan)\.so"
    r"|lib(fontconfig|freetype|harfbuzz|expat|z|uuid|dbus-1|asound|xkbcommon|xkbcommon-x11)\.so"
    r"|libwayland-|libxcb|libX11|libXau|libXdmcp|libXext|libXrender|libXi|libXfixes|libSM|libICE)")
APPIMAGETOOL_VERSION = "1.9.0"
APPIMAGETOOL_SHA256 = {"x86_64": "46fdd785094c7f6e545b61afcfb0f3d98d8eab243f644b4b17698c01d06083d1"}


def run(*args: object) -> str:
    return subprocess.check_output([str(a) for a in args], text=True).strip()


def copy_tree(source: Path, dest: Path) -> None:
    if source.is_dir():
        shutil.copytree(source, dest, symlinks=False, dirs_exist_ok=True)


def report_ci_failure(error: BaseException) -> None:
    """Expose the actual package failure when Actions log storage is unavailable."""
    if os.environ.get("GITHUB_ACTIONS") != "true":
        return
    message = f"{type(error).__name__}: {error}"
    message = re.sub(r"(?i)([\"']?(?:token|authorization|password|secret)[\"']?\s*[:=]\s*)[^\s,}]+", r"\1[redacted]", message)
    message = re.sub(r"\b[0-9a-fA-F]{32,}\b", "[redacted hex]", message)
    message = "".join(character if character in "\r\n\t" or ord(character) >= 32 else "?" for character in message)
    message = message.encode("utf-8")[:6000].decode("utf-8", errors="ignore")
    message = message.replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")
    print(f"::error file=scripts/package-linux.py,line=1,title=Linux runtime packaging failed::{message}", flush=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=Path("build"))
    parser.add_argument("--qt-prefix", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path("dist"))
    parser.add_argument("--version", required=True)
    parser.add_argument("--format", choices=("appimage", "tarball"), default="appimage")
    parser.add_argument("--tools", type=Path, default=Path("build/tools"), help="cache for the pinned appimagetool")
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[1]
    build, qt_prefix, output = args.build.resolve(), args.qt_prefix.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    arch = "arm64" if platform.machine() in ("aarch64", "arm64") else "x64"
    name = f"Arcade-Shelf-{args.version}-linux-{arch}"
    qpaths = qt_prefix / "bin/qtpaths6"
    if not qpaths.exists():
        qpaths = qt_prefix / "bin/qtpaths"
    if not qpaths.exists():
        raise SystemExit("qtpaths6 or qtpaths is required under --qt-prefix/bin")
    patchelf = shutil.which("patchelf")
    if not patchelf:
        raise SystemExit("patchelf is required to make registered ELF launches relocatable")
    plugin_source = Path(run(qpaths, "--query", "QT_INSTALL_PLUGINS"))
    qml_source = Path(run(qpaths, "--query", "QT_INSTALL_QML"))
    reported_prefix = Path(run(qpaths, "--query", "QT_INSTALL_PREFIX"))

    def runtime_roots(reported: Path, sdk_name: str, environment_name: str) -> list[Path]:
        roots = [reported]
        if reported.is_relative_to(reported_prefix):
            roots.append(qt_prefix / reported.relative_to(reported_prefix))
        roots.append(qt_prefix / sdk_name)
        roots.extend(Path(path) for path in os.environ.get(environment_name, "").split(os.pathsep) if path)
        return list(dict.fromkeys(path for path in roots if path.is_dir()))

    plugin_roots = runtime_roots(plugin_source, "plugins", "QT_PLUGIN_PATH")
    qml_roots = runtime_roots(qml_source, "qml", "QML_IMPORT_PATH")
    # Qt's distribution may relocate its prefix with qt.conf.
    if not plugin_source.is_dir():
        plugin_source = qt_prefix / "plugins"
    if not qml_source.is_dir():
        qml_source = qt_prefix / "qml"
    if not plugin_source.is_dir() or not qml_source.is_dir():
        raise SystemExit("Qt plugin/QML directories could not be located")
    with tempfile.TemporaryDirectory(prefix="arcade-shelf-package-") as temporary:
        bundle = Path(temporary) / name
        subprocess.check_call(["cmake", "--install", str(build), "--prefix", str(bundle)])
        executable = bundle / "bin/arcade-shelf"
        if not executable.is_file():
            raise SystemExit("Expected CMake install to create bin/arcade-shelf")
        for plugin in ("platforms", "wayland-decoration-client", "wayland-graphics-integration-client", "wayland-shell-integration", "imageformats", "iconengines", "platformthemes", "xcbglintegrations", "egldeviceintegrations", "tls"):
            for plugin_root in plugin_roots:
                copy_tree(plugin_root / plugin, bundle / "plugins" / plugin)
        # Shelf uses only SQLite. Other SDK SQL drivers link external server
        # clients (MySQL/Mimer/PostgreSQL) that are neither needed nor bundled.
        sqlite_dir = bundle / "plugins/sqldrivers"
        sqlite_dir.mkdir(parents=True, exist_ok=True)
        for plugin_root in plugin_roots:
            sqlite_plugin = plugin_root / "sqldrivers/libqsqlite.so"
            if sqlite_plugin.is_file():
                shutil.copy2(sqlite_plugin, sqlite_dir / sqlite_plugin.name)
        if not (bundle / "plugins/sqldrivers/libqsqlite.so").is_file():
            raise SystemExit("Qt's QSQLITE runtime plugin is required in the bundle")
        # Only runtime modules used by Qt Quick Controls/Shelf. Qt5Compat is
        # harmless when absent and allows compatible visual effect imports.
        for module in ("QtQml", "QtQuick", "Qt5Compat"):
            for qml_root in qml_roots:
                copy_tree(qml_root / module, bundle / "qml" / module)
        (bundle / "bin/qt.conf").write_text("[Paths]\nPrefix=..\nLibraries=lib\nPlugins=plugins\nQmlImports=qml\n")
        library_dir = bundle / "lib"
        library_dir.mkdir(exist_ok=True)
        queue = [executable, *bundle.rglob("*.so"), *bundle.rglob("*.so.*")]
        inspected = set()
        inventory = {}
        while queue:
            elf = queue.pop()
            if str(elf) in inspected:
                continue
            inspected.add(str(elf))
            result = subprocess.run(["ldd", str(elf)], text=True, capture_output=True)
            if "not found" in result.stdout:
                raise SystemExit(f"Unresolved runtime dependency of {elf}:\n{result.stdout}")
            for line in result.stdout.splitlines():
                match = re.match(r"\s*(\S+) => (/\S+) \(", line)
                if not match:
                    continue
                soname, resolved = match.groups()
                if BASE_LIBS.match(soname):
                    continue
                destination = library_dir / soname
                if destination.exists():
                    continue
                resolved_path = Path(resolved)
                shutil.copy2(resolved_path, destination, follow_symlinks=True)
                inventory[soname] = {"source": str(resolved_path), "sha256": hashlib.sha256(destination.read_bytes()).hexdigest()}
                queue.append(destination)
        # Link registers this ELF, so a stopped peer launches it directly.
        # DT_RPATH is inherited for transitive dependencies; DT_RUNPATH alone
        # does not cover distribution Qt libraries without their own rpaths.
        subprocess.check_call([patchelf, "--force-rpath", "--set-rpath", "$ORIGIN:$ORIGIN/../lib", str(executable)])
        launcher = bundle / "arcade-shelf"
        launcher.write_text('#!/bin/sh\nset -eu\nSHELF_BUNDLE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)\nexec "$SHELF_BUNDLE/bin/arcade-shelf" "$@"\n')
        launcher.chmod(0o755)
        notices = bundle / "share/arcade-shelf/licenses"
        notices.mkdir(parents=True, exist_ok=True)
        for license_dir in (qt_prefix / "licenses", qt_prefix / "share/qt6/licenses"):
            copy_tree(license_dir, notices / "Qt")
        # Preserve notices supplied with a rootlessly extracted Debian Qt build.
        # This directory is absent in official Qt SDK layouts.
        doc_source = qt_prefix / "share/doc"
        for qt_notice in doc_source.glob("*qt6*/copyright"):
            target = notices / "Qt-distribution" / qt_notice.parent.name
            target.mkdir(parents=True, exist_ok=True)
            shutil.copy2(qt_notice, target / "copyright")
        # Preserve Debian package copyright statements for every bundled ELF.
        copyright_dir = notices / "distribution"
        if shutil.which("dpkg-query"):
            copyright_dir.mkdir(exist_ok=True)
            for entry in inventory.values():
                owner = subprocess.run(["dpkg-query", "-S", entry["source"]], capture_output=True, text=True)
                for line in owner.stdout.splitlines():
                    package = line.split(": ", 1)[0].split(":", 1)[0]
                    copyright_file = Path("/usr/share/doc") / package / "copyright"
                    if copyright_file.is_file():
                        shutil.copy2(copyright_file, copyright_dir / f"{package}.copyright")
        (notices / "runtime-inventory.json").write_text(json.dumps({"qtVersion": run(qpaths, "--qt-version"), "host": platform.platform(), "libraries": inventory, "systemProvided": BASE_LIBS.pattern}, indent=2) + "\n")
        # Smoke the exact packaged executable in an isolated profile; this does
        # not prove interactive Wayland drag-out or broad distro compatibility.
        with tempfile.TemporaryDirectory(prefix="arcade-shelf-package-smoke-") as profile:
            env = {key: value for key, value in os.environ.items() if key not in (
                "LD_LIBRARY_PATH", "QT_PLUGIN_PATH", "QML_IMPORT_PATH", "QML2_IMPORT_PATH")}
            env.update(QT_QPA_PLATFORM="offscreen", ARCADE_HOME=f"{profile}/arcade", ARCADE_SHELF_HOME=f"{profile}/shelf")
            env["QT_QUICK_BACKEND"] = "software"
            subprocess.check_call([str(executable), "--version"], env=env)
            manifest = json.loads(subprocess.check_output([str(executable), "--arcade-manifest"], env=env, text=True))
            if manifest["id"] != "arcade.shelf" or "invoke" in manifest.get("launch", {}):
                raise SystemExit("Packaged manifest ID/lifecycle is invalid")
            if Path(manifest["executable"]) != executable:
                raise SystemExit("Packaged manifest must register the installed ELF")
            log_path = Path(profile) / "background.log"
            with log_path.open("wb") as log:
                resident = subprocess.Popen([manifest["executable"], *manifest["launch"]["background"]], env=env, stdout=log, stderr=log)
                try:
                    endpoint = Path(profile) / "arcade/run/arcade.shelf.endpoint"
                    deadline = time.monotonic() + 3.0
                    while not endpoint.is_file() and resident.poll() is None and time.monotonic() < deadline:
                        time.sleep(0.02)
                    if not endpoint.is_file():
                        raise SystemExit(f"Clean registered-ELF background launch failed:\n{log_path.read_text(errors='replace')}")
                    status = json.loads(subprocess.check_output([manifest["executable"], "--status"], env=env, text=True, timeout=3))
                    if status.get("id") != "arcade.shelf":
                        raise SystemExit(f"Clean packaged status failed: {status}")
                    subprocess.check_call([manifest["executable"], "--quit"], env=env, timeout=3)
                    resident.wait(timeout=3)
                finally:
                    if resident.poll() is None:
                        resident.terminate()
                        try:
                            resident.wait(timeout=3)
                        except subprocess.TimeoutExpired:
                            resident.kill()
                            resident.wait(timeout=3)
        if args.format == "tarball":
            archive_path = output / f"{name}.tar.gz"
            with tarfile.open(archive_path, "w:gz") as archive:
                archive.add(bundle, arcname=name)
            print(f"Created {archive_path} ({archive_path.stat().st_size} bytes); {len(inventory)} dynamic libraries")
            return 0
        appimage = build_appimage(source, bundle, output, args.version, args.tools.resolve())
        print(f"Created {appimage} ({appimage.stat().st_size} bytes); {len(inventory)} dynamic libraries")
    return 0


def build_appimage(source: Path, bundle: Path, output: Path, version: str, tools: Path) -> Path:
    machine = platform.machine()
    if machine not in APPIMAGETOOL_SHA256:
        raise SystemExit(f"No pinned appimagetool for {machine}")
    tool = tools / f"appimagetool-{APPIMAGETOOL_VERSION}-{machine}.AppImage"
    if not tool.is_file():
        tools.mkdir(parents=True, exist_ok=True)
        partial = tool.with_suffix(".part")
        url = f"https://github.com/AppImage/appimagetool/releases/download/{APPIMAGETOOL_VERSION}/appimagetool-{machine}.AppImage"
        subprocess.check_call(["curl", "-fsSL", "-o", str(partial), url])
        if hashlib.sha256(partial.read_bytes()).hexdigest() != APPIMAGETOOL_SHA256[machine]:
            partial.unlink()
            raise SystemExit("appimagetool checksum mismatch")
        partial.chmod(0o755)
        partial.rename(tool)
    # The relocatable bundle becomes usr/: bin/qt.conf already resolves
    # plugins, QML and libraries relative to the executable.
    appdir = bundle.parent / "Arcade-Shelf.AppDir"
    shutil.copytree(bundle, appdir / "usr", symlinks=True)
    (appdir / "usr/arcade-shelf").unlink()
    icon = "io.github.qa_p1.ArcadeShelf"
    for directory in ("usr/share/applications", "usr/share/icons/hicolor/scalable/apps", "usr/share/metainfo"):
        (appdir / directory).mkdir(parents=True, exist_ok=True)
    shutil.copy2(source / "packaging/linux/arcade.shelf.desktop", appdir / "usr/share/applications/arcade.shelf.desktop")
    shutil.copy2(source / "packaging/linux/arcade.shelf.desktop", appdir / "arcade.shelf.desktop")
    shutil.copy2(source / "assets/shelf.svg", appdir / f"usr/share/icons/hicolor/scalable/apps/{icon}.svg")
    shutil.copy2(source / "assets/shelf.svg", appdir / f"{icon}.svg")
    shutil.copy2(source / "packaging/linux/io.github.qa_p1.ArcadeShelf.metainfo.xml", appdir / "usr/share/metainfo/io.github.qa_p1.ArcadeShelf.metainfo.xml")
    (appdir / ".DirIcon").symlink_to(f"{icon}.svg")
    apprun = appdir / "AppRun"
    apprun.write_text('#!/bin/sh\nHERE=$(dirname "$(readlink -f "$0")")\nexec "$HERE/usr/bin/arcade-shelf" "$@"\n')
    apprun.chmod(0o755)
    arch = {"x86_64": "x86_64", "aarch64": "aarch64"}[platform.machine()]
    target = output / f"Arcade-Shelf-{version}-{arch}.AppImage"
    env = dict(os.environ, ARCH=arch, APPIMAGE_EXTRACT_AND_RUN="1")
    # --no-appstream: appstreamcli is not on every build host; the metainfo
    # file still ships for software centers that read it.
    subprocess.check_call([str(tool), "--no-appstream", str(appdir), str(target)], env=env)
    with tempfile.TemporaryDirectory(prefix="arcade-shelf-appimage-smoke-") as profile:
        smoke = dict(env, QT_QPA_PLATFORM="offscreen", QT_QUICK_BACKEND="software",
                     ARCADE_HOME=f"{profile}/arcade", ARCADE_SHELF_HOME=f"{profile}/shelf")
        for key in ("LD_LIBRARY_PATH", "QT_PLUGIN_PATH", "QML_IMPORT_PATH", "QML2_IMPORT_PATH"):
            smoke.pop(key, None)
        reported = subprocess.check_output([str(target), "--version"], env=smoke, text=True).strip()
        if reported != f"Arcade Shelf {version}":
            raise SystemExit(f"AppImage reports {reported!r}, expected version {version}")
    return target


if __name__ == "__main__":
    try:
        result = main()
    except (Exception, SystemExit) as error:
        if not isinstance(error, SystemExit) or error.code not in (None, 0):
            report_ci_failure(error)
        raise
    raise SystemExit(result)

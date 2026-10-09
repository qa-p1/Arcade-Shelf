#!/usr/bin/env python3
"""Drive the real resident Shelf executable with frozen Link v1 JSON lines.

No peer SDK, daemon, test-only action, or existing profile is used. Linux/macOS
use a Unix socket; Windows unit/GUI CI must test named pipes separately.

    python3 tests/process_smoke.py --binary build/arcade-shelf
    QT_QPA_PLATFORM=xcb xvfb-run -a python3 tests/process_smoke.py --binary build/arcade-shelf

Set QT_QPA_PLATFORM=wayland inside a headless Weston session for native Wayland
startup coverage. These process checks do not claim pointer/DnD validation.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import socket
import sqlite3
import stat
import subprocess
import sys
import tempfile
import time
import unittest
import uuid

MAX_LINE = 1024 * 1024
BINARY = Path(__file__).resolve().parents[1] / "build" / "arcade-shelf"
KEEP_TEMP = False


class LinkError(RuntimeError):
    def __init__(self, error: dict):
        self.error = error
        self.code = error.get("code", "internal")
        super().__init__(f"{self.code}: {error.get('message', '')}")


class LinkClient:
    """Small bounded stdlib client; notifications are retained between calls."""

    def __init__(self, endpoint: Path, *, token: str | None = None, timeout: float = 3.0):
        info = json.loads(endpoint.read_text())
        if info["transport"] != "unix":
            raise RuntimeError("Process runner currently supports Unix Link endpoints only")
        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.socket.settimeout(timeout)
        self.timeout = timeout
        self.buffer = bytearray()
        self.next_id = 0
        self.notifications: list[dict] = []
        try:
            self.socket.connect(info["address"])
            self.call("hello", {"token": info["token"] if token is None else token,
                               "client": {"id": "arcade.find", "version": "process-test"}, "protocol": [1]})
        except BaseException:
            self.close()
            raise

    def close(self):
        self.socket.close()

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()

    def read(self, deadline: float | None = None) -> dict:
        if deadline is None:
            deadline = time.monotonic() + self.timeout
        while b"\n" not in self.buffer:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("Timed out waiting for Link response")
            self.socket.settimeout(remaining)
            data = self.socket.recv(65536)
            if not data:
                raise ConnectionError("Peer disconnected")
            self.buffer.extend(data)
            if len(self.buffer) > MAX_LINE and b"\n" not in self.buffer:
                raise ValueError("Peer sent an oversized Link line")
        raw, _, self.buffer = self.buffer.partition(b"\n")
        if len(raw) + 1 > MAX_LINE:
            raise ValueError("Peer sent an oversized Link line")
        message = json.loads(raw)
        if not isinstance(message, dict) or message.get("v", 0) < 1:
            raise ValueError("Peer sent an invalid Link message")
        return message

    def call(self, method: str, params: dict | None = None) -> dict:
        self.next_id += 1
        request_id = self.next_id
        message = {"v": 1, "id": request_id, "method": method, "params": params or {}}
        raw = json.dumps(message, ensure_ascii=False, separators=(",", ":")).encode() + b"\n"
        if len(raw) > MAX_LINE:
            raise ValueError("Request exceeds Link's line limit")
        self.socket.sendall(raw)
        deadline = time.monotonic() + self.timeout
        while True:
            answer = self.read(deadline)
            if "id" not in answer:
                self.notifications.append(answer)
                continue
            if answer["id"] != request_id:
                raise ValueError("Unexpected response ID")
            if "error" in answer:
                raise LinkError(answer["error"])
            if "result" not in answer:
                raise ValueError("Response has no result")
            return answer["result"]

    def invoke(self, action: str, inputs: list | None = None, options: dict | None = None,
               version: int = 1) -> dict:
        return self.call("invoke", {"action": action, "version": version,
                                   "inputs": inputs if inputs is not None else [],
                                   "options": options if options is not None else {},
                                   "context": {"source": "arcade.find", "interactive": True,
                                               "reason": "user-click"}})

    def job_done(self, job: str, timeout: float = 3.0) -> dict:
        deadline = time.monotonic() + timeout
        while True:
            for i, message in enumerate(self.notifications):
                if message.get("method") == "job.done" and message.get("params", {}).get("job") == job:
                    return self.notifications.pop(i)["params"]
            self.notifications.append(self.read(deadline))


def isolated_env(root: Path) -> dict[str, str]:
    env = dict(os.environ)
    for name in ("ARCADE_HOME", "ARCADE_SHELF_HOME", "APPIMAGE", "APPDIR", "OWD", "ARGV0",
                 "HYPRLAND_INSTANCE_SIGNATURE", "SWAYSOCK"):
        env.pop(name, None)
    for name in ("data", "config", "cache", "runtime", "arcade", "shelf"):
        (root / name).mkdir(mode=0o700, parents=True, exist_ok=True)
    # Preserve a caller's Wayland runtime/socket: it belongs to the test compositor.
    runtime = env.get("XDG_RUNTIME_DIR") if env.get("QT_QPA_PLATFORM") == "wayland" else str(root / "runtime")
    env.update({"ARCADE_HOME": str(root / "arcade"), "ARCADE_SHELF_HOME": str(root / "shelf"),
                "XDG_DATA_HOME": str(root / "data"), "XDG_CONFIG_HOME": str(root / "config"),
                "XDG_CACHE_HOME": str(root / "cache"), "XDG_RUNTIME_DIR": runtime or str(root / "runtime"),
                "ARCADE_SHELF_DISABLE_GLOBAL_SHORTCUT": "1", "QT_QUICK_BACKEND": "software"})
    env.setdefault("QT_QPA_PLATFORM", "offscreen")
    return env


class IsolatedShelf:
    def __init__(self, binary: Path, *, keep: bool = False):
        self.binary = binary.resolve()
        self.root = Path(tempfile.mkdtemp(prefix="shelf-process-"))
        self.env = isolated_env(self.root)
        self.endpoint = self.root / "arcade/run/arcade.shelf.endpoint"
        self.control = self.root / "shelf/instance/run/arcade.shelf.endpoint"
        self.database = self.root / "shelf/shelf.sqlite3"
        self.manifest = self.root / "arcade/apps/arcade.shelf.json"
        self.log_path = self.root / "process.log"
        self.log = self.log_path.open("ab")
        self.process: subprocess.Popen | None = None
        self.keep = keep

    def start(self, *, enabled: bool = True, deadline_seconds: float = 3.0) -> float:
        before = time.perf_counter()
        self.process = subprocess.Popen([str(self.binary), "--background"], env=self.env,
                                        stdin=subprocess.DEVNULL, stdout=self.log, stderr=self.log)
        self.wait_ready(self.endpoint if enabled else self.control, deadline_seconds)
        return (time.perf_counter() - before) * 1000

    def wait_ready(self, endpoint: Path, timeout: float = 3.0):
        deadline = time.monotonic() + timeout
        last_error = None
        while time.monotonic() < deadline:
            if self.process is not None and self.process.poll() is not None:
                raise AssertionError(f"Shelf exited {self.process.returncode}:\n{self.log_path.read_text(errors='replace')}")
            try:
                with LinkClient(endpoint, timeout=min(0.15, max(0.01, deadline - time.monotonic()))) as c:
                    if c.call("app.status")["id"] == "arcade.shelf":
                        return
            except (OSError, ValueError, ConnectionError, LinkError, TimeoutError) as error:
                last_error = error
            time.sleep(0.01)
        raise AssertionError(f"Shelf endpoint not ready in {timeout}s ({last_error}):\n{self.log_path.read_text(errors='replace')}")

    def stop(self):
        if self.process is None or self.process.poll() is not None:
            return
        endpoint = self.endpoint if self.endpoint.exists() else self.control
        try:
            with LinkClient(endpoint) as c:
                c.call("app.quit", {"force": True})
            self.process.wait(timeout=3)
        except (OSError, TimeoutError, ConnectionError, LinkError, subprocess.TimeoutExpired):
            self.process.kill()
            self.process.wait(timeout=3)

    def kill(self):
        if self.process is not None and self.process.poll() is None:
            self.process.kill()
            self.process.wait(timeout=3)

    def close(self):
        self.stop()
        self.log.close()
        if self.keep:
            print(f"Retained isolated profile: {self.root}", file=sys.stderr)
        else:
            import shutil
            shutil.rmtree(self.root)

    def rows(self) -> list[sqlite3.Row]:
        with sqlite3.connect(f"file:{self.database}?mode=ro", uri=True) as db:
            db.row_factory = sqlite3.Row
            return db.execute("SELECT * FROM items ORDER BY position").fetchall()

    def set_link_enabled_while_stopped(self, enabled: bool):
        if self.process is not None and self.process.poll() is None:
            raise RuntimeError("Preferences are only seeded while Shelf is stopped")
        with sqlite3.connect(self.database) as db:
            row = db.execute("SELECT value FROM meta WHERE key='settings'").fetchone()
            settings = json.loads(row[0]) if row else {}
            settings["linkEnabled"] = enabled
            db.execute("INSERT OR REPLACE INTO meta(key,value) VALUES('settings',?)", (json.dumps(settings),))

    def user_file(self, name: str, content: str = "source stays untouched") -> Path:
        directory = self.root / "user-files"
        directory.mkdir(exist_ok=True)
        path = directory / name
        path.write_text(content)
        return path


@unittest.skipUnless(hasattr(socket, "AF_UNIX") and sys.platform != "win32", "Unix socket process coverage")
class ShelfProcessTests(unittest.TestCase):
    def setUp(self):
        self.shelf = IsolatedShelf(BINARY, keep=KEEP_TEMP)
        self.addCleanup(self.shelf.close)

    def assert_error(self, code: str, callback):
        with self.assertRaises(LinkError) as caught:
            callback()
        self.assertEqual(caught.exception.code, code)
        return caught.exception.error

    def assert_add(self, result: dict, added: int, reasons: tuple[str, ...] | list[str] = ()):
        self.assertNotIn("job", result, "shelf.add must acknowledge committed persistence synchronously")
        self.assertEqual(len(result["data"]["added"]), added)
        self.assertEqual([row["reason"] for row in result["data"]["skipped"]], list(reasons))
        self.assertTrue(result["message"].strip())
        self.assertIn(result["data"]["shelf"]["name"], result["message"])

    def test_manifest_has_no_side_effects(self):
        before = sorted(str(p.relative_to(self.shelf.root)) for p in self.shelf.root.rglob("*"))
        r = subprocess.run([str(BINARY), "--arcade-manifest"], env=self.shelf.env,
                           capture_output=True, text=True, timeout=3)
        self.assertEqual(r.returncode, 0, r.stderr)
        manifest = json.loads(r.stdout)
        self.assertEqual(manifest["id"], "arcade.shelf")
        self.assertEqual(manifest["launch"]["background"], ["--background"])
        self.assertNotIn("invoke", manifest["launch"])
        actions = {a["id"]: a for a in manifest["actions"]}
        self.assertEqual(set(actions), {"shelf.add", "shelf.show", "shelf.pick"})
        self.assertFalse(actions["shelf.add"]["interactive"])
        self.assertEqual(actions["shelf.add"]["effects"], ["persists"])
        self.assertTrue(actions["shelf.pick"]["interactive"])
        after = sorted(str(p.relative_to(self.shelf.root)) for p in self.shelf.root.rglob("*"))
        self.assertEqual(before, after, "Manifest probe must create no metadata, listeners, or manifests")

    def test_background_readiness_single_instance_and_quit(self):
        self.assertLess(self.shelf.start(), 3000)
        endpoint = json.loads(self.shelf.endpoint.read_text())
        self.assertEqual(endpoint["pid"], self.shelf.process.pid)
        self.assertEqual(stat.S_IMODE(self.shelf.endpoint.stat().st_mode), 0o600)
        r = subprocess.run([str(BINARY), "--background"], env=self.shelf.env,
                           capture_output=True, timeout=3)
        self.assertEqual(r.returncode, 0, r.stderr.decode(errors="replace"))
        self.assertEqual(endpoint, json.loads(self.shelf.endpoint.read_text()))
        with LinkClient(self.shelf.endpoint) as c:
            self.assertEqual(c.call("app.status")["status"]["mode"], "background")
            self.assertEqual(c.call("app.quit"), {"quitting": True})
        self.shelf.process.wait(timeout=3)
        self.assertEqual(self.shelf.process.returncode, 0)
        self.assertFalse(self.shelf.endpoint.exists())

    def test_authentication_and_malformed_wire_do_not_mutate(self):
        self.shelf.start()
        self.assert_error("denied", lambda: LinkClient(self.shelf.endpoint, token="0" * 64))
        info = json.loads(self.shelf.endpoint.read_text())
        with socket.socket(socket.AF_UNIX) as raw:
            raw.settimeout(1)
            raw.connect(info["address"])
            raw.sendall(b'{"v":1,"id":1,"method":"invoke","params":{"action":"shelf.add","inputs":[]}}\n')
            self.assertEqual(json.loads(raw.recv(65536))["error"]["code"], "denied")
        with LinkClient(self.shelf.endpoint) as c:
            c.socket.sendall(b'not json\n[1,2]\n{"v":0,"id":88,"method":"describe"}\n')
            self.assertEqual(len(c.call("describe")["actions"]), 3)
        self.assertEqual(self.shelf.rows(), [])

    def test_single_array_mixed_partial_duplicates_and_references(self):
        self.shelf.start()
        first = self.shelf.user_file("one.txt")
        second = self.shelf.user_file("two.pdf", "%PDF test fixture")
        third = self.shelf.user_file("three.txt")
        folder = self.shelf.root / "user-files/folder"
        folder.mkdir()
        missing = folder / "missing.txt"
        with LinkClient(self.shelf.endpoint) as c:
            self.assert_add(c.invoke("shelf.add", [{"type": "file/text", "path": str(first)}]), 1)
            self.assert_add(c.invoke("shelf.add", [{"type": "file/any[]", "paths": [str(second), str(third)]},
                                                   {"type": "folder/reference", "path": str(folder)}]), 3)
            self.assert_add(c.invoke("shelf.add", [{"type": "file/any[]", "paths": [str(first), str(missing)]}]),
                            0, ["duplicate", "missing"])
            fourth = self.shelf.user_file("four.txt")
            self.assert_add(c.invoke("shelf.add", [{"type": "file/any[]", "paths": [str(fourth), str(missing)]}]),
                            1, ["missing"])
            self.assert_error("unsupported_input", lambda: c.invoke("shelf.add", [{"type": "file/any", "path": str(missing)}]))
        rows = self.shelf.rows()
        self.assertEqual([r["path"] for r in rows], [str(first), str(second), str(third), str(folder), str(fourth)])
        self.assertTrue(all(r["owned"] == 0 for r in rows))
        self.assertEqual(list((self.shelf.root / "shelf/objects").iterdir()), [])
        self.assertEqual(first.read_text(), "source stays untouched")
        self.assertTrue(folder.is_dir())

    def test_thousand_reference_batch_is_transactional_and_deduplicated(self):
        self.shelf.start()
        files = [self.shelf.user_file(f"bulk-{index:04d}.txt") for index in range(1000)]
        request = [{"type": "file/text[]", "paths": [str(path) for path in files]}]
        with LinkClient(self.shelf.endpoint) as c:
            self.assert_add(c.invoke("shelf.add", request, {"reveal": False}), 1000)
            self.assert_add(c.invoke("shelf.add", request, {"reveal": False}), 0, ["duplicate"] * 1000)
        self.assertEqual(len(self.shelf.rows()), 1000)
        self.assertEqual(list((self.shelf.root / "shelf/objects").iterdir()), [])
        self.shelf.stop()
        self.shelf.start()
        self.assertEqual(len(self.shelf.rows()), 1000)
        self.assertTrue(all(path.read_text() == "source stays untouched" for path in files))

    def test_invalid_content_options_and_request_limits(self):
        self.shelf.start()
        path = self.shelf.user_file("safe.txt")
        cases = [[], [None], [{"type": "structured/command", "data": "touch forbidden"}],
                 [{"type": "file/any", "path": "relative.txt"}], [{"type": "file/any", "path": str(path) + "\0"}],
                 [{"type": "file/any[]", "paths": "bad"}], [{"type": "folder/reference", "path": str(path)}],
                 [{"type": "file/pdf", "path": str(path)}], [{"type": "text/plain", "text": ""}],
                 [{"type": "text/url", "text": "javascript:alert(1)"}],
                 [{"type": "file/any[]", "paths": [str(path)] * 1001}]]
        with LinkClient(self.shelf.endpoint) as c:
            for inputs in cases:
                with self.subTest(inputs=str(inputs)[:100]):
                    self.assert_error("unsupported_input", lambda: c.invoke("shelf.add", inputs))
            self.assert_error("unsupported_input", lambda: c.invoke("shelf.add", [{"type": "text/plain", "text": "ok"}], {"reveal": "true"}))
            self.assert_error("unsupported_input", lambda: c.call("invoke", {"action": "shelf.add", "inputs": "bad", "options": {}}))
            self.assert_error("version_mismatch", lambda: c.invoke("shelf.add", [], version=2))
            self.assert_error("unsupported_input", lambda: c.invoke("shelf.open"))
            self.assert_error("too_large", lambda: c.invoke("shelf.add", [{"type": "text/plain", "text": "x" * (256 * 1024 + 1)}]))
        self.assertEqual(self.shelf.rows(), [])
        self.assertEqual(path.read_text(), "source stays untouched")

    def test_text_urls_and_peer_handoff_survive_sender_cleanup(self):
        self.shelf.start()
        handoff = self.shelf.root / "arcade/handoff" / uuid.uuid4().hex
        handoff.mkdir(mode=0o700, parents=True)
        payload = ("A large Unicode handoff — keep me.\n" * 10000).encode()
        source = handoff / "capture.txt"
        source.write_bytes(payload)
        source.chmod(0o600)
        outside = self.shelf.user_file("outside.txt")
        with LinkClient(self.shelf.endpoint) as c:
            self.assert_add(c.invoke("shelf.add", [{"type": "text/plain", "text": "a staged note"},
                                                   {"type": "text/url", "text": "https://example.org/path"},
                                                   {"type": "text/plain", "path": str(source), "owner": "arcade.clipboard"}]), 3)
            # Box and other peers can annotate durable output with provenance.
            # owner alone must never turn a normal user file into owned storage.
            self.assert_add(c.invoke("shelf.add", [{"type": "file/text", "path": str(outside), "owner": "arcade.box"}]), 1)
        outside_row = self.shelf.rows()[3]
        self.assertEqual(outside_row["path"], str(outside))
        self.assertEqual(outside_row["owned"], 0)
        source.unlink()
        handoff.rmdir()
        row = self.shelf.rows()[2]
        self.assertEqual(row["owned"], 1)
        self.assertNotEqual(row["path"], str(source))
        self.assertEqual(Path(row["path"]).read_bytes(), payload)
        self.assertEqual(stat.S_IMODE(Path(row["path"]).stat().st_mode), 0o600)
        self.shelf.stop()
        self.shelf.start()
        row = self.shelf.rows()[2]
        self.assertEqual(Path(row["path"]).read_bytes(), payload)
        self.assertEqual(outside.read_text(), "source stays untouched")

    def test_symlinks_remain_references_and_owned_symlinks_are_rejected(self):
        self.shelf.start()
        target = self.shelf.user_file("original.txt")
        link = target.with_name("alias.txt")
        link.symlink_to(target)
        dangling = target.with_name("dangling.txt")
        dangling.symlink_to(target.with_name("absent.txt"))
        handoff = self.shelf.root / "arcade/handoff" / uuid.uuid4().hex
        handoff.mkdir(mode=0o700, parents=True)
        owned_link = handoff / "alias.txt"
        owned_link.symlink_to(target)
        with LinkClient(self.shelf.endpoint) as c:
            self.assert_add(c.invoke("shelf.add", [{"type": "file/any[]", "paths": [str(target), str(link), str(dangling)]}]), 3)
            self.assert_error("unsupported_input", lambda: c.invoke("shelf.add", [{"type": "file/text", "path": str(owned_link), "owner": "arcade.lens"}]))
        self.assertEqual([r["path"] for r in self.shelf.rows()], [str(target), str(link), str(dangling)])
        self.assertTrue(link.is_symlink())
        self.assertTrue(dangling.is_symlink())

    def test_disabled_integration_retains_standalone_lifecycle(self):
        self.shelf.start()
        self.shelf.stop()
        self.shelf.set_link_enabled_while_stopped(False)
        self.shelf.start(enabled=False)
        manifest = json.loads(self.shelf.manifest.read_text())
        self.assertFalse(manifest["settings"]["linkEnabled"])
        self.assertEqual(manifest["actions"], [])
        self.assertFalse(self.shelf.endpoint.exists())
        with LinkClient(self.shelf.control) as c:
            self.assertEqual(c.call("app.status")["id"], "arcade.shelf")
        self.shelf.stop()
        self.shelf.set_link_enabled_while_stopped(True)
        self.shelf.start()
        with LinkClient(self.shelf.endpoint) as c:
            self.assertEqual(len(c.call("describe")["actions"]), 3)

    def test_picker_is_explicit_cancellable_and_recovers_from_caller_death(self):
        self.shelf.start()
        with LinkClient(self.shelf.endpoint) as c:
            self.assert_add(c.invoke("shelf.add", [{"type": "text/plain", "text": "private collection item"}]), 1)
            job = c.invoke("shelf.pick", options={"multiple": True, "types": ["text/plain"]})
            self.assertEqual(set(job), {"job"}, "Opening picker must disclose no content")
            with LinkClient(self.shelf.endpoint) as other:
                self.assert_error("busy", lambda: other.invoke("shelf.pick"))
            self.assertTrue(c.call("job.cancel", job)["cancelled"])
            done = c.job_done(job["job"])
            self.assertEqual(done["status"], "cancelled")
            self.assertEqual(done["outputs"], [])
            self.assertEqual(done["error"]["code"], "cancelled")
        dying = LinkClient(self.shelf.endpoint)
        dying.invoke("shelf.pick")
        dying.close()
        deadline = time.monotonic() + 3
        with LinkClient(self.shelf.endpoint) as observer:
            while observer.call("app.status")["busy"]:
                self.assertLess(time.monotonic(), deadline, "Disconnected picker was not cancelled")
                time.sleep(0.02)
            next_job = observer.invoke("shelf.pick")
            self.assertTrue(observer.call("job.cancel", next_job)["cancelled"])
            self.assertEqual(observer.job_done(next_job["job"])["outputs"], [])
        self.assertEqual(len(self.shelf.rows()), 1)

    def test_crash_recovery_preserves_committed_additions_and_replaces_endpoint(self):
        self.shelf.start()
        source = self.shelf.user_file("persist.txt")
        c = LinkClient(self.shelf.endpoint)
        self.assert_add(c.invoke("shelf.add", [{"type": "file/text", "path": str(source)}]), 1)
        old_pid = self.shelf.process.pid
        self.shelf.kill()
        with self.assertRaises((OSError, ConnectionError, TimeoutError)):
            c.call("app.status")
        c.close()
        self.shelf.start()
        with LinkClient(self.shelf.endpoint) as c:
            self.assertNotEqual(c.call("app.status")["pid"], old_pid)
            self.assert_add(c.invoke("shelf.add", [{"type": "file/text", "path": str(source)}]), 0, ["duplicate"])
        self.assertEqual(len(self.shelf.rows()), 1)
        with sqlite3.connect(self.shelf.database) as db:
            self.assertEqual(db.execute("PRAGMA quick_check").fetchone()[0], "ok")
        self.assertEqual(source.read_text(), "source stays untouched")


def main() -> int:
    global BINARY, KEEP_TEMP
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=BINARY)
    parser.add_argument("--keep-temp", action="store_true", help="Retain isolated profiles and process logs")
    args, unittest_args = parser.parse_known_args()
    BINARY = args.binary.resolve()
    KEEP_TEMP = args.keep_temp
    if not BINARY.is_file():
        parser.error(f"Executable does not exist: {BINARY}")
    suite = unittest.TestLoader().loadTestsFromTestCase(ShelfProcessTests)
    if unittest_args:
        suite = unittest.TestLoader().loadTestsFromNames(unittest_args, module=sys.modules[__name__])
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())

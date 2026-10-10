#!/usr/bin/env python3
"""Measure the real resident process in an isolated profile (Linux /proc).

    python3 scripts/measure.py --binary build/arcade-shelf --output benchmarks/local.json

Startup is executable launch to successful authenticated app.status. Filesystem
caches are not flushed; repeated startups are not cold-disk measurements. Warm
add latency excludes connection/authentication and includes SQLite commit. CPU
uses the target process's own utime + stime, including threads and excluding
child counters; process exit/PID replacement invalidates the sample.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
import hashlib
import os
from pathlib import Path
import platform
import statistics
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
from process_smoke import IsolatedShelf, LinkClient  # noqa: E402


def proc_sample(pid: int) -> dict:
    stat_text = Path(f"/proc/{pid}/stat").read_text()
    # comm (field 2) can contain spaces and ')' characters. Parse after its final ')'.
    fields = stat_text[stat_text.rfind(")") + 2:].split()
    ticks = int(fields[11]) + int(fields[12])  # fields 14/15 only; never cutime/cstime
    start_ticks = int(fields[19])  # field 22 detects PID reuse
    status = Path(f"/proc/{pid}/status").read_text()
    values = dict(line.split(":", 1) for line in status.splitlines() if ":" in line)
    rss_kib = int(values["VmRSS"].split()[0])
    return {"cpu_ticks": ticks, "start_ticks": start_ticks, "rss_kib": rss_kib,
            "threads": int(values["Threads"].strip())}


def summary(values: list[float]) -> dict:
    ordered = sorted(values)
    # Nearest-rank p95, useful even for a small startup sample.
    import math
    return {"count": len(values), "min": min(values), "median": statistics.median(values),
            "p95": ordered[max(0, math.ceil(len(values) * .95) - 1)], "max": max(values)}


def require_add(result: dict, expected_added: int, expected_duplicates: int = 0):
    if "job" in result:
        raise AssertionError("shelf.add acknowledged an asynchronous job, not persisted content")
    data = result["data"]
    if len(data["added"]) != expected_added:
        raise AssertionError(f"Unexpected added count: {result}")
    if len(data["skipped"]) != expected_duplicates or any(x["reason"] != "duplicate" for x in data["skipped"]):
        raise AssertionError(f"Unexpected skips: {result}")


def run_one(binary: Path, cycles: int, idle_seconds: float, keep: bool) -> dict:
    shelf = IsolatedShelf(binary, keep=keep)
    try:
        binary_sha256 = hashlib.sha256(binary.read_bytes()).hexdigest()
        startup_ms = shelf.start()
        pid = shelf.process.pid
        with LinkClient(shelf.endpoint) as client:
            # Import one first item before warm measurements so initial SQLite work
            # does not disappear into an unlabelled warm sample.
            first = shelf.user_file("warm-up.txt")
            require_add(client.invoke("shelf.add", [{"type": "file/text", "path": str(first)}]), 1)
            rss_warm = proc_sample(pid)["rss_kib"]
            latencies, duplicate_latencies = [], []
            for index in range(cycles):
                source = shelf.user_file(f"staged-{index:04d}.txt")
                request = [{"type": "file/text", "path": str(source)}]
                started = time.perf_counter_ns()
                result = client.invoke("shelf.add", request)
                latencies.append((time.perf_counter_ns() - started) / 1_000_000)
                require_add(result, 1)
                started = time.perf_counter_ns()
                duplicate = client.invoke("shelf.add", request)
                duplicate_latencies.append((time.perf_counter_ns() - started) / 1_000_000)
                require_add(duplicate, 0, 1)
            if len(shelf.rows()) != cycles + 1:
                raise AssertionError("Repeated adds/duplicates changed persisted item count")
            rss_after = proc_sample(pid)["rss_kib"]
            # The capsule is background/collapsed, and no IPC is sent during this
            # interval. /proc reads occur only before and after the idle sample.
            time.sleep(1.0)  # Let the add pulse/transition finish before steady-state sampling.
            before = proc_sample(pid)
            idle_started = time.perf_counter()
            time.sleep(idle_seconds)
            actual_idle = time.perf_counter() - idle_started
            after = proc_sample(pid)
            if shelf.process.poll() is not None or before["start_ticks"] != after["start_ticks"]:
                raise AssertionError("Shelf exited or PID changed during CPU measurement")
            delta_ticks = after["cpu_ticks"] - before["cpu_ticks"]
            if delta_ticks < 0:
                raise AssertionError("CPU counters went backwards; measurement is invalid")
            cpu_seconds = delta_ticks / os.sysconf("SC_CLK_TCK")
            status = client.call("app.status")
            activate_started = time.perf_counter_ns()
            activation = client.call("app.activate")
            activation_ms = (time.perf_counter_ns() - activate_started) / 1_000_000
            if activation != {"activated": True}:
                raise AssertionError("Shelf did not accept native activation")
            if status["pid"] != pid:
                raise AssertionError("Wrong resident instance measured")
            return {"binary_sha256": binary_sha256, "startup_to_authenticated_status_ms": startup_ms,
                    "warm_add_ms": summary(latencies), "duplicate_add_ms": summary(duplicate_latencies),
                    "activate_acknowledgement_ms": activation_ms,
                    "rss_kib_after_warmup": rss_warm, "rss_kib_after_repeated_use": rss_after,
                    "rss_kib_after_idle": after["rss_kib"], "threads_after_idle": after["threads"],
                    "idle_wall_seconds": actual_idle, "idle_cpu_seconds": cpu_seconds,
                    "idle_cpu_percent_one_core": cpu_seconds / actual_idle * 100,
                    "persisted_items": cycles + 1, "repeated_add_duplicate_cycles": cycles}
    finally:
        shelf.close()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=ROOT / "build/arcade-shelf")
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--cycles", type=int, default=100)
    parser.add_argument("--idle-seconds", type=float, default=5.0)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--keep-temp", action="store_true")
    args = parser.parse_args()
    if not sys.platform.startswith("linux") or not Path("/proc/self/stat").is_file():
        parser.error("RSS/CPU runner requires Linux /proc; other platforms need native measurements")
    if args.runs < 1 or args.cycles < 1 or args.idle_seconds < 1 or args.idle_seconds > 60:
        parser.error("Use positive runs/cycles and idle interval between 1 and 60 seconds")
    binary = args.binary.resolve()
    if not binary.is_file():
        parser.error(f"Executable does not exist: {binary}")
    samples = []
    for run in range(args.runs):
        print(f"Measuring resident process {run + 1}/{args.runs}…", file=sys.stderr, flush=True)
        samples.append(run_one(binary, args.cycles, args.idle_seconds, args.keep_temp))
    build_type = "unknown"
    cache = binary.parent / "CMakeCache.txt"
    if cache.is_file():
        for line in cache.read_text(errors="replace").splitlines():
            if line.startswith("CMAKE_BUILD_TYPE:STRING="):
                build_type = line.partition("=")[2]
    report = {"build_type": build_type, "measured_at": datetime.now(timezone.utc).isoformat(), "binary": str(binary),
              "platform": platform.platform(), "python": platform.python_version(),
              "qt_platform": os.environ.get("QT_QPA_PLATFORM", "offscreen"),
              "render_backend": "software", "filesystem_caches_flushed": False,
              "startup_definition": "process spawn to authenticated app.status on public Link endpoint",
              "warm_add_definition": "same-connection invoke to synchronous SQLite committed response",
              "cpu_definition": "target process utime+stime delta; threads included, children excluded",
              "activation_definition": "app.activate request acknowledgement; excludes visible compositor/frame latency",
              "samples": samples,
              "startup_ms": summary([x["startup_to_authenticated_status_ms"] for x in samples])}
    output = json.dumps(report, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        temporary = args.output.with_name(args.output.name + ".tmp")
        temporary.write_text(output)
        temporary.replace(args.output)
    print(output, end="")
    return 0


if __name__ == "__main__":
    sys.exit(main())

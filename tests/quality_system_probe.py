#!/usr/bin/env python3
"""Launch/measure/terminate probe for the macOS Fovelle bundle.

The process metrics are intentionally collected with platform tools rather than
instrumenting application code.  CPU/RSS/handles are process-level observations;
disk and network counters are host-level observations and are labelled as such in
the output so they are not confused with per-process accounting.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import shutil
import signal
import subprocess
import sys
import time
from pathlib import Path
from statistics import mean


THRESHOLDS = {
    "startup_average_seconds": 2.0,
    "startup_p99_seconds": 3.0,
    "startup_max_seconds": 5.0,
    "throughput_runs_per_second": 0.5,
    "cpu_peak_percent_normalized": 100.0,
    "rss_peak_kib": 512 * 1024,
    "disk_space_used_percent": 95.0,
    "disk_io_peak_mb_per_second": 500.0,
    "open_handles": 512,
    "network_sockets": 4,
}


def command(*args: str) -> str:
    result = subprocess.run(args, text=True, capture_output=True, check=False)
    return result.stdout


def process_sample(pid: int) -> dict | None:
    output = command("ps", "-o", "%cpu=,rss=", "-p", str(pid)).strip()
    if not output:
        return None
    parts = output.split()
    if len(parts) < 2:
        return None
    try:
        return {"cpu_percent": float(parts[0]), "rss_kib": int(float(parts[1]))}
    except ValueError:
        return None


def lsof_sample(pid: int) -> tuple[int | None, int | None]:
    output = subprocess.run(
        ["lsof", "-p", str(pid)], text=True, capture_output=True, check=False
    ).stdout
    if not output:
        return None, None
    lines = output.splitlines()
    handles = max(0, len(lines) - 1)
    network_output = subprocess.run(
        ["lsof", "-a", "-p", str(pid), "-i", "-n", "-P"],
        text=True,
        capture_output=True,
        check=False,
    ).stdout
    network_lines = network_output.splitlines()
    sockets = max(0, len(network_lines) - 1) if network_lines else 0
    return handles, sockets


def disk_usage_percent(path: Path) -> float:
    usage = shutil.disk_usage(path)
    return usage.used * 100.0 / usage.total if usage.total else 0.0


def netstat_bytes() -> dict[str, int]:
    output = command("netstat", "-ib")
    received = 0
    sent = 0
    for line in output.splitlines()[1:]:
        parts = line.split()
        if len(parts) < 10:
            continue
        try:
            received += int(parts[6])
            sent += int(parts[9])
        except (ValueError, IndexError):
            continue
    return {"received_bytes": received, "sent_bytes": sent}


def iostat_sample() -> dict:
    output = command("iostat", "-d", "-w", "1", "-c", "2")
    candidates: list[float] = []
    for line in output.splitlines():
        parts = line.split()
        try:
            numbers = [float(part) for part in parts]
        except ValueError:
            continue
        if len(numbers) >= 3:
            # `iostat -d` emits KB/t, transfers/s, and MB/s per disk.
            candidates.append(sum(numbers[2::3]))
    return {
        "disk_mb_per_second": max(candidates) if candidates else None,
        "raw_tail": output[-1000:],
    }


def percentile99(values: list[float]) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    index = max(0, math.ceil(len(ordered) * 0.99) - 1)
    return ordered[index]


def launch_probe(app: Path, image: Path, hold_seconds: float) -> dict:
    started = time.perf_counter()
    process = subprocess.Popen(
        [str(app), str(image)],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        start_new_session=True,
        env={**os.environ, "QT_QPA_PLATFORM": "cocoa"},
    )
    startup_seconds: float | None = None
    samples: list[dict] = []
    handle_samples: list[int] = []
    socket_samples: list[int] = []
    deadline = started + max(hold_seconds, 0.2)
    while time.perf_counter() < deadline:
        if process.poll() is not None:
            break
        sample = process_sample(process.pid)
        if sample is not None:
            startup_seconds = startup_seconds or time.perf_counter() - started
            samples.append(sample)
            handles, sockets = lsof_sample(process.pid)
            if handles is not None:
                handle_samples.append(handles)
            if sockets is not None:
                socket_samples.append(sockets)
        time.sleep(0.05)

    if process.poll() is None:
        process.send_signal(signal.SIGTERM)
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=3)
    stderr = process.stderr.read().decode(errors="replace") if process.stderr else ""
    ended = time.perf_counter()
    return {
        "startup_seconds": startup_seconds,
        "elapsed_seconds": ended - started,
        "terminated": True,
        "return_code": process.returncode,
        "stderr": stderr[-2000:],
        "samples": samples,
        "peak_cpu_percent": max((item["cpu_percent"] for item in samples), default=None),
        "peak_rss_kib": max((item["rss_kib"] for item in samples), default=None),
        "peak_handles": max(handle_samples, default=None),
        "peak_network_sockets": max(socket_samples, default=0),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--app", type=Path, required=True)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument("--hold-seconds", type=float, default=0.8)
    parser.add_argument("--output", type=Path, default=None)
    args = parser.parse_args()

    app = args.app.resolve()
    image = args.image.resolve()
    repo = args.repo.resolve()
    if not app.is_file() or not image.is_file() or args.runs < 1:
        print("app, image, and runs must be valid", file=sys.stderr)
        return 2

    before_network = netstat_bytes()
    before_iostat = iostat_sample()
    runs = [launch_probe(app, image, args.hold_seconds) for _ in range(args.runs)]
    after_iostat = iostat_sample()
    after_network = netstat_bytes()

    startup = [run["startup_seconds"] for run in runs if run["startup_seconds"] is not None]
    cpu = [run["peak_cpu_percent"] for run in runs if run["peak_cpu_percent"] is not None]
    rss = [run["peak_rss_kib"] for run in runs if run["peak_rss_kib"] is not None]
    handles = [run["peak_handles"] for run in runs if run["peak_handles"] is not None]
    sockets = [run["peak_network_sockets"] for run in runs]
    elapsed = sum(run["elapsed_seconds"] for run in runs)
    disk_io_values = [
        value
        for value in (before_iostat["disk_mb_per_second"], after_iostat["disk_mb_per_second"])
        if value is not None
    ]
    disk_space = disk_usage_percent(repo)
    logical_cpus = os.cpu_count() or 1
    raw_cpu_peak = max(cpu, default=None)
    metrics = {
        "startup_average_seconds": mean(startup) if startup else None,
        "startup_p99_seconds": percentile99(startup),
        "startup_max_seconds": max(startup, default=None),
        "throughput_runs_per_second": len(runs) / elapsed if elapsed else None,
        "cpu_peak_percent_process_raw": raw_cpu_peak,
        "cpu_peak_percent_normalized": raw_cpu_peak / logical_cpus if raw_cpu_peak is not None else None,
        "logical_cpu_count": logical_cpus,
        "rss_peak_kib": max(rss, default=None),
        "open_handles_peak": max(handles, default=None),
        "network_sockets_peak": max(sockets, default=None),
        "disk_space_used_percent": disk_space,
        "disk_io_peak_mb_per_second_host_observation": max(disk_io_values, default=None),
        "network_bytes_delta_host_observation": {
            "received_bytes": after_network["received_bytes"] - before_network["received_bytes"],
            "sent_bytes": after_network["sent_bytes"] - before_network["sent_bytes"],
        },
    }

    pass_flags = {
        "S-01 all runs started": len(startup) == len(runs),
        "S-02 startup average": metrics["startup_average_seconds"] is not None and metrics["startup_average_seconds"] <= THRESHOLDS["startup_average_seconds"],
        "S-03 startup p99": metrics["startup_p99_seconds"] is not None and metrics["startup_p99_seconds"] <= THRESHOLDS["startup_p99_seconds"],
        "S-04 startup max": metrics["startup_max_seconds"] is not None and metrics["startup_max_seconds"] <= THRESHOLDS["startup_max_seconds"],
        "S-05 throughput": metrics["throughput_runs_per_second"] is not None and metrics["throughput_runs_per_second"] >= THRESHOLDS["throughput_runs_per_second"],
        "S-06 CPU": metrics["cpu_peak_percent_normalized"] is not None and metrics["cpu_peak_percent_normalized"] <= THRESHOLDS["cpu_peak_percent_normalized"],
        "S-07 memory": metrics["rss_peak_kib"] is not None and metrics["rss_peak_kib"] <= THRESHOLDS["rss_peak_kib"],
        "S-08 storage space": metrics["disk_space_used_percent"] <= THRESHOLDS["disk_space_used_percent"],
        "S-09 storage I/O host observation": metrics["disk_io_peak_mb_per_second_host_observation"] is not None and metrics["disk_io_peak_mb_per_second_host_observation"] <= THRESHOLDS["disk_io_peak_mb_per_second"],
        "S-10 handles": metrics["open_handles_peak"] is not None and metrics["open_handles_peak"] <= THRESHOLDS["open_handles"],
        "S-11 network sockets": metrics["network_sockets_peak"] is not None and metrics["network_sockets_peak"] <= THRESHOLDS["network_sockets"],
    }
    result = {
        "kind": "system",
        "app": str(app),
        "image": str(image),
        "runs": runs,
        "metrics": metrics,
        "thresholds": THRESHOLDS,
        "iostat": {"before": before_iostat, "after": after_iostat},
        "network_counters": {"before": before_network, "after": after_network},
        "pass_flags": pass_flags,
        "passed": all(pass_flags.values()),
        "limitations": [
            "CPU, RSS, handles, and network socket counts are per-process samples.",
            "iostat and netstat byte counters are host-level observations and may include unrelated background activity.",
            "Process readiness is a launch-time proxy; image decode correctness is covered by the Qt unit/integration tests.",
        ],
    }
    output = args.output or repo / "reports" / "evidence" / "system.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())

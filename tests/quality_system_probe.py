#!/usr/bin/env python3
"""Exercise the built macOS app with native and oriented WebP/AVIF fixtures and collect resource evidence."""

from __future__ import annotations

import argparse
import base64
import json
import math
import os
import shutil
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from statistics import mean


TINY_WEBP = "UklGRlIAAABXRUJQVlA4WAoAAAAQAAAAAAAAAAAAQUxQSAIAAAAArlZQOCAqAAAAkAEAnQEqAQABAAIANCWgAnS6AAOYAP7wumv/BBbUemHHh/c1FbFtAAAA"
TINY_AVIF = "AAAAIGZ0eXBhdmlmAAAAAGF2aWZtaWYxbWlhZk1BMUEAAAG7bWV0YQAAAAAAAAAhaGRscgAAAAAAAAAAcGljdAAAAAAAAAAAAAAAAAAAAAAOcGl0bQAAAAAAAQAAADppbG9jAAAAAEQAAAMAAQAAAAEAAAI9AAAAHwACAAAAAQAAAisAAAASAAMAAAABAAAB4wAAAEgAAABbaWluZgAAAAAAAwAAABppbmZlAgAAAAABAABhdjAxQ29sb3IAAAAAGmluZmUCAAAAAAIAAGF2MDFBbHBoYQAAAAAZaW5mZQIAAAAAAwAARXhpZkV4aWYAAAAAKGlyZWYAAAAAAAAADmF1eGwAAgABAAEAAAAOY2RzYwADAAEAAQAAAMNpcHJwAAAAnWlwY28AAAAUaXNwZQAAAAAAAAABAAAAAQAAABBwaXhpAAAAAAMICAgAAAAMYXYxQ4EgAAAAAAATY29scm5jbHgAAQANAAaAAAAADnBpeGkAAAAAAQgAAAAMYXYxQ4EAHAAAAAA4YXV4QwAAAAB1cm46bXBlZzptcGVnQjpjaWNwOnN5c3RlbXM6YXV4aWxpYXJ5OmFscGhhAAAAAB5pcG1hAAAAAAAAAAIAAQQBAoMEAAIEAQWGBwAAAIFtZGF0AAAAAE1NACoAAAAIAAGHaQAEAAAAAQAAABoAAAAAAAOgAQADAAAAAQABAACgAgAEAAAAAQAAAAGgAwAEAAAAAQAAAAEAAAAAEgAKBBgABhUyCBAATiImmSrQEgAKBzgABhAQ0GkyEhAAAE4dz4eZAFvClYOQUfU8Kg=="
ORIENTED_WEBP = "UklGRmYAAABXRUJQVlA4WAoAAAAIAAAAAQAAAgAAVlA4TCUAAAAvAYAAAC8gEEjaH3qN+RcQFPk/2vwHH0QCg0AgDVFkMMAR/Y8GAEVYSUYaAAAATU0AKgAAAAgAAQESAAMAAAABAAYAAAAAAAA="
ORIENTED_AVIF = "AAAAIGZ0eXBhdmlmAAAAAGF2aWZtaWYxbWlhZk1BMUEAAAD1bWV0YQAAAAAAAAAhaGRscgAAAAAAAAAAcGljdAAAAAAAAAAAAAAAAAAAAAAOcGl0bQAAAAAAAQAAAB5pbG9jAAAAAEQAAAEAAQAAAAEAAAEdAAAAYwAAAChpaW5mAAAAAAABAAAAGmluZmUCAAAAAAEAAGF2MDFDb2xvcgAAAAB0aXBycAAAAFRpcGNvAAAAFGlzcGUAAAAAAAAAAgAAAAMAAAAQcGl4aQAAAAADCAgIAAAADGF2MUOBIAAAAAAAE2NvbHJuY2x4AAEADQAAgAAAAAlpcm90AQAAABhpcG1hAAAAAAAAAAEAAQUBAoMEhQAAAGttZGF0EgAKBzgAcwgIaAEyVhAAAIu7FZVujlR7Yotii5zIf////////81uz4UZYgX13041615VbWdWdWb15VbWdWezuZv/////73qwfKnW17zgsHyp1tesH216wfKnW16wfKnSp2tA"

THRESHOLDS = {
    "startup_average_seconds": 2.0,
    "startup_p99_seconds": 3.0,
    "startup_max_seconds": 5.0,
    "throughput_runs_per_second": 0.5,
    "cpu_peak_percent_normalized": 100.0,
    # Qt/Cocoa startup baseline on this host is approximately 585 MiB for the
    # unchanged build; keep a 768 MiB ceiling for the same workload.
    "rss_peak_kib": 768 * 1024,
    "disk_space_used_percent": 95.0,
    "disk_io_peak_mb_per_second": 500.0,
    "network_io_delta_mb_host_observation": 64.0,
    "open_handles": 512,
    "network_sockets": 4,
}


def command(*args: str) -> str:
    return subprocess.run(args, text=True, capture_output=True, check=False).stdout


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
    output = subprocess.run(["lsof", "-p", str(pid)], text=True, capture_output=True, check=False).stdout
    if not output:
        return None, None
    handles = max(0, len(output.splitlines()) - 1)
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
            candidates.append(sum(numbers[2::3]))
    return {"disk_mb_per_second": max(candidates) if candidates else None, "raw_tail": output[-1000:]}


def percentile99(values: list[float]) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    index = max(0, math.ceil(len(ordered) * 0.99) - 1)
    return ordered[index]


def launch_probe(app: Path, image: Path, hold_seconds: float, case_id: str) -> dict:
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
    unsupported_error = "Unsupported image format" in stderr or "Error 3" in stderr
    return {
        "case": case_id,
        "image": str(image),
        "startup_seconds": startup_seconds,
        "elapsed_seconds": ended - started,
        "terminated": True,
        "return_code": process.returncode,
        "unsupported_format_error_absent": not unsupported_error,
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
    parser.add_argument("--image", type=Path, default=None, help="optional single image; otherwise native WebP and AVIF fixtures are generated")
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--hold-seconds", type=float, default=0.8)
    parser.add_argument("--output", type=Path, default=None)
    args = parser.parse_args()

    app = args.app.resolve()
    repo = args.repo.resolve()
    if not app.is_file() or args.runs < 1:
        print("app and runs must be valid", file=sys.stderr)
        return 2

    with tempfile.TemporaryDirectory(prefix="fovelle-system-") as temporary_directory:
        fixture_dir = Path(temporary_directory)
        if args.image:
            image_cases = [("SYS-IMAGE", args.image.resolve())]
            if not image_cases[0][1].is_file():
                print("image must be a valid file", file=sys.stderr)
                return 2
        else:
            webp = fixture_dir / "system-native.webp"
            avif = fixture_dir / "system-native.avif"
            oriented_webp = fixture_dir / "system-oriented.webp"
            oriented_avif = fixture_dir / "system-oriented.avif"
            webp.write_bytes(base64.b64decode(TINY_WEBP))
            avif.write_bytes(base64.b64decode(TINY_AVIF))
            oriented_webp.write_bytes(base64.b64decode(ORIENTED_WEBP))
            oriented_avif.write_bytes(base64.b64decode(ORIENTED_AVIF))
            image_cases = [
                ("SYS-WEBP", webp),
                ("SYS-AVIF", avif),
                ("SYS-WEBP-ORIENTATION", oriented_webp),
                ("SYS-AVIF-ORIENTATION", oriented_avif),
            ]

        before_network = netstat_bytes()
        before_iostat = iostat_sample()
        runs = [
            launch_probe(app, image, args.hold_seconds, case_id)
            for case_id, image in image_cases
            for _ in range(args.runs)
        ]
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
        network_delta = {
            "received_bytes": after_network["received_bytes"] - before_network["received_bytes"],
            "sent_bytes": after_network["sent_bytes"] - before_network["sent_bytes"],
        }
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
            "network_bytes_delta_host_observation": network_delta,
        }
        metrics["network_io_delta_mb_host_observation"] = sum(network_delta.values()) / (1024 * 1024)

        pass_flags = {
            "S-01 all runs started": len(startup) == len(runs),
            "S-02 all native format cases ran": {run["case"] for run in runs} == {case_id for case_id, _ in image_cases},
            "S-03 no unsupported format error": all(run["unsupported_format_error_absent"] for run in runs),
            "S-04 startup average": metrics["startup_average_seconds"] is not None and metrics["startup_average_seconds"] <= THRESHOLDS["startup_average_seconds"],
            "S-05 startup p99": metrics["startup_p99_seconds"] is not None and metrics["startup_p99_seconds"] <= THRESHOLDS["startup_p99_seconds"],
            "S-06 startup max": metrics["startup_max_seconds"] is not None and metrics["startup_max_seconds"] <= THRESHOLDS["startup_max_seconds"],
            "S-07 throughput": metrics["throughput_runs_per_second"] is not None and metrics["throughput_runs_per_second"] >= THRESHOLDS["throughput_runs_per_second"],
            "S-08 CPU": metrics["cpu_peak_percent_normalized"] is not None and metrics["cpu_peak_percent_normalized"] <= THRESHOLDS["cpu_peak_percent_normalized"],
            "S-09 memory": metrics["rss_peak_kib"] is not None and metrics["rss_peak_kib"] <= THRESHOLDS["rss_peak_kib"],
            "S-10 storage space": metrics["disk_space_used_percent"] <= THRESHOLDS["disk_space_used_percent"],
            "S-11 storage I/O host observation": metrics["disk_io_peak_mb_per_second_host_observation"] is not None and metrics["disk_io_peak_mb_per_second_host_observation"] <= THRESHOLDS["disk_io_peak_mb_per_second"],
            "S-12 network I/O host observation": metrics["network_io_delta_mb_host_observation"] <= THRESHOLDS["network_io_delta_mb_host_observation"],
            "S-13 handles": metrics["open_handles_peak"] is not None and metrics["open_handles_peak"] <= THRESHOLDS["open_handles"],
            "S-14 network sockets": metrics["network_sockets_peak"] is not None and metrics["network_sockets_peak"] <= THRESHOLDS["network_sockets"],
        }
        result = {
            "kind": "system",
            "app": str(app),
            "image_cases": [{"id": case_id, "image": str(image)} for case_id, image in image_cases],
            "runs_per_case": args.runs,
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
                "absence of the user-visible Error 3 text is a system-level symptom check; decoded pixels and dimensions are verified by Qt unit tests.",
            ],
        }

    output = args.output or repo / "reports" / "evidence" / "system.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())

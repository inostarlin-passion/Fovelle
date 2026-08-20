#!/usr/bin/env python3
"""Exercise the compiled decoder against the two supplied real-world files."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import re
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def sample_record(path: Path) -> dict:
    return {
        "path": str(path),
        "bytes": path.stat().st_size,
        "sha256": sha256(path),
        "suffix": path.suffix,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--jpeg", type=Path, required=True)
    parser.add_argument("--raw", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    binary = args.binary.resolve()
    jpeg = args.jpeg.resolve()
    raw = args.raw.resolve()
    missing = [str(path) for path in (binary, jpeg, raw) if not path.is_file()]
    if missing:
        raise SystemExit(f"missing integration input: {missing}")

    environment = {
        "QT_QPA_PLATFORM": "cocoa",
        "QT_FATAL_WARNINGS": "1",
        "QTEST_FUNCTION_TIMEOUT": "30000",
        "FOVELLE_TEST_SUITE": "HDRSampleTests",
        "FOVELLE_HDR_JPEG_SAMPLE": str(jpeg),
        "FOVELLE_HDR_RAW_SAMPLE": str(raw),
    }
    command = [str(binary), "-o", "-,txt"]
    started = time.perf_counter()
    result = subprocess.run(
        command,
        text=True,
        capture_output=True,
        env={**os.environ, **environment},
        timeout=45,
        check=False,
    )
    elapsed = time.perf_counter() - started
    output = result.stdout + result.stderr
    raw_peak_match = re.search(r"FOVELLE_RAW_PEAK sdr_max=([0-9.]+) hdr_max=([0-9.]+)", output)
    jpeg_peak_match = re.search(r"FOVELLE_JPEG_PEAK sdr_max=([0-9.]+) hdr_max=([0-9.]+)", output)
    raw_pixel_statistics = {
        "sdr_maximum_component": float(raw_peak_match.group(1)) if raw_peak_match else None,
        "hdr_maximum_component": float(raw_peak_match.group(2)) if raw_peak_match else None,
    }
    jpeg_pixel_statistics = {
        "sdr_maximum_component": float(jpeg_peak_match.group(1)) if jpeg_peak_match else None,
        "hdr_maximum_component": float(jpeg_peak_match.group(2)) if jpeg_peak_match else None,
    }
    expected = (
        ("IT-HDR-GAINMAP-JPEG", "testGainMapJPEGCreatesNativeHDRGraph"),
        ("IT-HDR-GAINMAP-JPEG-PEAK", "testGainMapJPEGHDRContainsAboveSDRValues"),
        ("IT-HDR-RAW-DNG", "testDNGCreatesNativeRawEDRGraph"),
        ("IT-HDR-RAW-DNG-PEAK", "testDNGRawEDRContainsAboveSDRValues"),
    )
    cases = []
    for identifier, function in expected:
        marker = f"PASS   : HDRSampleTests::{function}()"
        item = {
            "id": identifier,
            "test_code": f"tests/tst_qviewtests.cpp::HDRSampleTests::{function}",
            "status": "passed" if marker in output else "failed",
            "evidence_marker": marker,
        }
        if identifier == "IT-HDR-GAINMAP-JPEG-PEAK":
            item["observations"] = jpeg_pixel_statistics
            if not jpeg_peak_match:
                item["status"] = "failed"
        elif identifier == "IT-HDR-RAW-DNG-PEAK":
            item["observations"] = raw_pixel_statistics
            if not raw_peak_match:
                item["status"] = "failed"
        cases.append(item)
    totals_match = re.search(
        r"Totals: (\d+) passed, (\d+) failed, (\d+) skipped, (\d+) blacklisted", output
    )
    totals = {
        "passed": int(totals_match.group(1)) if totals_match else 0,
        "failed": int(totals_match.group(2)) if totals_match else -1,
        "skipped": int(totals_match.group(3)) if totals_match else -1,
        "blacklisted": int(totals_match.group(4)) if totals_match else -1,
    }
    passed = (
        result.returncode == 0
        and totals == {"passed": 6, "failed": 0, "skipped": 0, "blacklisted": 0}
        and all(item["status"] == "passed" for item in cases)
    )
    record = {
        "schema_version": "1.0",
        "kind": "integration-test-evidence",
        "release": "v0.1.4",
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "host": {"platform": platform.platform(), "python": platform.python_version()},
        "command": command,
        "environment": environment,
        "samples": {"jpeg": sample_record(jpeg), "raw": sample_record(raw)},
        "elapsed_seconds": elapsed,
        "return_code": result.returncode,
        "totals": totals,
        "cases": cases,
        "output": output,
        "summary": {
            "total": len(cases),
            "passed": sum(item["status"] == "passed" for item in cases),
            "failed": sum(item["status"] != "passed" for item in cases),
        },
        "passed": passed,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(record, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"kind": record["kind"], "summary": record["summary"], "passed": passed}, ensure_ascii=False))
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())

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
import math
from datetime import datetime, timezone
from pathlib import Path

from PIL import Image, ImageFilter


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


def edge_cosine_similarity(lhs: Path, rhs: Path) -> float:
    vectors = []
    for path in (lhs, rhs):
        with Image.open(path) as source:
            image = source.convert("L").resize((240, 180), Image.Resampling.LANCZOS)
        vectors.append([value / 255.0 for value in image.filter(ImageFilter.FIND_EDGES).getdata()])
    left, right = vectors
    dot = sum(a * b for a, b in zip(left, right))
    norm = math.sqrt(sum(value * value for value in left) * sum(value * value for value in right))
    return dot / norm if norm > 0 else 0.0


def mean_absolute_rgb_error(lhs: Path, rhs: Path) -> float:
    with Image.open(lhs) as left_source, Image.open(rhs) as right_source:
        left = left_source.convert("RGB")
        right = right_source.convert("RGB").resize(left.size, Image.Resampling.LANCZOS)
        differences = [
            abs(a - b)
            for left_pixel, right_pixel in zip(left.getdata(), right.getdata())
            for a, b in zip(left_pixel, right_pixel)
        ]
    return sum(differences) / len(differences) if differences else math.inf


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--jpeg", type=Path, required=True)
    parser.add_argument("--raw", type=Path, required=True)
    parser.add_argument("--plain-dng", type=Path, required=True)
    parser.add_argument("--nef", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    binary = args.binary.resolve()
    jpeg = args.jpeg.resolve()
    raw = args.raw.resolve()
    plain_dng = args.plain_dng.resolve()
    nef = args.nef.resolve()
    missing = [
        str(path) for path in (binary, jpeg, raw, plain_dng, nef)
        if not path.is_file()
    ]
    if missing:
        raise SystemExit(f"missing integration input: {missing}")

    environment = {
        "QT_QPA_PLATFORM": "cocoa",
        "QT_FATAL_WARNINGS": "1",
        "QTEST_FUNCTION_TIMEOUT": "60000",
        "FOVELLE_TEST_SUITE": "HDRSampleTests",
        "FOVELLE_HDR_JPEG_SAMPLE": str(jpeg),
        "FOVELLE_HDR_RAW_SAMPLE": str(raw),
        "FOVELLE_HDR_PLAIN_DNG_SAMPLE": str(plain_dng),
        "FOVELLE_HDR_NEF_SAMPLE": str(nef),
    }
    command = [str(binary), "-o", "-,txt"]
    started = time.perf_counter()
    result = subprocess.run(
        command,
        text=True,
        capture_output=True,
        env={**os.environ, **environment},
        timeout=90,
        check=False,
    )
    elapsed = time.perf_counter() - started
    output = result.stdout + result.stderr
    raw_peak_match = re.search(r"FOVELLE_RAW_PEAK sdr_max=([0-9.]+) hdr_max=([0-9.]+)", output)
    raw_headroom_match = re.search(
        r"FOVELLE_RAW_HEADROOM metadata=([0-9.]+) measured=([0-9.]+)", output
    )
    jpeg_peak_match = re.search(r"FOVELLE_JPEG_PEAK sdr_max=([0-9.]+) hdr_max=([0-9.]+)", output)
    raw_pixel_statistics = {
        "sdr_maximum_component": float(raw_peak_match.group(1)) if raw_peak_match else None,
        "hdr_maximum_component": float(raw_peak_match.group(2)) if raw_peak_match else None,
    }
    raw_headroom_statistics = {
        "metadata_content_headroom": float(raw_headroom_match.group(1)) if raw_headroom_match else None,
        "measured_maximum_component": float(raw_headroom_match.group(2)) if raw_headroom_match else None,
    }
    jpeg_pixel_statistics = {
        "sdr_maximum_component": float(jpeg_peak_match.group(1)) if jpeg_peak_match else None,
        "hdr_maximum_component": float(jpeg_peak_match.group(2)) if jpeg_peak_match else None,
    }
    expected = (
        ("IT-HDR-GAINMAP-JPEG", "testGainMapJPEGCreatesNativeHDRGraph"),
        ("IT-HDR-GAINMAP-JPEG-PEAK", "testGainMapJPEGHDRContainsAboveSDRValues"),
        ("IT-HDR-RAW-DNG", "testDNGCreatesProcessedGainMapHDRGraph"),
        ("IT-HDR-RAW-DNG-PEAK", "testDNGProcessedGainMapContainsAboveSDRValues"),
        ("IT-HDR-RAW-DNG-HEADROOM-TAG", "testDNGGainMapHeadroomMatchesMetadataContract"),
        ("IT-HDR-RAW-DNG-REPEATABILITY", "testDNGProcessedGraphRepeatedFloatProbeIsStable"),
        ("IT-HDR-RAW-PLAIN-DNG", "testPlainDNGCreatesNativeRawEDRGraph"),
        ("IT-HDR-RAW-NEF", "testNEFCreatesNativeRawEDRGraph"),
        ("IT-HDR-RAW-NEF-REPEATABILITY", "testNEFRawRepeatedFloatProbeIsStable"),
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
        elif identifier == "IT-HDR-RAW-DNG-HEADROOM-TAG":
            item["observations"] = raw_headroom_statistics
            if not raw_headroom_match:
                item["status"] = "failed"
        cases.append(item)

    quality_directory = args.output.resolve().parent / "quicklook_quality"
    quality_directory.mkdir(parents=True, exist_ok=True)
    quicklook_png = quality_directory / f"{raw.name}.png"
    processed_preview_png = quality_directory / "dng_processed_preview.png"
    if quicklook_png.exists():
        quicklook_png.unlink()
    if processed_preview_png.exists():
        processed_preview_png.unlink()
    ql_command = ["qlmanage", "-t", "-s", "1024", "-o", str(quality_directory), str(raw)]
    ql_result = subprocess.run(ql_command, text=True, capture_output=True, timeout=30, check=False)
    probe_command = [
        "xcrun", "swift", str(Path(__file__).resolve().parent / "hdr_gain_map_probe.swift"),
        str(raw), "--preview-png", str(processed_preview_png),
    ]
    probe_result = subprocess.run(
        probe_command, text=True, capture_output=True, timeout=60, check=False
    )
    try:
        gain_map_probe = json.loads(probe_result.stdout) if probe_result.returncode == 0 else None
    except json.JSONDecodeError:
        gain_map_probe = None
    quality_files_exist = quicklook_png.is_file() and processed_preview_png.is_file()
    structure_similarity = (
        edge_cosine_similarity(processed_preview_png, quicklook_png)
        if quality_files_exist else 0.0
    )
    rgb_mae = (
        mean_absolute_rgb_error(processed_preview_png, quicklook_png)
        if quality_files_exist else math.inf
    )
    quality_case = {
        "id": "IT-HDR-DNG-QUICKLOOK-DETAIL",
        "test_code": "tests/hdr_quality_integration.py::edge_cosine_similarity",
        "status": "passed" if (
            ql_result.returncode == 0
            and probe_result.returncode == 0
            and quality_files_exist
            and gain_map_probe is not None
            and gain_map_probe.get("processed_preview_extent", {}).get("width") == 8064
            and gain_map_probe.get("gain_map_extent", {}).get("width") == 4032
            and structure_similarity >= 0.994
            and rgb_mae <= 3.0
        ) else "failed",
        "checks": {
            "quicklook_thumbnail_generated": ql_result.returncode == 0 and quicklook_png.is_file(),
            "production_processed_preview_generated": (
                probe_result.returncode == 0 and processed_preview_png.is_file()
            ),
            "full_resolution_preview_and_half_resolution_gain_map": (
                gain_map_probe is not None
                and gain_map_probe.get("processed_preview_extent", {}).get("width") == 8064
                and gain_map_probe.get("processed_preview_extent", {}).get("height") == 6048
                and gain_map_probe.get("gain_map_extent", {}).get("width") == 4032
                and gain_map_probe.get("gain_map_extent", {}).get("height") == 3024
            ),
            "edge_structure_similarity_at_least_0_994": structure_similarity >= 0.994,
            "mean_absolute_rgb_error_at_most_3": rgb_mae <= 3.0,
        },
        "observations": {
            "edge_cosine_similarity": structure_similarity,
            "mean_absolute_rgb_error": rgb_mae,
            "production_preview": (
                sample_record(processed_preview_png) if processed_preview_png.is_file() else None
            ),
            "quicklook_thumbnail": (
                sample_record(quicklook_png) if quicklook_png.is_file() else None
            ),
            "gain_map_probe": gain_map_probe,
            "quicklook_command": ql_command,
            "quicklook_output": ql_result.stdout + ql_result.stderr,
            "probe_command": probe_command,
            "probe_output": probe_result.stdout + probe_result.stderr,
        },
    }
    cases.append(quality_case)
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
        and totals == {"passed": 11, "failed": 0, "skipped": 0, "blacklisted": 0}
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
        "samples": {
            "jpeg": sample_record(jpeg),
            "raw": sample_record(raw),
            "plain_dng": sample_record(plain_dng),
            "nef": sample_record(nef),
        },
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

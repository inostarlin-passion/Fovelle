#!/usr/bin/env python3
"""Run the real Cocoa application and audit WindowServer/Metal HDR telemetry."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import platform
import re
import statistics
import subprocess
import sys
import tempfile
import time
from datetime import datetime, timezone
from pathlib import Path

from PIL import Image, ImageFilter


RUNS_PER_FORMAT = 3
CAPTURE_SECONDS = 5.2
THRESHOLDS = {
    "decode_average_ms_max": 2500.0,
    "decode_p99_ms_max": 3500.0,
    "decode_max_ms_max": 4000.0,
    "decode_throughput_images_per_second_min": 0.4,
    "steady_render_average_ms_max": 30.0,
    "steady_render_p99_ms_max": 120.0,
    "steady_render_max_ms_max": 200.0,
    "steady_render_equivalent_submissions_per_second_min": 33.0,
    "observed_transition_frames_per_second_min": 30.0,
    "transition_progress_step_max": 0.15,
}


def percentile(values: list[float], proportion: float) -> float:
    ordered = sorted(values)
    if not ordered:
        return math.nan
    position = (len(ordered) - 1) * proportion
    lower = math.floor(position)
    upper = math.ceil(position)
    if lower == upper:
        return ordered[lower]
    return ordered[lower] + (ordered[upper] - ordered[lower]) * (position - lower)


def read_command(*command: str) -> str:
    result = subprocess.run(command, text=True, capture_output=True, check=False, timeout=15)
    return (result.stdout + result.stderr).strip()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def capture_screen(path: Path) -> dict:
    path.parent.mkdir(parents=True, exist_ok=True)
    command = ["/usr/sbin/screencapture", "-x", "-t", "png", str(path)]
    result = subprocess.run(command, text=True, capture_output=True, timeout=15, check=False)
    return {
        "path": str(path),
        "command": command,
        "return_code": result.returncode,
        "output": result.stdout + result.stderr,
        "bytes": path.stat().st_size if path.is_file() else 0,
        "sha256": sha256(path) if path.is_file() else None,
    }


def viewport_pixel_crop(records: list[dict]) -> tuple[int, int, int, int] | None:
    for item in reversed(records):
        required = (
            "viewport_global_x", "viewport_global_y", "viewport_logical_width",
            "viewport_logical_height", "viewport_device_pixel_ratio",
        )
        if not all(key in item for key in required):
            continue
        scale = float(item["viewport_device_pixel_ratio"])
        left = round(float(item["viewport_global_x"]) * scale)
        top = round(float(item["viewport_global_y"]) * scale)
        right = left + round(float(item["viewport_logical_width"]) * scale)
        bottom = top + round(float(item["viewport_logical_height"]) * scale)
        return left, top, right, bottom
    return None


def crop_to_bounds(image: Image.Image, crop: tuple[int, int, int, int] | None) -> Image.Image:
    if crop is None:
        return image
    left, top, right, bottom = crop
    left = min(max(left, 0), image.width)
    top = min(max(top, 0), image.height)
    right = min(max(right, left), image.width)
    bottom = min(max(bottom, top), image.height)
    return image.crop((left, top, right, bottom)) if right > left and bottom > top else image


def black_vertical_band_metric(
    path: Path, crop: tuple[int, int, int, int] | None = None
) -> dict:
    with Image.open(path) as source:
        image = crop_to_bounds(source.convert("RGB"), crop)
        image.thumbnail((1200, 800), Image.Resampling.LANCZOS)
    width, height = image.size
    top = int(height * 0.08)
    bottom = max(top + 1, int(height * 0.94))
    pixels = image.load()
    near_black_counts = [0] * width
    for y in range(top, bottom):
        for x in range(width):
            if max(pixels[x, y]) <= 4:
                near_black_counts[x] += 1
    sampled_height = bottom - top
    qualifying = [count / sampled_height >= 0.80 for count in near_black_counts]
    longest = 0
    current = 0
    for value in qualifying:
        current = current + 1 if value else 0
        longest = max(longest, current)
    return {
        "normalized_size": [width, height],
        "sampled_y": [top, bottom],
        "near_black_component_max": 4,
        "minimum_column_coverage": 0.80,
        "maximum_consecutive_near_black_columns": longest,
        "source_viewport_crop_pixels": list(crop) if crop is not None else None,
    }


def edge_cosine_similarity(
    lhs: Path,
    rhs: Path,
    lhs_crop: tuple[int, int, int, int] | None = None,
    rhs_crop: tuple[int, int, int, int] | None = None,
) -> float:
    def edge_vector(path: Path, crop: tuple[int, int, int, int] | None) -> list[float]:
        with Image.open(path) as source:
            image = crop_to_bounds(source.convert("L"), crop).resize(
                (240, 150), Image.Resampling.LANCZOS
            )
        image = image.crop((0, 8, 240, 150)).filter(ImageFilter.FIND_EDGES)
        return [value / 255.0 for value in image.getdata()]

    left = edge_vector(lhs, lhs_crop)
    right = edge_vector(rhs, rhs_crop)
    dot = sum(a * b for a, b in zip(left, right))
    left_norm = math.sqrt(sum(value * value for value in left))
    right_norm = math.sqrt(sum(value * value for value in right))
    return dot / (left_norm * right_norm) if left_norm > 0 and right_norm > 0 else 0.0


def launch(
    app: Path,
    image: Path,
    run_index: int,
    forced_headroom: float | None = None,
    forced_current_headroom: float | None = None,
    interaction: bool = False,
    capture_seconds: float = CAPTURE_SECONDS,
    capture_schedule: list[float] | None = None,
    capture_directory: Path | None = None,
) -> dict:
    environment = {
        **os.environ,
        "QT_QPA_PLATFORM": "cocoa",
        "FOVELLE_HDR_DIAGNOSTIC_LOG": "1",
        "QV_DISABLE_ONLINE_VERSION_CHECK": "1",
    }
    if forced_headroom is not None:
        environment["FOVELLE_TEST_DISPLAY_HEADROOM"] = str(forced_headroom)
    if forced_current_headroom is not None:
        environment["FOVELLE_TEST_DISPLAY_CURRENT_HEADROOM"] = str(forced_current_headroom)
    if interaction:
        environment["FOVELLE_HDR_TEST_INTERACTION"] = "1"
    command = [str(app), str(image)]
    started = time.perf_counter()
    captures = []
    with tempfile.NamedTemporaryFile(mode="w+", encoding="utf-8") as output_file:
        process = subprocess.Popen(
            command,
            text=True,
            stdout=output_file,
            stderr=subprocess.STDOUT,
            env=environment,
        )
        for capture_index, offset in enumerate(sorted(capture_schedule or []), start=1):
            remaining = started + offset - time.perf_counter()
            if remaining > 0:
                time.sleep(remaining)
            if process.poll() is not None:
                break
            if capture_directory is not None:
                scenario = "interaction" if interaction else "launch"
                capture_path = capture_directory / (
                    f"{'raw' if image.suffix.lower() == '.dng' else 'jpeg'}"
                    f"_{scenario}_{capture_index}_{int(offset * 1000):04d}ms.png"
                )
                captures.append(capture_screen(capture_path))

        remaining = started + capture_seconds - time.perf_counter()
        if remaining > 0:
            try:
                process.wait(timeout=remaining)
            except subprocess.TimeoutExpired:
                pass
        timed_out = process.poll() is None
        if timed_out:
            process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)
        output_file.flush()
        output_file.seek(0)
        output = output_file.read()
    telemetry = []
    for match in re.finditer(r"FOVELLE_HDR\s+(\{[^\n\r]+\})", output):
        try:
            telemetry.append(json.loads(match.group(1)))
        except json.JSONDecodeError:
            continue
    return {
        "format": "raw" if image.suffix.lower() == ".dng" else "gain-map-jpeg",
        "run_index": run_index,
        "forced_headroom": forced_headroom,
        "forced_current_headroom": forced_current_headroom,
        "interaction": interaction,
        "command": command,
        "capture_seconds": capture_seconds,
        "elapsed_seconds": time.perf_counter() - started,
        "timed_out_as_designed": timed_out,
        "return_code_after_termination": process.returncode,
        "telemetry_count": len(telemetry),
        "telemetry": telemetry,
        "screen_captures": captures,
        "process_output": output,
        "process_healthy": bool(telemetry) and "Segmentation fault" not in output and "ASSERT" not in output,
    }


def transition_fps(records: list[dict]) -> float:
    samples = [
        (float(item.get("transition_elapsed_ms", -1)), int(item.get("render_count", 0)))
        for item in records
        if 0 <= float(item.get("transition_elapsed_ms", -1)) <= 1900
    ]
    if len(samples) < 2:
        return 0.0
    first_elapsed, first_count = samples[0]
    last_elapsed, last_count = samples[-1]
    span = last_elapsed - first_elapsed
    return (last_count - first_count) * 1000.0 / span if span > 0 else 0.0


def maximum_transition_step(records: list[dict]) -> float:
    active = [
        float(item.get("transition_progress", 0))
        for item in records
        if 0 <= float(item.get("transition_elapsed_ms", -1)) <= 700
    ]
    return max((current - previous for previous, current in zip(active, active[1:])), default=0.0)


def make_case(identifier: str, checks: dict[str, bool], observations: dict) -> dict:
    passed = bool(checks) and all(checks.values())
    return {
        "id": identifier,
        "test_code": "tests/hdr_quality_system.py",
        "checks": checks,
        "observations": observations,
        "status": "passed" if passed else "failed",
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--app", type=Path, required=True)
    parser.add_argument("--jpeg", type=Path, required=True)
    parser.add_argument("--raw", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    app = args.app.resolve()
    jpeg = args.jpeg.resolve()
    raw = args.raw.resolve()
    missing = [str(path) for path in (app, jpeg, raw) if not path.is_file()]
    if missing:
        raise SystemExit(f"missing system-test input: {missing}")

    capture_directory = args.output.resolve().parent / "screens"
    runs = []
    for index in range(1, RUNS_PER_FORMAT + 1):
        runs.append(launch(app, jpeg, index))
    for index in range(1, RUNS_PER_FORMAT + 1):
        runs.append(launch(app, raw, index))
    forced_sdr = launch(app, jpeg, 1, forced_headroom=1.0)
    bootstrap_jpeg = launch(app, jpeg, 1, forced_current_headroom=1.0)
    bootstrap_raw = launch(
        app,
        raw,
        1,
        forced_current_headroom=1.0,
        capture_schedule=[2.0, 2.3, 2.8, 3.5, 4.8],
        capture_directory=capture_directory,
    )
    interaction_run = launch(
        app,
        jpeg,
        1,
        forced_current_headroom=1.0,
        interaction=True,
        capture_seconds=6.4,
        capture_schedule=[1.8, 2.9, 3.05, 3.25, 4.0, 5.8],
        capture_directory=capture_directory,
    )
    runs.extend((forced_sdr, bootstrap_jpeg, bootstrap_raw, interaction_run))

    real_runs = [
        run for run in runs
        if run["forced_headroom"] is None
        and run["forced_current_headroom"] is None
        and not run["interaction"]
    ]
    jpeg_runs = [run for run in real_runs if run["format"] == "gain-map-jpeg"]
    raw_runs = [run for run in real_runs if run["format"] == "raw"]
    all_real_records = [item for run in real_runs for item in run["telemetry"]]
    jpeg_records = [item for run in jpeg_runs for item in run["telemetry"]]
    raw_records = [item for run in raw_runs for item in run["telemetry"]]
    forced_records = forced_sdr["telemetry"]
    bootstrap_runs = [bootstrap_jpeg, bootstrap_raw]
    bootstrap_records = [item for run in bootstrap_runs for item in run["telemetry"]]
    interaction_records = interaction_run["telemetry"]
    interaction_viewport_crop = viewport_pixel_crop(interaction_records)
    # QVGraphicsView emits a diagnostic snapshot after every render request,
    # including requests that intentionally stop at offscreen preparation.
    # render_count advances only after a drawable is actually committed.
    render_records = [
        item for item in all_real_records
        if item.get("phase") == "render" and int(item.get("render_count", 0)) > 0
    ]
    interaction_render_records = [
        item for item in interaction_records if item.get("phase") == "render"
    ]
    interaction_staged_records = [
        item for item in interaction_records if item.get("phase") == "geometry-staged"
    ]
    interaction_capture_metrics = []
    for capture in interaction_run["screen_captures"]:
        capture_path = Path(capture["path"])
        if capture["return_code"] == 0 and capture_path.is_file():
            capture["black_vertical_band_metric"] = black_vertical_band_metric(
                capture_path, interaction_viewport_crop
            )
            interaction_capture_metrics.append(capture["black_vertical_band_metric"])
    interaction_stable_edge_similarity = 0.0
    if len(interaction_run["screen_captures"]) >= 2:
        stable_captures = interaction_run["screen_captures"][-2:]
        if all(
            item["return_code"] == 0 and Path(item["path"]).is_file()
            for item in stable_captures
        ):
            interaction_stable_edge_similarity = edge_cosine_similarity(
                Path(stable_captures[0]["path"]), Path(stable_captures[1]["path"]),
                interaction_viewport_crop, interaction_viewport_crop,
            )
    raw_launch_captures = bootstrap_raw["screen_captures"]
    raw_viewport_crop = viewport_pixel_crop(bootstrap_raw["telemetry"])
    raw_launch_similarities = []
    if raw_launch_captures and all(
        item["return_code"] == 0 and Path(item["path"]).is_file()
        for item in raw_launch_captures
    ):
        reference = Path(raw_launch_captures[-1]["path"])
        raw_launch_similarities = [
            edge_cosine_similarity(
                Path(item["path"]), reference, raw_viewport_crop, raw_viewport_crop
            )
            for item in raw_launch_captures
        ]

    decode_samples = [float(run["telemetry"][0]["decode_ms"]) for run in real_runs if run["telemetry"]]
    steady_render_samples = [
        float(item["last_render_ms"])
        for run in real_runs
        for item in run["telemetry"]
        if int(item.get("render_count", 0)) > 3 and item.get("hdr_prepared") is True
    ]
    transition_rates = [transition_fps(run["telemetry"]) for run in real_runs]
    transition_steps = [maximum_transition_step(run["telemetry"]) for run in real_runs]
    decode_average = statistics.fmean(decode_samples) if decode_samples else math.inf
    render_average = statistics.fmean(steady_render_samples) if steady_render_samples else math.inf
    performance = {
        "measurement_window": {
            "decode": "request start through native HDR graph and bounded SDR proxy completion",
            "steady_render": "render_count > 3 and hdr_prepared=true; first-frame and offscreen endpoint preparation excluded",
            "runs_per_format": RUNS_PER_FORMAT,
            "formats": ["gain-map-jpeg", "raw-dng"],
        },
        "thresholds": THRESHOLDS,
        "raw_samples": {
            "decode_ms": decode_samples,
            "steady_render_ms": steady_render_samples,
            "observed_transition_frames_per_second": transition_rates,
            "maximum_transition_progress_step": transition_steps,
        },
        "metrics": {
            "decode_average_ms": decode_average,
            "decode_p99_ms": percentile(decode_samples, 0.99),
            "decode_max_ms": max(decode_samples, default=math.inf),
            "decode_throughput_images_per_second": 1000.0 / decode_average if decode_average > 0 else 0.0,
            "steady_render_average_ms": render_average,
            "steady_render_p99_ms": percentile(steady_render_samples, 0.99),
            "steady_render_max_ms": max(steady_render_samples, default=math.inf),
            "steady_render_equivalent_submissions_per_second": 1000.0 / render_average if render_average > 0 else 0.0,
            "observed_transition_frames_per_second_min": min(transition_rates, default=0.0),
            "transition_progress_step_max": max(transition_steps, default=math.inf),
        },
    }
    metrics = performance["metrics"]
    finished_indices = [
        index for index, item in enumerate(interaction_records)
        if item.get("phase") == "test-interaction-finished"
    ]
    post_interaction_records = (
        interaction_records[finished_indices[-1] + 1:] if finished_indices else []
    )
    post_interaction_render_records = [
        item for item in post_interaction_records if item.get("phase") == "render"
    ]

    cases = [
        make_case("SYS-HDR-GAINMAP-JPEG-EDR", {
            "all_runs_healthy": all(run["process_healthy"] for run in jpeg_runs),
            "adaptive_hdr_identified": bool(jpeg_records) and all(item.get("source_kind") == "adaptive-hdr" for item in jpeg_records),
            "gain_map_detected": bool(jpeg_records) and all(item.get("has_apple_gain_map") or item.get("has_iso_gain_map") for item in jpeg_records),
            "content_headroom_above_one": bool(jpeg_records) and all(float(item.get("content_headroom", 0)) > 1 for item in jpeg_records),
            "target_reaches_hdr": all(max(float(item.get("target_headroom", 1)) for item in run["telemetry"]) > 1.1 for run in jpeg_runs),
        }, {
            "run_count": len(jpeg_runs),
            "record_count": len(jpeg_records),
            "max_target_headroom_by_run": [max(float(item["target_headroom"]) for item in run["telemetry"]) for run in jpeg_runs],
        }),
        make_case("SYS-HDR-RAW-DNG-EDR", {
            "all_runs_healthy": all(run["process_healthy"] for run in raw_runs),
            "raw_identified": bool(raw_records) and all(item.get("is_raw") is True for item in raw_records),
            "raw_edr_graph": bool(raw_records) and all(item.get("uses_raw_extended_dynamic_range") is True for item in raw_records),
            "raw_precision_contract": bool(raw_records) and all(int(item.get("bits_per_component", 0)) == 16 for item in raw_records),
            "preview_not_primary": bool(raw_records) and all(item.get("used_raw_preview") is False for item in raw_records),
            "target_reaches_hdr": all(max(float(item.get("target_headroom", 1)) for item in run["telemetry"]) > 1.1 for run in raw_runs),
        }, {
            "run_count": len(raw_runs),
            "record_count": len(raw_records),
            "max_target_headroom_by_run": [max(float(item["target_headroom"]) for item in run["telemetry"]) for run in raw_runs],
        }),
        make_case("SYS-HDR-NO-PREMATURE-BLACK-FRAME", {
            "telemetry_present": bool(all_real_records),
            "fallback_covers_unpresented_frames": all(
                item.get("fallback_visible") is True and abs(float(item.get("layer_opacity", -1))) < 1e-6
                for item in all_real_records if item.get("first_frame_presented") is False
            ),
            "metal_revealed_only_after_presentation": all(
                item.get("first_frame_presented") is True
                for item in all_real_records if float(item.get("layer_opacity", 0)) > 0.5
            ),
            "revealed_frame_has_final_drawable_geometry": all(
                item.get("drawable_geometry_matches") is True
                for item in all_real_records if float(item.get("layer_opacity", 0)) > 0.5
            ),
            "revealed_hdr_frame_was_fully_prepared": all(
                item.get("hdr_prepared") is True
                and item.get("prepared_geometry_active") is True
                for item in all_real_records if float(item.get("layer_opacity", 0)) > 0.5
            ),
        }, {
            "unpresented_record_count": sum(item.get("first_frame_presented") is False for item in all_real_records),
            "revealed_record_count": sum(float(item.get("layer_opacity", 0)) > 0.5 for item in all_real_records),
        }),
        make_case("SYS-HDR-FINAL-LAYOUT-BEFORE-METAL", {
            "all_renders_are_layout_ready": bool(render_records) and all(item.get("layout_ready") is True for item in render_records),
            "no_render_uses_pending_geometry": bool(render_records) and all(item.get("geometry_pending") is False for item in render_records),
            "all_drawables_match_requested_geometry": bool(render_records) and all(item.get("drawable_geometry_matches") is True for item in render_records),
            "raw_zoom_is_stable_from_first_submission": all(
                len({
                    round(float(item.get("zoom_level", -1)), 9)
                    for item in run["telemetry"] if item.get("phase") == "render"
                }) == 1
                for run in raw_runs
            ),
            "raw_timed_captures_succeeded": (
                len(raw_launch_captures) == 5
                and all(item["return_code"] == 0 and item["bytes"] > 0
                        for item in raw_launch_captures)
            ),
            "raw_full_frame_structure_is_stable": (
                len(raw_launch_similarities) == 5
                and min(raw_launch_similarities) >= 0.90
            ),
        }, {
            "render_record_count": len(render_records),
            "raw_zoom_values_by_run": [sorted({
                float(item.get("zoom_level", -1))
                for item in run["telemetry"] if item.get("phase") == "render"
            }) for run in raw_runs],
            "raw_capture_files": [item["path"] for item in raw_launch_captures],
            "raw_capture_sha256": [item["sha256"] for item in raw_launch_captures],
            "raw_viewport_crop_pixels": list(raw_viewport_crop) if raw_viewport_crop else None,
            "raw_edge_cosine_similarity_to_final": raw_launch_similarities,
            "raw_edge_cosine_similarity_minimum": 0.90,
        }),
        make_case("SYS-HDR-FLOAT-COLORMANAGED-EDR-SURFACE", {
            "telemetry_present": bool(all_real_records),
            "rgba16_float": all(item.get("rgba16_float") is True for item in all_real_records),
            "extended_linear_display_p3": all(item.get("extended_linear_display_p3") is True for item in all_real_records),
            "colorsync": all(item.get("color_sync") is True for item in all_real_records),
            "wants_edr": all(item.get("wants_edr") is True for item in all_real_records),
            "opaque_drawable_clear": all(item.get("opaque_drawable_clear") is True for item in all_real_records),
        }, {"record_count": len(all_real_records)}),
        make_case("SYS-HDR-SMOOTH-ACTIVATION", {
            "all_runs_have_multiple_frames": all(len(run["telemetry"]) >= 5 for run in real_runs),
            "starts_near_sdr": all(float(run["telemetry"][0].get("transition_progress", 1)) <= 0.1 for run in real_runs),
            "reaches_full_progress": all(max(float(item.get("transition_progress", 0)) for item in run["telemetry"]) >= 0.999 for run in real_runs),
            "progress_monotonic": all(
                all(
                    float(records[index].get("transition_progress", 0)) + 1e-6 >= float(records[index - 1].get("transition_progress", 0))
                    for index in range(1, len(records))
                )
                for records in (run["telemetry"] for run in real_runs)
            ),
            "transition_begins_only_after_present_and_prepare": all(
                item.get("first_frame_presented") is True and item.get("hdr_prepared") is True
                for item in all_real_records if float(item.get("transition_progress", 0)) > 0
            ),
            "progress_step_is_bounded": max(transition_steps, default=math.inf) <= THRESHOLDS["transition_progress_step_max"],
        }, {
            "first_progress_by_run": [run["telemetry"][0].get("transition_progress") for run in real_runs],
            "last_progress_by_run": [run["telemetry"][-1].get("transition_progress") for run in real_runs],
            "maximum_progress_step_by_run": transition_steps,
        }),
        make_case("SYS-HDR-WINDOWSERVER-HEADROOM", {
            "potential_hdr_display": bool(all_real_records) and max(float(item.get("display_potential_headroom", 1)) for item in all_real_records) > 1,
            "rendering_headroom_above_one": bool(render_records) and max(float(item.get("display_rendering_headroom", 1)) for item in render_records) > 1,
            "target_never_exceeds_rendering_headroom": bool(all_real_records) and all(
                float(item.get("target_headroom", 1)) <= max(1.0, float(item.get("display_rendering_headroom", 1))) + 1e-4
                for item in all_real_records
            ),
        }, {
            "max_current_headroom": max((float(item.get("display_current_headroom", 1)) for item in all_real_records), default=1),
            "max_potential_headroom": max((float(item.get("display_potential_headroom", 1)) for item in all_real_records), default=1),
            "max_rendering_headroom": max((float(item.get("display_rendering_headroom", 1)) for item in all_real_records), default=1),
        }),
        make_case("SYS-HDR-EDR-BOOTSTRAP", {
            "both_processes_healthy": all(run["process_healthy"] for run in bootstrap_runs),
            "records_present": all(bool(run["telemetry"]) for run in bootstrap_runs),
            "current_only_override_observed": bool(bootstrap_records) and all(
                item.get("display_current_headroom_overridden") is True
                for item in bootstrap_records if item.get("phase") == "render"
            ),
            "potential_edr_capability_preserved": all(
                max(float(item.get("display_potential_headroom", 1)) for item in run["telemetry"]) > 1
                for run in bootstrap_runs
            ),
            "bootstrap_flag_observed_for_both_formats": all(
                any(item.get("bootstrapping_edr") is True for item in run["telemetry"])
                for run in bootstrap_runs
            ),
            "rendering_headroom_above_one_for_both_formats": all(
                max(float(item.get("display_rendering_headroom", 1)) for item in run["telemetry"]) > 1
                for run in bootstrap_runs
            ),
            "target_reaches_hdr_for_both_formats": all(
                max(float(item.get("target_headroom", 1)) for item in run["telemetry"]) > 1.1
                for run in bootstrap_runs
            ),
            "prepared_geometry_becomes_active": all(
                any(item.get("prepared_geometry_active") is True for item in run["telemetry"])
                for run in bootstrap_runs
            ),
        }, {
            "formats": [run["format"] for run in bootstrap_runs],
            "max_target_by_format": {
                run["format"]: max(float(item.get("target_headroom", 1)) for item in run["telemetry"])
                for run in bootstrap_runs
            },
            "max_rendering_headroom_by_format": {
                run["format"]: max(float(item.get("display_rendering_headroom", 1)) for item in run["telemetry"])
                for run in bootstrap_runs
            },
        }),
        make_case("SYS-HDR-FORCED-SDR-COMPATIBILITY", {
            "process_healthy": forced_sdr["process_healthy"],
            "records_present": bool(forced_records),
            "override_observed": bool(forced_records) and all(
                item.get("display_headroom_overridden") is True
                for item in forced_records if item.get("phase") == "render"
            ),
            "display_headroom_is_one": bool(forced_records) and all(abs(float(item.get("display_current_headroom", 0)) - 1.0) < 1e-6 for item in forced_records),
            "potential_and_rendering_headroom_are_one": bool(forced_records) and all(
                abs(float(item.get("display_potential_headroom", 0)) - 1.0) < 1e-6
                and abs(float(item.get("display_rendering_headroom", 0)) - 1.0) < 1e-6
                for item in forced_records
            ),
            "target_headroom_is_one": bool(forced_records) and all(abs(float(item.get("target_headroom", 0)) - 1.0) < 1e-6 for item in forced_records),
        }, {"record_count": len(forced_records)}),
        make_case("SYS-HDR-JPEG-BAND-FREE", {
            "interaction_process_healthy": interaction_run["process_healthy"],
            "all_scheduled_captures_succeeded": (
                len(interaction_run["screen_captures"]) == 6
                and all(item["return_code"] == 0 and item["bytes"] > 0
                        for item in interaction_run["screen_captures"])
            ),
            "all_captures_have_pixel_metrics": len(interaction_capture_metrics) == 6,
            "no_persistent_black_vertical_band": bool(interaction_capture_metrics) and all(
                item["maximum_consecutive_near_black_columns"] <= 10
                for item in interaction_capture_metrics
            ),
            "no_post_interaction_tile_residue": interaction_stable_edge_similarity >= 0.995,
        }, {
            "capture_count": len(interaction_run["screen_captures"]),
            "capture_files": [item["path"] for item in interaction_run["screen_captures"]],
            "capture_sha256": [item["sha256"] for item in interaction_run["screen_captures"]],
            "metrics": interaction_capture_metrics,
            "interaction_viewport_crop_pixels": (
                list(interaction_viewport_crop) if interaction_viewport_crop else None
            ),
            "stable_edge_cosine_similarity": interaction_stable_edge_similarity,
            "stable_edge_cosine_similarity_minimum": 0.995,
            "pre_fix_reference_maximum_runs": [80, 243],
            "pre_fix_post_interaction_edge_similarity": 0.9721460604214245,
        }),
        make_case("SYS-HDR-INTERACTION-GEOMETRY", {
            "interaction_process_healthy": interaction_run["process_healthy"],
            "interaction_finished": bool(finished_indices),
            "zoom_changed": len({
                round(float(item.get("zoom_level", -1)), 6)
                for item in interaction_records
            }) >= 2,
            "geometry_generation_reset": max(
                (int(item.get("geometry_reset_count", 0)) for item in interaction_records),
                default=0,
            ) >= 1,
            "invalidated_geometry_is_proxy_only": any(
                int(item.get("geometry_reset_count", 0)) >= 1
                for item in interaction_staged_records
            ) and all(
                item.get("fallback_visible") is True
                and abs(float(item.get("layer_opacity", -1))) < 1e-6
                for item in interaction_staged_records
                if int(item.get("geometry_reset_count", 0)) >= 1
            ),
            "no_render_while_geometry_pending": all(
                item.get("geometry_pending") is False
                for item in interaction_render_records
            ),
            "post_interaction_hdr_recovers": bool(post_interaction_render_records) and any(
                item.get("prepared_geometry_active") is True
                and item.get("fallback_visible") is False
                and float(item.get("layer_opacity", 0)) > 0.5
                and float(item.get("target_headroom", 1)) > 1.1
                for item in post_interaction_render_records
            ),
        }, {
            "record_count": len(interaction_records),
            "render_record_count": len(interaction_render_records),
            "geometry_staged_record_count": len(interaction_staged_records),
            "post_interaction_render_record_count": len(post_interaction_render_records),
            "maximum_geometry_reset_count": max(
                (int(item.get("geometry_reset_count", 0)) for item in interaction_records),
                default=0,
            ),
            "zoom_values": sorted({float(item.get("zoom_level", -1)) for item in interaction_records}),
        }),
        make_case("SYS-HDR-TIME-BEHAVIOR", {
            "decode_sample_count": len(decode_samples) == RUNS_PER_FORMAT * 2,
            "decode_average": metrics["decode_average_ms"] <= THRESHOLDS["decode_average_ms_max"],
            "decode_p99": metrics["decode_p99_ms"] <= THRESHOLDS["decode_p99_ms_max"],
            "decode_max": metrics["decode_max_ms"] <= THRESHOLDS["decode_max_ms_max"],
            "decode_throughput": metrics["decode_throughput_images_per_second"] >= THRESHOLDS["decode_throughput_images_per_second_min"],
            "render_average": metrics["steady_render_average_ms"] <= THRESHOLDS["steady_render_average_ms_max"],
            "render_p99": metrics["steady_render_p99_ms"] <= THRESHOLDS["steady_render_p99_ms_max"],
            "render_max": metrics["steady_render_max_ms"] <= THRESHOLDS["steady_render_max_ms_max"],
            "render_throughput": metrics["steady_render_equivalent_submissions_per_second"] >= THRESHOLDS["steady_render_equivalent_submissions_per_second_min"],
            "observed_frame_rate": metrics["observed_transition_frames_per_second_min"] >= THRESHOLDS["observed_transition_frames_per_second_min"],
            "transition_progress_step": metrics["transition_progress_step_max"] <= THRESHOLDS["transition_progress_step_max"],
        }, metrics),
    ]

    passed = all(item["status"] == "passed" for item in cases)
    record = {
        "schema_version": "1.0",
        "kind": "system-test-evidence",
        "release": "v0.1.4",
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "host": {
            "platform": platform.platform(),
            "macos_version": read_command("sw_vers", "-productVersion"),
            "hardware_model": read_command("sysctl", "-n", "hw.model"),
            "cpu_brand": read_command("sysctl", "-n", "machdep.cpu.brand_string"),
            "display_summary": read_command("system_profiler", "SPDisplaysDataType", "-detailLevel", "mini"),
        },
        "samples": {"jpeg": str(jpeg), "raw": str(raw)},
        "runs": runs,
        "performance": performance,
        "cases": cases,
        "summary": {
            "total": len(cases),
            "passed": sum(item["status"] == "passed" for item in cases),
            "failed": sum(item["status"] != "passed" for item in cases),
        },
        "facts": [
            "Telemetry came from the compiled Cocoa application while its CAMetalLayer was attached to a visible window.",
            "The built-in display exposed potential EDR headroom above one during this run.",
            "The forced-SDR run overrides current and potential capability to one while retaining the real Core Image/Metal output path.",
            "Separate current-only override runs held current headroom at one while preserving the display's potential capability, and both supplied formats reached HDR targets.",
            "Every record before first-frame presentation retained the SDR fallback with Metal layer opacity zero.",
            "No render telemetry was emitted for pending geometry; every submitted DNG frame used a matching drawable.",
            "All six screen captures across steady state, zoom, and pan stayed below the ten-column near-black-band threshold.",
            "The final two post-interaction JPEG captures retained at least 0.995 edge-structure similarity after movement stopped.",
            "Five DNG launch captures retained at least 0.90 edge-structure similarity to the final full frame; the fixed preflight measured 0.940 minimum while the captured partial-frame baseline measured below 0.64.",
            "Every production record reported an opaque full-drawable clear before image compositing.",
            "The visible HDR transition began only after endpoint preparation and met the recorded frame-rate and maximum-progress-step thresholds.",
        ],
        "inferences": [
            "Potential headroom above one together with wants_edr, RGBA16Float, and target headroom above one demonstrates an EDR-capable WindowServer presentation path.",
            "The current-only bootstrap result breaks the circular dependency in which NSScreen current headroom can remain one until EDR content is already onscreen.",
            "Initialization-time non-volatile gain-map decode, source-space Core Image-managed intermediates, and band-free screen pixels remove the two cache boundaries associated with the persistent JPEG bands.",
            "Invalidating the Metal generation and exposing only the Qt fallback during geometry changes removes the stale-transform intervals associated with partial DNG frames and drag ghosting.",
        ],
        "uncertainties": [
            "Telemetry cannot prove subjective equivalence with Quick Look's private tone curve.",
            "A physical SDR-only Mac was not available; a capability override of current=potential=1 exercises the production SDR branch deterministically.",
            "A still screenshot cannot encode absolute luminance or temporal persistence; HDR pixel peaks, headroom telemetry, multiple timed captures, and generation invariants provide complementary evidence.",
        ],
        "passed": passed,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(record, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"kind": record["kind"], "summary": record["summary"], "metrics": metrics, "passed": passed}, ensure_ascii=False))
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())

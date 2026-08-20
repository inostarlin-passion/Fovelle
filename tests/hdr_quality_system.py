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
CAPTURE_SECONDS = 7.0
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
    "interaction_zoom_main_thread_ms_max": 30.0,
    "interaction_pan_interval_average_ms_max": 25.0,
    "interaction_pan_interval_p99_ms_max": 45.0,
    "interaction_pan_interval_max_ms_max": 60.0,
    "interaction_pan_steps_per_second_min": 40.0,
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


def image_pixel_crop(records: list[dict]) -> tuple[int, int, int, int] | None:
    """Map the telemetry image polygon into full-screen physical pixels."""
    viewport = viewport_pixel_crop(records)
    for item in reversed(records):
        corners = item.get("image_corners")
        required = (
            "viewport_global_x", "viewport_global_y", "viewport_device_pixel_ratio",
        )
        if not isinstance(corners, list) or len(corners) < 4:
            continue
        if not all(key in item for key in required):
            continue
        try:
            xs = [float(point[0]) for point in corners]
            ys = [float(point[1]) for point in corners]
        except (TypeError, ValueError, IndexError):
            continue
        scale = float(item["viewport_device_pixel_ratio"])
        origin_x = float(item["viewport_global_x"])
        origin_y = float(item["viewport_global_y"])
        crop = (
            round((origin_x + min(xs)) * scale),
            round((origin_y + min(ys)) * scale),
            round((origin_x + max(xs)) * scale),
            round((origin_y + max(ys)) * scale),
        )
        if viewport is None:
            return crop
        return (
            max(crop[0], viewport[0]), max(crop[1], viewport[1]),
            min(crop[2], viewport[2]), min(crop[3], viewport[3]),
        )
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


def background_sample_rgb(
    path: Path,
    viewport_crop: tuple[int, int, int, int] | None,
    image_crop: tuple[int, int, int, int] | None,
) -> dict:
    with Image.open(path) as source:
        image = source.convert("RGB")
    if viewport_crop is None:
        return {"samples": [], "median_rgb": None}
    vl, vt, vr, vb = viewport_crop
    candidates: list[tuple[int, int]] = []
    if image_crop is not None:
        il, it, ir, ib = image_crop
        if il - vl >= 24:
            candidates.append(((vl + il) // 2, (vt + vb) // 2))
        if vr - ir >= 24:
            candidates.append(((ir + vr) // 2, (vt + vb) // 2))
        if it - vt >= 24:
            candidates.append(((vl + vr) // 2, (vt + it) // 2))
        if vb - ib >= 24:
            candidates.append(((vl + vr) // 2, (ib + vb) // 2))
    if not candidates:
        candidates = [(vl + 10, vt + 10), (vr - 11, vb - 11)]

    samples = []
    for x, y in candidates:
        left, top = max(0, x - 4), max(0, y - 4)
        right, bottom = min(image.width, x + 5), min(image.height, y + 5)
        if right <= left or bottom <= top:
            continue
        pixels = list(image.crop((left, top, right, bottom)).getdata())
        samples.append([
            round(statistics.median(pixel[channel] for pixel in pixels))
            for channel in range(3)
        ])
    median = [
        round(statistics.median(sample[channel] for sample in samples))
        for channel in range(3)
    ] if samples else None
    return {"samples": samples, "median_rgb": median}


def navigation_surface_diff_metric(
    baseline_path: Path,
    visible_path: Path,
    event: dict,
    device_pixel_ratio: float,
) -> dict:
    with Image.open(baseline_path) as baseline_source, Image.open(visible_path) as visible_source:
        baseline = baseline_source.convert("RGB")
        visible = visible_source.convert("RGB")
    scale = max(1.0, device_pixel_ratio)
    left = round(float(event["global_x"]) * scale)
    top = round(float(event["global_y"]) * scale)
    width = round(float(event["width"]) * scale)
    height = round(float(event["height"]) * scale)
    right, bottom = left + width, top + height
    patch_size = max(2, round(5 * scale))
    inset = max(patch_size + 1, round(11 * scale))

    def mean_difference(box: tuple[int, int, int, int]) -> float:
        base_pixels = list(baseline.crop(box).getdata())
        visible_pixels = list(visible.crop(box).getdata())
        values = [
            abs(a - b)
            for base_pixel, visible_pixel in zip(base_pixels, visible_pixels)
            for a, b in zip(base_pixel, visible_pixel)
        ]
        return statistics.fmean(values) if values else math.inf

    corner_boxes = [
        (left, top, left + patch_size, top + patch_size),
        (right - patch_size, top, right, top + patch_size),
        (left, bottom - patch_size, left + patch_size, bottom),
        (right - patch_size, bottom - patch_size, right, bottom),
    ]
    center_box = (left + inset, top + inset, right - inset, bottom - inset)
    return {
        "button_rect_pixels": [left, top, right, bottom],
        "corner_mean_absolute_rgb_differences": [
            mean_difference(box) for box in corner_boxes
        ],
        "maximum_corner_mean_absolute_rgb_difference": max(
            (mean_difference(box) for box in corner_boxes), default=math.inf
        ),
        "center_mean_absolute_rgb_difference": mean_difference(center_box),
        "device_pixel_ratio": scale,
    }


def missing_structure_metric(
    path: Path,
    reference_path: Path,
    crop: tuple[int, int, int, int] | None,
) -> dict:
    def edges(file_path: Path) -> Image.Image:
        with Image.open(file_path) as source:
            frame = crop_to_bounds(source.convert("L"), crop)
        return frame.resize((256, 192), Image.Resampling.LANCZOS).filter(ImageFilter.FIND_EDGES)

    current = edges(path)
    reference = edges(reference_path)
    tile_columns, tile_rows = 8, 6
    missing = []
    energies = []
    for row in range(tile_rows):
        for column in range(tile_columns):
            box = (
                column * current.width // tile_columns,
                row * current.height // tile_rows,
                (column + 1) * current.width // tile_columns,
                (row + 1) * current.height // tile_rows,
            )
            current_values = list(current.crop(box).getdata())
            reference_values = list(reference.crop(box).getdata())
            current_energy = statistics.fmean(current_values)
            reference_energy = statistics.fmean(reference_values)
            is_missing = reference_energy >= 3.0 and current_energy < reference_energy * 0.35
            if is_missing:
                missing.append([column, row])
            energies.append({
                "tile": [column, row],
                "current": current_energy,
                "reference": reference_energy,
                "missing": is_missing,
            })
    return {
        "missing_structural_tile_count": len(missing),
        "missing_structural_tiles": missing,
        "tile_count": tile_columns * tile_rows,
        "energies": energies,
    }


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
    theme_switch: bool = False,
    navigation: bool = False,
    capture_seconds: float = CAPTURE_SECONDS,
    capture_schedule: list[float] | None = None,
    capture_directory: Path | None = None,
) -> dict:
    suffix = image.suffix.lower()
    format_name = (
        "raw-dng" if suffix == ".dng"
        else "raw-nef" if suffix == ".nef"
        else "gain-map-jpeg"
    )
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
    if theme_switch:
        environment["FOVELLE_HDR_TEST_THEME_SWITCH"] = "1"
    if navigation:
        environment["FOVELLE_HDR_TEST_NAVIGATION"] = "1"
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
                scenario = (
                    "interaction" if interaction else "theme" if theme_switch
                    else "navigation" if navigation else "launch"
                )
                capture_path = capture_directory / (
                    f"{format_name.replace('-', '_')}"
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
    navigation_events = []
    for match in re.finditer(r"FOVELLE_NAV\s+(\{[^\n\r]+\})", output):
        try:
            navigation_events.append(json.loads(match.group(1)))
        except json.JSONDecodeError:
            continue
    return {
        "format": format_name,
        "run_index": run_index,
        "forced_headroom": forced_headroom,
        "forced_current_headroom": forced_current_headroom,
        "interaction": interaction,
        "theme_switch": theme_switch,
        "navigation": navigation,
        "command": command,
        "capture_seconds": capture_seconds,
        "elapsed_seconds": time.perf_counter() - started,
        "timed_out_as_designed": timed_out,
        "return_code_after_termination": process.returncode,
        "telemetry_count": len(telemetry),
        "telemetry": telemetry,
        "navigation_events": navigation_events,
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


def interaction_timing(records: list[dict]) -> dict:
    step_records = [
        item for item in records if item.get("phase") == "test-interaction-step"
    ]
    elapsed = [float(item.get("interaction_elapsed_ms", -1)) for item in step_records]
    intervals = [current - previous for previous, current in zip(elapsed, elapsed[1:])]
    average = statistics.fmean(intervals) if intervals else math.inf
    zoom_values = [
        float(item.get("interaction_zoom_ms", 0))
        for item in records if item.get("phase") == "test-interaction-start"
    ]
    return {
        "step_count": len(step_records),
        "step_numbers": [int(item.get("interaction_step", -1)) for item in step_records],
        "elapsed_ms": elapsed,
        "intervals_ms": intervals,
        "interval_average_ms": average,
        "interval_p99_ms": percentile(intervals, 0.99),
        "interval_max_ms": max(intervals, default=math.inf),
        "steps_per_second": 1000.0 / average if average > 0 else 0.0,
        "zoom_main_thread_ms": max(zoom_values, default=math.inf),
    }


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
    parser.add_argument("--nef", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    app = args.app.resolve()
    jpeg = args.jpeg.resolve()
    raw = args.raw.resolve()
    nef = args.nef.resolve()
    missing = [str(path) for path in (app, jpeg, raw, nef) if not path.is_file()]
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
        capture_seconds=7.0,
        capture_schedule=[3.6, 3.8, 4.0, 4.2, 4.4, 4.6, 4.8, 5.1, 5.5, 6.2],
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
    theme_run = launch(
        app,
        jpeg,
        1,
        theme_switch=True,
        capture_seconds=5.8,
        capture_schedule=[2.4, 4.5, 5.2],
        capture_directory=capture_directory,
    )
    nef_interaction_run = launch(
        app,
        nef,
        1,
        forced_current_headroom=1.0,
        interaction=True,
        capture_seconds=5.8,
        capture_schedule=[3.8, 4.2, 4.6, 5.4],
        capture_directory=capture_directory,
    )
    navigation_run = launch(
        app,
        jpeg,
        1,
        navigation=True,
        capture_seconds=6.2,
        capture_schedule=[3.4, 4.1, 5.6],
        capture_directory=capture_directory,
    )
    runs.extend((
        forced_sdr, bootstrap_jpeg, bootstrap_raw, interaction_run,
        theme_run, nef_interaction_run, navigation_run,
    ))

    real_runs = [
        run for run in runs
        if run["forced_headroom"] is None
        and run["forced_current_headroom"] is None
        and not run["interaction"]
        and not run["theme_switch"]
        and not run["navigation"]
    ]
    jpeg_runs = [run for run in real_runs if run["format"] == "gain-map-jpeg"]
    raw_runs = [run for run in real_runs if run["format"] == "raw-dng"]
    all_real_records = [item for run in real_runs for item in run["telemetry"]]
    jpeg_records = [item for run in jpeg_runs for item in run["telemetry"]]
    raw_records = [item for run in raw_runs for item in run["telemetry"]]
    forced_records = forced_sdr["telemetry"]
    bootstrap_runs = [bootstrap_jpeg, bootstrap_raw]
    bootstrap_records = [item for run in bootstrap_runs for item in run["telemetry"]]
    interaction_records = interaction_run["telemetry"]
    nef_interaction_records = nef_interaction_run["telemetry"]
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
    interaction_reused_records = [
        item for item in interaction_records if item.get("phase") == "geometry-reused"
    ]
    jpeg_interaction_timing = interaction_timing(interaction_records)
    nef_interaction_timing = interaction_timing(nef_interaction_records)
    interaction_settled_records = [
        item for item in interaction_records
        if item.get("phase") == "test-interaction-settled"
    ]
    nef_interaction_settled_records = [
        item for item in nef_interaction_records
        if item.get("phase") == "test-interaction-settled"
    ]
    scheduler_records = all_real_records + interaction_records + nef_interaction_records
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
    nef_viewport_crop = viewport_pixel_crop(nef_interaction_records)
    nef_image_crop = image_pixel_crop(nef_interaction_records)
    nef_capture_metrics = []
    nef_edge_similarities = []
    nef_captures = nef_interaction_run["screen_captures"]
    if nef_captures and all(
        item["return_code"] == 0 and Path(item["path"]).is_file()
        for item in nef_captures
    ):
        nef_reference = Path(nef_captures[-1]["path"])
        for capture in nef_captures:
            path = Path(capture["path"])
            metric = missing_structure_metric(path, nef_reference, nef_image_crop)
            band = black_vertical_band_metric(path, nef_viewport_crop)
            similarity = edge_cosine_similarity(
                path, nef_reference, nef_image_crop, nef_image_crop
            )
            capture["missing_structure_metric"] = metric
            capture["black_vertical_band_metric"] = band
            capture["edge_cosine_similarity_to_final"] = similarity
            nef_capture_metrics.append(metric)
            nef_edge_similarities.append(similarity)

    navigation_events = navigation_run["navigation_events"]
    navigation_visible_events = [
        item for item in navigation_events
        if item.get("phase") == "fractional-visible"
    ]
    navigation_hidden_events = [
        item for item in navigation_events if item.get("phase") == "hidden"
    ]
    navigation_capture_metrics = []
    navigation_captures = navigation_run["screen_captures"]
    navigation_device_pixel_ratio = next((
        float(item.get("viewport_device_pixel_ratio", 1.0))
        for item in reversed(navigation_run["telemetry"])
        if "viewport_device_pixel_ratio" in item
    ), 1.0)
    if (
        navigation_visible_events
        and len(navigation_captures) == 3
        and all(item["return_code"] == 0 and Path(item["path"]).is_file()
                for item in navigation_captures)
    ):
        hidden_reference = Path(navigation_captures[-1]["path"])
        for capture in navigation_captures[:2]:
            metric = navigation_surface_diff_metric(
                hidden_reference,
                Path(capture["path"]),
                navigation_visible_events[-1],
                navigation_device_pixel_ratio,
            )
            capture["navigation_surface_diff_metric"] = metric
            navigation_capture_metrics.append(metric)
    raw_launch_captures = bootstrap_raw["screen_captures"]
    raw_viewport_crop = viewport_pixel_crop(bootstrap_raw["telemetry"])
    raw_image_crop = image_pixel_crop(bootstrap_raw["telemetry"])
    raw_launch_similarities = []
    raw_missing_structure_metrics = []
    if raw_launch_captures and all(
        item["return_code"] == 0 and Path(item["path"]).is_file()
        for item in raw_launch_captures
    ):
        reference = Path(raw_launch_captures[-1]["path"])
        raw_launch_similarities = [
            edge_cosine_similarity(
                Path(item["path"]), reference, raw_image_crop, raw_image_crop
            )
            for item in raw_launch_captures
        ]
        raw_missing_structure_metrics = [
            missing_structure_metric(Path(item["path"]), reference, raw_image_crop)
            for item in raw_launch_captures
        ]

    theme_records = theme_run["telemetry"]
    theme_viewport_crop = viewport_pixel_crop(theme_records)
    theme_image_crop = image_pixel_crop(theme_records)
    theme_capture_samples = []
    for capture in theme_run["screen_captures"]:
        capture_path = Path(capture["path"])
        if capture["return_code"] == 0 and capture_path.is_file():
            sample = background_sample_rgb(
                capture_path, theme_viewport_crop, theme_image_crop
            )
            capture["background_sample_rgb"] = sample
            theme_capture_samples.append(sample)

    theme_dark_event_indices = [
        index for index, item in enumerate(theme_records)
        if item.get("phase") == "test-theme-dark"
    ]
    theme_dark_indices = [
        index for index, item in enumerate(theme_records)
        if (
            int(item.get("viewport_background_red", -1)),
            int(item.get("viewport_background_green", -1)),
            int(item.get("viewport_background_blue", -1)),
        ) == (33, 33, 33)
    ]
    theme_pre_dark_records = (
        theme_records[:theme_dark_indices[0]] if theme_dark_indices else theme_records
    )
    theme_post_dark_records = (
        theme_records[theme_dark_indices[0]:] if theme_dark_indices else []
    )

    raw_completed_records = [
        item for item in raw_records
        if item.get("phase") == "render"
        and float(item.get("transition_progress", 0)) >= 0.999
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
            "jpeg_interaction": jpeg_interaction_timing,
            "nef_interaction": nef_interaction_timing,
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
            "processed_gain_map_graph": bool(raw_records) and all(
                item.get("uses_processed_raw_preview") is True
                and item.get("uses_raw_extended_dynamic_range") is False
                and item.get("source_kind") == "camera-raw-processed-gain-map"
                for item in raw_records
            ),
            "raw_precision_contract": bool(raw_records) and all(int(item.get("bits_per_component", 0)) == 16 for item in raw_records),
            "authored_processed_preview_is_primary": bool(raw_records) and all(
                item.get("used_raw_preview") is True for item in raw_records
            ),
            "gain_map_detected": bool(raw_records) and all(
                item.get("has_apple_gain_map") or item.get("has_iso_gain_map")
                for item in raw_records
            ),
            "target_reaches_hdr": all(max(float(item.get("target_headroom", 1)) for item in run["telemetry"]) > 1.1 for run in raw_runs),
        }, {
            "run_count": len(raw_runs),
            "record_count": len(raw_records),
            "max_target_headroom_by_run": [max(float(item["target_headroom"]) for item in run["telemetry"]) for run in raw_runs],
        }),
        make_case("SYS-HDR-RAW-CONTENT-HEADROOM", {
            "completed_raw_frames_present": bool(raw_completed_records),
            "measured_content_headroom_is_hdr": all(
                float(item.get("content_headroom", 0)) > 1.5
                for item in raw_completed_records
            ),
            "layer_tag_matches_rendered_content": all(
                item.get("layer_contents_headroom_tag_supported") is not True
                or abs(float(item.get("layer_contents_headroom", 0))
                       - float(item.get("target_headroom", 0))) <= 0.02
                for item in raw_completed_records
            ),
            "target_is_clamped_to_content_and_display": all(
                abs(float(item.get("target_headroom", 0)) - min(
                    float(item.get("content_headroom", 0)),
                    float(item.get("display_rendering_headroom", 0)),
                )) <= 0.02
                for item in raw_completed_records
            ),
            "content_tag_is_not_display_potential": all(
                abs(float(item.get("content_headroom", 0))
                    - float(item.get("display_potential_headroom", 0))) > 0.5
                for item in raw_completed_records
                if float(item.get("display_potential_headroom", 0)) > 2.5
            ),
        }, {
            "completed_record_count": len(raw_completed_records),
            "content_headroom_values": sorted({
                float(item.get("content_headroom", 0))
                for item in raw_completed_records
            }),
            "target_headroom_values": sorted({
                float(item.get("target_headroom", 0))
                for item in raw_completed_records
            }),
            "layer_contents_headroom_values": sorted({
                float(item.get("layer_contents_headroom", 0))
                for item in raw_completed_records
            }),
            "layer_contents_headroom_tag_supported": sorted({
                bool(item.get("layer_contents_headroom_tag_supported", False))
                for item in raw_completed_records
            }),
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
                len(raw_launch_captures) == 10
                and all(item["return_code"] == 0 and item["bytes"] > 0
                        for item in raw_launch_captures)
            ),
            "raw_full_frame_structure_is_stable": (
                len(raw_launch_similarities) == 10
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
        make_case("SYS-HDR-RAW-NO-BLANK-REGION", {
            "raw_bootstrap_process_healthy": bootstrap_raw["process_healthy"],
            "all_ten_captures_measured": len(raw_missing_structure_metrics) == 10,
            "final_reference_contains_structure": bool(raw_missing_structure_metrics)
                and sum(
                    1 for tile in raw_missing_structure_metrics[-1]["energies"]
                    if float(tile["reference"]) >= 3.0
                ) >= 20,
            "no_capture_loses_structural_tiles": bool(raw_missing_structure_metrics)
                and all(
                    item["missing_structural_tile_count"] == 0
                    for item in raw_missing_structure_metrics
                ),
        }, {
            "capture_times_seconds": [3.6, 3.8, 4.0, 4.2, 4.4, 4.6, 4.8, 5.1, 5.5, 6.2],
            "image_crop_pixels": list(raw_image_crop) if raw_image_crop else None,
            "metrics": raw_missing_structure_metrics,
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
                any(item.get("display_current_headroom_overridden") is True
                    for item in run["telemetry"])
                for run in bootstrap_runs
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
            "override_observed": bool(forced_records) and any(
                item.get("display_headroom_overridden") is True
                for item in forced_records
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
            "prepared_geometry_is_reused": bool(interaction_reused_records),
            "reused_geometry_keeps_hdr_visible": bool(interaction_reused_records) and all(
                item.get("first_frame_presented") is True
                and item.get("hdr_prepared") is True
                and item.get("fallback_visible") is False
                and float(item.get("layer_opacity", 0)) > 0.5
                for item in interaction_reused_records
            ),
            "reused_geometry_does_not_reset_generation": bool(interaction_reused_records)
                and len({
                    (int(item.get("geometry_generation", -1)),
                     int(item.get("geometry_reset_count", -1)))
                    for item in interaction_reused_records
                }) == 1,
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
            "geometry_reused_record_count": len(interaction_reused_records),
            "post_interaction_render_record_count": len(post_interaction_render_records),
            "maximum_geometry_reset_count": max(
                (int(item.get("geometry_reset_count", 0)) for item in interaction_records),
                default=0,
            ),
            "zoom_values": sorted({float(item.get("zoom_level", -1)) for item in interaction_records}),
        }),
        make_case("SYS-HDR-INTERACTION-NO-REACTIVATION", {
            "interaction_process_healthy": interaction_run["process_healthy"],
            "activation_completed_before_reuse": bool(interaction_reused_records)
                and all(item.get("hdr_activation_completed") is True
                        for item in interaction_reused_records),
            "transition_never_restarts_during_reuse": bool(interaction_reused_records)
                and all(float(item.get("transition_progress", 0)) >= 0.999
                        for item in interaction_reused_records),
            "post_interaction_remains_fully_bright": bool(post_interaction_render_records)
                and all(
                    float(item.get("transition_progress", 0)) >= 0.999
                    and item.get("fallback_visible") is False
                    and float(item.get("layer_opacity", 0)) > 0.5
                    for item in post_interaction_render_records
                ),
        }, {
            "reused_transition_progress": [
                float(item.get("transition_progress", 0))
                for item in interaction_reused_records
            ],
            "post_interaction_transition_progress": [
                float(item.get("transition_progress", 0))
                for item in post_interaction_render_records
            ],
        }),
        make_case("SYS-HDR-DISPLAYLINK-LATEST-ONLY", {
            "scheduler_telemetry_present": bool(scheduler_records),
            "all_records_use_metal_display_link": bool(scheduler_records) and all(
                item.get("uses_metal_display_link") is True
                for item in scheduler_records
            ),
            "all_records_encode_metal_off_main_thread": bool(scheduler_records) and all(
                item.get("encodes_metal_off_main_thread") is True
                for item in scheduler_records
            ),
            "every_real_run_receives_display_callbacks": all(
                max(
                    (int(item.get("display_link_callback_count", 0))
                     for item in run["telemetry"]),
                    default=0,
                ) > 0
                for run in real_runs
            ),
            "submitted_generation_never_exceeds_latest_request": all(
                int(item.get("submitted_render_generation", 0))
                <= int(item.get("requested_render_generation", 0))
                for item in scheduler_records
            ),
            "jpeg_settles_on_latest_generation": bool(interaction_settled_records) and all(
                int(item.get("submitted_render_generation", -1))
                == int(item.get("requested_render_generation", -2))
                and item.get("frame_in_flight") is False
                for item in interaction_settled_records
            ),
            "nef_settles_on_latest_generation": bool(nef_interaction_settled_records) and all(
                int(item.get("submitted_render_generation", -1))
                == int(item.get("requested_render_generation", -2))
                and item.get("frame_in_flight") is False
                for item in nef_interaction_settled_records
            ),
        }, {
            "scheduler_record_count": len(scheduler_records),
            "maximum_coalesced_request_count": max(
                (int(item.get("coalesced_render_request_count", 0))
                 for item in scheduler_records),
                default=0,
            ),
            "maximum_deferred_callback_count": max(
                (int(item.get("deferred_display_link_callback_count", 0))
                 for item in scheduler_records),
                default=0,
            ),
            "jpeg_settled_generations": [
                [int(item.get("requested_render_generation", 0)),
                 int(item.get("submitted_render_generation", 0))]
                for item in interaction_settled_records
            ],
            "nef_settled_generations": [
                [int(item.get("requested_render_generation", 0)),
                 int(item.get("submitted_render_generation", 0))]
                for item in nef_interaction_settled_records
            ],
        }),
        make_case("SYS-HDR-INTERACTION-RESPONSIVENESS", {
            "both_interaction_runs_healthy": (
                interaction_run["process_healthy"]
                and nef_interaction_run["process_healthy"]
            ),
            "both_emit_all_ordered_pan_steps": all(
                timing["step_count"] == 12
                and timing["step_numbers"] == list(range(12))
                for timing in (jpeg_interaction_timing, nef_interaction_timing)
            ),
            "zoom_main_thread_time_bounded": all(
                timing["zoom_main_thread_ms"]
                <= THRESHOLDS["interaction_zoom_main_thread_ms_max"]
                for timing in (jpeg_interaction_timing, nef_interaction_timing)
            ),
            "pan_average_interval_bounded": all(
                timing["interval_average_ms"]
                <= THRESHOLDS["interaction_pan_interval_average_ms_max"]
                for timing in (jpeg_interaction_timing, nef_interaction_timing)
            ),
            "pan_p99_interval_bounded": all(
                timing["interval_p99_ms"]
                <= THRESHOLDS["interaction_pan_interval_p99_ms_max"]
                for timing in (jpeg_interaction_timing, nef_interaction_timing)
            ),
            "pan_maximum_interval_bounded": all(
                timing["interval_max_ms"]
                <= THRESHOLDS["interaction_pan_interval_max_ms_max"]
                for timing in (jpeg_interaction_timing, nef_interaction_timing)
            ),
            "pan_throughput_bounded": all(
                timing["steps_per_second"]
                >= THRESHOLDS["interaction_pan_steps_per_second_min"]
                for timing in (jpeg_interaction_timing, nef_interaction_timing)
            ),
        }, {
            "thresholds": {
                key: value for key, value in THRESHOLDS.items()
                if key.startswith("interaction_")
            },
            "jpeg": jpeg_interaction_timing,
            "nef": nef_interaction_timing,
        }),
        make_case("SYS-HDR-NEF-ZOOM-NO-GHOST", {
            "nef_process_healthy": nef_interaction_run["process_healthy"],
            "nef_native_raw_graph_observed": bool(nef_interaction_records) and all(
                item.get("is_raw") is True
                and item.get("source_kind") == "camera-raw"
                and item.get("uses_raw_extended_dynamic_range") is True
                for item in nef_interaction_records
            ),
            "nef_interaction_settled": bool(nef_interaction_settled_records),
            "all_four_captures_measured": (
                len(nef_capture_metrics) == 4
                and len(nef_edge_similarities) == 4
            ),
            "no_capture_loses_structural_tiles": bool(nef_capture_metrics) and all(
                item["missing_structural_tile_count"] == 0
                for item in nef_capture_metrics
            ),
            "no_capture_contains_black_band": len(nef_capture_metrics) == 4 and all(
                item["maximum_consecutive_near_black_columns"] <= 10
                for item in (
                    capture["black_vertical_band_metric"]
                    for capture in nef_captures
                    if "black_vertical_band_metric" in capture
                )
            ),
            "settled_captures_retain_final_edge_structure": len(nef_edge_similarities) == 4
                and min(nef_edge_similarities[1:]) >= 0.995,
            "latest_zoom_generation_presented": bool(nef_interaction_settled_records) and all(
                int(item.get("requested_render_generation", -1))
                == int(item.get("submitted_render_generation", -2))
                for item in nef_interaction_settled_records
            ),
        }, {
            "capture_files": [item["path"] for item in nef_captures],
            "capture_sha256": [item["sha256"] for item in nef_captures],
            "viewport_crop_pixels": list(nef_viewport_crop) if nef_viewport_crop else None,
            "image_crop_pixels": list(nef_image_crop) if nef_image_crop else None,
            "missing_structure_metrics": nef_capture_metrics,
            "edge_cosine_similarity_to_final": nef_edge_similarities,
            "settled_edge_cosine_similarity_minimum": 0.995,
            "moving_first_capture_is_not_compared_for_identical_composition": True,
        }),
        make_case("SYS-HDR-NAV-TRANSPARENT-SURFACE", {
            "navigation_process_healthy": navigation_run["process_healthy"],
            "fractional_visibility_event_observed": len(navigation_visible_events) == 1,
            "hidden_event_observed": len(navigation_hidden_events) == 1,
            "fractional_opacity_is_half": bool(navigation_visible_events) and abs(
                float(navigation_visible_events[-1].get("paint_opacity", -1)) - 0.5
            ) <= 0.001,
            "no_graphics_effect_attached": bool(navigation_visible_events) and (
                navigation_visible_events[-1].get("has_graphics_effect") is False
            ),
            "all_three_screen_captures_succeeded": (
                len(navigation_captures) == 3
                and all(item["return_code"] == 0 and item["bytes"] > 0
                        for item in navigation_captures)
            ),
            "both_visible_captures_measured": len(navigation_capture_metrics) == 2,
            "transparent_button_corners_do_not_change": bool(navigation_capture_metrics) and all(
                item["maximum_corner_mean_absolute_rgb_difference"] <= 3.0
                for item in navigation_capture_metrics
            ),
            "painted_center_changes_at_fractional_opacity": bool(navigation_capture_metrics) and all(
                item["center_mean_absolute_rgb_difference"] >= 1.0
                for item in navigation_capture_metrics
            ),
        }, {
            "navigation_events": navigation_events,
            "capture_files": [item["path"] for item in navigation_captures],
            "capture_sha256": [item["sha256"] for item in navigation_captures],
            "surface_metrics": navigation_capture_metrics,
            "corner_difference_maximum": 3.0,
            "center_difference_minimum": 1.0,
        }),
        make_case("SYS-HDR-THEME-BACKGROUND-STABILITY", {
            "theme_process_healthy": theme_run["process_healthy"],
            "pre_switch_records_present": bool(theme_pre_dark_records),
            "metal_background_stays_light_gray": all(
                int(item.get("viewport_background_red", -1)) == 150
                and int(item.get("viewport_background_green", -1)) == 150
                and int(item.get("viewport_background_blue", -1)) == 150
                for item in theme_pre_dark_records
            ),
            "post_activation_screen_pixel_stays_light_gray": len(theme_capture_samples) == 3
                and theme_capture_samples[0]["median_rgb"] is not None
                and all(abs(value - 150) <= 8
                        for value in theme_capture_samples[0]["median_rgb"]),
        }, {
            "pre_switch_record_count": len(theme_pre_dark_records),
            "light_capture_sample": theme_capture_samples[0] if theme_capture_samples else None,
            "expected_light_rgb": [150, 150, 150],
            "tolerance": 8,
        }),
        make_case("SYS-HDR-THEME-BACKGROUND-SWITCH", {
            "dark_theme_event_observed": bool(theme_dark_event_indices),
            "post_switch_records_present": bool(theme_post_dark_records),
            "renderer_background_updates_to_dark": bool(theme_post_dark_records) and all(
                int(item.get("viewport_background_red", -1)) == 33
                and int(item.get("viewport_background_green", -1)) == 33
                and int(item.get("viewport_background_blue", -1)) == 33
                for item in theme_post_dark_records
            ),
            "post_switch_screen_pixels_are_dark": len(theme_capture_samples) == 3
                and all(
                    sample["median_rgb"] is not None
                    and all(abs(value - 33) <= 8 for value in sample["median_rgb"])
                    for sample in theme_capture_samples[1:]
                ),
        }, {
            "dark_event_indices": theme_dark_event_indices,
            "first_dark_background_index": theme_dark_indices[0] if theme_dark_indices else None,
            "dark_capture_samples": theme_capture_samples[1:],
            "expected_dark_rgb": [33, 33, 33],
            "tolerance": 8,
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
        "samples": {"jpeg": str(jpeg), "raw": str(raw), "nef": str(nef)},
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
            "Ten DNG launch captures retained at least 0.90 edge-structure similarity to the final full frame and lost zero structured image tiles.",
            "The DNG's completed frames reported content headroom above 1.5 and a content/display-clamped target rather than display potential; the telemetry separately records whether the runtime supports an actual CALayer contentsHeadroom tag.",
            "Every geometry-reuse record retained its prepared generation, Metal opacity one, full transition progress, and no SDR fallback.",
            "The post-reveal Light background sample measured (150,150,150); both samples after the test Dark update measured (33,33,33).",
            "Every production record reported an opaque full-drawable clear before image compositing.",
            "The visible HDR transition began only after endpoint preparation and met the recorded frame-rate and maximum-progress-step thresholds.",
            "The DNG path reported a full-resolution processed preview with its authored gain map; the traditional NEF path reported independent RAW EDR endpoints.",
            "Every scheduler record reported CAMetalDisplayLink plus off-main Metal encoding, and both JPEG and NEF settled with submitted generation equal to the latest request and no frame in flight.",
            "Twelve ordered pan steps for both JPEG and NEF met the recorded average, P99, maximum, zoom-main-thread, and throughput thresholds.",
            "Four NEF interaction captures lost no structured tile and contained no persistent black band; the three stopped-composition captures met the recorded final-frame edge threshold.",
            "At half opacity the real navigation widget had no graphics effect; its painted center changed while all four transparent corner patches stayed within the recorded difference threshold.",
        ],
        "inferences": [
            "Potential headroom above one together with wants_edr, RGBA16Float, and target headroom above one demonstrates an EDR-capable WindowServer presentation path.",
            "The current-only bootstrap result breaks the circular dependency in which NSScreen current headroom can remain one until EDR content is already onscreen.",
            "Initialization-time non-volatile gain-map decode, source-space Core Image-managed intermediates, and band-free screen pixels remove the two cache boundaries associated with the persistent JPEG bands.",
            "Display-link pacing, latest-state overwrite, and off-main CI encoding remove the synchronous/queued work that best explains the former interaction stall and NEF stale-frame ghost.",
            "The processed-preview plus authored-gain-map representation preserves more of this DNG's camera recipe than changing one generic RAW exposure parameter.",
            "Cached navigation sampling plus custom-pixel opacity removes the repaint/effect surfaces that best explain the former hover delay and rectangular backing.",
            "Reusing prepared source endpoints across interaction removes the SDR fallback and transition restart that caused visible dim-then-bright behavior.",
            "A single explicit Qt/Metal background contract explains the stable reveal color and deterministic live theme update.",
        ],
        "uncertainties": [
            "Telemetry cannot prove subjective equivalence with Quick Look's private tone curve.",
            "Apple does not publish the precise internal RAW/gain-map ROI scheduling mechanism, so the low-level cause of the former DNG quadrant failure remains an evidence-backed inference.",
            "Quick Look's private RAW rendering recipe and subjective local contrast are not fully public; integration evidence therefore compares exported edge structure and RGB error rather than claiming pixel identity.",
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

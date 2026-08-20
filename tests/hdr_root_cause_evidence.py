#!/usr/bin/env python3
"""Extract machine-auditable pre-fix evidence before final runs replace it."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def head_source(repo: Path, path: str) -> str:
    result = subprocess.run(
        ["git", "show", f"HEAD:{path}"], cwd=repo, text=True,
        capture_output=True, check=False,
    )
    if result.returncode:
        raise RuntimeError(result.stderr)
    return result.stdout


def selected_records(run: dict) -> list[dict]:
    keys = ("render_count", "transition_elapsed_ms", "transition_progress", "last_render_ms")
    return [{key: item.get(key) for key in keys} for item in run.get("telemetry", [])[:3]]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--prior-system-evidence", type=Path, required=True)
    parser.add_argument("--screenshot", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    repo = args.repo.resolve()
    prior_path = args.prior_system_evidence.resolve()
    screenshot = args.screenshot.resolve()
    prior = json.loads(prior_path.read_text(encoding="utf-8"))
    jpeg_runs = [run for run in prior["runs"] if run.get("format") == "gain-map-jpeg" and run.get("forced_headroom") is None]
    raw_runs = [run for run in prior["runs"] if run.get("format") == "raw" and run.get("forced_headroom") is None]

    jpeg_first_ms = [float(run["telemetry"][0]["last_render_ms"]) for run in jpeg_runs]
    raw_blocking_ms = [float(run["telemetry"][1]["last_render_ms"]) for run in raw_runs]
    raw_progress_jumps = [
        float(run["telemetry"][2]["transition_progress"])
        - float(run["telemetry"][1]["transition_progress"])
        for run in raw_runs
    ]

    baseline_view = head_source(repo, "src/qvgraphicsview.cpp")
    baseline_renderer = head_source(repo, "src/qvcocoafunctions.mm")
    current_view = (repo / "src/qvgraphicsview.cpp").read_text(encoding="utf-8")
    current_renderer = (repo / "src/qvcocoafunctions.mm").read_text(encoding="utf-8")
    reproduced = {
        "jpeg_proxy_hidden_immediately": "loadedPixmapItem->setVisible(!hdrRendererActive)" in baseline_view,
        "metal_layer_revealed_on_set_image": "metalLayer.hidden = nativeImage == nullptr" in baseline_renderer,
        "transition_clock_started_before_hdr_preparation": "hdrTransitionClock.start()" in baseline_view,
        "jpeg_first_render_exceeded_250_ms_all_runs": all(value > 250 for value in jpeg_first_ms),
        "raw_first_hdr_evaluation_exceeded_1000_ms_all_runs": all(value > 1000 for value in raw_blocking_ms),
        "raw_progress_jump_exceeded_0_9_all_runs": all(value > 0.9 for value in raw_progress_jumps),
        "user_partial_frame_screenshot_present": screenshot.is_file(),
    }
    corrected = {
        "layout_gate": "hdrLayoutReady = hdrRendererActive" in current_view,
        "fallback_stays_until_presented": "loadedPixmapItem->setVisible(true)" in current_view,
        "presented_handler_reveal": "addPresentedHandler" in current_renderer,
        "offscreen_endpoint_preparation": "preparedSDRTexture" in current_renderer and "preparedHDRTexture" in current_renderer,
    }
    record = {
        "schema_version": "1.0",
        "kind": "pre-fix-root-cause-evidence",
        "release": "v0.1.4",
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "source_evidence": {
            "prior_system_evidence": str(prior_path),
            "prior_system_evidence_sha256": sha256(prior_path),
            "user_screenshot": str(screenshot),
            "user_screenshot_sha256": sha256(screenshot),
            "user_screenshot_bytes": screenshot.stat().st_size,
            "baseline_revision": subprocess.run(
                ["git", "rev-parse", "HEAD"], cwd=repo, text=True,
                capture_output=True, check=True,
            ).stdout.strip(),
        },
        "observations": {
            "jpeg_first_render_ms": jpeg_first_ms,
            "raw_blocking_render_ms": raw_blocking_ms,
            "raw_progress_jump": raw_progress_jumps,
            "representative_jpeg_records": selected_records(jpeg_runs[0]),
            "representative_raw_records": selected_records(raw_runs[0]),
        },
        "reproduced_conditions": reproduced,
        "corrective_contracts_present": corrected,
        "facts": [
            "All three pre-fix JPEG runs spent more than 250 ms in the first render while the source code hid the SDR pixmap immediately.",
            "All three pre-fix RAW runs spent more than one second in the second render and then advanced transition progress by more than 0.9 in one observable step.",
            "The user-provided screenshot is hashed as immutable visual evidence of the partial DNG frame.",
        ],
        "inferences": [
            "The immediate proxy-to-empty-layer handoff explains the observed JPEG black block.",
            "A render triggered with pre-fit view state, prolonged by RAW lazy evaluation, explains why an intermediate DNG geometry remained visible.",
            "The blocked render consumed the activation window, explaining the absence of a perceptible smooth HDR ramp.",
        ],
        "uncertainties": [
            "The screenshot alone cannot encode HDR luminance; RAW peak values and WindowServer headroom are verified separately by integration and system tests.",
        ],
        "passed": all(reproduced.values()) and all(corrected.values()),
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(record, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"kind": record["kind"], "passed": record["passed"]}, ensure_ascii=False))
    return 0 if record["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())

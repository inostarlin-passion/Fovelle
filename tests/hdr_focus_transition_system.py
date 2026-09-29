#!/usr/bin/env python3
"""Focused native-window regression for HDR fade-out and sharp SDR endpoint."""

from __future__ import annotations

import argparse
import json
from datetime import datetime, timezone
from pathlib import Path

from hdr_quality_system import (
    edge_cosine_similarity,
    focus_proxy_matches_source_resolution,
    focus_endpoint_captures,
    focus_transition_metrics,
    launch,
)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--app", type=Path, required=True)
    parser.add_argument("--hdr-image", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if not args.app.is_file() or not args.hdr_image.is_file():
        raise SystemExit("--app and --hdr-image must refer to readable files")

    output = args.output.resolve()
    run = launch(
        args.app.resolve(), args.hdr_image.resolve(), 1,
        focus_transition=True,
        capture_seconds=3.4,
        capture_schedule=[1.2],
        capture_directory=output.parent / "hdr-focus-screens",
    )
    metrics = focus_transition_metrics(run["transition_records"])
    first_presented = next((
        item for item in run["telemetry"]
        if item.get("first_frame_presented") is True
    ), None)
    captures = {
        round(float(item.get("requested_offset_seconds", -1)), 2): item
        for item in run["screen_captures"]
        if item.get("requested_offset_seconds") is not None
    }
    initial, inactive = focus_endpoint_captures(
        run["telemetry"], run["screen_captures"]
    )
    edge_similarity = 0.0
    if (
        initial and inactive
        and initial["return_code"] == 0 and inactive["return_code"] == 0
        and Path(initial["path"]).is_file() and Path(inactive["path"]).is_file()
    ):
        # The runner deliberately zooms before both endpoint captures. Compare
        # the complete native-window captures so both states use identical
        # device-pixel geometry; image-polygon telemetry can be stale during
        # the immediately preceding zoom transaction.
        edge_similarity = edge_cosine_similarity(
            Path(initial["path"]), Path(inactive["path"])
        )

    checks = {
        "process_healthy": run["process_healthy"],
        "fixture_is_a_real_gain_map_hdr_image": (
            first_presented is not None
            and first_presented.get("source_kind") == "adaptive-hdr"
            and (first_presented.get("has_apple_gain_map") is True
                 or first_presented.get("has_iso_gain_map") is True)
            and float(first_presented.get("target_headroom", 1.0)) > 1.0
        ),
        "deactivation_has_multiple_intermediate_frames": metrics[
            "deactivation_has_multiple_intermediate_frames"
        ],
        "deactivation_opacity_decreases_monotonically": metrics[
            "deactivation_opacity_decreases_monotonically"
        ],
        "deactivation_duration_matches_opening": metrics[
            "deactivation_matches_opening_duration"
        ],
        "inactive_endpoint_is_crisp_sdr_proxy": metrics[
            "inactive_endpoint_is_crisp_sdr_proxy"
        ],
        "inactive_proxy_matches_source_resolution":
            focus_proxy_matches_source_resolution(run["telemetry"]),
        "inactive_edge_detail_matches_focused_image": edge_similarity >= 0.90,
        "activation_opacity_increases_monotonically": metrics[
            "activation_opacity_increases_monotonically"
        ],
        "activation_restores_hdr_endpoint": metrics[
            "focused_hdr_endpoint_is_restored"
        ],
    }
    result = {
        "schema_version": "1.0",
        "kind": "hdr-focus-transition-system-test",
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "app": str(args.app.resolve()),
        "hdr_image": str(args.hdr_image.resolve()),
        "checks": checks,
        "passed": all(checks.values()),
        "focus_transition_metrics": metrics,
        "focused_to_inactive_edge_cosine_similarity": edge_similarity,
        "inactive_proxy_matches_source_resolution":
            focus_proxy_matches_source_resolution(run["telemetry"]),
        "screen_captures": run["screen_captures"],
        "transition_record_count": len(run["transition_records"]),
        "first_presented_hdr_record": first_presented,
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())

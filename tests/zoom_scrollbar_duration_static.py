#!/usr/bin/env python3
"""Static acceptance checks for the scrollbar-boundary and zoom-duration fix.

AC-STATIC-01-TRACEABILITY is implemented by this executable gate.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    repo = args.repo.resolve()

    view_h = (repo / "src/qvgraphicsview.h").read_text(encoding="utf-8")
    view_cpp = (repo / "src/qvgraphicsview.cpp").read_text(encoding="utf-8")
    tests_cpp = (repo / "tests/tst_qviewtests.cpp").read_text(encoding="utf-8")
    cmake = (repo / "tests/CMakeLists.txt").read_text(encoding="utf-8")
    reports = {
        name: (repo / "reports" / name).read_text(encoding="utf-8")
        for name in (
            "technical_design_document.md",
            "test_case_specification.md",
            "test_completion_report.md",
        )
    }

    checks: list[dict[str, object]] = []

    def add(identifier: str, passed: bool, actual: object, expected: str) -> None:
        checks.append({"id": identifier, "pass": bool(passed), "actual": actual, "expected": expected})

    duration_markers = (
        "ZoomTransitionMaximumDurationMs = 400",
        "zoomTransitionDurationMs(qreal fromLevel, qreal toLevel",
        "qLn(toLevel / fromLevel)",
        "zoomAnimation->setDuration(transitionDuration);",
        "zoomAbsolute(targetRatio, zoomAnchor, true, animateTransition, true)",
        "false, true, true);",
    )
    add(
        "ST-DURATION-STATIC",
        all(marker in view_h + view_cpp for marker in duration_markers),
        {marker: marker in view_h + view_cpp for marker in duration_markers},
        "semantic zoom uses bounded log-distance duration and Fit->100% opts into it",
    )

    transaction_markers = (
        "QEventLoop::ExcludeUserInputEvents",
        "horizontalTopologyChanged",
        "setUpdatesEnabled(false)",
        "restorePendingZoomAnchor();",
        "requestHDRRendererUpdate();",
    )
    add(
        "ST-HBAR-ATOMIC-FRAME",
        all(marker in view_cpp for marker in transaction_markers),
        {marker: marker in view_cpp for marker in transaction_markers},
        "the scrollbar topology transition is coalesced before the next visible/native frame",
    )

    anchor_markers = (
        "const bool requestedViewportCenter",
        "pendingZoomAnchorViewport = pos",
        "const QPoint anchorViewport = pendingZoomAnchorViewport.value()",
        "transitionDuration + ZoomAnchorSettleDelayMs",
        "if (isZoomTransitionRunning())",
    )
    add(
        "ST-TOGGLE-FROZEN-ANCHOR",
        all(marker in view_cpp for marker in anchor_markers),
        {marker: marker in view_cpp for marker in anchor_markers},
        "Toggle resolves a center request once and keeps its anchor alive through the actual animation and layout settle",
    )

    test_markers = (
        "testZoomTransitionDurationUsesLogDistance",
        "testWheelZoomCrossesHorizontalScrollbarWithoutPositionJump",
        "testToggleFitAnd100FreezesViewportCenterDuringScrollbarTransition",
        "FOVELLE_SCROLLBAR_ZOOM_SAMPLE",
        "QSize(3840, 4407)",
        "four-forward-terminal",
        "one-reverse-terminal",
        "Toggle center anchor moved",
    )
    add(
        "ST-DYNAMIC-TEST-CODE",
        all(marker in tests_cpp for marker in test_markers),
        {marker: marker in tests_cpp for marker in test_markers},
        "tests cover the real 4-in/1-out entry sequence and duration contract",
    )

    required_fields = ("测试目的", "前置条件", "输入数据", "操作步骤", "预期结果", "后置条件")
    atomic_ids = (
        "AC-HBAR-01-ROUND-TRIP",
        "AC-HBAR-02-ANCHOR-CONTINUITY",
        "AC-DURATION-01-LOG-DISTANCE",
        "AC-DURATION-02-FIXED-STEP",
        "AC-TOGGLE-DIRECTIONAL-ANCHOR",
        "AC-TOGGLE-FROZEN-CENTER-ANCHOR",
        "AC-TOGGLE-ANCHOR-LIFETIME",
        "AC-TOGGLE-MONOTONIC-TERMINAL",
        "AC-TOGGLE-QUIESCENT-FINAL",
        "AC-STATIC-01-TRACEABILITY",
    )
    case_ids = (
        "TC-HBAR-FOUR-IN-ONE-OUT",
        "TC-DURATION-LOG-DISTANCE",
        "TC-DURATION-FIXED-STEP",
        "TC-TOGGLE-DIRECTIONAL-ANCHOR",
        "TC-TOGGLE-FROZEN-CENTER-ANCHOR",
        "TC-TOGGLE-ANCHOR-LIFETIME",
        "TC-TOGGLE-STABILITY-TRAJECTORY",
        "TC-STATIC-TRACEABILITY",
    )
    specification = reports["test_case_specification.md"]
    case_sections = {}
    for case_id in case_ids:
        start = specification.find("### " + case_id)
        end = specification.find("\n### ", start + len(case_id)) if start >= 0 else -1
        section = specification[start:end if end >= 0 else None] if start >= 0 else ""
        case_sections[case_id] = {field: field in section for field in required_fields}
    add(
        "ST-REPORT-TRACEABILITY",
        all(all(fields.values()) for fields in case_sections.values())
        and all(identifier in specification for identifier in atomic_ids)
        and all(identifier in reports["technical_design_document.md"] for identifier in atomic_ids)
        and all(identifier in reports["test_completion_report.md"] for identifier in atomic_ids),
        case_sections,
        "every atomic acceptance standard has a six-field structured case and report traceability",
    )

    add(
        "ST-CTEST-REGISTRATION",
        "FovelleZoomScrollbarDurationStatic" in cmake
        and "FovelleScrollbarZoomDurationAcceptance" in cmake
        and "FovelleToggleFitAnchorAcceptance" in cmake
        and "testZoomTransitionDurationUsesLogDistance" in cmake
        and "testWheelZoomCrossesHorizontalScrollbarWithoutPositionJump" in cmake,
        {
            "static_registered": "FovelleZoomScrollbarDurationStatic" in cmake,
            "dynamic_registered": "FovelleScrollbarZoomDurationAcceptance" in cmake,
            "toggle_anchor_registered": "FovelleToggleFitAnchorAcceptance" in cmake,
        },
        "static and dynamic acceptance tests are registered in CTest",
    )

    record = {
        "kind": "zoom-scrollbar-duration-static-check",
        "repo": str(repo),
        "checks": checks,
        "passed": all(check["pass"] for check in checks),
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(record, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(record, ensure_ascii=False, indent=2))
    return 0 if record["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())

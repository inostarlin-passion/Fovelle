#!/usr/bin/env python3
"""Static traceability checks for the Toggle Fit and 100% stability fix."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re


ATOMIC_CRITERIA = (
    "AC-TOGGLE-DIRECTIONAL-ANCHOR",
    "AC-TOGGLE-FROZEN-CENTER-ANCHOR",
    "AC-TOGGLE-ANCHOR-LIFETIME",
    "AC-TOGGLE-MONOTONIC-TERMINAL",
    "AC-TOGGLE-QUIESCENT-FINAL",
)

CASE_IDS = (
    "TC-TOGGLE-DIRECTIONAL-ANCHOR",
    "TC-TOGGLE-FROZEN-CENTER-ANCHOR",
    "TC-TOGGLE-ANCHOR-LIFETIME",
    "TC-TOGGLE-STABILITY-TRAJECTORY",
)

REQUIRED_CASE_FIELDS = (
    "测试目的",
    "前置条件",
    "输入数据",
    "操作步骤",
    "预期结果",
    "后置条件",
)


def function_body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start < 0:
        return ""
    brace = source.find("{", start)
    if brace < 0:
        return ""
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]
    return ""


def markdown_section(markdown: str, heading_token: str) -> str:
    match = re.search(
        rf"^#{{1,6}}\s+.*{re.escape(heading_token)}.*$",
        markdown,
        re.MULTILINE,
    )
    if not match:
        return ""
    remainder = markdown[match.end() :]
    next_heading = re.search(r"^#{1,6}\s+", remainder, re.MULTILINE)
    end = match.end() + (
        next_heading.start() if next_heading else len(remainder)
    )
    return markdown[match.start() : end]


def add_check(
    checks: list[dict[str, object]],
    identifier: str,
    passed: bool,
    actual: object,
    expected: str,
) -> None:
    checks.append(
        {
            "id": identifier,
            "pass": bool(passed),
            "actual": actual,
            "expected": expected,
        }
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--repo", type=Path, default=Path(__file__).resolve().parents[1]
    )
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    repo = args.repo.resolve()
    view_cpp = (repo / "src/qvgraphicsview.cpp").read_text(encoding="utf-8")
    view_h = (repo / "src/qvgraphicsview.h").read_text(encoding="utf-8")
    tests_cpp = (repo / "tests/tst_qviewtests.cpp").read_text(encoding="utf-8")
    cmake = (repo / "tests/CMakeLists.txt").read_text(encoding="utf-8")
    technical = (repo / "reports/technical_design_document.md").read_text(
        encoding="utf-8"
    )
    specification = (repo / "reports/test_case_specification.md").read_text(
        encoding="utf-8"
    )
    completion = (repo / "reports/test_completion_report.md").read_text(
        encoding="utf-8"
    )

    checks: list[dict[str, object]] = []

    helper = function_body(
        view_cpp, "QSize QVGraphicsView::getFitViewportSize"
    )
    recalculate = function_body(
        view_cpp, "void QVGraphicsView::recalculateZoom"
    )
    calculate = function_body(
        view_cpp, "qreal QVGraphicsView::calculateZoomLevelForMode"
    )
    implementation_contract = {
        "helper_declared": "QSize getFitViewportSize" in view_h,
        "uses_documented_maximum": "maximumViewportSize()" in helper,
        "subtracts_titlebar_obscuration": "obscuredHeight" in helper,
        "preserves_fit_overscan": "fitOverscan * 2" in helper,
        "recalculate_uses_stable_size": (
            "getFitViewportSize(true)" in recalculate
            and "getFitViewportSize()" in recalculate
            and "getUsableViewportRect(true).size()" not in recalculate
        ),
        "query_uses_same_stable_size": (
            "getFitViewportSize(true)" in calculate
            and "getFitViewportSize()" in calculate
            and "getUsableViewportRect(true).size()" not in calculate
        ),
    }
    add_check(
        checks,
        "ST-FIT-01",
        all(implementation_contract.values()),
        implementation_contract,
        "both fit calculators use one no-scrollbar viewport contract while retaining titlebar and overscan adjustments",
    )

    test_body = function_body(
        tests_cpp,
        "void GraphicsViewTests::testToggleFitReturnHasMonotonicStableTerminalSize()",
    )
    executable_contract = {
        "all_atomic_markers": all(
            criterion in tests_cpp for criterion in ATOMIC_CRITERIA
        ),
        "all_dynamic_case_markers": all(
            case_id in tests_cpp
            for case_id in CASE_IDS
            if case_id != "TC-STATIC-FIT-TARGET-CONTRACT"
        ),
        "real_z_binding": (
            'QStringLiteral("togglefitand100")' in test_body
            and "QKeySequence(Qt::Key_Z)" in test_body
            and "QTest::keySequence" in test_body
        ),
        "stable_target_oracle": (
            "referenceFitLevel" in test_body
            and "fitZoomChangeSpy.count(), 1" in test_body
        ),
        "monotonic_oracle": (
            "reversalCount" in test_body and "reversalCount == 0" in test_body
        ),
        "terminal_oracle": (
            "lastAnimationValueSize" in test_body
            and "animationFinishedSize" in test_body
        ),
        "quiet_oracle": (
            "QTest::qWait(650)" in test_body and "quietSize" in test_body
        ),
        "portable_fixture": "QSize(2560, 2938)" in test_body,
        "provided_fixture": (
            "provided-3840x4407-jpeg" in tests_cpp
            and "FOVELLE_TOGGLE_FIT_SAMPLE" in tests_cpp
        ),
    }
    add_check(
        checks,
        "ST-FIT-02",
        all(executable_contract.values()),
        executable_contract,
        "the QtTest encodes every atomic oracle, a portable same-aspect fixture, and the provided JPEG row",
    )

    fields_by_case: dict[str, object] = {}
    for case_id in CASE_IDS:
        section = markdown_section(specification, case_id)
        fields_by_case[case_id] = {
            field: field in section for field in REQUIRED_CASE_FIELDS
        }
    add_check(
        checks,
        "ST-FIT-03",
        all(
            section_fields and all(section_fields.values())
            for section_fields in fields_by_case.values()
        ),
        fields_by_case,
        "each structured case contains purpose, preconditions, input, steps, expected result, and postconditions",
    )

    traceability = {
        criterion: {
            "technical_design": criterion in technical,
            "test_specification": criterion in specification,
            "completion_report": criterion in completion,
            "test_code": criterion in tests_cpp,
        }
        for criterion in ATOMIC_CRITERIA
    }
    add_check(
        checks,
        "ST-FIT-04",
        all(all(locations.values()) for locations in traceability.values()),
        traceability,
        "every atomic acceptance identifier is traceable across design, case specification, executable code, and completion report",
    )

    research_sources = (
        "https://doc.qt.io/qt-6/qabstractscrollarea.html",
        "https://doc.qt.io/qt-6/qgraphicsview.html",
        "https://github.com/qt/qtbase/blob/v6.11.1/src/widgets/graphicsview/qgraphicsview.cpp",
        "https://d3js.org/d3-interpolate/zoom",
    )
    research_contract = {
        source: source in technical for source in research_sources
    }
    research_contract.update(
        {
            "explicit_premises": "显式前提" in technical,
            "evidence_gaps": "证据缺口" in technical,
            "cross_validation": "交叉验证" in technical,
            "chain_reasoning": "链式推理" in technical,
        }
    )
    add_check(
        checks,
        "ST-FIT-05",
        all(research_contract.values()),
        research_contract,
        "the design records the authoritative multi-hop sources, evidence gaps, premises, cross-validation, and reasoning chain",
    )

    registration_contract = {
        "static_registered": "FovelleToggleFitStabilityStatic" in cmake,
        "dynamic_registered": "FovelleToggleFitStabilityAcceptance" in cmake,
        "dynamic_entry_point": (
            "testToggleFitReturnHasMonotonicStableTerminalSize" in cmake
        ),
        "static_and_dynamic_labels": (
            'LABELS "zoom;toggle-fit;static;acceptance"' in cmake
            and 'LABELS "zoom;toggle-fit;dynamic;acceptance"' in cmake
        ),
    }
    add_check(
        checks,
        "ST-FIT-06",
        all(registration_contract.values()),
        registration_contract,
        "CTest registers separate static and dynamic acceptance gates",
    )

    result = {
        "kind": "toggle-fit-stability-static",
        "passed": all(check["pass"] for check in checks),
        "atomic_criteria": list(ATOMIC_CRITERIA),
        "checks": checks,
    }
    output = args.output if args.output.is_absolute() else repo / args.output
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        json.dumps(result, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())

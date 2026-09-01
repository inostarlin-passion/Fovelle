#!/usr/bin/env python3
"""Static acceptance checks for the three requested UI regressions."""

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

    source = {
        relative: (repo / relative).read_text(encoding="utf-8")
        for relative in (
            "src/mainwindow.cpp",
            "src/qvgraphicsview.cpp",
            "src/qvgraphicsview.h",
            "src/qvimagecore.cpp",
            "src/qvimagecore.h",
            "src/qvinfodialog.cpp",
            "src/qvinfodialog.h",
            "tests/tst_qviewtests.cpp",
        )
    }
    checks: list[dict] = []

    def add(identifier: str, passed: bool, actual: dict, expected: str) -> None:
        checks.append(
            {
                "id": identifier,
                "pass": bool(passed),
                "actual": actual,
                "expected": expected,
            }
        )

    navigation_source = source["src/mainwindow.cpp"] + source["src/qvimagecore.cpp"]
    navigation_test = source["tests/tst_qviewtests.cpp"]
    navigation_markers = (
        "bool QVImageCore::hasPreviousFile()",
        "bool QVImageCore::hasNextFile()",
        "getIsLoopFoldersEnabled()",
        "const bool leftAvailable = graphicsView->hasPreviousFile();",
        "const bool rightAvailable = graphicsView->hasNextFile();",
        "navigationRequestedVisible",
        "testPreviousNavigationButtonHiddenWithoutPreviousFile",
        "testNextNavigationButtonHiddenWithoutNextFile",
    )
    add(
        "ST-TASK-NAVIGATION",
        all(marker in navigation_source + navigation_test for marker in navigation_markers),
        {marker: marker in navigation_source + navigation_test for marker in navigation_markers},
        "boundary visibility is derived from adjacent-file availability and has dedicated first/last tests",
    )

    info_source = source["src/qvinfodialog.cpp"] + source["src/qvinfodialog.h"]
    locale_markers = (
        "formatModifiedDateTime",
        "SettingsManager::languageCodeForLocale",
        "MMM d, yyyy, h:mm AP",
        "yyyy年M月d日 HH:mm",
        "yyyy年M月d日 APh:mm",
        "d MMM yyyy, HH:mm",
        "testFileInfoModifiedUsesUiLanguageFormats",
    )
    add(
        "ST-TASK-MODIFIED-LOCALE",
        all(marker in info_source + navigation_test for marker in locale_markers),
        {marker: marker in info_source + navigation_test for marker in locale_markers},
        "Modified is formatted through the selected UI language with all reference formats covered by QtTest",
    )

    scrollbar_source = source["src/qvgraphicsview.cpp"] + source["src/mainwindow.cpp"]
    scrollbar_markers = (
        "verticalScrollBar()->parentWidget()",
        "scheduleVerticalScrollBarGeometry",
        "QScrollBar::rangeChanged",
        "getViewportPosition().obscuredHeight",
        "setGeometry(adjustedGeometry)",
        "testVerticalScrollBarAvoidsTitlebarOverlap",
    )
    add(
        "ST-TASK-SCROLLBAR-SAFE-AREA",
        all(marker in scrollbar_source + navigation_test for marker in scrollbar_markers),
        {marker: marker in scrollbar_source + navigation_test for marker in scrollbar_markers},
        "the physical vertical scrollbar geometry is reapplied after Qt layout and checked against the titlebar inset",
    )

    specification = (repo / "reports/test_case_specification.md").read_text(encoding="utf-8")
    required_spec_fields = (
        "测试目的",
        "前置条件",
        "输入数据",
        "操作步骤",
        "预期结果",
        "后置条件",
    )
    specification_markers = (
        "AC-NAV-PREVIOUS-ABSENT",
        "AC-NAV-NEXT-ABSENT",
        "AC-FILEINFO-MODIFIED-FORMAT",
        "AC-SCROLLBAR-TITLEBAR-INSET",
    )
    test_case_ids = (
        "TC-NAV-PREVIOUS-ABSENT",
        "TC-NAV-NEXT-ABSENT",
        "TC-FILEINFO-MODIFIED-FORMAT",
        "TC-SCROLLBAR-TITLEBAR-INSET",
    )
    case_field_presence = {}
    for case_id in test_case_ids:
        heading = f"## {case_id}"
        start = specification.find(heading)
        end = specification.find("\n## ", start + len(heading)) if start >= 0 else -1
        section = specification[start:end if end >= 0 else None] if start >= 0 else ""
        case_field_presence[case_id] = {
            field: field in section for field in required_spec_fields
        }
    all_cases_have_required_fields = all(
        all(fields.values()) for fields in case_field_presence.values()
    )
    add(
        "ST-TASK-SPECIFICATION",
        all(marker in specification for marker in specification_markers)
        and all_cases_have_required_fields,
        {
            "case_fields": case_field_presence,
            "acceptance_ids": {marker: marker in specification for marker in specification_markers},
        },
        "the Markdown test specification contains six required fields for every atomic acceptance case",
    )

    record = {
        "kind": "task-acceptance-static-check",
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

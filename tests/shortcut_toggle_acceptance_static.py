#!/usr/bin/env python3
"""Static acceptance checks for the fit/100% shortcut replacement.

The script deliberately checks the production source, the four translation
catalogs, the executable-test markers, and the Markdown case fields.  It does
not replace the Cocoa QtTest run; it provides a reproducible source-level
traceability gate for the same atomic criteria.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import xml.etree.ElementTree as ET


def add_check(
    checks: list[dict],
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


def section_for(markdown: str, heading: str) -> str:
    start = markdown.find(f"## {heading}")
    if start < 0:
        return ""
    end = markdown.find("\n## ", start + 1)
    return markdown[start : end if end >= 0 else len(markdown)]


def translated_messages(path: Path, source: str) -> dict[str, list[tuple[str, str | None]]]:
    root = ET.parse(path).getroot()
    result: dict[str, list[tuple[str, str | None]]] = {}
    for context in root.findall("context"):
        name = context.findtext("name") or ""
        for message in context.findall("message"):
            if message.findtext("source") != source:
                continue
            translation = message.find("translation")
            value = "" if translation is None else "".join(translation.itertext()).strip()
            message_type = None if translation is None else translation.get("type")
            result.setdefault(name, []).append((value, message_type))
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    repo = args.repo.resolve()
    shortcut_cpp = (repo / "src/shortcutmanager.cpp").read_text(encoding="utf-8")
    action_cpp = (repo / "src/actionmanager.cpp").read_text(encoding="utf-8")
    mainwindow_cpp = (repo / "src/mainwindow.cpp").read_text(encoding="utf-8")
    tests_cpp = (repo / "tests/tst_qviewtests.cpp").read_text(encoding="utf-8")
    specification = (repo / "reports/test_case_specification.md").read_text(encoding="utf-8")

    checks: list[dict] = []

    list_start = shortcut_cpp.find("void ShortcutManager::initializeShortcutsList()")
    list_end = shortcut_cpp.find("void ShortcutManager::hideShortcuts()", list_start)
    shortcut_list = shortcut_cpp[list_start:list_end if list_end >= 0 else len(shortcut_cpp)]
    shortcut_inventory = {
        "new_source": 'tr("Toggle Fit and 100%")' in shortcut_list,
        "new_key": '"togglefitand100"' in shortcut_list,
        "default_z": 'QStringList(QKeySequence(Qt::Key_Z).toString())' in shortcut_list,
        "old_original_absent": 'tr("Original Size")' not in shortcut_list,
        "old_fit_absent": 'tr("Zoom to Fit")' not in shortcut_list,
        "old_navigation_absent": 'tr("Navigation Resets Zoom")' not in shortcut_list,
    }
    add_check(
        checks,
        "ST-SC-01",
        all(shortcut_inventory.values()),
        shortcut_inventory,
        "the Shortcuts inventory contains only the new combined row and defaults it to Z",
    )

    action_inventory = {
        "view_menu_uses_new_key": 'addCloneOfAction(viewMenu, "togglefitand100")' in action_cpp,
        "dispatch_uses_new_key": 'key == "togglefitand100"' in action_cpp
        and "relevantWindow->toggleFitAnd100();" in action_cpp,
        "new_action_registered": 'actionLibrary.insert("togglefitand100", toggleFitAnd100Action);' in action_cpp,
        "old_action_registration_absent": all(
            f'actionLibrary.insert("{key}"' not in action_cpp
            for key in ("originalsize", "zoomtofit", "navresetszoom")
        ),
        "old_view_menu_clones_absent": all(
            f'addCloneOfAction(viewMenu, "{key}")' not in action_cpp
            for key in ("originalsize", "zoomtofit", "navresetszoom")
        ),
    }
    add_check(
        checks,
        "ST-SC-02",
        all(action_inventory.values()),
        action_inventory,
        "the View menu, action library, and dispatcher expose the same combined action key",
    )

    toggle_start = mainwindow_cpp.find("void MainWindow::toggleFitAnd100()")
    fill_start = mainwindow_cpp.find("void MainWindow::setFillWindow", toggle_start)
    toggle_method = mainwindow_cpp[toggle_start:fill_start if fill_start >= 0 else len(mainwindow_cpp)]
    behavior_inventory = {
        "fit_state_is_tested": "getCalculatedZoomMode() == Qv::CalculatedZoomMode::ZoomToFit" in toggle_method,
        "manual_zoom_is_one": "zoomAbsolute(1.0, Qv::CalculateViewportCenterPos);" in toggle_method,
        "fit_state_is_selected": "setCalculatedZoomMode(Qv::CalculatedZoomMode::ZoomToFit);" in toggle_method,
        "fit_recalculated": "fitOrConstrainImage();" in toggle_method,
    }
    add_check(
        checks,
        "ST-SC-03",
        all(behavior_inventory.values()),
        behavior_inventory,
        "the MainWindow state machine maps non-fit to fit and fit to exact 100%",
    )

    test_inventory = {
        marker: marker in tests_cpp
        for marker in (
            "AC-SHORTCUT-ACTION-SURFACE",
            "AC-SHORTCUT-DEFAULT-Z",
            "AC-SHORTCUT-TOGGLE-BEHAVIOR",
            "AC-SHORTCUT-TRANSLATIONS",
        )
    }
    test_inventory.update(
        {
            name: name in tests_cpp
            for name in (
                "testToggleFitAnd100IsTheOnlyFitShortcutAction",
                "testToggleFitAnd100DefaultsToZ",
                "testToggleFitAnd100ChangesBetweenFitAnd100Percent",
                "testToggleFitAnd100Translations",
            )
        }
    )
    add_check(
        checks,
        "ST-SC-04",
        all(test_inventory.values()),
        test_inventory,
        "each shortcut atomic criterion has an executable QtTest marker and entry point",
    )

    expected_translations = {
        "qview_zh_Hans.ts": "切换适合窗口/100%",
        "qview_zh_Hant.ts": "切換符合視窗/100%",
        "qview_es.ts": "Alternar Ajustar/100 %",
        "qview_ja.ts": "合わせる/100%切り替え",
    }
    translation_inventory: dict[str, object] = {}
    for catalog_name, expected in expected_translations.items():
        path = repo / "i18n" / catalog_name
        try:
            messages = translated_messages(path, "Toggle Fit and 100%")
            context_results = {}
            for context in ("ActionManager", "ShortcutManager"):
                candidates = messages.get(context, [])
                context_results[context] = any(
                    value == expected and message_type != "unfinished"
                    for value, message_type in candidates
                )
            translation_inventory[catalog_name] = context_results
        except (OSError, ET.ParseError) as error:
            translation_inventory[catalog_name] = {"error": str(error)}
    add_check(
        checks,
        "ST-SC-05",
        all(
            isinstance(result, dict) and all(result.get(context, False) for context in ("ActionManager", "ShortcutManager"))
            for result in translation_inventory.values()
        ),
        translation_inventory,
        "all four supported catalogs contain the exact completed translation in both production contexts",
    )

    required_fields = ("测试目的", "前置条件", "输入数据", "操作步骤", "预期结果", "后置条件")
    case_ids = (
        "TC-SC-ACTION-SURFACE",
        "TC-SC-DEFAULT-Z",
        "TC-SC-TOGGLE-BEHAVIOR",
        "TC-SC-TRANSLATIONS",
    )
    fields_by_case = {
        case_id: {field: field in section_for(specification, case_id) for field in required_fields}
        for case_id in case_ids
    }
    add_check(
        checks,
        "ST-SC-06",
        all(section_for(specification, case_id) and all(fields.values()) for case_id, fields in fields_by_case.items()),
        {"fields_by_case": fields_by_case},
        "each atomic shortcut case documents all six requested test-design fields",
    )

    result = {
        "kind": "shortcut-toggle-acceptance-static",
        "passed": all(check["pass"] for check in checks),
        "checks": checks,
    }
    output = args.output if args.output.is_absolute() else repo / args.output
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())

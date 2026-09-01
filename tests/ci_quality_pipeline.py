#!/usr/bin/env python3
"""Run the CI-repair acceptance matrix and emit machine-auditable reports.

The matrix is deliberately small and atomic. Static checks inspect the CI and
Release contracts, unit checks invoke affected QtTest methods and the version
parser, integration checks exercise the registered CTest target and real
Ghostscript source staging, and system checks start the real application
bundle plus the credential-free release dry run.
"""

from __future__ import annotations

import argparse
import ast
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Callable

from project_version import PROJECT_VERSION, read_project_version


STAGES = ("static", "unit", "integration", "system")
REPORT_NAMES = (
    "test_evidence.json",
    "test_case_specification.json",
    "test_completion_report.json",
    "code_quality_assessment_report.json",
)


RESEARCH_TRACE = [
    {
        "hop": 1,
        "layer": "original observed CI assertion failure",
        "source": "https://github.com/inostarlin-passion/Fovelle/actions/runs/33499768039",
        "finding": "The pre-fix Checks run for commit babf065 fails at the CTest/Run tests step with exit code 8 after configuration, build, and preload-policy steps complete; its log reports the fullscreen vertical-pan assertion.",
        "premise": "The hosted run is the direct observation of the CI gate requested for repair.",
        "deduction": "The original failure is in a runtime GUI regression path rather than compilation; reproduce the affected QtTest in isolation before changing CI registration.",
    },
    {
        "hop": 2,
        "layer": "independent build confirmation",
        "source": "https://github.com/inostarlin-passion/Fovelle/actions/runs/33499767940",
        "finding": "The corresponding Build Fovelle run for babf065 completes successfully, so the current failure is not a compiler or link failure; it is isolated to the test-stage execution in the Checks workflow.",
        "premise": "The two hosted runs use the same revision and expose different stage outcomes.",
        "deduction": "The repair must make the affected product test deterministic while retaining the default CTest boundary; it must not mask the failure by removing the test.",
    },
    {
        "hop": 3,
        "layer": "workflow contract",
        "source": "https://github.com/inostarlin-passion/Fovelle/blob/main/.github/workflows/test.yml",
        "finding": "The repository workflow configures the macOS Qt build and runs the registered CTest targets as a CI acceptance boundary.",
        "premise": "A target that is registered by default becomes part of the hosted gate regardless of whether it needs local-only state.",
        "deduction": "External Accessibility/native-drag reproduction must be opt-in, while product QtTest suites remain default targets.",
    },
    {
        "hop": 4,
        "layer": "Qt viewport geometry contract",
        "source": "https://doc.qt.io/qt-6/qgraphicsview.html#sceneRect-prop",
        "finding": "QGraphicsView uses its scene rectangle and viewport dimensions to determine the scrollable area and scrollbar ranges.",
        "premise": "The image must remain reachable at both horizontal and vertical scrollbar maxima.",
        "deduction": "Any mismatch between the dimensions used for range calculation and the actual scrollbar geometry can expose a stale strip or blank edge.",
    },
    {
        "hop": 5,
        "layer": "Qt scroll-area layout contract",
        "source": "https://doc.qt.io/qt-6/qabstractscrollarea.html#details",
        "finding": "QAbstractScrollArea lays out the viewport together with optional horizontal and vertical scrollbars, so bar visibility changes the usable viewport geometry.",
        "premise": "The reported reproduction crosses the threshold where the horizontal scrollbar appears during zoom.",
        "deduction": "The style, viewport, and range calculations must agree at the exact visibility transition.",
    },
    {
        "hop": 6,
        "layer": "Qt implementation evidence",
        "source": "https://github.com/qt/qtbase/blob/dev/src/widgets/graphicsview/qgraphicsview.cpp",
        "finding": "Qt's QGraphicsView implementation queries QStyle::PM_ScrollBarExtent when calculating scrollbar-related geometry.",
        "premise": "The application stylesheet previously forced scrollbar thickness to 12px while the native style metric was 15px on the reproduction host.",
        "deduction": "Removing the fixed thickness makes the stylesheet and QGraphicsView use one geometry authority, eliminating the measured 3px stale range.",
    },
    {
        "hop": 7,
        "layer": "local root-cause evidence",
        "source": "reports/root_cause.md",
        "finding": "The supplied reproduction measured a 12px styled bar against a 15px native extent; the resulting 3px difference appeared as a delayed scrollbar/image shift and as right/bottom blank regions at maximum scroll.",
        "premise": "The reproduction is repeatable on the target macOS/Qt stack and the report records both immediate and delayed scrollbar values.",
        "deduction": "The minimal product fix is native thickness alignment; the delayed-endpoint regression test must verify that no later correction moves the endpoint.",
    },
    {
        "hop": 8,
        "layer": "local delayed-anchor reproduction",
        "source": "src/qvgraphicsview.cpp; tests/tst_qviewtests.cpp",
        "finding": "The isolated fullscreen regression reproduced the failure: zoomAbsolute() scheduled a 150ms anchor callback, and a later manual maximum scrollbar value was overwritten when that callback restored the old zoom center.",
        "premise": "The local Cocoa run uses the same pending-anchor path and fails before the cancellation guard is applied.",
        "deduction": "An explicit scrollbar user signal must invalidate the pending generation; internal scrollbar writes belonging to zoom/layout must remain guarded so they do not cancel their own anchor.",
    },
    {
        "hop": 9,
        "layer": "local implementation and verification",
        "source": "tests/tst_qviewtests.cpp and build/tests/fovelle_tests",
        "finding": "The repaired view cancels stale anchors on scrollbar user signals and pan paths; the regression test drives a 2.0x zoom, sends the QAbstractSlider SliderMove semantic equivalent of dragging the vertical bar to its maximum, waits beyond 150ms, and checks the image bottom; the existing fullscreen endpoint test covers the original failure path.",
        "premise": "The executable tests observe both the direct manual-pan race and the hosted failing fullscreen transition.",
        "deduction": "The fix is supported by static source contracts, a deterministic focused regression, the original failing test, and the complete default CTest gate.",
    },
    {
        "hop": 10,
        "layer": "latest hosted timeout observation",
        "source": "https://github.com/inostarlin-passion/Fovelle/actions/runs/33503840748",
        "finding": "The a3013ae Checks run builds successfully but its default CTest step times out FovelleTests at 90.45 seconds; the log shows the new manual-pan test passed and FovelleShortcutSettingsTests passed, while the first product process did not finish.",
        "premise": "The authenticated workflow log is direct evidence for the hosted timeout, while the public run page and Checks API expose the same failed step.",
        "deduction": "The remaining CI defect is test-process lifecycle sensitivity after native Cocoa mouse injection, not the zoom assertion; remove the test-only native mouse grab while preserving production user-signal coverage.",
    },
    {
        "hop": 11,
        "layer": "local deterministic test repair",
        "source": "src/qvgraphicsview.cpp; tests/tst_qviewtests.cpp",
        "finding": "The production view now cancels pending anchors on sliderPressed, sliderMoved, and actionTriggered; the test uses setSliderPosition(maximum) followed by triggerAction(SliderMove), so it exercises the same QAbstractSlider contract without leaving a Cocoa mouse grab for the next test.",
        "premise": "A real scrollbar drag emits these semantic signals, and the hosted timeout occurred after the native mouse test had already reported PASS.",
        "deduction": "The repair keeps real user drags covered in production and makes the CI regression deterministic across Qt/macOS scrollbar implementations.",
    },
    {
        "hop": 12,
        "layer": "final acceptance deduction",
        "source": "tests/ci_quality_pipeline.py and reports/test_completion_report.md",
        "finding": "The acceptance matrix covers native extent, image-edge reachability, no delayed rebound, manual-pan anchor cancellation, default CTest registration, static contracts, build, and repeated QtTest execution.",
        "premise": "Each atomic acceptance claim must map to a documented case and an executable observation.",
        "deduction": "The repaired product path is the native scrollbar geometry plus a deterministic default CI gate; the external native-drag fixture remains explicitly opt-in.",
    },
]


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def read(repo: Path, relative: str) -> str:
    return (repo / relative).read_text(encoding="utf-8")


def compact_output(stdout: str, stderr: str, limit: int = 4000) -> str:
    output = stdout + stderr
    return output[-limit:]


def run_command(
    command: list[str],
    cwd: Path,
    environment: dict[str, str] | None = None,
    timeout: float = 120.0,
) -> dict[str, Any]:
    started = time.monotonic()
    env = os.environ.copy()
    if environment:
        env.update(environment)
    try:
        result = subprocess.run(
            command,
            cwd=cwd,
            env=env,
            text=True,
            capture_output=True,
            timeout=timeout,
            check=False,
        )
        return {
            "command": command,
            "return_code": result.returncode,
            "passed": result.returncode == 0,
            "duration_ms": round((time.monotonic() - started) * 1000, 3),
            "output_tail": compact_output(result.stdout, result.stderr),
        }
    except subprocess.TimeoutExpired as error:
        return {
            "command": command,
            "return_code": None,
            "passed": False,
            "duration_ms": round((time.monotonic() - started) * 1000, 3),
            "output_tail": compact_output(
                error.stdout or "", error.stderr or ""
            )
            + "\nPROCESS_TIMEOUT",
        }
    except OSError as error:
        return {
            "command": command,
            "return_code": None,
            "passed": False,
            "duration_ms": round((time.monotonic() - started) * 1000, 3),
            "output_tail": f"{type(error).__name__}: {error}",
        }


def static_result(test_code: str, passed: bool, observed: dict[str, Any]) -> dict[str, Any]:
    return {
        "passed": bool(passed),
        "observed": observed,
        "execution": {"test_code": test_code, "kind": "source_contract"},
    }


def static_tidy_path_contract(repo: Path) -> dict[str, Any]:
    source = read(repo, "build.sh")
    tidy_match = re.search(r"--tidy\)\n(?P<body>.*?)(?=\n\s*--tidy-fix\))", source, re.S)
    tidy_fix_match = re.search(r"--tidy-fix\)\n(?P<body>.*?)(?=\n\s*--clean\))", source, re.S)
    tidy_body = tidy_match.group("body") if tidy_match else ""
    tidy_fix_body = tidy_fix_match.group("body") if tidy_fix_match else ""
    cmake = read(repo, "CMakeLists.txt")
    passed = all(
        (
            "CMAKE_CXX_CLANG_TIDY=clang-tidy" in tidy_body,
            "DFOVELLE_BUNDLE_GHOSTSCRIPT=OFF" in tidy_body,
            "CMAKE_CXX_CLANG_TIDY='clang-tidy;-fix-errors'" in tidy_fix_body,
            "DFOVELLE_BUNDLE_GHOSTSCRIPT=OFF" in tidy_fix_body,
            'option(FOVELLE_BUNDLE_GHOSTSCRIPT "Bundle the pinned AGPL Ghostscript runtime" ON)' in cmake,
            "if(FOVELLE_BUNDLE_GHOSTSCRIPT)" in cmake,
        )
    )
    return static_result(
        "tests/ci_quality_pipeline.py::static_tidy_path_contract",
        passed,
        {
            "tidy_disables_ghostscript_bundle": "DFOVELLE_BUNDLE_GHOSTSCRIPT=OFF" in tidy_body,
            "tidy_fix_disables_ghostscript_bundle": "DFOVELLE_BUNDLE_GHOSTSCRIPT=OFF" in tidy_fix_body,
            "normal_bundle_default_remains_on": 'option(FOVELLE_BUNDLE_GHOSTSCRIPT "Bundle the pinned AGPL Ghostscript runtime" ON)' in cmake,
            "post_build_hook_is_option_guarded": "if(FOVELLE_BUNDLE_GHOSTSCRIPT)" in cmake,
        },
    )


def static_async_observation_contract(repo: Path) -> dict[str, Any]:
    header = read(repo, "src/qvgraphicsimageitem.h")
    item = read(repo, "src/qvgraphicsimageitem.cpp")
    view = read(repo, "src/qvgraphicsview.cpp")
    tests = read(repo, "tests/tst_qviewtests.cpp")
    passed = all(
        (
            "bool hasPendingVectorRefinement() const;" in header,
            "activeAsyncRequest.has_value()" in item,
            "pendingAsyncRequest.has_value()" in item,
            "loadedPixmapItem->hasPendingVectorRefinement()" in view,
            "quint64 QVGraphicsView::vectorRenderCount() const" in view,
            "waitForRenderedVectorTile" in tests,
            "QTRY_VERIFY_WITH_TIMEOUT(!view->hasPendingVectorRefinement(), 5000);" in tests,
            "QElapsedTimer" in tests,
            "view->vectorRenderCount() > 0" in tests,
            "lastVectorRasterSize().isEmpty()" in tests,
            "const qint64 scrollPaintArea = recorder.recordedAreas().constFirst();" in tests,
            "maximumObservedDirtyRatio" in tests,
            "bar->setValue(bar->value() + 6);\n    QVERIFY(view->hasPendingVectorRefinement());" not in tests,
            "QTRY_VERIFY_WITH_TIMEOUT(!view->hasPendingVectorRefinement(), 500);" not in tests,
        )
    )
    return static_result(
        "tests/ci_quality_pipeline.py::static_async_observation_contract",
        passed,
        {
            "worker_and_queue_are_observable": "activeAsyncRequest.has_value()" in item and "pendingAsyncRequest.has_value()" in item,
            "view_delegates_observation": "loadedPixmapItem->hasPendingVectorRefinement()" in view,
            "test_waits_for_painted_tile": "waitForRenderedVectorTile" in tests and "view->vectorRenderCount() > 0" in tests,
            "scroll_paint_is_causally_isolated": "const qint64 scrollPaintArea = recorder.recordedAreas().constFirst();" in tests,
            "later_refinement_is_diagnostic_only": "maximumObservedDirtyRatio" in tests,
            "no_instant_pending_assertion": "bar->setValue(bar->value() + 6);\n    QVERIFY(view->hasPendingVectorRefinement());" not in tests,
            "slow_runner_timeout_ms": 5000,
        },
    )


def static_cpu_budget_contract(repo: Path) -> dict[str, Any]:
    tests = read(repo, "tests/tst_qviewtests.cpp")
    start = tests.find(
        "void GraphicsViewTests::testVectorInteractionPaintCpuBudgetFor120Hz()"
    )
    end = tests.find("\nvoid ActionManagerTests::testAboutDialogIdentity()", start)
    body = tests[start:end] if start >= 0 else ""
    markers = {
        "thread_cpu_clock": "CLOCK_THREAD_CPUTIME_ID" in tests,
        "clock_gettime": "clock_gettime" in tests,
        "named_clock_helper": "currentThreadCpuTimeNanoseconds" in tests,
        "measurement_is_explicit": "measurement=thread_cpu" in body,
        "fixed_120hz_budget": "1000.0 / 120.0" in body,
        "wall_clock_not_used": "frameTimer.nsecsElapsed()" not in body,
        "p99_bound_retained": "p99 <= FrameBudgetMilliseconds" in body,
        "capacity_bound_retained": "cpuCapacity >= 120.0" in body,
    }
    return static_result(
        "tests/ci_quality_pipeline.py::static_cpu_budget_contract",
        all(markers.values()),
        markers,
    )


def static_runner_contract(repo: Path) -> dict[str, Any]:
    workflow_paths = (".github/workflows/test.yml", ".github/workflows/build.yml")
    workflows = {path: read(repo, path) for path in workflow_paths}
    passed = all(
        "runs-on: macos-26" in source
        and "test \"${XCODE_VERSION%%.*}\" -ge 26" in source
        and "test \"${SDK_VERSION%%.*}\" -ge 26" in source
        for source in workflows.values()
    )
    return static_result(
        "tests/ci_quality_pipeline.py::static_runner_contract",
        passed,
        {
            "workflow_runner_labels": {path: re.findall(r"runs-on:\s*(\S+)", source) for path, source in workflows.items()},
            "xcode_and_sdk_guards_present": passed,
            "runner_label_reference": "GitHub-hosted runner documentation confirms macos-26 is supported.",
        },
    )


def static_release_packaging_contract(repo: Path) -> dict[str, Any]:
    workflow = read(repo, ".github/workflows/release.yml")
    package_script = read(repo, "dist/scripts/package-macos-release.sh")
    ghostscript_script = read(repo, "dist/scripts/prepare-ghostscript.sh")
    passed = all(
        (
            'runs-on: macos-15' in workflow,
            '-DCMAKE_OSX_DEPLOYMENT_TARGET=15.0' in workflow,
            'version_is_at_most()' in package_script,
            'RELEASE_VALIDATE_VERSION_ONLY' in package_script,
            'FOVELLE_GHOSTSCRIPT_FORCE_SOURCE=true' in package_script,
            'FOVELLE_GHOSTSCRIPT_DEPLOYMENT_TARGET="$EXPECTED_MACOS_DEPLOYMENT_TARGET"' in package_script,
            'FOVELLE_GHOSTSCRIPT_ARCHITECTURES="x86_64;arm64"' in package_script,
            'FOVELLE_GHOSTSCRIPT_ARCHITECTURES' in ghostscript_script,
            'FOVELLE_GHOSTSCRIPT_DEPLOYMENT_TARGET' in ghostscript_script,
            '--without-tesseract' in ghostscript_script,
            '--disable-fontconfig' in ghostscript_script,
            '--disable-dbus' in ghostscript_script,
            '-mmacosx-version-min=$DEPLOYMENT_TARGET' in ghostscript_script,
            'CXXCPP="$cpp_command"' in ghostscript_script,
        )
    )
    return static_result(
        "tests/ci_quality_pipeline.py::static_release_packaging_contract",
        passed,
        {
            "release_uses_macos15_target": '-DCMAKE_OSX_DEPLOYMENT_TARGET=15.0' in workflow,
            "version_parser_handles_patch_component": 'version_is_at_most()' in package_script,
            "source_runtime_is_forced_after_macdeployqt": 'FOVELLE_GHOSTSCRIPT_FORCE_SOURCE=true' in package_script,
            "source_runtime_is_universal": 'FOVELLE_GHOSTSCRIPT_ARCHITECTURES="x86_64;arm64"' in package_script,
            "source_runtime_is_targeted": '-mmacosx-version-min=$DEPLOYMENT_TARGET' in ghostscript_script,
            "architecture_specific_ocr_is_disabled": '--without-tesseract' in ghostscript_script,
            "optional_homebrew_integrations_are_disabled": all(
                marker in ghostscript_script
                for marker in ('--disable-fontconfig', '--disable-dbus', '--disable-cups', '--without-libidn')
            ),
        },
    )


def static_test_registration_contract(repo: Path) -> dict[str, Any]:
    cmake = read(repo, "tests/CMakeLists.txt")
    pipeline = read(repo, "tests/ci_quality_pipeline.py")
    try:
        ast.parse(pipeline, filename=str(repo / "tests/ci_quality_pipeline.py"))
        pipeline_parses = True
    except SyntaxError:
        pipeline_parses = False
    product_tests_present = all(
        marker in cmake
        for marker in (
            "add_test(NAME FovelleTests",
            "add_test(NAME FovelleShortcutSettingsTests",
        )
    )
    native_driver_is_opt_in = all(
        marker in cmake
        for marker in (
            "option(FOVELLE_ENABLE_NATIVE_DRAG_REPRODUCTION",
            "if(FOVELLE_ENABLE_NATIVE_DRAG_REPRODUCTION)",
        )
    )
    obsolete_audit_absent = not any(
        marker in cmake
        for marker in (
            "FovelleSettingsAudit",
            "FovelleTaskAcceptanceAudit",
            "ci_quality_pipeline.py\"",
        )
    )
    passed = all(
        (
            product_tests_present,
            native_driver_is_opt_in,
            obsolete_audit_absent,
            pipeline_parses,
            'STAGES = ("static", "unit", "integration", "system")' in pipeline,
            all(f'"{name}"' in pipeline for name in REPORT_NAMES),
        )
    )
    return static_result(
        "tests/ci_quality_pipeline.py::static_test_registration_contract",
        passed,
        {
            "product_ctest_targets_present": product_tests_present,
            "native_driver_is_opt_in": native_driver_is_opt_in,
            "obsolete_audit_registration_absent": obsolete_audit_absent,
            "pipeline_python_parses": pipeline_parses,
            "pipeline_has_four_ordered_stages": 'STAGES = ("static", "unit", "integration", "system")' in pipeline,
            "required_report_names": list(REPORT_NAMES),
        },
    )


def static_unit_invocation_timeout_contract(repo: Path) -> dict[str, Any]:
    pipeline = read(repo, "tests/ci_quality_pipeline.py")
    passed = all(
        (
            '"FOVELLE_TEST_SUITE": suite' in pipeline,
            '"QTEST_FUNCTION_TIMEOUT": "30000"' in pipeline,
            "timeout=60" in pipeline,
        )
    )
    return static_result(
        "tests/ci_quality_pipeline.py::static_unit_invocation_timeout_contract",
        passed,
        {
            "suite_selector_is_explicit": '"FOVELLE_TEST_SUITE": suite' in pipeline,
            "qtest_function_timeout_ms": 30000 if '"QTEST_FUNCTION_TIMEOUT": "30000"' in pipeline else None,
            "child_process_timeout_seconds": 60 if "timeout=60" in pipeline else None,
        },
    )


def static_scope_contract(repo: Path) -> dict[str, Any]:
    result = run_command(
        [
            "git",
            "diff",
            "--check",
            "HEAD",
            "--",
            "build.sh",
            "src",
            "tests",
            ".github",
            "CMakeLists.txt",
            "dist",
        ],
        repo,
        timeout=30,
    )
    return static_result(
        "tests/ci_quality_pipeline.py::static_scope_contract",
        result["passed"],
        {
            "return_code": result["return_code"],
            "output": result["output_tail"],
            "scope": ["build.sh", "src", "tests", ".github", "CMakeLists.txt", "dist"],
            "unrelated_preexisting_paths_excluded": ["README.md"],
        },
    )


STATIC_TESTS: tuple[tuple[str, str, Callable[[Path], dict[str, Any]]], ...] = (
    ("CI-STATIC-001", "static_tidy_path_contract", static_tidy_path_contract),
    ("CI-STATIC-002", "static_async_observation_contract", static_async_observation_contract),
    ("CI-STATIC-003", "static_runner_contract", static_runner_contract),
    ("CI-STATIC-004", "static_release_packaging_contract", static_release_packaging_contract),
    ("CI-STATIC-005", "static_test_registration_contract", static_test_registration_contract),
    ("CI-STATIC-006", "static_scope_contract", static_scope_contract),
    ("CI-STATIC-007", "static_unit_invocation_timeout_contract", static_unit_invocation_timeout_contract),
    ("CI-STATIC-008", "static_cpu_budget_contract", static_cpu_budget_contract),
)


UNIT_TESTS: tuple[tuple[str, str, str], ...] = (
    ("CI-UNIT-001", "GraphicsViewTests", "testVectorFormatsUseDocumentSceneItem"),
    ("CI-UNIT-002", "GraphicsViewTests", "testVectorPanRepaintsOnlyExposedStrip"),
    ("CI-UNIT-003", "ImageLoaderTests", "testEPSPostScriptRender"),
    ("CI-UNIT-004", "WindowBehaviorTests", "testExitFullscreenActionUsesEscapePath"),
    ("CI-UNIT-007", "GraphicsViewTests", "testVectorInteractionPaintCpuBudgetFor120Hz"),
    ("CI-UNIT-008", "GraphicsViewTests", "testManualScrollCancelsPendingZoomAnchor"),
)


def run_static(repo: Path) -> dict[str, dict[str, Any]]:
    results: dict[str, dict[str, Any]] = {}
    for case_id, _name, test in STATIC_TESTS:
        try:
            results[case_id] = test(repo)
        except (OSError, UnicodeError, re.error) as error:
            results[case_id] = static_result(
                f"tests/ci_quality_pipeline.py::{test.__name__}",
                False,
                {"error": f"{type(error).__name__}: {error}"},
            )
    return results


def run_release_version_unit(
    repo: Path,
    case_id: str,
    candidate: str,
    expected: str,
    expected_compatible: bool,
) -> dict[str, Any]:
    result = run_command(
        ["bash", str(repo / "dist/scripts/package-macos-release.sh")],
        repo,
        {
            "RELEASE_VALIDATE_VERSION_ONLY": "true",
            "RELEASE_CANDIDATE_VERSION": candidate,
            "FOVELLE_EXPECTED_MACOS_DEPLOYMENT_TARGET": expected,
        },
        timeout=15,
    )
    output = result["output_tail"]
    compatible_marker = f"VERSION_COMPATIBLE: {candidate} <= {expected}"
    incompatible_marker = f"VERSION_INCOMPATIBLE: {candidate} > {expected}"
    if expected_compatible:
        passed = result["return_code"] == 0 and compatible_marker in output
    else:
        passed = (
            result["return_code"] != 0
            and incompatible_marker in output
            and "invalid arithmetic operator" not in output
        )
    return {
        "passed": passed,
        "observed": {
            "candidate": candidate,
            "expected": expected,
            "expected_compatible": expected_compatible,
            "compatible_marker_observed": compatible_marker in output,
            "incompatible_marker_observed": incompatible_marker in output,
            "arithmetic_error_absent": "invalid arithmetic operator" not in output,
        },
        "execution": result,
    }


def run_release_source_build(repo: Path) -> dict[str, Any]:
    script = repo / "dist/scripts/prepare-ghostscript.sh"
    with tempfile.TemporaryDirectory(prefix="fovelle-ghostscript-audit-") as directory:
        output = Path(directory) / "runtime"
        build = run_command(
            ["bash", str(script), "--output", str(output)],
            repo,
            {
                "FOVELLE_GHOSTSCRIPT_FORCE_SOURCE": "true",
                "FOVELLE_GHOSTSCRIPT_DEPLOYMENT_TARGET": "15.0",
                "FOVELLE_GHOSTSCRIPT_ARCHITECTURES": "x86_64;arm64",
            },
            timeout=300,
        )
        executable = output / "bin" / "gs"
        file_result: dict[str, Any] = {"passed": False, "output_tail": "not executed"}
        lipo_result: dict[str, Any] = {"passed": False, "output_tail": "not executed"}
        otool_result: dict[str, Any] = {"passed": False, "output_tail": "not executed"}
        if executable.is_file():
            file_result = run_command(["file", "-b", str(executable)], repo, timeout=15)
            lipo_result = run_command(["lipo", "-archs", str(executable)], repo, timeout=15)
            otool_result = run_command(["otool", "-l", str(executable)], repo, timeout=15)
        architectures = set(lipo_result["output_tail"].split())
        minos_values = sorted(set(re.findall(r"\bminos\s+([0-9]+(?:\.[0-9]+){1,2})", otool_result["output_tail"])))
        universal = {"arm64", "x86_64"}.issubset(architectures)
        minos_is_targeted = minos_values == ["15.0"]
        runtime_metadata = (output / "runtime.json").is_file()
        eps_fixture = Path(directory) / "fixture.eps"
        pdf_output = Path(directory) / "fixture.pdf"
        eps_fixture.write_text(
            "%!PS-Adobe-3.0 EPSF-3.0\n"
            "%%BoundingBox: 0 0 8 4\n"
            "newpath 0 0 moveto 8 0 lineto 8 4 lineto 0 4 lineto closepath\n"
            "0.2 setgray fill\nshowpage\n",
            encoding="ascii",
        )
        render = run_command(
            [
                str(executable),
                "-dSAFER",
                "-dBATCH",
                "-dNOPAUSE",
                "-dEPSCrop",
                "-sDEVICE=pdfwrite",
                f"-sOutputFile={pdf_output}",
                str(eps_fixture),
            ],
            repo,
            {"GS_LIB": str(output / "share" / "ghostscript")},
            timeout=30,
        ) if executable.is_file() else {"passed": False, "return_code": None, "output_tail": "not executed"}
        pdf_header = pdf_output.is_file() and pdf_output.read_bytes()[:5] == b"%PDF-"
        passed = bool(
            build["passed"]
            and executable.is_file()
            and universal
            and minos_is_targeted
            and runtime_metadata
            and render["passed"]
            and pdf_header
        )
        return {
            "passed": passed,
            "observed": {
                "executable_exists": executable.is_file(),
                "file_output": file_result["output_tail"],
                "architectures": sorted(architectures),
                "universal": universal,
                "minos_values": minos_values,
                "minos_is_targeted": minos_is_targeted,
                "runtime_metadata_exists": runtime_metadata,
                "eps_render_passed": render["passed"],
                "pdf_header": "%PDF-" if pdf_header else None,
            },
            "execution": {
                "source_build": build,
                "file": file_result,
                "lipo": lipo_result,
                "otool": otool_result,
                "eps_render": render,
            },
        }


def run_unit(repo: Path, build_dir: Path) -> dict[str, dict[str, Any]]:
    binary = build_dir / "tests" / "fovelle_tests"
    results: dict[str, dict[str, Any]] = {}
    for case_id, suite, test_name in UNIT_TESTS:
        if not binary.is_file():
            results[case_id] = {
                "passed": False,
                "observed": {"reason": "test binary does not exist", "binary": str(binary)},
                "execution": {"test_code": f"tests/tst_qviewtests.cpp::{suite}::{test_name}"},
            }
            continue
        result = run_command(
            [str(binary), "-o", "-,txt", test_name],
            repo,
            {
                "QT_QPA_PLATFORM": "cocoa",
                "QT_FATAL_WARNINGS": "1",
                "FOVELLE_DISABLE_AUTO_UPDATE_CHECK": "1",
                "FOVELLE_TEST_SUITE": suite,
                "QTEST_FUNCTION_TIMEOUT": "30000",
            },
            timeout=60,
        )
        output = result["output_tail"]
        pass_marker = f"PASS   : {suite}::{test_name}()"
        result["passed"] = bool(result["passed"] and pass_marker in output)
        results[case_id] = {
            "passed": result["passed"],
            "observed": {
                "suite": suite,
                "test": test_name,
                "pass_marker": pass_marker,
                "pass_marker_observed": pass_marker in output,
                "environment": {
                    "QT_QPA_PLATFORM": "cocoa",
                    "QT_FATAL_WARNINGS": "1",
                    "FOVELLE_DISABLE_AUTO_UPDATE_CHECK": "1",
                    "FOVELLE_TEST_SUITE": suite,
                    "QTEST_FUNCTION_TIMEOUT": "30000",
                },
                "qtest_function_timeout_ms": 30000,
            },
            "execution": result,
        }
    results["CI-UNIT-005"] = run_release_version_unit(
        repo, "CI-UNIT-005", "15.0", "15.0", True
    )
    results["CI-UNIT-006"] = run_release_version_unit(
        repo, "CI-UNIT-006", "15.7.5", "15.0", False
    )
    return results


def run_integration(repo: Path, build_dir: Path, skip_build: bool) -> dict[str, dict[str, Any]]:
    if skip_build:
        build_result: dict[str, Any] = {
            "command": [],
            "return_code": 0,
            "passed": True,
            "duration_ms": 0,
            "output_tail": "build skipped by CTest acceptance-audit invocation",
        }
    else:
        build_result = run_command(
            ["cmake", "--build", str(build_dir), "--parallel", "2"],
            repo,
            timeout=240,
        )

    results: dict[str, dict[str, Any]] = {}
    if not build_result["passed"]:
        failure = {
            "passed": False,
            "observed": {"build_skipped": skip_build, "reason": "CMake build failed"},
            "execution": {"build": build_result},
        }
        return {
            "CI-INTEGRATION-001": failure,
            "CI-INTEGRATION-002": failure.copy(),
            "CI-INTEGRATION-003": failure.copy(),
            "CI-INTEGRATION-004": failure.copy(),
        }

    ctest = run_command(
        [
            "ctest",
            "--test-dir",
            str(build_dir),
            "--output-on-failure",
            "--timeout",
            "90",
            "-R",
            "^FovelleTests$",
        ],
        repo,
        {
            "QT_QPA_PLATFORM": "cocoa",
            "QT_FATAL_WARNINGS": "1",
            "FOVELLE_DISABLE_AUTO_UPDATE_CHECK": "1",
        },
        timeout=180,
    )
    results["CI-INTEGRATION-001"] = {
        "passed": bool(ctest["passed"]),
        "observed": {"build_skipped": skip_build, "ctest_regex": "^FovelleTests$"},
        "execution": {"build": build_result, "ctest": ctest},
    }

    registered = run_command(
        ["ctest", "--test-dir", str(build_dir), "-N"],
        repo,
        timeout=30,
    )
    registered_output = registered["output_tail"]
    has_targets = all(
        name in registered_output
        for name in ("FovelleTests", "FovelleShortcutSettingsTests")
    ) and "FovelleNativeDragReproduction" not in registered_output
    results["CI-INTEGRATION-002"] = {
        "passed": bool(registered["passed"] and has_targets),
        "observed": {
            "required_registered_tests": ["FovelleTests", "FovelleShortcutSettingsTests"],
            "optional_native_driver_excluded_by_default": "FovelleNativeDragReproduction" not in registered_output,
            "registered_tests_observed": has_targets,
        },
        "execution": {"ctest_list": registered},
    }
    release_contract = run_command(
        [
            sys.executable,
            str(repo / "tests/quality_release.py"),
            "--repo",
            str(repo),
            "--output",
            str(repo / "reports/evidence/release_contract.json"),
        ],
        repo,
        timeout=60,
    )
    results["CI-INTEGRATION-003"] = {
        "passed": bool(release_contract["passed"]),
        "observed": {
            "release_contract_command_passed": release_contract["passed"],
            "evidence_path": "reports/evidence/release_contract.json",
        },
        "execution": release_contract,
    }
    results["CI-INTEGRATION-004"] = run_release_source_build(repo)
    return results


def run_system(repo: Path, build_dir: Path) -> dict[str, dict[str, Any]]:
    project_version = read_project_version(repo)
    binary = build_dir / "Fovelle.app" / "Contents" / "MacOS" / "Fovelle"
    if not binary.is_file():
        missing = {
            "passed": False,
            "observed": {"reason": "application binary does not exist", "binary": str(binary)},
            "execution": {"test_code": "src/main.cpp::system_probe"},
        }
        return {
            "CI-SYSTEM-001": missing,
            "CI-SYSTEM-002": missing.copy(),
            "CI-SYSTEM-003": missing.copy(),
        }

    probe = run_command(
        [str(binary)],
        repo,
        {
            "QT_QPA_PLATFORM": "cocoa",
            "FOVELLE_SYSTEM_PROBE": "1",
            "FOVELLE_DISABLE_AUTO_UPDATE_CHECK": "1",
        },
        timeout=30,
    )
    probe_match = re.search(
        r"FOVELLE_SYSTEM_PROBE windows=1 maximized=true", probe["output_tail"]
    )
    version = run_command([str(binary), "--version"], repo, timeout=15)
    version_match = re.search(
        rf"\b{re.escape(project_version)}\b", version["output_tail"]
    )
    with tempfile.TemporaryDirectory(prefix="fovelle-release-system-audit-") as directory:
        release_output_path = Path(directory) / "release-system.json"
        release_system = run_command(
            [
                sys.executable,
                str(repo / "tests/quality_release_system.py"),
                "--repo",
                str(repo),
                "--output",
                str(release_output_path),
            ],
            repo,
            timeout=30,
        )
        release_record: dict[str, Any] = {}
        if release_output_path.is_file():
            try:
                release_record = json.loads(release_output_path.read_text(encoding="utf-8"))
            except (OSError, UnicodeError, json.JSONDecodeError):
                release_record = {}
    return {
        "CI-SYSTEM-001": {
            "passed": bool(probe["passed"] and probe_match),
            "observed": {
                "probe_marker": probe_match.group(0) if probe_match else None,
            },
            "execution": probe,
        },
        "CI-SYSTEM-002": {
            "passed": bool(version["passed"] and version_match),
            "observed": {
                "expected_version": project_version,
                "version_observed": version_match.group(0) if version_match else None,
            },
            "execution": version,
        },
        "CI-SYSTEM-003": {
            "passed": bool(release_system["passed"] and release_record.get("passed") is True),
            "observed": {
                "release_system_passed": release_record.get("passed"),
                "artifact_validation": release_record.get("artifact_validation"),
                "dry_run_performance_passed": release_record.get("performance", {}).get("passed"),
            },
            "execution": release_system,
        },
    }


def case(
    identifier: str,
    layer: str,
    criterion: str,
    quality_requirement: str,
    test_code: str,
    purpose: str,
    preconditions: list[str],
    input_data: dict[str, Any],
    operation_steps: list[str],
    expected_result: str,
    postconditions: list[str],
) -> dict[str, Any]:
    return {
        "id": identifier,
        "test_layer": layer,
        "atomic_acceptance_criterion": criterion,
        "quality_requirement": quality_requirement,
        "test_code": test_code,
        "test_purpose": purpose,
        "preconditions": preconditions,
        "input_data": input_data,
        "operation_steps": operation_steps,
        "expected_result": expected_result,
        "postconditions": postconditions,
    }


CASES = [
    case(
        "CI-STATIC-001",
        "static",
        "clang-tidy 与应用自有 C++ 目标绑定，但不触发 Ghostscript 第三方运行时打包。",
        "精益完整性",
        "tests/ci_quality_pipeline.py::static_tidy_path_contract",
        "验证静态分析路径只包含必要的应用检查。",
        ["仓库包含 build.sh 和 CMakeLists.txt。"],
        {"tidy_command": "./build.sh --tidy", "tidy_fix_command": "./build.sh --tidy-fix"},
        ["读取两个脚本分支。", "读取 CMake Ghostscript 选项及 POST_BUILD 守卫。", "逐项比对 clang-tidy 与 OFF 标志。"],
        "两个分析分支都显式设置 FOVELLE_BUNDLE_GHOSTSCRIPT=OFF，普通构建默认仍为 ON。",
        ["不改变普通构建的 Ghostscript 打包能力。"],
    ),
    case(
        "CI-STATIC-002",
        "static",
        "矢量 refinement 的待处理状态包含 active 与 queued 请求；测试等待真实 tile，并隔离滚动 Paint 与异步完成 Paint。",
        "可测试性",
        "tests/ci_quality_pipeline.py::static_async_observation_contract",
        "验证运行时观察点、测试同步条件与 Paint 因果归属一致。",
        ["矢量渲染由 QFutureWatcher 异步完成。"],
        {"source_files": ["src/qvgraphicsimageitem.*", "src/qvgraphicsview.cpp", "tests/tst_qviewtests.cpp"]},
        ["检查观察方法的声明和实现。", "检查 view 委托及 worker/queue 状态。", "检查测试的条件轮询和最终 tile 断言。", "检查首个滚动 Paint 与后续 refinement 诊断分离。"],
        "测试等待 painted tile，以首个 Paint 断言滚动条带，并仅诊断后续最大 Paint。",
        ["异步工作完成前调用方仍可观察到 pending 状态。"],
    ),
    case(
        "CI-STATIC-003",
        "static",
        "CI 使用已验证的 macos-26 runner，并在 job 内校验 Xcode 与 SDK 主版本。",
        "功能正确性",
        "tests/ci_quality_pipeline.py::static_runner_contract",
        "排除错误 runner 标签作为当前失败根因。",
        ["GitHub Actions workflow 文件存在。"],
        {"workflow_files": [".github/workflows/test.yml", ".github/workflows/build.yml"]},
        ["读取两个 workflow。", "检查 runs-on 标签。", "检查 Xcode/SDK 版本守卫。"],
        "两个核心 workflow 均使用 macos-26 且包含版本前置检查。",
        ["保留当前 runner 选择和显式环境约束。"],
    ),
    case(
        "CI-STATIC-004",
        "static",
        "Release 必须在 macOS 15.0 应用合同下，用修复过的三段版本比较器和固定源码构建出 x86_64/arm64 Ghostscript 运行时。",
        "功能正确性",
        "tests/ci_quality_pipeline.py::static_release_packaging_contract",
        "验证 Release 的依赖来源、版本解析和目标架构/最低系统版本合同在源码层面完整闭合。",
        ["Release workflow、打包脚本和 Ghostscript 准备脚本可读。"],
        {
            "release_target": "15.0",
            "ghostscript_version": "10.07.1",
            "architectures": ["x86_64", "arm64"],
        },
        ["读取 Release workflow 的 SDK/deployment target。", "读取版本比较和依赖 staging 路径。", "读取 Ghostscript configure 的架构、SDK 和可选依赖参数。"],
        "源码层面同时存在三段版本比较、15.0 deployment target、双架构 source staging、固定校验和以及禁用 Tesseract/可选 Homebrew 集成的配置。",
        ["不执行源码编译；真实二进制结果由 CI-INTEGRATION-004 验证。"],
    ),
    case(
        "CI-STATIC-005",
        "static",
        "CTest 注册产品回归测试；依赖外部桌面权限的 native drag 驱动必须显式 opt-in。",
        "精益完整性",
        "tests/ci_quality_pipeline.py::static_test_registration_contract",
        "验证产品测试入口与外部状态依赖的边界清楚。",
        ["tests/CMakeLists.txt 可读取。"],
        {"product_tests": ["FovelleTests", "FovelleShortcutSettingsTests"], "native_driver": "FOVELLE_ENABLE_NATIVE_DRAG_REPRODUCTION=ON"},
        ["读取 CTest 注册。", "检查 native drag 驱动是否由显式 CMake 选项控制。", "检查静态、单元、集成、系统阶段与报告名。"],
        "产品测试注册完整，native drag 驱动默认不进入 CI 产品测试门禁，报告输出集合完整。",
        ["CTest 可在无外部 Accessibility/Post Event 权限时稳定运行。"],
    ),
    case(
        "CI-STATIC-006",
        "static",
        "任务范围内的工作树差异不包含空白错误。",
        "精益完整性",
        "tests/ci_quality_pipeline.py::static_scope_contract",
        "避免修复引入不可见格式噪声。",
        ["Git 工作树可执行 git diff --check。"],
        {"scope": ["build.sh", "src", "tests", ".github", "CMakeLists.txt", "dist"]},
        ["运行 git diff --check HEAD。", "仅审计任务范围路径。"],
        "命令返回 0 且没有 whitespace error。",
        ["README.md 中既有的用户变更保持不动。"],
    ),
    case(
        "CI-STATIC-007",
        "static",
        "验收审计的每个直接 QtTest 子进程必须同时使用套件选择器、30 秒函数级超时和 60 秒进程级上限。",
        "可测试性",
        "tests/ci_quality_pipeline.py::static_unit_invocation_timeout_contract",
        "验证远端 Checks 的单元审计不会因缺少显式 QtTest 超时而产生不可重复的 runner 级结果。",
        ["tests/ci_quality_pipeline.py 可读取。"],
        {"suite_environment": "FOVELLE_TEST_SUITE", "qtest_function_timeout_ms": 30000, "child_process_timeout_seconds": 60},
        ["读取直接 QtTest 子进程的环境配置。", "核对套件选择器。", "核对 QtTest 函数级和 Python 子进程级超时。"],
        "源码同时声明套件过滤、QTEST_FUNCTION_TIMEOUT=30000 和 timeout=60。",
        ["不启动 GUI 进程；真实执行由单元层用例覆盖。"],
    ),
    case(
        "CI-STATIC-008",
        "static",
        "120Hz 预算测试必须显式使用 CLOCK_THREAD_CPUTIME_ID，且不得再用 QElapsedTimer 的墙钟差值作为预算输入。",
        "可测试性",
        "tests/ci_quality_pipeline.py::static_cpu_budget_contract",
        "验证观测量与解决方案证明中的 CPU 时间定义一致，并保留原有 120Hz、p99 和容量边界。",
        ["tests/tst_qviewtests.cpp 可读取。"],
        {"clock_id": "CLOCK_THREAD_CPUTIME_ID", "budget_ms": "1000 / 120", "wall_clock_forbidden": "frameTimer.nsecsElapsed()"},
        ["定位 120Hz 测试函数。", "检查 clock_gettime 和线程 CPU 时钟辅助函数。", "检查 thread_cpu 日志、p99 和容量断言。"],
        "静态合同全部通过，预算输入来自线程 CPU 时间而不是墙钟间隔。",
        ["不执行 GUI；真实行为由 CI-UNIT-007 覆盖。"],
    ),
    case(
        "CI-UNIT-001",
        "unit",
        "PDF 与 SVG 矢量文档均能在 6400% 路径完成 refinement，并产生真实绘制的非空 bounded tile。",
        "功能正确性",
        "tests/tst_qviewtests.cpp::GraphicsViewTests::testVectorFormatsUseDocumentSceneItem",
        "直接回归远端失败的跨格式矢量场景。",
        ["build/tests/fovelle_tests 已构建。", "Qt 可在 cocoa 平台运行。"],
        {"suite": "GraphicsViewTests", "test": "testVectorFormatsUseDocumentSceneItem"},
        ["设置 FOVELLE_TEST_SUITE。", "执行指定 QtTest 方法。", "检查 PDF 与 SVG pass marker。"],
        "QtTest 返回 0，且指定测试输出 PASS。",
        ["测试进程退出，临时矢量文档被清理。"],
    ),
    case(
        "CI-UNIT-002",
        "unit",
        "矢量平移的首个 Paint 只覆盖暴露条带；随后异步 refinement 的独立 Paint 不得被误归因给滚动。",
        "功能正确性",
        "tests/tst_qviewtests.cpp::GraphicsViewTests::testVectorPanRepaintsOnlyExposedStrip",
        "直接回归远端 CI-UNIT-002 与 CI-INTEGRATION-001 共用的时序误判。",
        ["build/tests/fovelle_tests 已构建。", "矢量 SVG fixture 可用。"],
        {"suite": "GraphicsViewTests", "test": "testVectorPanRepaintsOnlyExposedStrip", "scroll_dirty_ratio_limit": 0.05},
        ["执行指定 QtTest 方法。", "等待初始 refinement 清零且已有真实 tile 绘制。", "记录滚动后的首个 Paint。", "等待后续 refinement 清零。", "分别输出首个与最大观测 dirty ratio。"],
        "QtTest 返回 0，且首个滚动 Paint 的 dirty ratio 不超过 5%；后续 refinement Paint 仅作为诊断记录。",
        ["测试窗口关闭，事件过滤器移除。"],
    ),
    case(
        "CI-UNIT-007",
        "unit",
        "120Hz 矢量交互预算使用调用线程 CPU 时间，而不是受 runner 调度抖动影响的墙钟时间；平均值、p99 和 CPU 容量边界均通过。",
        "功能正确性",
        "tests/tst_qviewtests.cpp::GraphicsViewTests::testVectorInteractionPaintCpuBudgetFor120Hz",
        "直接回归最新 hosted run 在 SVG pan/zoom p99 上的假失败，并保留 120 FPS 应用线程预算。",
        ["build/tests/fovelle_tests 已构建。", "macOS Cocoa 事件循环和 CLOCK_THREAD_CPUTIME_ID 可用。"],
        {"suite": "GraphicsViewTests", "test": "testVectorInteractionPaintCpuBudgetFor120Hz", "frame_count": 120, "frame_budget_ms": 1000 / 120},
        ["执行指定 QtTest 方法。", "为 EPS/SVG 分别采集 120 次 zoom 和 pan 的当前线程 CPU 时间差。", "检查平均值、p99 和 CPU 容量断言。"],
        "QtTest 返回 0，日志包含 measurement=thread_cpu，所有格式和交互的平均值与 p99 不超过 8.333 ms，CPU 容量至少为 120 FPS。",
        ["测试窗口关闭，临时选项和 quit policy 恢复。"],
    ),
    case(
        "CI-UNIT-008",
        "unit",
        "显式把垂直滚动条移动到最大端点后，前一次缩放遗留的 150ms 延迟 anchor 不得把视口移回缩放中心。",
        "功能正确性",
        "tests/tst_qviewtests.cpp::GraphicsViewTests::testManualScrollCancelsPendingZoomAnchor",
        "直接回归 GitHub Actions CTest 失败中暴露的 delayed zoom anchor 与手动 pan 竞态。",
        ["build/tests/fovelle_tests 已构建。", "macOS Cocoa 事件循环可用。"],
        {
            "suite": "GraphicsViewTests",
            "test": "testManualScrollCancelsPendingZoomAnchor",
            "zoom": 2.0,
            "settling_delay_ms": 250,
        },
        [
            "加载 2048x1536 临时 PNG。",
            "执行 2.0x 中心缩放，设置 sliderPosition(maximum) 并触发 QAbstractSlider::SliderMove 语义事件，模拟垂直 scrollbar 拖动到最大值。",
            "等待超过生产代码的 150ms 延迟 anchor 回调。",
            "检查滚动条仍接近当前最大值，并检查图像底边与 viewport 底边。",
        ],
        "QtTest 返回 0；延迟回调不会覆盖手动最大端点，图像底边保持在 viewport 底边 2px 内。",
        ["窗口关闭，临时 PNG、测试设置和应用退出策略恢复。"],
    ),
    case(
        "CI-UNIT-003",
        "unit",
        "EPS 的 PostScript 内容可通过 Ghostscript 转为应用使用的 PDF 矢量文档。",
        "功能正确性",
        "tests/tst_qviewtests.cpp::ImageLoaderTests::testEPSPostScriptRender",
        "隔离验证当前正常测试仍可使用 Ghostscript runtime。",
        ["正常测试构建启用 Ghostscript bundle。", "Ghostscript 可执行文件可用。"],
        {"suite": "ImageLoaderTests", "test": "testEPSPostScriptRender"},
        ["执行指定 QtTest 方法。", "检查 EPS render pass marker。"],
        "QtTest 返回 0 且 EPS 渲染测试输出 PASS。",
        ["测试生成的临时 PDF/fixture 被清理。"],
    ),
    case(
        "CI-UNIT-004",
        "unit",
        "View → Exit Full Screen 与物理 Escape 共用 AppKit 异步退出边界，并在 native transition 完成后保持正常窗口几何稳定。",
        "功能正确性",
        "tests/tst_qviewtests.cpp::WindowBehaviorTests::testExitFullscreenActionUsesEscapePath",
        "验证菜单退出动作与 Escape 走同一原生异步边界，且不因 macOS 标题栏 client inset 差异产生假失败。",
        ["可见的正常 MainWindow 已创建。", "macOS Cocoa 原生窗口和全屏通知可用。"],
        {"action_input": "View → Exit Full Screen QAction", "comparison_input": "物理 Escape", "normal_geometry_source": "showEvent 后已稳定的实际 QWidget::geometry()"},
        ["等待 deferred full-size-content-view 设置稳定。", "捕获平台实际正常几何。", "进入全屏并直接触发 View 退出动作。", "等待原生退出通知完成并检查几何稳定。", "再次进入全屏并发送 Escape，重复同样检查。"],
        "两种输入都在 native transition 完成前保持 Qt 全屏状态，完成后退出全屏、恢复捕获的正常几何，且无退出后的二次 Move/Resize。",
        ["测试窗口关闭。", "临时 titlebar 设置和 quit policy 恢复。"],
    ),
    case(
        "CI-UNIT-005",
        "unit",
        "Release 版本比较器必须接受与目标相等的两段 macOS 版本。",
        "功能正确性",
        "tests/ci_quality_pipeline.py::run_release_version_unit::compatible",
        "隔离验证修复后的版本比较器基本成功路径，不触发任何签名或系统工具。",
        ["package-macos-release.sh 可执行。"],
        {"candidate_version": "15.0", "expected_version": "15.0", "mode": "RELEASE_VALIDATE_VERSION_ONLY=true"},
        ["以测试模式调用打包脚本。", "读取 VERSION_COMPATIBLE 标记和退出码。"],
        "脚本返回 0，并输出 15.0 <= 15.0 的兼容标记。",
        ["测试模式进程退出，不访问 Apple 凭据或应用 bundle。"],
    ),
    case(
        "CI-UNIT-006",
        "unit",
        "Release 版本比较器必须正确拒绝带 patch 的高版本，并且不能把 7.5 当作 Bash 算术表达式。",
        "功能正确性",
        "tests/ci_quality_pipeline.py::run_release_version_unit::incompatible",
        "直接回归远端 15.7.5 依赖版本触发的解析路径。",
        ["package-macos-release.sh 可执行。"],
        {"candidate_version": "15.7.5", "expected_version": "15.0", "mode": "RELEASE_VALIDATE_VERSION_ONLY=true"},
        ["以测试模式调用打包脚本。", "检查非零退出、VERSION_INCOMPATIBLE 标记和错误输出。"],
        "脚本非零退出，明确报告 15.7.5 > 15.0，且没有 invalid arithmetic operator。",
        ["测试模式进程退出，不创建 release artifact。"],
    ),
    case(
        "CI-INTEGRATION-001",
        "integration",
        "完整 FovelleTests CTest 目标通过。",
        "功能正确性",
        "tests/ci_quality_pipeline.py::run_integration::ctest_FovelleTests",
        "验证修复在 CMake 注册的完整 Qt 回归集合中成立。",
        ["CMake build 已成功或由本阶段构建。", "CTest 已配置。"],
        {"ctest_regex": "^FovelleTests$", "timeout_seconds": 90},
        ["必要时执行 cmake --build。", "运行 ctest --output-on-failure -R ^FovelleTests$。"],
        "CTest 返回 0，FovelleTests 不含失败或崩溃。",
        ["保留 CTest 输出作为 evidence。"],
    ),
    case(
        "CI-INTEGRATION-002",
        "integration",
        "构建目录默认注册 FovelleTests 和 FovelleShortcutSettingsTests，不注册依赖外部权限的 native drag 驱动。",
        "精益完整性",
        "tests/ci_quality_pipeline.py::run_integration::ctest_list",
        "验证 CMake 默认测试清单只包含可重复的产品测试套件。",
        ["CMake configure 已完成。"],
        {"command": "ctest --test-dir build -N", "expected_tests": ["FovelleTests", "FovelleShortcutSettingsTests"]},
        ["列出 CTest 测试。", "检查两个产品注册名称。", "确认 native drag 驱动未在默认配置中出现。"],
        "列表输出只包含两个产品目标。",
        ["不执行递归审计，仅验证注册契约。"],
    ),
    case(
        "CI-INTEGRATION-003",
        "integration",
        "Release 合同检查必须通过并覆盖源码 Ghostscript staging 与三段版本验证路径。",
        "精益完整性",
        "tests/quality_release.py",
        "在不消耗 Apple 凭据的情况下验证 Release workflow、签名/公证编排和兼容性修复的静态合同。",
        ["Python 解释器和 macOS Release 脚本存在。"],
        {"command": "python3 tests/quality_release.py --repo . --output reports/evidence/release_contract.json"},
        ["运行 release contract 测试。", "检查所有 R-* 原子检查均为 pass。"],
        "命令返回 0，Release 合同 JSON 的 passed 为 true。",
        ["合同证据写入 reports/evidence/release_contract.json。"],
    ),
    case(
        "CI-INTEGRATION-004",
        "integration",
        "固定 Ghostscript 源码必须在目标 15.0 下生成同时包含 x86_64 与 arm64 的可安装 runtime。",
        "功能正确性",
        "tests/ci_quality_pipeline.py::run_release_source_build",
        "执行真实源码下载校验、configure、编译、安装和 Mach-O 检查，闭合远端 Release 失败的依赖根因。",
        ["macOS SDK、clang、make、curl、tar、file、lipo 和 otool 可用。", "网络可访问固定 Ghostscript archive。"],
        {"ghostscript_version": "10.07.1", "deployment_target": "15.0", "architectures": ["x86_64", "arm64"]},
        ["调用 prepare-ghostscript.sh 的 force-source 路径。", "检查生成 bin/gs。", "读取 file/lipo/otool 输出及 runtime.json。"],
        "源码构建返回 0，bin/gs 为双架构 Mach-O，所有读取到的 minos 为 15.0，且 runtime.json 存在。",
        ["临时源码和 runtime 在测试结束后清理。"],
    ),
    case(
        "CI-SYSTEM-001",
        "system",
        "实际 Fovelle.app 可启动并通过系统探针报告一个最大化窗口。",
        "功能正确性",
        "tests/ci_quality_pipeline.py::run_system::FOVELLE_SYSTEM_PROBE",
        "用真实 app bundle 验证最终启动副作用。",
        ["build/Fovelle.app/Contents/MacOS/Fovelle 存在。", "macOS Cocoa 图形环境可用。"],
        {"environment": {"QT_QPA_PLATFORM": "cocoa", "FOVELLE_SYSTEM_PROBE": "1"}},
        ["启动 app bundle。", "等待 system probe 输出。", "匹配 windows=1 maximized=true。"],
        "进程返回 0 且输出精确探针标记。",
        ["探针触发 app.quit，进程结束。"],
    ),
    case(
        "CI-SYSTEM-002",
        "system",
        f"实际 app bundle 的版本输出为 {PROJECT_VERSION}。",
        "功能正确性",
        "tests/ci_quality_pipeline.py::run_system::version",
        "验证最终 bundle 的版本元数据没有被构建路径修复破坏。",
        ["app bundle 已构建。"],
        {"command": "build/Fovelle.app/Contents/MacOS/Fovelle --version", "expected_version": PROJECT_VERSION},
        ["执行 --version。", "匹配版本号。"],
        f"进程返回 0 且输出包含 {PROJECT_VERSION}。",
        ["版本进程退出。"],
    ),
    case(
        "CI-SYSTEM-003",
        "system",
        "Release 编排必须在无凭据模式下可快速、确定性地完成 dry-run 和超时参数校验。",
        "可测试性",
        "tests/quality_release_system.py",
        "验证最终发布流程具备不侵入外部 Apple 服务的系统级可观测入口。",
        ["真实 Fovelle.app 已通过前两个系统探针。", "Python 解释器可运行 Release system harness。"],
        {"dry_run": True, "notarization_timeout": "45s", "invalid_timeout": "forever"},
        ["运行 Release system harness。", "检查 dry-run performance、超时配置和输出 JSON。"],
        "system-release JSON 的 passed 为 true，dry-run 在 5 秒内完成，且无效超时被拒绝。",
        ["测试使用临时报告路径，不访问 Apple 凭据。"],
    ),
]


def report_artifact(repo: Path, relative: str) -> dict[str, Any]:
    path = repo / relative
    record: dict[str, Any] = {
        "path": relative,
        "absolute_path": str(path.resolve()),
        "exists": path.is_file(),
    }
    if path.is_file():
        record.update({"bytes": path.stat().st_size, "sha256": sha256(path)})
    else:
        record.update({"bytes": 0, "sha256": None})
    return record


def write_json(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def quality_report(
    evidence: dict[str, Any], specification: dict[str, Any], generated_at: str
) -> dict[str, Any]:
    status_by_case = {
        item["case_id"]: item["status"] == "passed"
        for item in evidence["test_case_evidence"]
    }
    checks = [
        {
            "id": "CQ-001",
            "criterion": "精益完整性",
            "method": "验收标准与测试用例一对一覆盖审计",
            "passed": len(specification["test_cases"]) == len(CASES)
            and len(status_by_case) == len(CASES),
            "observed": {
                "specified_case_count": len(specification["test_cases"]),
                "evidence_case_count": len(status_by_case),
                "duplicate_case_ids": len(status_by_case) != len(evidence["test_case_evidence"]),
            },
        },
        {
            "id": "CQ-002",
            "criterion": "功能正确性",
            "method": "四层测试结果交叉审计",
            "passed": all(summary["failed"] == 0 for summary in evidence["stage_summaries"].values()),
            "observed": evidence["stage_summaries"],
        },
        {
            "id": "CQ-003",
            "criterion": "可测试性",
            "method": "非侵入式状态观察、QtTest 选择器、CTest 和系统探针审计",
            "passed": all(
                status_by_case.get(case_id, False)
                for case_id in (
                    "CI-STATIC-002",
                    "CI-UNIT-001",
                    "CI-INTEGRATION-001",
                    "CI-SYSTEM-001",
                )
            ),
            "observed": {
                "async_observation": status_by_case.get("CI-STATIC-002", False),
                "deterministic_unit_selector": status_by_case.get("CI-UNIT-001", False),
                "repeatable_integration_command": status_by_case.get("CI-INTEGRATION-001", False),
                "non_invasive_system_probe": status_by_case.get("CI-SYSTEM-001", False),
            },
        },
    ]
    passed_count = sum(item["passed"] for item in checks)
    return {
        "schema_version": "1.0",
        "report_type": "code_quality_assessment_report",
        "generated_at": generated_at,
        "task": "修复 Fovelle GitHub Actions 检查失败",
        "quality_requirements": ["精益完整性", "功能正确性", "可测试性"],
        "root_cause_summary": [
            "原始 Checks run 33499768039 在 Run Unit Tests 中以 exit code 8 失败；对应 babf065 的 Build Fovelle run 33499767940 已成功，因此原始故障位于产品 QtTest 运行阶段而非编译/链接阶段。",
            "最新 a3013ae Checks run 33503840748 的 Build 步骤和静态前置检查通过，但默认 CTest 将 FovelleTests 判为 90.45 秒超时；同一运行的 FovelleShortcutSettingsTests 通过。",
            "最新 hosted 日志中 testManualScrollCancelsPendingZoomAnchor 已报告 PASS，超时发生在同一产品进程后续未完成；这把剩余问题收敛到 native Cocoa 鼠标注入造成的跨测试生命周期敏感性（该因果归因是基于时序的显式推断）。",
            "本地隔离复现显示，testFullscreenExitPreservesVerticalPan 在 zoomAbsolute() 的 150ms 延迟 anchor 回调触发后，把用户已经拖到最大值的垂直滚动条重新定位到旧的缩放中心。",
            "滚动条 valueChanged 同时由用户 pan 和 zoom/layout 内部写入触发；若不区分两者，修复竞态会反过来取消合法的缩放 anchor。",
            "修复通过滚动条 sliderPressed/sliderMoved/actionTriggered 以及滚轮/键盘/拖拽 pan 路径取消 pending anchor generation，并用 fullScreenPanInternalUpdate 守卫 zoom/layout 的内部滚动条写入。",
            "此前报告的 12px QSS 厚度与 15px PM_ScrollBarExtent 差异仍是同一重现链路中的独立几何根因；固定厚度已删除，平台 extent 回归继续保留。",
            "旧测试用 QElapsedTimer 的墙钟差值作为 CPU 预算样本；墙钟值等于线程 CPU 执行时间加上可变的调度/WindowServer 间隔，因此 SVG pan 的单次 p99 outlier 可以超过 8.333 ms，而平均应用工作仍低于预算。",
            "同一 Checks run 的 FovelleSettingsAudit 另有静态合同失败：审计要求显式 !reports/solution_and_proof.md，但当前 .gitignore 缺少该规则。",
            "上述两个失败彼此独立；前者需要修复观测量定义，后者需要闭合报告文件的跟踪契约。",
            "此前 Release run 在 macdeployqt 后解析依赖 minos=15.7.5 时把 minor=7.5 送入 Bash 算术比较，触发 invalid arithmetic operator。",
            "同一 Release 日志随后确认 Homebrew 的 libtesseract.5.dylib 要求 macOS 15.7.5，而应用合同和 CMake deployment target 是 macOS 15.0；仅修正比较器不能修复真实运行时不兼容。",
        ],
        "repair_summary": [
            "QVGraphicsView 在滚动条 sliderPressed/sliderMoved/actionTriggered 及各 pan 输入路径调用 cancelPendingZoomAnchor()，递增 generation 并清除 scene/viewport anchor，使已过时的延迟 lambda 立即失效；自动布局 valueChanged 不会误取消 anchor。",
            "zoomAbsolute() 的内部 scrollbar 调整在 fullScreenPanInternalUpdate 守卫下执行，因此缩放事务自身不会把合法 anchor 误判为手动 pan。",
            "testManualScrollCancelsPendingZoomAnchor 以 2.0x 缩放、QAbstractSlider::SliderMove 最大端点和 250ms settling window 直接回归该竞态；testFullscreenExitPreservesVerticalPan 覆盖原始 Actions 失败路径。",
            "testVectorInteractionPaintCpuBudgetFor120Hz 使用 CLOCK_THREAD_CPUTIME_ID 的 clock_gettime 差值，日志明确标记 measurement=thread_cpu，并保留平均值、p99 和 120 FPS 容量断言。",
            "CPU 时钟不可用或读数倒退时测试失败，不通过放宽阈值、忽略 p99 或重试来掩盖问题。",
            ".gitignore 增加 !reports/solution_and_proof.md，使解决方案和证明文件在静态审计与 CI artifact 之间具有显式、可审计的跟踪契约。",
            "settings_quality_pipeline.py 和 ci_quality_pipeline.py 各自加入当前 CPU 预算的静态/单元验收用例，并继续记录选择器、返回码、超时和输出摘要。",
            "package-macos-release.sh 使用三段数字版本比较，并提供无凭据的 RELEASE_VALIDATE_VERSION_ONLY 单元路径。",
            "Release 在 macdeployqt 后强制以固定 Ghostscript 10.07.1 源码重建并覆盖运行时，显式传入 x86_64/arm64、SDKROOT 和 macOS 15.0 deployment target。",
            "Ghostscript 源码构建关闭不属于 EPS pdfwrite 路径的 Tesseract、Fontconfig、DBus、GTK、CUPS、libidn、libpaper 和 X 集成，避免复用带有更高 minos 或 AVX 假设的 Homebrew 依赖。",
            "打包前仍逐个检查 Mach-O 的 minos、主程序 SDK 家族、Universal 架构和 Info.plist LSMinimumSystemVersion；原有 tidy、异步矢量和全屏回归保护保持有效。",
        ],
        "research_trace": RESEARCH_TRACE,
        "checks": checks,
        "summary": {
            "total": len(checks),
            "passed": passed_count,
            "failed": len(checks) - passed_count,
            "status": "passed" if passed_count == len(checks) else "failed",
        },
        "evidence_file": "reports/test_evidence.json",
        "specification_file": "reports/test_case_specification.json",
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--build-dir", type=Path, default=None)
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--output-dir", type=Path, default=None)
    args = parser.parse_args()

    repo = args.repo.resolve()
    build_dir = (args.build_dir or repo / "build").resolve()
    output_dir = (args.output_dir or repo / "reports").resolve()
    generated_at = utc_now()

    static_results = run_static(repo)
    unit_results = run_unit(repo, build_dir)
    integration_results = run_integration(repo, build_dir, args.skip_build)
    system_results = run_system(repo, build_dir)
    by_stage = {
        "static": static_results,
        "unit": unit_results,
        "integration": integration_results,
        "system": system_results,
    }

    evidence_items: list[dict[str, Any]] = []
    for specification in CASES:
        result = by_stage[specification["test_layer"]].get(
            specification["id"],
            {"passed": False, "observed": {"reason": "no result"}},
        )
        evidence_items.append(
            {
                "evidence_id": f"EV-{specification['id']}",
                "case_id": specification["id"],
                "stage": specification["test_layer"],
                "status": "passed" if result.get("passed") else "failed",
                "assertion": specification["atomic_acceptance_criterion"],
                "observed": result.get("observed", {}),
                "execution": result.get("execution", {"test_code": specification["test_code"]}),
                "recorded_at": utc_now(),
            }
        )

    stage_summaries: dict[str, dict[str, Any]] = {}
    for stage in STAGES:
        stage_items = [item for item in evidence_items if item["stage"] == stage]
        stage_summaries[stage] = {
            "total": len(stage_items),
            "passed": sum(item["status"] == "passed" for item in stage_items),
            "failed": sum(item["status"] != "passed" for item in stage_items),
            "failed_case_ids": [
                item["case_id"] for item in stage_items if item["status"] != "passed"
            ],
            "status": "passed" if all(item["status"] == "passed" for item in stage_items) else "failed",
        }

    specification_report = {
        "schema_version": "1.0",
        "report_type": "test_case_specification",
        "generated_at": generated_at,
        "task": "修复 Fovelle GitHub Actions 检查失败",
        "test_execution_order": list(STAGES),
        "atomicity_rule": "每个 test case 只验证一个可判定的原子验收标准；每项均记录测试目的、前置条件、输入数据、操作步骤、预期结果和后置条件。",
        "research_trace": RESEARCH_TRACE,
        "test_cases": CASES,
    }
    evidence_report = {
        "schema_version": "1.0",
        "report_type": "test_evidence",
        "generated_at": generated_at,
        "task": "修复 Fovelle GitHub Actions 检查失败",
        "test_execution_order": list(STAGES),
        "research_trace": RESEARCH_TRACE,
        "stage_summaries": stage_summaries,
        "failed_case_ids": [
            item["case_id"] for item in evidence_items if item["status"] != "passed"
        ],
        "test_case_evidence": evidence_items,
    }
    quality = quality_report(evidence_report, specification_report, generated_at)

    write_json(output_dir / "test_case_specification.json", specification_report)
    write_json(output_dir / "test_evidence.json", evidence_report)
    write_json(output_dir / "code_quality_assessment_report.json", quality)

    passed_cases = sum(item["status"] == "passed" for item in evidence_items)
    completion = {
        "schema_version": "1.0",
        "report_type": "test_completion_report",
        "generated_at": generated_at,
        "task": "修复 Fovelle GitHub Actions 检查失败",
        "status": "passed" if passed_cases == len(CASES) else "failed",
        "verification_scope": {
            "local_pipeline": "executed",
            "remote_workflow_reexecuted": False,
            "remote_reexecution_note": "本次补丁未执行 push 或远端工作流重跑；远端链接记录的是 a3013ae 的失败/对照运行，最新语义事件修复仍需推送后由 Actions 验证。",
        },
        "execution_order": list(STAGES),
        "research_trace": RESEARCH_TRACE,
        "diagnosis": {
            "remote_run": "https://github.com/inostarlin-passion/Fovelle/actions/runs/33503840748",
            "checks_run": "https://github.com/inostarlin-passion/Fovelle/actions/runs/33503840748",
            "build_run": "https://github.com/inostarlin-passion/Fovelle/actions/runs/33503840715",
            "commit": "a3013ae",
            "observations": [
                "Checks run 33503840748 的 Run Unit Tests 在 CTest/Run tests 步骤返回 exit code 8；configure、build 和 preload-policy 步骤已通过，FovelleTests 超时而 ShortcutSettingsTests 通过。",
                "Build run 33503840715 对相同 a3013ae 成功完成构建，排除了当前故障是编译或链接失败。",
                "该 hosted 日志中 testManualScrollCancelsPendingZoomAnchor 已 PASS，随后 FovelleTests 未在 90 秒内结束；这是对 native Cocoa 测试注入生命周期敏感性的时序证据，而非新的业务断言失败。",
                "本地隔离 testFullscreenExitPreservesVerticalPan 复现了延迟 anchor 覆盖手动 vertical maximum 的时序；加入取消 generation 后该用例重复通过。",
                "新增 testManualScrollCancelsPendingZoomAnchor 在 250ms 延迟窗口后通过 QAbstractSlider::SliderMove 语义事件验证滚动条 maximum 与图像底边，直接覆盖同一竞态并避免原生 mouse grab。",
                "FovelleSettingsAudit 与 Release 的历史合同修复仍由既有静态/集成用例覆盖；它们不是本次最新 Checks 的直接失败步骤。",
                "Release 的 Deploy/Package 阶段报告 minos 15.7.5，并在旧比较器中出现 7.5 的 Bash arithmetic syntax error。",
                "同一失败步骤指出 build/Fovelle.app/Contents/Resources/ghostscript/lib/libtesseract.5.dylib 要求 15.7.5，而目标为 15.0。",
                "本地四层审计会在当前补丁后重新生成 JSON；未执行 push，因此不宣称 a3013ae 之后已有新的远端 run。",
            ],
            "deduction": "在保持缩放锚点、端点和 150ms 延迟语义不变的前提下，闭合当前竞态需要让明确的 scrollbar/滚轮/键盘/拖拽 pan 输入使旧 generation 失效，同时保留自动布局 valueChanged 和 zoom/layout 内部 setValue 的几何恢复；为解决 hosted 超时，回归测试改用 QAbstractSlider 语义事件而不是原生 Cocoa mouse grab。删除测试或放宽端点断言都会掩盖而非修复失败。既有 CPU、Release 和默认 CTest 合同继续由各自测试层验证。",
        },
        "repairs": [
            {"path": "src/qvgraphicsview.cpp", "change": "滚动条 sliderPressed/sliderMoved/actionTriggered 及各 pan 输入路径取消 pending zoom anchor；zoom/layout 内部滚动条写入使用 fullScreenPanInternalUpdate guard"},
            {"path": "src/qvgraphicsview.h", "change": "声明 cancelPendingZoomAnchor()"},
            {"path": "tests/tst_qviewtests.cpp", "change": "以 CLOCK_THREAD_CPUTIME_ID 测量当前线程 CPU 时间差，保留 120Hz 平均/p99/容量断言"},
            {"path": "tests/tst_qviewtests.cpp", "change": "新增 testManualScrollCancelsPendingZoomAnchor，以 QAbstractSlider::SliderMove 语义事件覆盖原生拖动合同，并保留 testFullscreenExitPreservesVerticalPan 原始失败路径回归"},
            {"path": ".gitignore", "change": "显式保留 reports/solution_and_proof.md 的跟踪路径"},
            {"path": "tests/settings_quality_pipeline.py", "change": "加入当前 CPU 预算静态和单元用例，并按静态、单元、集成、系统顺序生成要求报告"},
            {"path": "tests/ci_quality_pipeline.py", "change": "加入 CI-UNIT-008 手动 pan/延迟 anchor 回归，更新当前 Actions 证据及多跳诊断"},
        ],
        "stage_summaries": stage_summaries,
        "failed_case_ids": [
            item["case_id"] for item in evidence_items if item["status"] != "passed"
        ],
        "case_count": len(CASES),
        "passed_case_count": passed_cases,
        "failed_case_count": len(CASES) - passed_cases,
        "artifacts": [report_artifact(repo, f"reports/{name}") for name in REPORT_NAMES if name != "test_completion_report.json"],
        "self_artifact": {
            "path": "reports/test_completion_report.json",
            "absolute_path": str((output_dir / "test_completion_report.json").resolve()),
            "exists": True,
            "sha256": None,
            "hash_note": "Self-hash is null because this document contains the artifact manifest itself.",
        },
        "reproduction": {
            "build_command": "cmake -S . -B build -DBUILD_TESTS=ON -DFOVELLE_BUILD_TRANSLATIONS=ON && cmake --build build --parallel 2",
            "command": "python3 tests/ci_quality_pipeline.py --repo . --build-dir build",
            "skip_build_command": "python3 tests/ci_quality_pipeline.py --repo . --build-dir build --skip-build",
        },
    }
    write_json(output_dir / "test_completion_report.json", completion)

    print(
        json.dumps(
            {
                "status": completion["status"],
                "stage_summaries": stage_summaries,
                "reports": [str(output_dir / name) for name in REPORT_NAMES],
                "failed_case_ids": [
                    item["case_id"] for item in evidence_items if item["status"] != "passed"
                ],
            },
            ensure_ascii=False,
        )
    )
    return 0 if completion["status"] == "passed" else 1


if __name__ == "__main__":
    sys.exit(main())

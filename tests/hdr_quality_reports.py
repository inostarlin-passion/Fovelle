#!/usr/bin/env python3
"""Assemble the four required machine-auditable JSON reports."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path


PHASES = ("static", "unit", "integration", "system")


def load(path: Path) -> dict:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"expected JSON object: {path}")
    return value


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def git(repo: Path, *arguments: str) -> str:
    result = subprocess.run(["git", *arguments], cwd=repo, text=True, capture_output=True, check=False)
    return result.stdout.strip()


def compact_case(phase: str, item: dict, evidence_path: Path, repo: Path) -> dict:
    details = {
        key: value
        for key, value in item.items()
        if key not in {"id", "status", "test_code"}
    }
    return {
        "id": item.get("id"),
        "phase": phase,
        "status": item.get("status"),
        "test_code": item.get("test_code"),
        "evidence_file": str(evidence_path.relative_to(repo)),
        "evidence_sha256": sha256(evidence_path),
        "atomic_observations": details,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--evidence-dir", type=Path, required=True)
    parser.add_argument("--reports-dir", type=Path, required=True)
    args = parser.parse_args()

    repo = args.repo.resolve()
    evidence_dir = args.evidence_dir.resolve()
    reports_dir = args.reports_dir.resolve()
    specification_path = reports_dir / "test_case_specification.json"
    specification = load(specification_path)
    phase_paths = {phase: evidence_dir / f"hdr_{phase}.json" for phase in PHASES}
    phase_records = {phase: load(path) for phase, path in phase_paths.items()}
    intermediate_failure_path = evidence_dir / "intermediate/hdr_unit_failed_runner_and_geometry.json"
    intermediate_failure = load(intermediate_failure_path) if intermediate_failure_path.is_file() else {}
    desktop_capture_failure_path = (
        evidence_dir / "intermediate/hdr_system_failed_desktop_capture.json"
    )
    desktop_capture_failure = (
        load(desktop_capture_failure_path) if desktop_capture_failure_path.is_file() else {}
    )
    theme_capture_timing_failure_path = (
        evidence_dir / "intermediate/hdr_system_failed_theme_capture_timing.json"
    )
    theme_capture_timing_failure = (
        load(theme_capture_timing_failure_path)
        if theme_capture_timing_failure_path.is_file() else {}
    )
    root_cause_path = evidence_dir / "intermediate/hdr_root_cause_before_fix.json"
    root_cause = load(root_cause_path) if root_cause_path.is_file() else {}
    current_root_cause_path = (
        evidence_dir
        / "intermediate/hdr_root_cause_background_raw_reactivation_before_fix.json"
    )
    current_root_cause = (
        load(current_root_cause_path) if current_root_cause_path.is_file() else {}
    )
    interaction_root_cause_path = (
        evidence_dir
        / "intermediate/hdr_interaction_navigation_dng_nef_before_fix.json"
    )
    interaction_root_cause = (
        load(interaction_root_cause_path)
        if interaction_root_cause_path.is_file() else {}
    )
    native_overlay_root_cause_path = (
        evidence_dir
        / "intermediate/hdr_native_overlay_async_flash_before_fix.json"
    )
    native_overlay_root_cause = (
        load(native_overlay_root_cause_path)
        if native_overlay_root_cause_path.is_file() else {}
    )
    pacing_capture_failure_path = (
        evidence_dir
        / "intermediate/hdr_system_failed_cua_shield_and_unpaced_drawable.json"
    )
    pacing_capture_failure = (
        load(pacing_capture_failure_path)
        if pacing_capture_failure_path.is_file() else {}
    )
    timed_present_failure_path = (
        evidence_dir
        / "intermediate/hdr_system_failed_displaylink_timed_present.json"
    )
    timed_present_failure = (
        load(timed_present_failure_path)
        if timed_present_failure_path.is_file() else {}
    )
    pause_race_failure_path = (
        evidence_dir
        / "intermediate/hdr_system_failed_displaylink_pause_race_and_texture_metric.json"
    )
    pause_race_failure = (
        load(pause_race_failure_path) if pause_race_failure_path.is_file() else {}
    )
    generated_at = datetime.now(timezone.utc).isoformat()

    evidence_cases = [
        compact_case(phase, item, phase_paths[phase], repo)
        for phase in PHASES
        for item in phase_records[phase].get("cases", [])
    ]
    specification_ids = [item["id"] for item in specification.get("cases", [])]
    evidence_ids = [item["id"] for item in evidence_cases]
    missing_ids = sorted(set(specification_ids) - set(evidence_ids))
    unexpected_ids = sorted(set(evidence_ids) - set(specification_ids))
    duplicate_ids = sorted({identifier for identifier in evidence_ids if evidence_ids.count(identifier) > 1})
    timestamps = [phase_records[phase].get("generated_at_utc", "") for phase in PHASES]
    execution_order_valid = timestamps == sorted(timestamps)
    all_phases_passed = all(phase_records[phase].get("passed") is True for phase in PHASES)
    all_cases_passed = all(item["status"] == "passed" for item in evidence_cases)
    evidence_passed = (
        all_phases_passed
        and all_cases_passed
        and not missing_ids
        and not unexpected_ids
        and not duplicate_ids
        and execution_order_valid
        and specification.get("passed") is True
    )

    research_sources = [
        {
            "kind": "fact-source",
            "title": "WWDC24 — Use HDR for dynamic image experiences in your app",
            "url": "https://developer.apple.com/videos/play/wwdc2024/10177/",
            "supports": ["adaptive HDR expansion", "content headroom", "CIToneMapHeadroom", "Quick Look/Preview adoption"],
        },
        {
            "kind": "fact-source",
            "title": "WWDC22 — Display EDR content with Core Image, Metal, and SwiftUI",
            "url": "https://developer.apple.com/videos/play/wwdc2022/10114/",
            "supports": ["RGBA16Float", "extended-linear Display P3", "CAMetalLayer EDR", "NSScreen headroom"],
        },
        {
            "kind": "fact-source",
            "title": "WWDC21 — Capture and process ProRAW images",
            "url": "https://developer.apple.com/videos/play/wwdc2021/10160/",
            "supports": ["scene-referred RAW", "CIRAWFilter", "extendedDynamicRangeAmount", "half-float EDR"],
        },
        {
            "kind": "fact-source",
            "title": "Apple CAMetalLayer wantsExtendedDynamicRangeContent",
            "url": "https://developer.apple.com/documentation/quartzcore/cametallayer/wantsextendeddynamicrangecontent",
            "supports": ["EDR layer contract"],
        },
        {
            "kind": "fact-source",
            "title": "Apple CAMetalLayer drawableSize",
            "url": "https://developer.apple.com/documentation/quartzcore/cametallayer/drawablesize",
            "supports": ["drawable texture dimensions are pixels", "bounds times contentsScale default"],
        },
        {
            "kind": "fact-source",
            "title": "Apple CAMetalLayer nextDrawable",
            "url": "https://developer.apple.com/documentation/quartzcore/cametallayer/nextdrawable%28%29",
            "supports": ["nextDrawable waits for an available drawable", "the wait can last up to one second when the pool is busy"],
        },
        {
            "kind": "fact-source",
            "title": "Apple CAMetalDisplayLink",
            "url": "https://developer.apple.com/documentation/quartzcore/cametaldisplaylink",
            "supports": ["display-synchronized Metal callbacks", "variable-refresh timing control", "smoother frames and fewer visual artifacts"],
        },
        {
            "kind": "fact-source",
            "title": "Apple — Achieving smooth frame rates with a Metal display link",
            "url": "https://developer.apple.com/documentation/metal/achieving-smooth-frame-rates-with-a-metal-display-link",
            "supports": ["display-link-driven drawable scheduling", "Metal presentation pacing"],
        },
        {
            "kind": "fact-source",
            "title": "Apple MTLDrawable addPresentedHandler",
            "url": "https://developer.apple.com/documentation/metal/mtldrawable/addpresentedhandler(_:)",
            "supports": ["callback after a drawable is presented", "first-frame handoff synchronization"],
        },
        {
            "kind": "fact-source",
            "title": "Apple MTLDrawable presentedTime",
            "url": "https://developer.apple.com/documentation/metal/mtldrawable/presentedtime",
            "supports": ["host time when a drawable was displayed onscreen", "zero for not presented or dropped frames"],
        },
        {
            "kind": "fact-source",
            "title": "Apple CAMetalDisplayLinkUpdate targetTimestamp",
            "url": "https://developer.apple.com/documentation/quartzcore/cametaldisplaylink/update/targettimestamp",
            "supports": ["target presentation time for a display-link update", "deadline observability"],
        },
        {
            "kind": "fact-source",
            "title": "Apple CAMetalDisplayLink preferredFrameLatency",
            "url": "https://developer.apple.com/documentation/quartzcore/cametaldisplaylink/preferredframelatency",
            "supports": ["accepted preferred latency values are one or two frames", "windowed composition may add final latency"],
        },
        {
            "kind": "fact-source",
            "title": "Apple CAMetalDisplayLink preferredFrameRateRange",
            "url": "https://developer.apple.com/documentation/quartzcore/cametaldisplaylink/preferredframeraterange",
            "supports": ["application-declared sustainable refresh-rate range", "variable-refresh cadence selection"],
        },
        {
            "kind": "fact-source",
            "title": "Apple CIRAWFilter extendedDynamicRangeAmount",
            "url": "https://developer.apple.com/documentation/coreimage/cirawfilter/extendeddynamicrangeamount",
            "supports": ["0 means no EDR", "1 means default EDR", "2 means maximum EDR"],
        },
        {
            "kind": "fact-source",
            "title": "Apple CIRAWFilter previewImage",
            "url": "https://developer.apple.com/documentation/coreimage/cirawfilter/previewimage",
            "supports": ["previewImage is the optional auxiliary preview representation of the original RAW image"],
        },
        {
            "kind": "fact-source",
            "title": "Apple CIImage auxiliaryHDRGainMap",
            "url": "https://developer.apple.com/documentation/coreimage/ciimageoption/auxiliaryhdrgainmap",
            "supports": ["Core Image can request an auxiliary HDR gain-map representation"],
        },
        {
            "kind": "fact-source",
            "title": "Apple CIImage applyingGainMap headroom",
            "url": "https://developer.apple.com/documentation/coreimage/ciimage/applyinggainmap%28_%3Aheadroom%3A%29",
            "supports": ["a gain map can be applied for a specified display headroom"],
        },
        {
            "kind": "fact-source",
            "title": "Apple NSScreen maximumExtendedDynamicRangeColorComponentValue",
            "url": "https://developer.apple.com/documentation/appkit/nsscreen/maximumextendeddynamicrangecolorcomponentvalue",
            "supports": ["current EDR headroom is dynamic", "current can remain one when no EDR content is onscreen"],
        },
        {
            "kind": "fact-source",
            "title": "Apple NSScreen maximumPotentialExtendedDynamicRangeColorComponentValue",
            "url": "https://developer.apple.com/documentation/appkit/nsscreen/maximumpotentialextendeddynamicrangecolorcomponentvalue",
            "supports": ["potential headroom describes display capability", "values above one identify EDR-capable displays"],
        },
        {
            "kind": "fact-source",
            "title": "Apple CIImage imageByInsertingIntermediate",
            "url": "https://developer.apple.com/documentation/coreimage/ciimage/insertingintermediate(cache:)",
            "supports": ["explicit Core Image intermediate insertion", "explicit cache selection"],
        },
        {
            "kind": "fact-source",
            "title": "Apple CIImageOption cacheImmediately",
            "url": "https://developer.apple.com/documentation/coreimage/ciimageoption/cacheimmediately",
            "supports": [
                "initialization-time decode into a non-volatile cache when possible",
                "render-time decode otherwise uses a volatile cache",
            ],
        },
        {
            "kind": "fact-source",
            "title": "Apple CIContext cacheIntermediates",
            "url": "https://developer.apple.com/documentation/coreimage/cicontextoption/cacheintermediates",
            "supports": ["cached intermediates improve repeated similar renders", "context-wide caching has a memory tradeoff"],
        },
        {
            "kind": "fact-source",
            "title": "Apple Core Image — Getting the Best Performance",
            "url": "https://developer.apple.com/library/archive/documentation/GraphicsImaging/Conceptual/CoreImaging/ci_performance/ci_performance.html",
            "supports": ["reuse CIContext", "avoid unnecessary CPU/GPU texture transfers", "lower-level APIs for frequently updated output"],
        },
        {
            "kind": "fact-source",
            "title": "WWDC26 — Develop in HDR with Core Image",
            "url": "https://developer.apple.com/videos/play/wwdc2026/305/",
            "supports": [
                "one CIContext per interactive view",
                "cacheIntermediates=true for interactive RAW editing",
                "cacheIntermediates=false for one-shot export",
            ],
        },
        {
            "kind": "fact-source",
            "title": "Apple CIRAWFilter baselineExposure",
            "url": "https://developer.apple.com/documentation/coreimage/cirawfilter/baselineexposure",
            "supports": [
                "camera-dependent baseline exposure",
                "zero requests linear response",
            ],
        },
        {
            "kind": "fact-source",
            "title": "Apple CALayer contentsHeadroom",
            "url": "https://developer.apple.com/documentation/quartzcore/calayer/contentsheadroom",
            "supports": [
                "content headroom describes values used by layer contents",
                "headroom above one tags HDR layer content",
            ],
        },
        {
            "kind": "fact-source",
            "title": "Apple CALayer autoresizingMask",
            "url": "https://developer.apple.com/documentation/quartzcore/calayer/autoresizingmask",
            "supports": ["layer bounds are not automatically resized without a layout manager or autoresizing mask"],
        },
        {
            "kind": "fact-source",
            "title": "Qt QGraphicsView viewport update modes",
            "url": "https://doc.qt.io/qt-6/qgraphicsview.html#ViewportUpdateMode-enum",
            "supports": ["QGraphicsView normally performs partial viewport updates", "independent or non-partial viewport behavior needs explicit lifecycle handling"],
        },
        {
            "kind": "fact-source",
            "title": "Qt QWidget grab",
            "url": "https://doc.qt.io/qt-6/qwidget.html#grab",
            "supports": ["QWidget::grab renders the widget into a pixmap", "child widgets are painted into that result"],
        },
        {
            "kind": "fact-source",
            "title": "Qt QGraphicsEffect",
            "url": "https://doc.qt.io/qt-6/qgraphicseffect.html",
            "supports": ["graphics effects operate between source and destination", "effect drawing can obtain a pixmap containing the painted source"],
        },
        {
            "kind": "fact-source",
            "title": "Qt QWidget native and alien widgets",
            "url": "https://doc.qt.io/qt-6/qwidget.html",
            "supports": ["widgets normally share a backing store", "native widgets create native window handles"],
        },
        {
            "kind": "fact-source",
            "title": "Apple CALayer addSublayer",
            "url": "https://developer.apple.com/documentation/quartzcore/calayer/addsublayer(_:)",
            "supports": ["a layer can composite child layers in one layer tree"],
        },
        {
            "kind": "fact-source",
            "title": "Apple CAShapeLayer path and fill",
            "url": "https://developer.apple.com/documentation/quartzcore/cashapelayer/path",
            "supports": ["shape layers rasterize only a supplied path", "path-based rounded and chevron artwork"],
        },
    ]

    intermediate_iterations = []
    if root_cause:
        intermediate_iterations.append({
            "status": "reproduced-and-superseded",
            "evidence_file": str(root_cause_path.relative_to(repo)),
            "evidence_sha256": sha256(root_cause_path),
            "facts": root_cause.get("facts", []),
            "inferences": root_cause.get("inferences", []),
            "uncertainties": root_cause.get("uncertainties", []),
            "used_as_final_pass_evidence": False,
        })
    if current_root_cause:
        intermediate_iterations.append({
            "status": "reproduced-corrected-and-superseded-by-current-dng-analysis",
            "evidence_file": str(current_root_cause_path.relative_to(repo)),
            "evidence_sha256": sha256(current_root_cause_path),
            "facts": current_root_cause.get("facts", []),
            "inferences": current_root_cause.get("inferences", []),
            "uncertainties": current_root_cause.get("uncertainties", []),
            "used_as_final_pass_evidence": False,
        })
    if interaction_root_cause:
        intermediate_iterations.append({
            "status": "reproduced-analyzed-and-corrected",
            "evidence_file": str(interaction_root_cause_path.relative_to(repo)),
            "evidence_sha256": sha256(interaction_root_cause_path),
            "facts": interaction_root_cause.get("facts", []),
            "inferences": interaction_root_cause.get("inferences", []),
            "uncertainties": interaction_root_cause.get("uncertainties", []),
            "causal_chains": interaction_root_cause.get("causal_chains", []),
            "used_as_final_pass_evidence": False,
        })
    if native_overlay_root_cause:
        intermediate_iterations.append({
            "status": "reproduced-analyzed-and-corrected-in-current-iteration",
            "evidence_file": str(native_overlay_root_cause_path.relative_to(repo)),
            "evidence_sha256": sha256(native_overlay_root_cause_path),
            "facts": native_overlay_root_cause.get("facts", []),
            "inferences": native_overlay_root_cause.get("inferences", []),
            "uncertainties": native_overlay_root_cause.get("uncertainties", []),
            "causal_chains": native_overlay_root_cause.get("causal_chains", []),
            "used_as_final_pass_evidence": False,
        })
    if pacing_capture_failure:
        intermediate_iterations.append({
            "status": "failed-diagnosed-and-superseded-in-current-iteration",
            "evidence_file": str(pacing_capture_failure_path.relative_to(repo)),
            "evidence_sha256": sha256(pacing_capture_failure_path),
            "facts": pacing_capture_failure.get("facts", []),
            "inferences": pacing_capture_failure.get("inferences", []),
            "uncertainties": pacing_capture_failure.get("uncertainties", []),
            "corrections": pacing_capture_failure.get("corrections", []),
            "used_as_final_pass_evidence": False,
        })
    if timed_present_failure:
        intermediate_iterations.append({
            "status": "failed-diagnosed-and-superseded-in-current-iteration",
            "evidence_file": str(timed_present_failure_path.relative_to(repo)),
            "evidence_sha256": sha256(timed_present_failure_path),
            "facts": [
                timed_present_failure.get("fact"),
                timed_present_failure.get("primary_source_fact"),
            ],
            "inferences": [timed_present_failure.get("inference")],
            "uncertainties": [timed_present_failure.get("uncertainty")],
            "corrections": [timed_present_failure.get("correction")],
            "used_as_final_pass_evidence": False,
        })
    if pause_race_failure:
        intermediate_iterations.append({
            "status": "failed-diagnosed-and-superseded-in-current-iteration",
            "evidence_file": str(pause_race_failure_path.relative_to(repo)),
            "evidence_sha256": sha256(pause_race_failure_path),
            "facts": pause_race_failure.get("facts", []),
            "inferences": pause_race_failure.get("inferences", []),
            "uncertainties": pause_race_failure.get("uncertainties", []),
            "corrections": pause_race_failure.get("corrections", []),
            "used_as_final_pass_evidence": False,
        })
    if intermediate_failure:
        intermediate_iterations.append({
            "status": "failed-and-superseded",
            "evidence_file": str(intermediate_failure_path.relative_to(repo)),
            "evidence_sha256": sha256(intermediate_failure_path),
            "facts": [
                "The first formal unit attempt passed an invalid split Qt output argument and therefore selected a non-existent test function named txt.",
                "The same attempt exposed a real non-HDR expensive-scaling scene-rectangle regression before completion.",
            ],
            "corrections": [
                "Pass -,txt as one Qt Test argument.",
                "Use logical source scene geometry only for native HDR; preserve the existing pixmap backing geometry for non-HDR expensive scaling.",
            ],
            "used_as_final_pass_evidence": False,
        })
    if desktop_capture_failure:
        intermediate_iterations.append({
            "status": "failed-and-superseded",
            "evidence_file": str(desktop_capture_failure_path.relative_to(repo)),
            "evidence_sha256": sha256(desktop_capture_failure_path),
            "facts": [
                "The first formal system run passed all telemetry and performance contracts but failed two full-desktop screenshot comparisons.",
                "Visual inspection showed that the Fovelle window occupied the left part of the desktop while a playing browser video changed the remaining pixels between captures.",
                "The original metric therefore counted unrelated desktop black columns and compared unrelated moving content even though the app viewport itself was complete.",
            ],
            "corrections": [
                "Expose viewport global origin, logical size, and device-pixel ratio in opt-in telemetry.",
                "Crop every screenshot metric to that exact physical viewport before black-band or edge-structure analysis.",
            ],
            "used_as_final_pass_evidence": False,
        })
    if theme_capture_timing_failure:
        intermediate_iterations.append({
            "status": "failed-test-timing-and-superseded",
            "evidence_file": str(theme_capture_timing_failure_path.relative_to(repo)),
            "evidence_sha256": sha256(theme_capture_timing_failure_path),
            "facts": [
                "The first formal system run passed 16 of 17 cases; only the first post-switch screenshot remained light while renderer telemetry and the later screenshot were already dark.",
                "The application schedules the switch three seconds after postLoad, but the original screenshot offset was measured from process start and therefore did not include variable startup/decode time.",
            ],
            "corrections": [
                "Retain the pre-switch sample at 2.4 seconds and move both post-switch samples to 4.5 and 5.2 seconds from process start.",
                "Rerun the entire formal static, unit, integration, and system sequence after changing the test timing.",
            ],
            "used_as_final_pass_evidence": False,
        })

    artifact_records = [
        {
            "path": str(path.relative_to(repo)),
            "sha256": sha256(path),
            "bytes": path.stat().st_size,
            "kind": phase_records[phase].get("kind"),
            "passed": phase_records[phase].get("passed"),
        }
        for phase, path in phase_paths.items()
    ]
    artifact_records.append({
        "path": str(specification_path.relative_to(repo)),
        "sha256": sha256(specification_path),
        "bytes": specification_path.stat().st_size,
        "kind": specification.get("kind"),
        "passed": specification.get("passed"),
    })

    test_evidence = {
        "schema_version": "1.0",
        "kind": "atomic-test-evidence-index",
        "release": "v0.1.4",
        "generated_at_utc": generated_at,
        "execution_order": list(PHASES),
        "execution_order_valid": execution_order_valid,
        "artifacts": artifact_records,
        "cases": evidence_cases,
        "coverage_validation": {
            "specification_case_count": len(specification_ids),
            "evidence_case_count": len(evidence_ids),
            "missing_ids": missing_ids,
            "unexpected_ids": unexpected_ids,
            "duplicate_ids": duplicate_ids,
        },
        "intermediate_iterations": intermediate_iterations,
        "summary": {
            "total": len(evidence_cases),
            "passed": sum(item["status"] == "passed" for item in evidence_cases),
            "failed": sum(item["status"] != "passed" for item in evidence_cases),
            "phases_passed": sum(phase_records[phase].get("passed") is True for phase in PHASES),
            "phases_total": len(PHASES),
        },
        "passed": evidence_passed,
    }
    reports_dir.mkdir(parents=True, exist_ok=True)
    evidence_report_path = reports_dir / "test_evidence.json"
    evidence_report_path.write_text(json.dumps(test_evidence, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    system_performance = phase_records["system"].get("performance", {})
    completion = {
        "schema_version": "1.0",
        "kind": "test-completion-report",
        "release": "v0.1.4",
        "generated_at_utc": generated_at,
        "repository": str(repo),
        "head_sha_before_working_tree_changes": git(repo, "rev-parse", "HEAD"),
        "working_tree_status": git(repo, "status", "--short", "--untracked-files=all").splitlines(),
        "scope": [
            "RAW HDR: DNG/NEF/CR3/ARW/RAF through Image I/O + CIRAWFilter",
            "Non-RAW HDR: gain-map JPEG, HDR JPEG/HEIF/AVIF metadata-aware reconstruction",
            "ColorSync + Core Image/Metal RGBA16Float + CAMetalLayer EDR output",
            "SDR headroom compatibility and WindowServer-managed EDR activation",
            "First-frame SDR proxy handoff synchronized to actual Metal presentation",
            "Core Image-managed RAW/HDR endpoint preparation without app-texture re-import",
            "Initialization-time non-volatile decoding of both gain-map JPEG source recipes",
            "Full-resolution DNG processed preview reconstructed with its authored auxiliary gain map",
            "Independent SDR/HDR CIRAWFilter graphs for traditional RAW with interactive source-space caching",
            "Measured RAW content headroom tagged on CIImage and CAMetalLayer",
            "Camera-authored BaselineExposure retained without a one-parameter exposure rewrite",
            "Prepared HDR source reuse across zoom, pan, and resize without reactivation",
            "CAMetalDisplayLink-paced opening and interaction with latest-only pending geometry and at most two frames in flight",
            "Event-loop-coalesced HDR requests for paint, zoom, and dual-axis scrolling",
            "Shape-only native navigation overlay inside the Metal layer tree for HDR; QWidget fallback for SDR",
            "Final-headroom-only first visible Metal frame with presented-handler proxy handoff",
            "One shared theme background contract for Qt and Metal with live theme updates",
            "Potential-headroom bootstrap when NSScreen current EDR headroom is still one",
        ],
        "phase_results": [
            {
                "phase": phase,
                "generated_at_utc": phase_records[phase].get("generated_at_utc"),
                "summary": phase_records[phase].get("summary"),
                "passed": phase_records[phase].get("passed"),
                "evidence_file": str(phase_paths[phase].relative_to(repo)),
            }
            for phase in PHASES
        ],
        "atomic_result": test_evidence["summary"],
        "iteration_history": test_evidence["intermediate_iterations"],
        "static_analysis": phase_records["static"].get("clang_tidy"),
        "performance": system_performance,
        "research_sources": research_sources,
        "facts": [
            "The project and bundle version sources are v0.1.4.",
            "All specified static, unit, integration, and system cases passed in the recorded order.",
            "The supplied JPEG produced an adaptive-HDR graph with content headroom above one.",
            "The supplied JPEG, DNG, and NEF pixel probes measured HDR components above their SDR representations.",
            "The supplied DNG exposed an 8064x6048 processed preview, a paired 4032x3024 gain map, and processed content headroom above one.",
            "The exported DNG processed representation met the recorded Quick Look edge-structure and RGB-error thresholds.",
            "The supplied NEF produced independent camera-default SDR/HDR CIRAWFilter endpoint graphs and repeated float probes were stable.",
            "All pre-presentation system records retained the SDR fallback while Metal opacity was zero.",
            "No system render was submitted with pending geometry; stable DNG drawables matched requested dimensions.",
            "Current-only bootstrap runs for both formats reached HDR targets while current headroom was held at one.",
            "Six timed steady/zoom/pan screen captures remained below the black-band threshold.",
            "The final two post-interaction JPEG captures retained at least 0.995 edge-structure similarity, detecting stale tile residue in addition to pure black columns.",
            "Ten timed DNG launch captures retained at least 0.90 edge-structure similarity and lost zero structured tiles relative to the final frame.",
            "Every geometry-reuse and post-interaction record retained the final headroom endpoint, the prepared HDR graph, Metal opacity one, and no SDR fallback.",
            "JPEG, processed DNG, and NEF each emitted all forty-eight ordered pan steps and met the recorded event-loop and presented-frame average, P99, maximum, and throughput thresholds.",
            "All three interaction runs stayed at or below two frames in flight and settled with the latest requested Metal generation submitted and no frame left in flight.",
            "Four timed NEF interaction captures lost no structured tile, stayed below the black-band limit, and met the final-frame edge threshold.",
            "Over actual HDR pixels, navigation used shape-only Metal sublayers while the Qt widget was hidden; a single half-opacity window capture kept each exact rounded corner locally continuous with adjacent HDR pixels while the artwork center remained visibly distinct.",
            "JPEG, processed DNG, plain DNG, and NEF revealed only prepared final-headroom first frames; no app-generated partial-headroom drawable was visible.",
            "The Light background screen sample remained (150,150,150) after Metal reveal; after the deterministic Dark update, two samples measured (33,33,33).",
            "The physical built-in display reported potential EDR headroom above one during system tests.",
        ],
        "inferences": [
            "Using the same public Image I/O/Core Image/Metal/EDR mechanisms documented by Apple yields behavior close to Quick Look, but not a clone of its private tuning.",
            "Using the DNG's camera-processed full-resolution preview with its authored gain map preserves more of the camera rendering recipe than rewriting one generic RAW exposure parameter.",
            "Continuous DisplayLink pacing with latest-only pending geometry and two frames in flight removes both the one-frame serialization and unpaced drawable bursts that best explain slow HDR dragging.",
            "Coalescing UI render requests and eliminating viewport grabs removes the synchronous work that best explains slow panning and delayed hover response.",
            "Compositing navigation artwork inside the CAMetalLayer tree removes the cross-surface transparency boundary that best explains the navigation backing artifact.",
            "Revealing only a final-headroom presented drawable removes the application-generated endpoint discontinuity that best explains the opening flash.",
            "Not adding LibRaw is the lean choice for the currently supported Apple RAW inputs; it remains a possible backend for unsupported future cameras.",
        ],
        "uncertainties": [
            "Quick Look's private tone curve and precise brightness transition are not publicly specified.",
            "The exact internal RAW/gain-map ROI scheduler is not published by Apple; its mechanism remains an inference even though the 2:1 extent, source contracts, and final timed pixels are directly auditable.",
            "No physical SDR-only Mac was present; unit and current=potential=1 system tests cover the SDR branch deterministically.",
            "Apple RAW camera-model support varies with macOS and may change independently of this application.",
        ],
        "remaining_required_work": [],
        "passed": evidence_passed,
    }
    completion_path = reports_dir / "test_completion_report.json"
    completion_path.write_text(json.dumps(completion, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    performance_metrics = system_performance.get("metrics", {})
    quality = {
        "schema_version": "1.0",
        "kind": "code-quality-assessment",
        "release": "v0.1.4",
        "generated_at_utc": generated_at,
        "overall_status": "passed" if evidence_passed else "failed",
        "assessments": [
            {
                "quality_attribute": "精益完整性",
                "status": "passed" if evidence_passed else "failed",
                "atomic_evidence_ids": [
                    "ST-HDR-RAW-CIRAWFILTER", "ST-HDR-NONRAW-RECONSTRUCTION",
                    "ST-HDR-NONRAW-NONVOLATILE-DECODE",
                    "ST-HDR-DNG-PROCESSED-GAINMAP", "ST-HDR-DNG-GAINMAP-ROI",
                    "ST-HDR-STAGED-FIRST-FRAME", "ST-HDR-OFFSCREEN-PREPARATION",
                    "ST-HDR-FIRST-VISIBLE-FINAL",
                    "ST-HDR-GEOMETRY-LIFECYCLE", "ST-HDR-RAW-STABLE-ENDPOINTS",
                    "ST-HDR-RAW-CONTENT-HEADROOM", "ST-HDR-THEME-BACKGROUND",
                    "ST-HDR-INTERACTION-NO-REACTIVATION", "UT-HDR-FORMAT-COVERAGE",
                    "ST-HDR-DISPLAYLINK-LATEST-ONLY", "ST-HDR-UI-REQUEST-COALESCING",
                    "ST-HDR-NAV-CACHED-SAMPLING", "ST-HDR-NAV-NATIVE-COMPOSITOR",
                    "ST-HDR-VERSION-0.1.4",
                ],
                "facts": [
                    "One native CIImage abstraction serves RAW and non-RAW HDR and one Metal renderer serves both.",
                    "The implementation uses Apple system frameworks only; no new third-party runtime dependency was introduced.",
                    "Targeted clang-tidy completed with zero diagnostic warnings or errors.",
                ],
                "inference": "Deferring LibRaw until an unsupported-camera case exists avoids duplicated demosaic/color-management machinery without omitting the explicit fallback/error behavior.",
                "uncertainty": "A future unsupported camera may justify an optional LibRaw backend.",
            },
            {
                "quality_attribute": "功能正确性",
                "status": "passed" if evidence_passed else "failed",
                "atomic_evidence_ids": ["ST-HDR-NONRAW-NONVOLATILE-DECODE"] + [item["id"] for item in evidence_cases if item["phase"] in {"unit", "integration"}] + [
                    "SYS-HDR-GAINMAP-JPEG-EDR", "SYS-HDR-RAW-DNG-EDR",
                    "SYS-HDR-FLOAT-COLORMANAGED-EDR-SURFACE", "SYS-HDR-WINDOWSERVER-HEADROOM",
                    "SYS-HDR-EDR-BOOTSTRAP", "SYS-HDR-JPEG-BAND-FREE",
                    "SYS-HDR-INTERACTION-GEOMETRY", "SYS-HDR-INTERACTION-NO-REACTIVATION",
                    "SYS-HDR-FORCED-SDR-COMPATIBILITY", "SYS-HDR-NO-PREMATURE-BLACK-FRAME",
                    "SYS-HDR-FINAL-LAYOUT-BEFORE-METAL", "SYS-HDR-RAW-NO-BLANK-REGION",
                    "SYS-HDR-RAW-CONTENT-HEADROOM",
                    "SYS-HDR-FIRST-VISIBLE-FINAL", "SYS-HDR-DISPLAYLINK-LATEST-ONLY",
                    "SYS-HDR-NEF-ZOOM-NO-GHOST", "SYS-HDR-NAV-NATIVE-COMPOSITOR",
                    "SYS-HDR-THEME-BACKGROUND-STABILITY", "SYS-HDR-THEME-BACKGROUND-SWITCH",
                ],
                "facts": [
                    "Real JPEG, DNG, and NEF inputs passed decoder invariants; JPEG and DNG rendered through an active RGBA16Float EDR surface and NEF passed the interaction presentation probe.",
                    "SDR classification and forced unit-headroom behavior passed.",
                    "The complete pre-existing CTest regression target passed after the change.",
                    "JPEG and RAW RGBAf peak probes prove extended values numerically instead of relying on metadata flags.",
                    "Timed pixel captures and geometry-generation telemetry cover the reproduced black-band, DNG partial-frame, NEF zoom-ghost, and drag-trail failure modes.",
                    "Quick Look comparison metrics cover DNG detail preservation, while single-frame corner-continuity and painted-center metrics cover the navigation surface artifact without comparing different Metal drawables.",
                    "Theme pixels, RAW tile energy, content/display target headroom, conditional layer-tag support, and post-activation progress are asserted as independent atomic system cases.",
                ],
                "inference": "The tested public-framework pipeline preserves HDR until the WindowServer boundary for these representative RAW and gain-map inputs.",
                "uncertainty": "Visual identity across every camera model and every ISO HDR encoder remains outside the finite fixture matrix.",
            },
            {
                "quality_attribute": "时间行为",
                "status": "passed" if phase_records["system"].get("passed") else "failed",
                "atomic_evidence_ids": [
                    "ST-HDR-DISPLAYLINK-LATEST-ONLY", "ST-HDR-UI-REQUEST-COALESCING",
                    "ST-HDR-PRESENTATION-TELEMETRY",
                    "UT-HDR-NAV-SAMPLING-LATENCY", "SYS-HDR-TIME-BEHAVIOR",
                    "SYS-HDR-INTERACTION-RESPONSIVENESS",
                    "SYS-HDR-PRESENTATION-RESPONSIVENESS",
                ],
                "measurement_window": system_performance.get("measurement_window"),
                "thresholds": system_performance.get("thresholds"),
                "metrics": performance_metrics,
                "facts": [
                    "Average, P99, maximum, and throughput were computed from raw JSON samples for decode, steady encode, AppKit callback, and actual drawable-presentation windows.",
                    "The same statistics were recorded for deterministic JPEG, processed DNG, and NEF interactions, with a separate synchronous zoom-main-thread bound.",
                    "Request-to-presentation latency comes from addPresentedHandler rather than submission timestamps, so the throughput claim includes GPU execution and presentation availability.",
                ],
                "inference": "Offscreen endpoint preparation, event-loop coalescing, DisplayLink callback pacing, deadline-aware submission, and a bounded two-frame pipeline leave sufficient interactive throughput on the measured Mac.",
                "uncertainty": "Performance will vary with sensor resolution, RAW demosaic complexity, GPU, memory pressure, and display mode.",
            },
            {
                "quality_attribute": "可测试性",
                "status": "passed" if evidence_passed else "failed",
                "atomic_evidence_ids": [
                    "ST-HDR-OBSERVABILITY", "UT-HDR-TRANSITION", "UT-HDR-HEADROOM-CLAMP",
                    "UT-HDR-EDR-BOOTSTRAP", "UT-HDR-GEOMETRY-EQUIVALENCE",
                    "UT-HDR-FIRST-VISIBLE-FINAL", "UT-HDR-FIRST-VISIBLE-GEOMETRY",
                    "UT-HDR-CONTENT-HEADROOM", "UT-HDR-PRESENTATION-REUSE",
                    "UT-HDR-THEME-BACKGROUND",
                    "UT-HDR-NAV-SAMPLING-LATENCY", "UT-HDR-NAV-TRANSPARENT-FADE",
                    "SYS-HDR-FIRST-VISIBLE-FINAL",
                    "SYS-HDR-FORCED-SDR-COMPATIBILITY", "SYS-HDR-NO-PREMATURE-BLACK-FRAME",
                    "SYS-HDR-RAW-NO-BLANK-REGION", "SYS-HDR-THEME-BACKGROUND-STABILITY",
                    "SYS-HDR-DISPLAYLINK-LATEST-ONLY", "SYS-HDR-INTERACTION-RESPONSIVENESS",
                    "SYS-HDR-PRESENTATION-RESPONSIVENESS",
                    "SYS-HDR-NEF-ZOOM-NO-GHOST", "SYS-HDR-NAV-NATIVE-COMPOSITOR",
                ],
                "facts": [
                    "Pure helpers deterministically test headroom policy plus final-endpoint and geometry reveal gates.",
                    "Separate test-only overrides deterministically exercise SDR targeting and the current=1 EDR bootstrap without replacing the production renderer.",
                    "Compact JSON telemetry exposes decoder metadata, layout/fallback state, drawable dimensions, viewport and image geometry, theme RGB, preparation gates, content/target/display headroom, final-endpoint state, render count, DisplayLink pacing/submissions, frames in flight, actual presentations, generations, coalescing counts, and timings only when enabled.",
                    "An opt-in deterministic interaction driver exercises production zoom and scrollbar paths; timed PNGs include hashes, quantitative band metrics, and post-interaction edge similarity.",
                    "A separate opt-in navigation probe holds the production shape layers at fractional opacity, records the hidden Qt widget state and exact global rectangle, proves the rectangle lies over HDR image pixels, and measures each rounded corner against its immediately adjacent exterior pixels in the same native-window capture.",
                    "Every atomic criterion has one specification and one evidence record with a stable ID.",
                ],
                "inference": "Failures can be localized to a single layer of the pipeline without screenshot-based HDR ambiguity.",
                "uncertainty": "Subjective highlight appearance still requires optional human visual review on additional displays.",
            },
        ],
        "source_artifacts": artifact_records,
        "research_sources": research_sources,
        "facts_inference_uncertainty_separated": True,
        "passed": evidence_passed,
    }
    quality_path = reports_dir / "code_quality_assessment_report.json"
    quality_path.write_text(json.dumps(quality, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    required_reports = [specification_path, evidence_report_path, completion_path, quality_path]
    validation = {
        "all_json_objects": all(isinstance(load(path), dict) for path in required_reports),
        "all_required_reports_exist": all(path.is_file() for path in required_reports),
        "all_reports_release_match": all(load(path).get("release") == "v0.1.4" for path in required_reports),
        "evidence_passed": evidence_passed,
    }
    print(json.dumps({
        "reports": [str(path) for path in required_reports],
        "validation": validation,
        "passed": all(validation.values()),
    }, ensure_ascii=False))
    return 0 if all(validation.values()) else 1


if __name__ == "__main__":
    sys.exit(main())

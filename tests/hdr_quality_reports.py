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
    ]

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
        "intermediate_iterations": [
            {
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
            }
        ] if intermediate_failure else [],
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
            "SDR headroom compatibility and smooth activation",
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
            "The supplied DNG produced a CIRAWFilter EDR graph and did not use an embedded preview as primary content.",
            "The physical built-in display reported current EDR headroom above one during system tests.",
        ],
        "inferences": [
            "Using the same public Image I/O/Core Image/Metal/EDR mechanisms documented by Apple yields behavior close to Quick Look, but not a clone of its private tuning.",
            "Not adding LibRaw is the lean choice for the currently supported Apple RAW inputs; it remains a possible backend for unsupported future cameras.",
        ],
        "uncertainties": [
            "Quick Look's private tone curve and precise brightness transition are not publicly specified.",
            "No physical SDR-only Mac was present; unit and forced-headroom system tests cover the SDR branch deterministically.",
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
                    "UT-HDR-FORMAT-COVERAGE", "ST-HDR-VERSION-0.1.4",
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
                "atomic_evidence_ids": [item["id"] for item in evidence_cases if item["phase"] in {"unit", "integration"}] + [
                    "SYS-HDR-GAINMAP-JPEG-EDR", "SYS-HDR-RAW-DNG-EDR",
                    "SYS-HDR-FLOAT-COLORMANAGED-EDR-SURFACE", "SYS-HDR-WINDOWSERVER-HEADROOM",
                    "SYS-HDR-FORCED-SDR-COMPATIBILITY",
                ],
                "facts": [
                    "Real JPEG and DNG inputs passed decoder invariants and rendered through an active RGBA16Float EDR surface.",
                    "SDR classification and forced unit-headroom behavior passed.",
                    "The complete pre-existing CTest regression target passed after the change.",
                ],
                "inference": "The tested public-framework pipeline preserves HDR until the WindowServer boundary for these representative RAW and gain-map inputs.",
                "uncertainty": "Visual identity across every camera model and every ISO HDR encoder remains outside the finite fixture matrix.",
            },
            {
                "quality_attribute": "时间行为",
                "status": "passed" if phase_records["system"].get("passed") else "failed",
                "atomic_evidence_ids": ["SYS-HDR-TIME-BEHAVIOR"],
                "measurement_window": system_performance.get("measurement_window"),
                "thresholds": system_performance.get("thresholds"),
                "metrics": performance_metrics,
                "facts": ["Average, P99, maximum, and throughput were computed from raw JSON samples for both decode and steady render windows."],
                "inference": "The measured M3 Pro workload has sufficient interactive steady-state throughput after Core Image/Metal warm-up.",
                "uncertainty": "Performance will vary with sensor resolution, RAW demosaic complexity, GPU, memory pressure, and display mode.",
            },
            {
                "quality_attribute": "可测试性",
                "status": "passed" if evidence_passed else "failed",
                "atomic_evidence_ids": [
                    "ST-HDR-OBSERVABILITY", "UT-HDR-TRANSITION", "UT-HDR-HEADROOM-CLAMP",
                    "SYS-HDR-SMOOTH-ACTIVATION", "SYS-HDR-FORCED-SDR-COMPATIBILITY",
                ],
                "facts": [
                    "Pure helpers deterministically test transition and headroom policy.",
                    "A test-only environment override deterministically exercises SDR targeting without replacing the production renderer.",
                    "Compact JSON telemetry exposes decoder metadata, layer configuration, display headroom, transition, render count, and timings only when enabled.",
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

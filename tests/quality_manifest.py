#!/usr/bin/env python3
"""Validate, hash, and index all machine-readable quality evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path


EVIDENCE_FILES = (
    "ci-failure.json",
    "static.json",
    "unit.json",
    "integration.json",
    "release.json",
    "system_feature.json",
    "system_probe.json",
    "system_layout.json",
    "test-specification.json",
)


def git(repo: Path, *args: str) -> str:
    result = subprocess.run(["git", *args], cwd=repo, text=True, capture_output=True, check=False)
    return result.stdout.strip()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_json(path: Path) -> dict:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return {}
    return value if isinstance(value, dict) else {}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    repo = args.repo.resolve()
    evidence_dir = repo / "reports" / "evidence"
    records: dict[str, dict] = {}
    errors: list[str] = []
    artifacts: list[dict] = []
    for name in EVIDENCE_FILES:
        path = evidence_dir / name
        record = load_json(path) if path.is_file() else {}
        records[name] = record
        if not path.is_file():
            errors.append(f"missing evidence: {name}")
            continue
        artifact = {
            "path": str(path.relative_to(repo)),
            "bytes": path.stat().st_size,
            "sha256": sha256(path),
            "kind": record.get("kind"),
        }
        if name != "ci-failure.json":
            artifact["passed"] = record.get("passed") is True
            if record.get("passed") is not True:
                errors.append(f"evidence did not pass: {name}")
        else:
            artifact["analysis_complete"] = bool(record.get("root_cause_inference") and record.get("uncertainties"))
            if not artifact["analysis_complete"]:
                errors.append("CI failure analysis is incomplete")
        artifacts.append(artifact)

    specification = records.get("test-specification.json", {})
    if specification.get("case_count") != 52:
        errors.append(f"unexpected atomic test case count: {specification.get('case_count')}")
    if specification.get("validation_errors"):
        errors.append("test specification contains validation errors")

    unit = records.get("unit.json", {})
    if unit.get("total_passed") != 67 or unit.get("total_failed") != 0 or unit.get("total_skipped") != 0:
        errors.append("unit evidence does not report 67 passed and zero failed/skipped")

    system_layout = records.get("system_layout.json", {})
    if system_layout.get("stable_after_queued_turn") is not True:
        errors.append("layout telemetry was not stable after queued turn")

    result = {
        "kind": "quality-evidence-manifest",
        "manifest_version": "1.0",
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "repo": str(repo),
        "head_sha": git(repo, "rev-parse", "HEAD"),
        "working_tree_status": git(repo, "status", "--short", "--untracked-files=all").splitlines(),
        "commands_executed": [
            "cmake -S . -B build -DBUILD_TESTS=ON -DFOVELLE_BUILD_TRANSLATIONS=OFF -DQV_DISABLE_ONLINE_VERSION_CHECK=ON",
            "cmake --build build --parallel 2",
            "ctest --test-dir build --output-on-failure --timeout 90",
            "python3 tests/quality_static.py --repo . --build-dir build",
            "python3 tests/quality_unit_runner.py --binary build/tests/fovelle_tests",
            "python3 tests/quality_integration.py --repo . --build-dir build",
            "python3 tests/quality_feature_system.py --binary build/tests/fovelle_tests",
            "python3 tests/quality_system_probe.py --app build/Fovelle.app/Contents/MacOS/Fovelle --runs 3",
            "python3 tests/quality_layout_system.py --app build/Fovelle.app/Contents/MacOS/Fovelle",
            "python3 tests/quality_specification.py --repo .",
        ],
        "evidence_files": artifacts,
        "summary": {
            "static": records.get("static.json", {}).get("passed"),
            "unit": records.get("unit.json", {}).get("passed"),
            "integration": records.get("integration.json", {}).get("passed"),
            "release_contract": records.get("release.json", {}).get("passed"),
            "system_feature": records.get("system_feature.json", {}).get("passed"),
            "system_probe": records.get("system_probe.json", {}).get("passed"),
            "system_layout": records.get("system_layout.json", {}).get("passed"),
            "atomic_specification": records.get("test-specification.json", {}).get("passed"),
        },
        "facts": [
            "Each evidence file is JSON and is included with a SHA-256 digest.",
            "The CI failure analysis is separated from local pass/fail evidence and retains facts, inference, confidence, and uncertainty.",
            "The manifest describes the working tree at generation time; a fresh remote workflow run is required after publishing the fix.",
        ],
        "errors": errors,
        "passed": not errors,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())

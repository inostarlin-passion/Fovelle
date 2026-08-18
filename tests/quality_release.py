#!/usr/bin/env python3
"""Validate the Universal, signed, notarized macOS release contract."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
from pathlib import Path


def add_check(checks: list[dict], identifier: str, passed: bool, actual: object, expected: str) -> None:
    checks.append({"id": identifier, "pass": bool(passed), "actual": actual, "expected": expected})


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    repo = args.repo.resolve()
    workflow = (repo / ".github/workflows/release.yml").read_text(encoding="utf-8")
    script_path = repo / "dist/scripts/package-macos-release.sh"
    script = script_path.read_text(encoding="utf-8")
    checks: list[dict] = []

    universal_contract = all(
        marker in workflow
        for marker in (
            'CMAKE_OSX_ARCHITECTURES="x86_64;arm64"',
            "macOS-universal.zip",
        )
    ) and all(marker in script for marker in ("lipo -archs", '"arm64"', '"x86_64"'))
    add_check(
        checks,
        "R-01",
        universal_contract,
        {
            "cmake_universal_flag": 'CMAKE_OSX_ARCHITECTURES="x86_64;arm64"' in workflow,
            "universal_asset_name": "macOS-universal.zip" in workflow,
            "lipo_verification": "lipo -archs" in script,
            "both_architectures_checked": '"arm64"' in script and '"x86_64"' in script,
        },
        "Release builds and verifies both arm64 and x86_64 in the app and publishes an explicitly Universal asset",
    )

    deployment_contract = "macdeployqt" in script and "-always-overwrite" in script and "Sign, notarize, validate, and package" in workflow
    add_check(
        checks,
        "R-02",
        deployment_contract,
        {
            "macdeployqt": "macdeployqt" in script,
            "always_overwrite": "-always-overwrite" in script,
            "workflow_step": "Sign, notarize, validate, and package" in workflow,
        },
        "the Release app is self-contained with Qt frameworks and plugins before signing",
    )

    secret_names = (
        "APPLE_APP_SPECIFIC_PASSWORD",
        "APPLE_CERTIFICATE_BASE64",
        "APPLE_CERTIFICATE_PASSWORD",
        "APPLE_ID",
        "APPLE_TEAM_ID",
    )
    secrets_contract = all(f"secrets.{name}" in workflow for name in secret_names)
    add_check(
        checks,
        "R-03",
        secrets_contract,
        {name: f"secrets.{name}" in workflow for name in secret_names},
        "all five configured GitHub Secrets are explicitly wired to the release step without embedding values",
    )

    signing_contract = all(
        marker in script
        for marker in (
            "security import",
            "security find-identity",
            "Developer ID Application:",
            "--timestamp",
            "--options runtime",
            "security set-key-partition-list",
        )
    ) and "codesign --sign -" not in script
    add_check(
        checks,
        "R-04",
        signing_contract,
        {
            "certificate_import": "security import" in script,
            "developer_id_identity_check": "Developer ID Application:" in script,
            "secure_timestamp": "--timestamp" in script,
            "hardened_runtime": "--options runtime" in script,
            "ad_hoc_signing_absent": "codesign --sign -" not in script,
        },
        "the app is signed with an imported Developer ID Application identity, secure timestamp, and Hardened Runtime",
    )

    notarization_contract = all(
        marker in script
        for marker in (
            "xcrun notarytool submit",
            "--apple-id",
            "--password",
            "--team-id",
            "--wait",
            "xcrun stapler staple",
            "xcrun stapler validate",
        )
    )
    add_check(
        checks,
        "R-05",
        notarization_contract,
        {
            "notarytool_wait": "xcrun notarytool submit" in script and "--wait" in script,
            "apple_credentials": all(marker in script for marker in ("--apple-id", "--password", "--team-id")),
            "staple_and_validate": "xcrun stapler staple" in script and "xcrun stapler validate" in script,
        },
        "the signed app is submitted to Apple, waits for approval, staples the ticket, and validates it",
    )

    verification_contract = all(
        marker in script
        for marker in (
            "spctl --assess --type execute",
            "codesign --verify --deep --strict",
            "ditto -x -k",
            "RELEASE_ZIP_PATH",
        )
    )
    add_check(
        checks,
        "R-06",
        verification_contract,
        {
            "gatekeeper": "spctl --assess --type execute" in script,
            "codesign_verify": "codesign --verify --deep --strict" in script,
            "final_zip_extracted": "ditto -x -k" in script,
            "zip_path": "RELEASE_ZIP_PATH" in script,
        },
        "the exact final zip is extracted and checked for architecture, signature, stapled ticket, and Gatekeeper acceptance",
    )

    secret_logging_contract = "set -x" not in script and all(f'echo "${name}"' not in script for name in secret_names)
    add_check(
        checks,
        "R-07",
        secret_logging_contract,
        {
            "shell_trace_disabled": "set -x" not in script,
            "secret_echo_absent": all(f'echo "${name}"' not in script for name in secret_names),
        },
        "the release script does not intentionally print certificate or Apple credential values",
    )

    dry_run = subprocess.run(
        ["bash", str(script_path)],
        cwd=repo,
        text=True,
        capture_output=True,
        env={
            **os.environ,
            "RELEASE_DRY_RUN": "true",
            "RELEASE_APP_PATH": "fixture/Fovelle.app",
            "RELEASE_ZIP_PATH": "fixture/Fovelle-universal.zip",
        },
        check=False,
    )
    dry_output = dry_run.stdout + dry_run.stderr
    dry_run_contract = dry_run.returncode == 0 and "Universal zip" in dry_output and "Developer ID Application" in dry_output
    add_check(
        checks,
        "R-08",
        dry_run_contract,
        {"return_code": dry_run.returncode, "output": dry_output[-2000:]},
        "the release orchestration is deterministically executable in dry-run mode without requiring secrets or network access",
    )

    result = {
        "kind": "release-contract",
        "repo": str(repo),
        "checks": checks,
        "passed": all(item["pass"] for item in checks),
        "limitations": [
            "Dry-run validates orchestration without consuming Apple credentials; actual notarization requires the configured GitHub Secrets and Apple service.",
            "Artifact-level Gatekeeper acceptance is verified by the Release workflow after the signed zip is produced.",
        ],
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())

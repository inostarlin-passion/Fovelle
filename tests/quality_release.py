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

    sdk15_contract = all(
        marker in workflow
        for marker in (
            "runs-on: macos-15",
            'test "${XCODE_VERSION%%.*}" -ge 16',
            'test "${SDK_VERSION%%.*}" -eq 15',
            "-DCMAKE_OSX_DEPLOYMENT_TARGET=15.0",
            'CMAKE_OSX_SYSROOT="$(xcrun --sdk macosx --show-sdk-path)"',
        )
    ) and all(
        marker in script
        for marker in (
            "EXPECTED_MACOS_DEPLOYMENT_TARGET",
            "assert_macos_deployment_target",
            "otool -l",
            "LSMinimumSystemVersion",
        )
    )
    add_check(
        checks,
        "R-10",
        sdk15_contract,
        {
            "release_runner_is_macos15": "runs-on: macos-15" in workflow,
            "release_sdk_is_exactly_15": 'test "${SDK_VERSION%%.*}" -eq 15' in workflow,
            "deployment_target_is_15": "-DCMAKE_OSX_DEPLOYMENT_TARGET=15.0" in workflow,
            "artifact_min_os_is_verified": "LSMinimumSystemVersion" in script,
            "artifact_sdk_is_verified": "otool -l" in script,
        },
        "Release compiles against the macOS 15 SDK and fails closed unless the final bundle targets macOS 15",
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
            "-t cert",
            "--timestamp",
            "--options runtime",
            "-A",
            "security unlock-keychain",
            "-T /usr/bin/codesign",
            "security list-keychain -d user -s",
            "security find-key -s -t private",
            "security set-key-partition-list",
            "if ! security set-key-partition-list",
            '-l "$SIGNING_IDENTITY"',
            "continuing with the imported codesign ACL",
            '--keychain "$KEYCHAIN_PATH"',
        )
    ) and "codesign --sign -" not in script
    add_check(
        checks,
        "R-04",
        signing_contract,
        {
            "certificate_import": "security import" in script,
            "pkcs12_certificate_type": "-t cert" in script,
            "developer_id_identity_check": "Developer ID Application:" in script,
            "secure_timestamp": "--timestamp" in script,
            "hardened_runtime": "--options runtime" in script,
            "ephemeral_keychain_access": "-A" in script and "security unlock-keychain" in script,
            "codesign_private_key_acl": "-T /usr/bin/codesign" in script,
            "keychain_search_list": "security list-keychain -d user -s" in script,
            "private_signing_key_preflight": "security find-key -s -t private" in script,
            "identity_scoped_partition_update": "security set-key-partition-list" in script and '-l "$SIGNING_IDENTITY"' in script,
            "partition_update_is_best_effort": "if ! security set-key-partition-list" in script and "continuing with the imported codesign ACL" in script,
            "explicit_keychain_for_codesign": '--keychain "$KEYCHAIN_PATH"' in script,
            "ad_hoc_signing_absent": "codesign --sign -" not in script,
        },
        "the app is signed with an imported Developer ID Application identity, verified private key access, explicit temporary keychain access, secure timestamp, and Hardened Runtime",
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
            "NOTARIZATION_TIMEOUT": "30m",
        },
        check=False,
    )
    dry_output = dry_run.stdout + dry_run.stderr
    dry_run_contract = (
        dry_run.returncode == 0
        and "Universal zip" in dry_output
        and "Developer ID Application" in dry_output
        and "DRY_RUN_NOTARIZATION_TIMEOUT: 30m" in dry_output
    )
    add_check(
        checks,
        "R-08",
        dry_run_contract,
        {"return_code": dry_run.returncode, "output": dry_output[-2000:]},
        "the release orchestration is deterministically executable in dry-run mode without requiring secrets or network access",
    )

    custom_timeout = subprocess.run(
        ["bash", str(script_path)],
        cwd=repo,
        text=True,
        capture_output=True,
        env={
            **os.environ,
            "RELEASE_DRY_RUN": "true",
            "RELEASE_APP_PATH": "fixture/Fovelle.app",
            "RELEASE_ZIP_PATH": "fixture/Fovelle-universal.zip",
            "NOTARIZATION_TIMEOUT": "45s",
        },
        check=False,
    )
    invalid_timeout = subprocess.run(
        ["bash", str(script_path)],
        cwd=repo,
        text=True,
        capture_output=True,
        env={
            **os.environ,
            "RELEASE_DRY_RUN": "true",
            "RELEASE_APP_PATH": "fixture/Fovelle.app",
            "RELEASE_ZIP_PATH": "fixture/Fovelle-universal.zip",
            "NOTARIZATION_TIMEOUT": "forever",
        },
        check=False,
    )
    timeout_contract = all(
        marker in script
        for marker in (
            'NOTARIZATION_TIMEOUT="${NOTARIZATION_TIMEOUT:-30m}"',
            "validate_notarization_timeout",
            'if ! xcrun notarytool submit',
            '--timeout "$NOTARIZATION_TIMEOUT"',
            "--verbose",
            "no release artifact was created",
        )
    ) and all(
        marker in workflow
        for marker in ("timeout-minutes: 60", "timeout-minutes: 45", 'NOTARIZATION_TIMEOUT: "30m"')
    )
    timeout_behavior = (
        custom_timeout.returncode == 0
        and "DRY_RUN_NOTARIZATION_TIMEOUT: 45s" in (custom_timeout.stdout + custom_timeout.stderr)
        and invalid_timeout.returncode != 0
        and "NOTARIZATION_TIMEOUT" in (invalid_timeout.stdout + invalid_timeout.stderr)
    )
    add_check(
        checks,
        "R-09",
        timeout_contract and timeout_behavior,
        {
            "default_timeout": 'NOTARIZATION_TIMEOUT="${NOTARIZATION_TIMEOUT:-30m}"' in script,
            "duration_validation": "validate_notarization_timeout" in script,
            "notary_timeout_argument": '--timeout "$NOTARIZATION_TIMEOUT"' in script,
            "notary_verbose_output": "--verbose" in script,
            "workflow_notarization_timeout": 'NOTARIZATION_TIMEOUT: "30m"' in workflow,
            "failure_is_fail_closed": "no release artifact was created" in script,
            "workflow_job_timeout": "timeout-minutes: 60" in workflow,
            "workflow_step_timeout": "timeout-minutes: 45" in workflow,
            "custom_timeout_dry_run": custom_timeout.returncode == 0 and "DRY_RUN_NOTARIZATION_TIMEOUT: 45s" in (custom_timeout.stdout + custom_timeout.stderr),
            "invalid_timeout_rejected": invalid_timeout.returncode != 0 and "NOTARIZATION_TIMEOUT" in (invalid_timeout.stdout + invalid_timeout.stderr),
        },
        "Apple notarization has a validated finite wait, observable progress, workflow-level safety bounds, and fail-closed timeout behavior",
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

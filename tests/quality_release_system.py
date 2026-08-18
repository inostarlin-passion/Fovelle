#!/usr/bin/env python3
"""Run the release orchestration as a system-level dry run or inspect a final artifact."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import tempfile
import time
from pathlib import Path


def run(*args: str, cwd: Path | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(args, cwd=cwd, text=True, capture_output=True, check=False)


def universal_macho_files(app: Path) -> tuple[bool, list[dict]]:
    observations: list[dict] = []
    for candidate in app.joinpath("Contents").rglob("*"):
        if not candidate.is_file():
            continue
        file_result = run("file", "-b", str(candidate))
        if "Mach-O" not in file_result.stdout:
            continue
        lipo_result = run("lipo", "-archs", str(candidate))
        architectures = lipo_result.stdout.strip()
        observations.append({"path": str(candidate), "architectures": architectures, "return_code": lipo_result.returncode})
        if lipo_result.returncode != 0 or not {"arm64", "x86_64"}.issubset(set(architectures.split())):
            return False, observations
    return bool(observations), observations


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--app", type=Path, default=None, help="optional extracted signed app to inspect")
    parser.add_argument("--zip", type=Path, default=None, help="optional final Universal zip to inspect")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    repo = args.repo.resolve()
    script = repo / "dist/scripts/package-macos-release.sh"
    cases: list[dict] = []

    # The script requires RELEASE_DRY_RUN, so run it with an isolated environment.
    dry_started = time.perf_counter()
    dry_run = subprocess.run(
        ["bash", str(script)],
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
    dry_elapsed_seconds = time.perf_counter() - dry_started
    dry_output = dry_run.stdout + dry_run.stderr
    dry_run_passed = dry_run.returncode == 0 and "Gatekeeper verification" in dry_output and "Universal zip" in dry_output and dry_elapsed_seconds <= 5.0
    cases.append(
        {
            "id": "TC-REL-SYSTEM-DRYRUN",
            "test": "package-macos-release.sh dry-run",
            "status": "passed" if dry_run_passed else "failed",
        }
    )

    artifact_validation = "deferred"
    artifact_observations: dict = {}
    if args.app is not None and args.zip is not None:
        app = args.app.resolve()
        release_zip = args.zip.resolve()
        universal, macho_observations = universal_macho_files(app)
        signature = run("codesign", "--verify", "--deep", "--strict", "--verbose=2", str(app))
        stapler = run("xcrun", "stapler", "validate", str(app))
        gatekeeper = run("spctl", "--assess", "--type", "execute", "--verbose=4", "--ignore-cache", str(app))
        with tempfile.TemporaryDirectory(prefix="fovelle-release-system-") as directory:
            extract = run("ditto", "-x", "-k", str(release_zip), directory)
            extracted_app = Path(directory) / app.name
            extracted_universal, extracted_macho_observations = universal_macho_files(extracted_app) if extracted_app.is_dir() else (False, [])
        artifact_validation = "executed"
        artifact_observations = {
            "app_universal": universal,
            "app_macho_files": macho_observations,
            "codesign_return_code": signature.returncode,
            "stapler_return_code": stapler.returncode,
            "gatekeeper_return_code": gatekeeper.returncode,
            "zip_extract_return_code": extract.returncode,
            "extracted_app_universal": extracted_universal,
            "extracted_macho_files": extracted_macho_observations,
        }
        cases.extend(
            [
                {"id": "TC-REL-SYSTEM-UNIVERSAL", "test": "final app Universal architecture", "status": "passed" if universal and extracted_universal else "failed"},
                {"id": "TC-REL-SYSTEM-SIGNATURE", "test": "final app Developer ID code signature", "status": "passed" if signature.returncode == 0 else "failed"},
                {"id": "TC-REL-SYSTEM-NOTARIZATION", "test": "final app stapled notarization ticket", "status": "passed" if stapler.returncode == 0 else "failed"},
                {"id": "TC-REL-SYSTEM-GATEKEEPER", "test": "final app Gatekeeper assessment", "status": "passed" if gatekeeper.returncode == 0 else "failed"},
            ]
        )

    record = {
        "kind": "system-release",
        "repo": str(repo),
        "cases": cases,
        "performance": {
            "dry_run_elapsed_seconds": dry_elapsed_seconds,
            "dry_run_max_seconds": 5.0,
            "passed": dry_run_passed,
        },
        "artifact_validation": artifact_validation,
        "artifact_observations": artifact_observations,
        "passed": all(case["status"] == "passed" for case in cases),
        "limitations": [
            "Without --app and --zip, this local gate intentionally runs only the deterministic orchestration dry run; it does not consume Apple credentials.",
            "The actual signed artifact gate runs inside the GitHub Release job after Apple notarization and extracts the exact uploaded zip.",
        ],
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(record, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(record, ensure_ascii=False, indent=2))
    return 0 if record["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())

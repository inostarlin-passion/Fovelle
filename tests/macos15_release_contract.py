#!/usr/bin/env python3
"""Reusable, non-invasive checks for the macOS 15 Release artifact contract."""

from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import tempfile
import time
from pathlib import Path


EXPECTED_MACOS_DEPLOYMENT_TARGET = "15.0"


def run(*command: str, timeout: float = 30.0) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        text=True,
        capture_output=True,
        timeout=timeout,
        check=False,
    )


def _version_tuple(value: str) -> tuple[int, int]:
    match = re.fullmatch(r"(\d+)\.(\d+)", value.strip())
    if not match:
        return (-1, -1)
    return int(match.group(1)), int(match.group(2))


def macho_records(app: Path) -> list[dict]:
    records: list[dict] = []
    contents = app / "Contents"
    if not contents.is_dir():
        return records
    for candidate in sorted(contents.rglob("*")):
        if not candidate.is_file():
            continue
        file_result = run("file", "-b", str(candidate))
        if "Mach-O" not in file_result.stdout:
            continue
        load_commands = run("otool", "-l", str(candidate))
        minos_match = re.search(r"^\s*minos\s+(\S+)", load_commands.stdout, re.MULTILINE)
        sdk_match = re.search(r"^\s*sdk\s+(\S+)", load_commands.stdout, re.MULTILINE)
        records.append(
            {
                "path": str(candidate),
                "min_os": minos_match.group(1) if minos_match else None,
                "sdk": sdk_match.group(1) if sdk_match else None,
                "otool_return_code": load_commands.returncode,
            }
        )
    return records


def plist_minimum_system_version(app: Path) -> str | None:
    plist = app / "Contents/Info.plist"
    result = run("/usr/libexec/PlistBuddy", "-c", "Print :LSMinimumSystemVersion", str(plist))
    return result.stdout.strip() if result.returncode == 0 else None


def validate_app(
    app: Path,
    expected: str = EXPECTED_MACOS_DEPLOYMENT_TARGET,
    require_sdk_family: bool = False,
) -> dict:
    app = app.resolve()
    records = macho_records(app)
    expected_version = _version_tuple(expected)
    main_path = app / "Contents/MacOS/Fovelle"
    main = next((item for item in records if Path(item["path"]) == main_path), None)
    dependency_checks = []
    for record in records:
        version = _version_tuple(record["min_os"] or "")
        dependency_checks.append(
            {
                "path": record["path"],
                "min_os": record["min_os"],
                "compatible_with_target": version >= (0, 0) and version <= expected_version,
            }
        )
    main_sdk = (main or {}).get("sdk")
    main_min_os = (main or {}).get("min_os")
    result = {
        "app": str(app),
        "expected_minimum_system_version": expected,
        "macho_count": len(records),
        "main_executable": main,
        "main_minimum_system_version_matches": main_min_os == expected,
        "main_sdk_major_matches_target": bool(main_sdk)
        and main_sdk.split(".", 1)[0] == expected.split(".", 1)[0],
        "embedded_dependencies_are_compatible": bool(dependency_checks)
        and all(item["compatible_with_target"] for item in dependency_checks),
        "info_plist_minimum_system_version": plist_minimum_system_version(app),
        "info_plist_minimum_system_version_matches": (
            plist_minimum_system_version(app) == expected
        ),
        "dependencies": dependency_checks,
    }
    result["sdk_family_required_for_pass"] = require_sdk_family
    result["passed"] = (
        result["macho_count"] > 0
        and result["main_minimum_system_version_matches"]
        and (not require_sdk_family or result["main_sdk_major_matches_target"])
        and result["embedded_dependencies_are_compatible"]
        and result["info_plist_minimum_system_version_matches"]
    )
    return result


def install_extract_and_launch(app: Path, output_directory: Path) -> dict:
    """Copy, deploy Qt, zip/extract, and launch an unsigned local app bundle."""
    output_directory.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="fovelle-macos15-smoke-") as temporary:
        root = Path(temporary)
        installed_app = root / app.name
        copy_result = run("ditto", "--rsrc", "--extattr", str(app), str(installed_app), timeout=120)
        macdeployqt = shutil.which("macdeployqt")
        deploy_result = None
        if macdeployqt and copy_result.returncode == 0:
            deploy_result = run(macdeployqt, str(installed_app), "-always-overwrite", timeout=180)
        zip_path = root / "Fovelle-macOS15-smoke.zip"
        zip_result = run(
            "ditto", "-c", "-k", "--sequesterRsrc", "--keepParent",
            str(installed_app), str(zip_path), timeout=120,
        )
        extracted_root = root / "extracted"
        extracted_root.mkdir()
        extract_result = run("ditto", "-x", "-k", str(zip_path), str(extracted_root), timeout=120)
        extracted_app = extracted_root / app.name
        validation = validate_app(extracted_app)

        launch_output = output_directory / "macos15_release_smoke.log"
        launch_command = [str(extracted_app / "Contents/MacOS/Fovelle")]
        started = time.perf_counter()
        process = subprocess.Popen(
            launch_command,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            env={**os.environ, "QT_QPA_PLATFORM": "cocoa"},
        )
        timed_out = False
        try:
            launch_stdout, _ = process.communicate(timeout=4.0)
        except subprocess.TimeoutExpired as error:
            timed_out = True
            process.terminate()
            try:
                launch_stdout, _ = process.communicate(timeout=2.0)
            except subprocess.TimeoutExpired:
                process.kill()
                launch_stdout, _ = process.communicate()
            if not launch_stdout:
                launch_stdout = error.output or ""
        elapsed = time.perf_counter() - started
        launch_output.write_text(launch_stdout or "", encoding="utf-8")
        launch_started = timed_out or process.returncode in (0, 143, 152, -15)
        record = {
            "source_app": str(app),
            "copy_return_code": copy_result.returncode,
            "macdeployqt": macdeployqt,
            "macdeployqt_return_code": deploy_result.returncode if deploy_result else None,
            "zip_return_code": zip_result.returncode,
            "extract_return_code": extract_result.returncode,
            "extracted_app": str(extracted_app),
            "artifact_contract": validation,
            "launch_command": launch_command,
            "launch_return_code": process.returncode,
            "launch_elapsed_seconds": elapsed,
            "launch_log": str(launch_output),
            "launch_timed_out_and_was_terminated": timed_out,
            "launch_started_before_timeout": launch_started,
        }
    record["passed"] = (
        record["copy_return_code"] == 0
        and record["macdeployqt_return_code"] == 0
        and record["zip_return_code"] == 0
        and record["extract_return_code"] == 0
        and record["artifact_contract"]["passed"]
        and record["launch_started_before_timeout"]
    )
    return record


def json_summary(record: dict) -> str:
    return json.dumps(record, ensure_ascii=False, sort_keys=True)

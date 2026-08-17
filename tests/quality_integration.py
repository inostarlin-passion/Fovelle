#!/usr/bin/env python3
"""Deterministic integration checks for the Fovelle upstream merge.

The checks intentionally use only Git, the built bundle, and repository text.  They
are suitable for CI and write a machine-readable evidence record for the audit
reports.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path


UPSTREAM_HEAD = "1748058632574831516f75e642ef28b7e3cc3e63"
SELECTED_COMMITS = [
    "947ee1b99bb8d1b5057fd377a79fcb8d87682ab1",
    "f083481195192fea2452e4caacee2059f513970a",
    "19d6a38fdba5bfa9d871752996adbc6a9542ac06",
    "eebace8be56afd7e85551881cbf58479d1b1bc67",
    "9285eb514edef90fe88e80db4352d30c85eb94d2",
    "bded864bd5919a2849b58f10dcd02e6a0db238ce",
    "fd1b0b76955b1d93429edb00f07c2b5122d1fc8b",
    "3a1621267bd82d8acbe43469fc843ea06cb92cce",
    "08cecddf0f478f753614e0cc78fa8b06f6f93473",
    "a964d84f4ce7df5cf3428db23c2e547f227ab774",
    "7fcc691a3de78aa05eafeb9f5c1a68908f08fab8",
    "ee20744c983f999906557e640b6001d57e32cccb",
]


def run(repo: Path, *args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["git", *args], cwd=repo, text=True, capture_output=True, check=False
    )


def check(checks: list[dict], identifier: str, passed: bool, actual: str, expected: str) -> None:
    checks.append(
        {
            "id": identifier,
            "pass": bool(passed),
            "actual": actual,
            "expected": expected,
        }
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--build-dir", type=Path, default=None)
    parser.add_argument("--output", type=Path, default=None)
    args = parser.parse_args()

    repo = args.repo.resolve()
    build_dir = (args.build_dir or repo / "build-quality").resolve()
    checks: list[dict] = []

    head = run(repo, "rev-parse", "HEAD").stdout.strip()
    merge_commit = run(repo, "log", "--merges", "-1", "--format=%H").stdout.strip()
    parents = run(repo, "rev-list", "--parents", "-n", "1", merge_commit).stdout.split()
    second_parent = parents[2] if len(parents) >= 3 else ""
    check(
        checks,
        "I-01",
        len(parents) == 3 and second_parent == UPSTREAM_HEAD,
        f"HEAD={head}; merge_commit={merge_commit or '<missing>'}; second_parent={second_parent or '<missing>'}",
        f"two-parent merge with second parent {UPSTREAM_HEAD}",
    )

    reachable = []
    for commit in SELECTED_COMMITS:
        object_type = run(repo, "cat-file", "-t", commit).stdout.strip()
        ancestry = run(repo, "merge-base", "--is-ancestor", commit, "HEAD").returncode == 0
        reachable.append({"commit": commit, "object_type": object_type, "ancestor_of_head": ancestry})
    check(
        checks,
        "I-02",
        all(item["object_type"] == "commit" and item["ancestor_of_head"] for item in reachable),
        json.dumps(reachable, sort_keys=True),
        "all 12 workbook commit IDs are commits reachable from HEAD",
    )

    tracked = run(repo, "ls-tree", "-r", "--name-only", "HEAD").stdout.splitlines()
    tracked_set = set(tracked)
    required_paths = [
        "src/qvimageloader.cpp",
        "src/qvfileenumerator.cpp",
        "src/qvmovie.cpp",
        "src/qvnamespace.h",
        "src/qvopenwithdialog.ui",
        "qView.pro",
        "dist/scripts/download-plugins.ps1",
        "dist/scripts/macdeploy.sh",
        "CMakeLists.txt",
        "tests/tst_qviewtests.cpp",
    ]
    missing = [path for path in required_paths if path not in tracked_set]
    check(
        checks,
        "I-03",
        not missing,
        f"missing={missing}",
        "all upstream/runtime/build/test paths are tracked",
    )

    conflict_scan = subprocess.run(
        [
            "grep",
            "-R",
            "-n",
            "--exclude-dir=.git",
            "--exclude-dir=build-quality",
            "-E",
            r"^(<<<<<<<|=======|>>>>>>>)",
            ".",
        ],
        cwd=repo,
        text=True,
        capture_output=True,
        check=False,
    )
    check(
        checks,
        "I-04",
        conflict_scan.returncode == 1,
        conflict_scan.stdout[:2000] or "no conflict markers",
        "no merge conflict marker in repository text",
    )

    diff_check = run(repo, "diff", "--check", "HEAD^1", "HEAD")
    check(
        checks,
        "I-05",
        diff_check.returncode == 0,
        diff_check.stdout + diff_check.stderr,
        "git diff --check is clean against the local first parent",
    )

    source_expectations = {
        "src/qvapplication.cpp": ["QFileOpenEvent", "queueFileOpen"],
        "src/qvimagecore.cpp": ["handleColorSpaceConversion", "QVMovie"],
        "dist/scripts/download-plugins.ps1": ["Fovelle.app", "Expand-Archive"],
        "dist/scripts/macdeploy.sh": ["Fovelle.app", "Fovelle-"],
        ".github/workflows/build.yml": ["Build Fovelle", "BUILD_TESTS=ON"],
    }
    source_results = {}
    for relative, needles in source_expectations.items():
        text = (repo / relative).read_text(encoding="utf-8")
        source_results[relative] = {needle: needle in text for needle in needles}
    check(
        checks,
        "I-06",
        all(all(values.values()) for values in source_results.values()),
        json.dumps(source_results, sort_keys=True),
        "Fovelle event, image, plugin, deployment, and CI integration points are present",
    )

    bundle = build_dir / "Fovelle.app"
    executable = bundle / "Contents" / "MacOS" / "Fovelle"
    info_plist = bundle / "Contents" / "Info.plist"
    bundle_result = {
        "bundle_exists": bundle.is_dir(),
        "executable_exists": executable.is_file(),
        "info_plist_exists": info_plist.is_file(),
    }
    if info_plist.is_file():
        plist_text = info_plist.read_text(encoding="utf-8", errors="replace")
        bundle_result["fovelle_identity"] = (
            "io.github.inostarlin-passion.Fovelle" in plist_text
            and "<string>Fovelle</string>" in plist_text
        )
    else:
        bundle_result["fovelle_identity"] = False
    check(
        checks,
        "I-07",
        all(bundle_result.values()),
        json.dumps(bundle_result, sort_keys=True),
        "built Fovelle.app contains executable and Fovelle bundle identity",
    )

    ctest = subprocess.run(
        ["ctest", "--test-dir", str(build_dir), "--output-on-failure"],
        cwd=repo,
        text=True,
        capture_output=True,
        check=False,
    )
    check(
        checks,
        "I-08",
        ctest.returncode == 0,
        ctest.stdout + ctest.stderr,
        "CTest integration suite exits zero",
    )

    result = {
        "kind": "integration",
        "repo": str(repo),
        "head": head,
        "merge_commit": merge_commit,
        "upstream_head": UPSTREAM_HEAD,
        "selected_commits": SELECTED_COMMITS,
        "checks": checks,
        "passed": all(item["pass"] for item in checks),
    }
    output = args.output or repo / "reports" / "evidence" / "integration.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())

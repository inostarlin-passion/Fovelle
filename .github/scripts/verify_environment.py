#!/usr/bin/env python3
"""Fail early on toolchain drift and persist the exact hosted environment."""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import platform
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def version(text):
    match = re.search(r'\d+\.\d+(?:\.\d+)?', text)
    if not match:
        raise ValueError(f'Cannot parse version: {text!r}')
    return match.group()


def collect(lane):
    observed = {'system': platform.system(), 'architecture': platform.machine(),
                'python': platform.python_version()}
    commands = {'clang_format': ['clang-format', '--version']} if lane == 'format' else {
        'xcode': ['xcodebuild', '-version'], 'sdk': ['xcrun', '--sdk', 'macosx', '--show-sdk-version'],
        'qt': ['qmake', '-query', 'QT_VERSION'], 'cmake': ['cmake', '--version'],
        'ninja': ['ninja', '--version'], 'clang_tidy': ['clang-tidy', '--version']}
    if lane == 'format':
        observed['distribution'] = platform.freedesktop_os_release().get('VERSION_ID', '')
    else:
        observed['os_major'] = platform.mac_ver()[0].split('.')[0]
        observed['developer_dir'] = os.environ.get('DEVELOPER_DIR', '')
        observed['deployment_target'] = os.environ.get('MACOSX_DEPLOYMENT_TARGET', '')
    for key, command in commands.items():
        observed[key] = version(subprocess.check_output(command, text=True, stderr=subprocess.STDOUT))
    return observed


def validate(lane, observed, policy):
    expected = dict(policy['lanes'][lane])
    expected.pop('runner')
    expected['python'] = policy['python']
    if lane == 'format':
        expected['clang_format'] = policy['llvm']
    else:
        expected.update({key: policy[key] for key in ['qt', 'cmake', 'ninja', 'deployment_target']})
        expected['clang_tidy'] = policy['llvm']
    return [f'{key}: expected {value}, observed {observed.get(key)!r}'
            for key, value in expected.items() if observed.get(key) != value]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--lane', choices=['main', 'compatibility', 'format'], required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    policy = json.loads((ROOT / '.github/ci-environment.json').read_text())
    record = {'lane': args.lane, 'timestamp': datetime.now(timezone.utc).isoformat(), 'observed': {},
              'runner_image': {k: os.environ.get(k) for k in ['ImageOS', 'ImageVersion', 'RUNNER_OS', 'RUNNER_ARCH']},
              'commit': os.environ.get('GITHUB_SHA')}
    try:
        record['observed'] = collect(args.lane)
        record['errors'] = validate(args.lane, record['observed'], policy)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        record['errors'] = [str(error)]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(record, indent=2) + '\n')
    print(json.dumps(record, indent=2))
    return 1 if record['errors'] else 0


if __name__ == '__main__':
    raise SystemExit(main())

#!/usr/bin/env python3
"""One bounded, deduplicated default gate for local development and CI."""
import argparse
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time

from run_portable_hdr import FULLSCREEN_ROWS, FIXTURES, prepare_output, run_suite, validate_fixtures

ROOT = Path(__file__).resolve().parents[1]
BUDGET_SECONDS = 165
REQUIRED = {
    'ImageLoaderTests': {'testColorSvgWideGamut', 'testColorNetpbm', 'testVectorPresentationFrameBudget',
                         'testVectorDisplayTargetSwitch', 'testVectorRequestBurstAndDestruction'},
    'FeatureTests': {'testKeepZoomPersistsAcrossProcesses', 'testUpdateConfigurationValidation'},
    'GraphicsViewTests': {'testMousePanKeepsOverflowRangeAndContinuity', 'testKeepZoomAcrossNavigation'},
    'HDRPolicyTests': {'testHDRBrightnessLinearLightPixels'},
    'ImageCoreAndMovieTests': {'testHDRPersistentSurfaceMetadata'},
    'WindowBehaviorTests': {'testNativeFullScreenRoundTrip'},
    'RemediationTests': {'testMoviePauseResume', 'testMoviePauseQueuedRead', 'testMovieAsyncPlaybackSpeed',
                         'testDownloadLimits', 'testLoaderNonblockingShutdown'},
    'ScrollHelperTests': set(), 'ApplicationEventTests': set(), 'ShortcutSettingsTests': set(),
}


def validate_manifest(manifest):
    if manifest.get('version') != 1 or manifest.get('budget_seconds') != BUDGET_SECONDS:
        raise ValueError('Unsupported manifest or changed shared budget')
    if manifest.get('portable_hdr') is not True or manifest.get('legacy_bt709') is not True:
        raise ValueError('Mandatory native HDR or legacy color coverage removed')
    groups = manifest.get('groups', [])
    if len({g['suite'] for g in groups}) != len(groups):
        raise ValueError('Duplicate Qt suite')
    by_suite = {g['suite']: g['functions'] for g in groups}
    if set(by_suite) != set(REQUIRED):
        raise ValueError('Missing or unexpected risk domain')
    for suite, selectors in by_suite.items():
        if not selectors or len(set(selectors)) != len(selectors):
            raise ValueError('Empty or duplicate function selectors: ' + suite)
        if not all(re.fullmatch(r'test\w+(?::[\w=-]+)?', s) for s in selectors):
            raise ValueError('Invalid function selector')
        bases = {s.split(':')[0] for s in selectors}
        if not REQUIRED[suite] <= bases:
            raise ValueError('Core regressions omitted: ' + suite)
        for selector in selectors:
            if ':' in selector and selector.split(':')[0] in selectors:
                raise ValueError('Function and its individual rows selected together')
    reserved = {'RemediationTests': 'testPortableHDR',
                'HDRPolicyTests': 'testHDRBrightnessNativeColorFidelity',
                'WindowBehaviorTests': 'testHDRFullScreenVisualContinuity'}
    for suite, function in reserved.items():
        if any(s.split(':')[0] == function for s in by_suite[suite]):
            raise ValueError('Native HDR function selected twice')
    scripts = [entry['script'] for entry in manifest['static']]
    if len(scripts) != len(set(scripts)) or 'test_fast_tests.py' not in scripts:
        raise ValueError('Duplicate static script or missing gate self-tests')
    for script in scripts:
        if Path(script).name != script or not (ROOT / 'tests' / script).is_file():
            raise ValueError('Invalid static script: ' + script)


def run_command(name, command, environment, output, deadline):
    output.mkdir(parents=True, exist_ok=True)
    record = {'name': name, 'status': 'failed', 'returncode': None, 'command': command}
    started = time.monotonic()
    try:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise subprocess.TimeoutExpired(command, 0)
        with (output / (name + '.log')).open('w') as stream:
            process = subprocess.Popen(command, env=environment, stdout=stream,
                                       stderr=subprocess.STDOUT, start_new_session=True)
            try:
                record['returncode'] = process.wait(timeout=remaining)
            except subprocess.TimeoutExpired:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                record['returncode'] = process.wait(timeout=3)
                raise
        record['status'] = 'passed' if record['returncode'] == 0 else 'failed'
    except subprocess.TimeoutExpired:
        record['status'] = 'timeout'
    except OSError as error:
        record['error'] = str(error)
    record['elapsed_seconds'] = round(time.monotonic() - started, 3)
    (output / (name + '.json')).write_text(json.dumps(record, indent=2) + '\n')
    return record


def main():
    started = time.monotonic()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('--profile', choices=['local', 'ci'],
                        default=os.environ.get('FOVELLE_TEST_PROFILE', 'local'))
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--portable-output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    summary = {'profile': args.profile, 'budget_seconds': BUDGET_SECONDS,
               'status': 'failed', 'stages': []}
    deadline = started + BUDGET_SECONDS
    try:
        manifest = json.loads((ROOT / 'tests/fast_test_manifest.json').read_text())
        validate_manifest(manifest)
        executable = str(args.executable.resolve())
        env = os.environ.copy()
        for variable in ('FOVELLE_TEST_SUITE', 'QT_SCALE_FACTOR', 'FOVELLE_HDR_REPRO_IMAGE',
                         'FOVELLE_HDR_JPEG_SAMPLE', 'FOVELLE_HDR_FULLSCREEN_IMAGE'):
            env.pop(variable, None)
        env.update(QT_QPA_PLATFORM='cocoa', QT_FATAL_WARNINGS='1',
                   QTEST_FUNCTION_TIMEOUT='30000', FOVELLE_DISABLE_AUTO_UPDATE_CHECK='1')
        portable = [
            {'suite': 'RemediationTests', 'functions': ['testPortableHDR']},
            {'suite': 'HDRPolicyTests', 'functions': ['testHDRBrightnessNativeColorFidelity']},
            {'suite': 'WindowBehaviorTests', 'functions': ['testHDRFullScreenVisualContinuity']},
        ]
        # Write every planned stage before executing anything, including HDR evidence.
        prepare_output(output, [g['suite'] for g in manifest['groups']])
        prepare_output(args.portable_output, [g['suite'] for g in portable])
        planned = [e['script'] for e in manifest['static']] + [g['suite'] for g in manifest['groups']]
        planned += ['legacy-bt709'] + ['portable-' + g['suite'] for g in portable]
        summary['stages'] = [{'name': name, 'status': 'not-run'} for name in planned]
        validate_fixtures()
        index = 0

        def complete(record):
            nonlocal index
            summary['stages'][index].update(record)
            print(json.dumps(summary['stages'][index]), flush=True)
            index += 1
            if record['status'] != 'passed':
                raise RuntimeError('Stage failed; inspect logs: ' + str(output))

        for entry in manifest['static']:
            command = [sys.executable, str(ROOT / 'tests' / entry['script'])]
            command += [arg.format(repo=ROOT, output=output) for arg in entry['args']]
            complete(run_command(entry['script'], command, env, output, deadline))
        for group in manifest['groups'] + [{'legacy': True}] + portable:
            if group.get('legacy'):
                complete(run_command('legacy-bt709', [str(Path(executable).with_name('fovelle_legacy_bt709_tests'))],
                                     env, output, deadline))
                continue
            native = group in portable
            destination = args.portable_output if native else output
            stage_env = env.copy()
            rows = {}
            for selector in group['functions']:
                if ':' in selector:
                    function, row = selector.split(':', 1)
                    rows.setdefault(function, []).append(row)
            if native:
                stage_env.update(FOVELLE_HDR_JPEG_SAMPLE=str(FIXTURES / 'gain-map.jpg'),
                                 FOVELLE_HDR_FULLSCREEN_IMAGE=str(FIXTURES / 'bayer.dng'))
                if group['suite'] == 'WindowBehaviorTests':
                    rows['testHDRFullScreenVisualContinuity'] = FULLSCREEN_ROWS
            try:
                record = run_suite(executable, group['suite'], group['functions'], stage_env,
                                   destination, timeout=deadline - time.monotonic(), required_rows=rows)
            except RuntimeError:
                record = json.loads((destination / (group['suite'] + '.json')).read_text())
            complete(record)
        summary['status'] = 'passed'
    except (OSError, ValueError, RuntimeError) as error:
        summary['error'] = str(error)
        print(str(error), file=sys.stderr)
    finally:
        summary['elapsed_seconds'] = round(time.monotonic() - started, 3)
        (output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    return 0 if summary['status'] == 'passed' else 1


if __name__ == '__main__':
    raise SystemExit(main())

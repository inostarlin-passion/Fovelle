#!/usr/bin/env python3
"""Mandatory real-file HDR gate with exact coverage and persistent diagnostics."""
from pathlib import Path
import argparse
from collections import Counter
import hashlib
import json
import os
import signal
import subprocess
import tempfile
import time
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parent
FIXTURES = ROOT / 'data/portable-hdr'
EXPECTED = {'gain-map.jpg', 'bayer.dng'}
FULLSCREEN_ROWS = [
    phase + '-' + visibility
    for visibility in ('visible', 'hidden')
    for phase in ('exit-bottom', 'enter-size', 'enter-position', 'exit-size', 'exit-position')
]


def validate_fixtures(root=FIXTURES):
    manifest = json.loads((root / 'manifest.json').read_text())
    if set(manifest) != EXPECTED:
        raise ValueError('Incomplete fixture manifest')
    for name, digest in manifest.items():
        if hashlib.sha256((root / name).read_bytes()).hexdigest() != digest:
            raise ValueError('Fixture checksum mismatch: ' + name)


def validate_xml(path, expected, *, suite=None, required_rows=None, invocations=None):
    root = ET.parse(path).getroot()
    if root.tag != 'TestCase' or (suite is not None and root.get('name') != suite):
        raise ValueError('Unexpected test suite')
    functions = root.findall('TestFunction')
    names = [function.get('name') for function in functions]
    if suite is not None and set(names) - (set(expected) | {'initTestCase', 'cleanupTestCase'}):
        raise ValueError('Unexpected executed test functions')
    counts = Counter(names)
    expected_counts = invocations or {name: 1 for name in names}
    if (not set(expected) <= set(names)
            or any(count != expected_counts.get(name, 1) for name, count in counts.items())):
        raise ValueError('Missing or duplicate executed test functions')
    for function in functions:
        incidents = function.findall('Incident')
        if not incidents or any(incident.get('type') != 'pass' for incident in incidents):
            raise ValueError('Non-pass or skipped test: ' + str(function.get('name')))
    for name, required in (required_rows or {}).items():
        if name not in names:
            raise ValueError('Missing data-driven function: ' + name)
        matching = [function for function in functions if function.get('name') == name]
        tags = [incident.findtext('DataTag') for function in matching
                for incident in function.findall('Incident')]
        if len(tags) != len(required) or set(tags) != set(required):
            raise ValueError('Missing, duplicate or unexpected data rows: ' + name)


def prepare_output(output, suites):
    """Remove all prior suite artifacts before any stage can fail early."""
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    for suite in suites:
        for suffix in ('xml', 'log', 'json'):
            (output / (suite + '.' + suffix)).unlink(missing_ok=True)
        (output / (suite + '.json')).write_text(json.dumps({
            'suite': suite, 'status': 'not-run', 'returncode': None,
            'error': 'This stage has not executed in the current gate invocation',
        }, indent=2) + '\n')


def run_suite(executable, suite, functions, environment, output, *, timeout=180, required_rows=None):
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    xml = output / (suite + '.xml')
    log = output / (suite + '.log')
    summary = output / (suite + '.json')
    # A crashed run must not reuse successful XML from a previous invocation.
    xml.unlink(missing_ok=True)
    log.write_text('')
    env = environment.copy()
    env['FOVELLE_TEST_SUITE'] = suite
    command = [executable, *functions, '-o', str(xml) + ',xml']
    record = {'suite': suite, 'functions': functions, 'status': 'failed',
              'returncode': None, 'error': None, 'xml': str(xml), 'log': str(log)}
    started = time.monotonic()
    try:
        if timeout <= 0:
            raise TimeoutError('The shared HDR gate budget has expired')
        with log.open('w') as stream:
            process = subprocess.Popen(command, env=env, stdout=stream, stderr=subprocess.STDOUT,
                                       start_new_session=True)
            try:
                record['returncode'] = process.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                # Qt crash diagnostics may have descendants. Reap our own
                # session rather than leaving a debugger behind on the runner.
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                record['returncode'] = process.wait(timeout=5)
                raise TimeoutError(f'{suite} exceeded its {timeout:.1f}s execution budget')
        if record['returncode'] != 0:
            raise ValueError(f'{suite} exited with status {record["returncode"]}')
        invocations = Counter(function.split(":", 1)[0] for function in functions)
        validate_xml(xml, set(invocations), suite=suite, required_rows=required_rows,
                     invocations=invocations)
        record['status'] = 'passed'
    except (OSError, ValueError, TimeoutError, subprocess.SubprocessError, ET.ParseError) as error:
        record['status'] = 'timeout' if isinstance(error, TimeoutError) else 'failed'
        record['error'] = str(error)
        raise RuntimeError(f'{error}; inspect {xml} and {log}') from error
    finally:
        record['elapsed_seconds'] = round(time.monotonic() - started, 3)
        summary.write_text(json.dumps(record, indent=2) + '\n')
    return record


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('executable')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    env = os.environ.copy()
    env.update(QT_QPA_PLATFORM='cocoa', FOVELLE_DISABLE_AUTO_UPDATE_CHECK='1',
               FOVELLE_HDR_JPEG_SAMPLE=str(FIXTURES / 'gain-map.jpg'),
               FOVELLE_HDR_FULLSCREEN_IMAGE=str(FIXTURES / 'bayer.dng'))
    runs = [
        ('RemediationTests', ['testPortableHDR'], None),
        ('HDRPolicyTests', ['testHDRBrightnessNativeColorFidelity'], None),
        ('WindowBehaviorTests', ['testHDRFullScreenVisualContinuity'],
         {'testHDRFullScreenVisualContinuity': FULLSCREEN_ROWS}),
    ]
    with tempfile.TemporaryDirectory(prefix='fovelle-hdr-gate-') as temporary:
        output = args.output or Path(temporary)
        prepare_output(output, [suite for suite, _, _ in runs])
        validate_fixtures()
        deadline = time.monotonic() + 270
        for suite, functions, rows in runs:
            run_suite(args.executable, suite, functions, env, output,
                      timeout=min(180, deadline - time.monotonic()), required_rows=rows)
            print(suite + ': actual HDR tests executed; zero failures/skips', flush=True)


if __name__ == '__main__':
    main()

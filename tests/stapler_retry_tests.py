#!/usr/bin/env python3
"""Release ticket retry contracts: real CLI execution plus injected failures."""
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
HELPER = ROOT / 'dist/scripts/stapler-with-retry.py'
NETWORK = 'Error Domain=NSURLErrorDomain Code=-1005 "The network connection was lost."'


def load_helper():
    spec = importlib.util.spec_from_file_location('stapler_retry', HELPER)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class StaplerRetryTests(unittest.TestCase):
    def exercise(self, results):
        module = load_helper()
        with patch.object(module.subprocess, 'run', side_effect=results) as run, \
                patch.object(module.time, 'sleep') as sleep:
            status = module.run_stapler('validate', '/tmp/app with spaces.app')
        return status, run, sleep

    def result(self, status, output=''):
        return subprocess.CompletedProcess([], status, output)

    def test_success_once(self):
        status, run, sleep = self.exercise([self.result(0, 'validated')])
        self.assertEqual(status, 0)
        self.assertEqual(run.call_count, 1)
        self.assertEqual(run.call_args.args[0], ['xcrun', 'stapler', 'validate', '/tmp/app with spaces.app'])
        self.assertEqual(run.call_args.kwargs['timeout'], 120)
        sleep.assert_not_called()

    def test_transient_recovery(self):
        status, run, sleep = self.exercise([self.result(68, NETWORK), self.result(68, NETWORK), self.result(0)])
        self.assertEqual(status, 0)
        self.assertEqual(run.call_count, 3)
        self.assertEqual([call.args[0] for call in sleep.call_args_list], [10, 20])

    def test_exhaustion(self):
        status, run, sleep = self.exercise([self.result(68, NETWORK)] * 5)
        self.assertEqual(status, 68)
        self.assertEqual(run.call_count, 5)
        self.assertEqual([call.args[0] for call in sleep.call_args_list], [10, 20, 40, 60])

    def test_permanent_failures(self):
        for code, output in [(68, 'not notarized'), (65, NETWORK), (77, 'revoked'),
                             (66, 'ticket missing'), (68, 'NSURLErrorDomain Code=-1009')]:
            with self.subTest(code=code, output=output):
                status, run, sleep = self.exercise([self.result(code, output)])
                self.assertEqual(status, code)
                self.assertEqual(run.call_count, 1)
                sleep.assert_not_called()

    def test_timeout_and_missing_tool(self):
        for error, expected in [(subprocess.TimeoutExpired(['xcrun'], 120), 124),
                                (FileNotFoundError('xcrun missing'), 127)]:
            with self.subTest(error=error):
                status, run, sleep = self.exercise([error])
                self.assertEqual(status, expected)
                self.assertEqual(run.call_count, 1)
                sleep.assert_not_called()

    def test_recovery_then_permanent_failure(self):
        status, run, sleep = self.exercise([self.result(68, NETWORK), self.result(65, 'invalid ticket')])
        self.assertEqual(status, 65)
        self.assertEqual(run.call_count, 2)
        self.assertEqual(sleep.call_count, 1)

    def test_cli_logs_paths_and_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            stub = Path(directory) / 'xcrun'
            stub.write_text('#!/bin/bash\nprintf "%s\\n" "$@"\nprintf "ticket diagnostic\\n" >&2\nexit "${FAKE_STATUS:-0}"\n')
            stub.chmod(0o755)
            for action in ('staple', 'validate'):
                for code in (0, 66, 68):
                    with self.subTest(action=action, code=code):
                        result = subprocess.run([sys.executable, str(HELPER), action, '/tmp/app with spaces.app'],
                                                env={**os.environ, 'PATH': directory + os.pathsep + os.environ['PATH'],
                                                     'FAKE_STATUS': str(code)}, capture_output=True, text=True, timeout=5)
                        self.assertEqual(result.returncode, code)
                        self.assertIn(action + '\n/tmp/app with spaces.app\n', result.stdout)
                        self.assertIn('ticket diagnostic', result.stdout)
                        self.assertIn('attempt 1/5', result.stdout)

    def test_cli_invalid_action(self):
        result = subprocess.run([sys.executable, str(HELPER), 'delete', 'app'], capture_output=True, timeout=5)
        self.assertEqual(result.returncode, 2)

    def test_static_release_gates(self):
        script = (ROOT / 'dist/scripts/package-macos-release.sh').read_text()
        for action, path in [('staple', 'APP_PATH'), ('validate', 'APP_PATH'), ('validate', 'VERIFIED_APP')]:
            self.assertIn(f'python3 "$RELEASE_ROOT/dist/scripts/stapler-with-retry.py" {action} "${path}"', script)
        self.assertNotIn('xcrun stapler ', script)
        self.assertIn('set -euo pipefail', script)
        start = script.index('VERIFIED_APP=')
        positions = [script.index(text, start) for text in [
            'codesign --verify', 'stapler-with-retry.py" validate', 'spctl --assess',
            'bash "$RELEASE_ROOT/dist/scripts/generate-update-appcast.sh"']]
        self.assertEqual(positions, sorted(positions))
        cmake = (ROOT / 'tests/CMakeLists.txt').read_text()
        self.assertIn('NAME FovelleStaplerRetryContracts', cmake)
        self.assertIn('${CMAKE_CURRENT_SOURCE_DIR}/stapler_retry_tests.py', cmake)


if __name__ == '__main__':
    unittest.main(verbosity=2)

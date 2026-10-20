"""Budget, coverage and failure-path regressions for the default test gate."""
import json
import os
from pathlib import Path
import sys
import tempfile
import time
import threading
import unittest
from unittest import mock

import run_fast_tests as runner
from run_portable_hdr import prepare_output, validate_xml


class FastGateTests(unittest.TestCase):
    def manifest(self):
        return json.loads((runner.ROOT / 'tests/fast_test_manifest.json').read_text())

    def test_manifest_coverage_and_duplicates(self):
        runner.validate_manifest(self.manifest())
        bad = self.manifest()
        bad['groups'][0]['functions'].append(bad['groups'][0]['functions'][0])
        with self.assertRaises(ValueError):
            runner.validate_manifest(bad)
        valid_rows = self.manifest()['portable_fullscreen_rows']
        for rows in ([], valid_rows[:-1], valid_rows + valid_rows[:1], valid_rows + ['unknown']):
            bad = self.manifest()
            bad['portable_fullscreen_rows'] = rows
            with self.assertRaises(ValueError):
                runner.validate_manifest(bad)
        bad = self.manifest()
        bad['portable_hdr'] = False
        with self.assertRaises(ValueError):
            runner.validate_manifest(bad)
        bad = self.manifest()
        bad['groups'] = [g for g in bad['groups'] if g['suite'] != 'RemediationTests']
        with self.assertRaises(ValueError):
            runner.validate_manifest(bad)

    def test_entrypoints_and_workflow_budget(self):
        cmake = (runner.ROOT / 'tests/CMakeLists.txt').read_text()
        self.assertIn('option(FOVELLE_EXTENDED_TESTS', cmake)
        self.assertIn('if(FOVELLE_EXTENDED_TESTS)', cmake)
        self.assertIn('NAME FovelleFastTests', cmake)
        self.assertIn('TIMEOUT 58', cmake)
        for name in ('test.yml', 'build.yml', 'release.yml', 'release-compatibility.yml'):
            workflow = (runner.ROOT / '.github/workflows' / name).read_text()
            self.assertRegex(workflow, r'- name: (?:Test|Run tests)\n\s+timeout-minutes: 1\n')

    def test_static_checks_overlap_and_preserve_failure_evidence(self):
        barrier = threading.Barrier(2)
        entries = [{'script': name, 'args': []} for name in ('failed.py', 'passed.py')]
        deadline = time.monotonic() + 3

        def execute(name, command, environment, output, shared_deadline):
            self.assertEqual(shared_deadline, deadline)
            barrier.wait(timeout=2)
            return {'name': name, 'status': 'failed' if name == 'failed.py' else 'passed'}

        with tempfile.TemporaryDirectory() as directory, mock.patch.object(runner, 'run_command', side_effect=execute):
            records = runner.run_static_checks(entries, {}, Path(directory), deadline)
        self.assertEqual([r['name'] for r in records], ['failed.py', 'passed.py'])
        self.assertEqual([r['status'] for r in records], ['failed', 'passed'])

    def test_failed_parallel_static_stage_stops_native_suites(self):
        entries = self.manifest()['static'][:2]
        records = [{'name': entry['script'], 'status': status}
                   for entry, status in zip(entries, ('failed', 'passed'))]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            arguments = ['gate', '/unused', '--output', str(root / 'fast'),
                         '--portable-output', str(root / 'hdr')]
            with mock.patch.object(sys, 'argv', arguments), mock.patch.object(runner, 'run_static_checks', return_value=records), mock.patch.object(runner, 'run_suite') as native:
                self.assertEqual(runner.main(), 1)
                native.assert_not_called()
            summary = json.loads((root / 'fast/summary.json').read_text())
            self.assertEqual(summary['status'], 'failed')
            self.assertEqual([s['status'] for s in summary['stages'][:2]], ['failed', 'passed'])
            self.assertTrue(all(s['status'] == 'not-run' for s in summary['stages'][2:]))

    def test_nonzero_exit_is_not_success(self):
        with tempfile.TemporaryDirectory() as directory:
            result = runner.run_command('failure', [sys.executable, '-c', 'raise SystemExit(7)'],
                                        os.environ.copy(), Path(directory), time.monotonic() + 3)
            self.assertEqual(result['status'], 'failed')
            self.assertEqual(result['returncode'], 7)

    def test_budget_exhaustion_does_not_launch(self):
        with tempfile.TemporaryDirectory() as directory, mock.patch.object(runner.subprocess, 'Popen') as launch:
            result = runner.run_command('expired', ['unused'], {}, Path(directory), time.monotonic() - 1)
            launch.assert_not_called()
            self.assertEqual(result['status'], 'timeout')

    def test_timeout_reaps_process_group(self):
        with tempfile.TemporaryDirectory() as directory, mock.patch.object(runner.os, 'killpg', wraps=os.killpg) as kill:
            started = time.monotonic()
            result = runner.run_command('timeout', [sys.executable, '-c', 'import time; time.sleep(30)'],
                                        os.environ.copy(), Path(directory), started + .2)
            self.assertEqual(result['status'], 'timeout')
            kill.assert_called_once()
            self.assertLess(time.monotonic() - started, 3)

    def test_xml_requires_exact_pass_rows_and_cleans_old_outputs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            xml = root / 'Suite.xml'
            xml.write_text('<TestCase name="Suite"><TestFunction name="testA"><Incident type="pass"><DataTag>raw</DataTag></Incident></TestFunction></TestCase>')
            validate_xml(xml, {'testA'}, suite='Suite', required_rows={'testA': ['raw']})
            with self.assertRaises(ValueError):
                validate_xml(xml, {'testMissing'}, suite='Suite')
            with self.assertRaises(ValueError):
                validate_xml(xml, {'testA'}, suite='Suite', required_rows={'testA': ['gain-map']})
            xml.write_text(xml.read_text().replace('type="pass"', 'type="skip"'))
            with self.assertRaises(ValueError):
                validate_xml(xml, {'testA'}, suite='Suite')
            xml.write_text('<TestCase name="Suite"><TestFunction name="testA"><Incident type="pass"><DataTag>raw</DataTag></Incident></TestFunction><TestFunction name="testA"><Incident type="pass"><DataTag>gain-map</DataTag></Incident></TestFunction></TestCase>')
            with self.assertRaises(ValueError):
                validate_xml(xml, {'testA'}, suite='Suite')
            validate_xml(xml, {'testA'}, suite='Suite', invocations={'testA': 2},
                         required_rows={'testA': ['raw', 'gain-map']})
            with self.assertRaises(ValueError):
                validate_xml(xml, {'testA'}, suite='Suite', invocations={'testA': 3})
            prepare_output(root, ['Suite'])
            self.assertFalse(xml.exists())
            self.assertEqual(json.loads((root / 'Suite.json').read_text())['status'], 'not-run')


if __name__ == '__main__':
    unittest.main()

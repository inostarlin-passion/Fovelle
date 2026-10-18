"""Budget, coverage and failure-path regressions for the default test gate."""
import json
import os
from pathlib import Path
import sys
import tempfile
import time
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
        self.assertIn('TIMEOUT 175', cmake)
        for name in ('test.yml', 'build.yml', 'release.yml', 'release-compatibility.yml'):
            workflow = (runner.ROOT / '.github/workflows' / name).read_text()
            self.assertRegex(workflow, r'- name: (?:Test|Run tests)\n\s+timeout-minutes: 3\n')

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

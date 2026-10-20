#!/usr/bin/env python3
"""CI1-CI8 contracts and adversarial execution tests; no native HDR is faked here."""
import json
import os
from pathlib import Path
import signal
import sys
import tempfile
import time
import unittest
import xml.etree.ElementTree as ET
from unittest.mock import patch

from run_portable_hdr import FULLSCREEN_ROWS, run_suite, validate_xml
import run_portable_hdr

ROOT = Path(__file__).resolve().parents[1]

class ActionsRegressionTests(unittest.TestCase):
    def test_required_rows_are_exact(self):
        with tempfile.TemporaryDirectory() as temporary:
            xml = Path(temporary) / 'rows.xml'
            def write(tags, status='pass', suite='WindowBehaviorTests'):
                root = ET.Element('TestCase', name=suite)
                function = ET.SubElement(root, 'TestFunction', name='testHDRFullScreenVisualContinuity')
                for tag in tags:
                    incident = ET.SubElement(function, 'Incident', type=status)
                    ET.SubElement(incident, 'DataTag').text = tag
                ET.ElementTree(root).write(xml)
            required = {'testHDRFullScreenVisualContinuity': FULLSCREEN_ROWS}
            write(FULLSCREEN_ROWS)
            validate_xml(xml, required, suite='WindowBehaviorTests', required_rows=required)
            for tags in (FULLSCREEN_ROWS[:-1], FULLSCREEN_ROWS + FULLSCREEN_ROWS[:1], FULLSCREEN_ROWS + ['unknown']):
                write(tags)
                with self.assertRaises(ValueError):
                    validate_xml(xml, required, suite='WindowBehaviorTests', required_rows=required)
            for status in ('fail', 'skip', 'xfail'):
                write(FULLSCREEN_ROWS, status)
                with self.assertRaises(ValueError):
                    validate_xml(xml, required, suite='WindowBehaviorTests', required_rows=required)
            write(FULLSCREEN_ROWS, suite='WrongSuite')
            with self.assertRaises(ValueError):
                validate_xml(xml, required, suite='WindowBehaviorTests', required_rows=required)

    def make_stub(self, root):
        script = root / 'fixture'
        script.write_text('''#!/usr/bin/env python3
import os,sys,time
from pathlib import Path
import xml.etree.ElementTree as ET
mode=os.environ.get('CI_GATE_STUB','pass')
time.sleep(float(os.environ.get('CI_GATE_STARTUP_DELAY','0')))
xml=Path(sys.argv[sys.argv.index('-o')+1].split(',')[0])
print('fixture diagnostic',flush=True)
print('fixture stderr',file=sys.stderr,flush=True)
if mode=='silent':sys.exit(0)
if mode in ('hang','fail'):
    xml.write_text('<TestCase>')
    if mode=='hang':time.sleep(10)
    sys.exit(7)
root=ET.Element('TestCase',name=os.environ['FOVELLE_TEST_SUITE'])
for name in sys.argv[1:sys.argv.index('-o')]:
    function=ET.SubElement(root,'TestFunction',name=name)
    ET.SubElement(function,'Incident',type='pass')
ET.ElementTree(root).write(xml)
''')
        script.chmod(0o755)
        return script

    def test_run_suite_success(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);script=self.make_stub(root)
            record=run_suite(str(script),'FixtureSuite',['required'],os.environ.copy(),root,timeout=2)
            self.assertEqual(record['status'],'passed')
            self.assertEqual(record['returncode'],0)
            self.assertIn('fixture diagnostic',(root/'FixtureSuite.log').read_text())
            self.assertEqual(json.loads((root/'FixtureSuite.json').read_text())['status'],'passed')

    def test_run_suite_without_timeout_preserves_xml_validation(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            script = self.make_stub(root)
            env = os.environ.copy()
            env['CI_GATE_STARTUP_DELAY'] = '0.1'
            with patch('run_portable_hdr.subprocess.Popen', wraps=run_portable_hdr.subprocess.Popen) as launch:
                record = run_suite(str(script), 'FixtureSuite', ['required'], env, root)
            self.assertEqual(record['status'], 'passed')
            self.assertEqual(record['returncode'], 0)
            self.assertEqual(launch.call_count, 1)
            # With no deadline, incorrect XML must still fail.
            env['CI_GATE_STUB'] = 'silent'
            with self.assertRaises(RuntimeError):
                run_suite(str(script), 'FixtureSuite', ['required'], env, root)
            self.assertEqual(json.loads((root/'FixtureSuite.json').read_text())['status'], 'failed')

    def test_run_suite_failure_and_timeout(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);script=self.make_stub(root)
            for mode in ('fail','hang'):
                env=os.environ.copy();env['CI_GATE_STUB']=mode;env['CI_GATE_STARTUP_DELAY']='0.35'
                started=time.monotonic()
                with self.assertRaises(RuntimeError):
                    run_suite(str(script),'FixtureSuite',['required'],env,root,
                              timeout=1 if mode=='hang' else 5)
                self.assertLess(time.monotonic()-started,10.5)
                record=json.loads((root/'FixtureSuite.json').read_text())
                self.assertEqual(record['status'],'timeout' if mode=='hang' else 'failed')
                self.assertIsNotNone(record['error'])
                if mode=='fail':self.assertEqual(record['returncode'],7)
                self.assertIn('fixture stderr',(root/'FixtureSuite.log').read_text())
                self.assertEqual((root/'FixtureSuite.xml').read_text(),'<TestCase>')

    def test_run_suite_rejects_stale_xml(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);script=self.make_stub(root)
            (root/'FixtureSuite.xml').write_text('<TestCase name="FixtureSuite"><TestFunction name="required"><Incident type="pass"/></TestFunction></TestCase>')
            env=os.environ.copy();env['CI_GATE_STUB']='silent'
            with self.assertRaises(RuntimeError):
                run_suite(str(script),'FixtureSuite',['required'],env,root,timeout=2)
            self.assertEqual(json.loads((root/'FixtureSuite.json').read_text())['status'],'failed')
            self.assertFalse((root/'FixtureSuite.xml').exists())

    def test_expired_budget_does_not_launch(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary)
            with patch('run_portable_hdr.subprocess.Popen') as launch:
                with self.assertRaises(RuntimeError):
                    run_suite('/unused','FixtureSuite',['required'],os.environ.copy(),root,timeout=0)
                launch.assert_not_called()
            self.assertEqual(json.loads((root/'FixtureSuite.json').read_text())['status'],'timeout')

    def test_prepare_output_rejects_stale_later_suite(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary)
            for suffix in ('xml','log','json'):(root/('LaterSuite.'+suffix)).write_text('stale pass')
            run_portable_hdr.prepare_output(root,['LaterSuite'])
            self.assertFalse((root/'LaterSuite.xml').exists())
            self.assertFalse((root/'LaterSuite.log').exists())
            self.assertEqual(json.loads((root/'LaterSuite.json').read_text())['status'],'not-run')

    def test_native_decode_budget_contract(self):
        source=(ROOT/'tests/remediation_tests.h').read_text()
        case=source.split('void testDownloadPreservesSources()')[1].split('void testWindowUrlAnimationAndArtifactLifetime()')[0]
        self.assertIn('decodeTimeout',case)
        self.assertIn('15000',case)
        self.assertIn('URL_SOURCE_DECODE',case)

    def test_download_fixture_contract(self):
        source=(ROOT/'tests/remediation_tests.h').read_text()
        self.assertIn('QAbstractSocket::LowDelayOption',source)
        self.assertIn('socket->flush()',source)
        self.assertIn('testDownloadProgressKeepsIdleAlive',source)
        total=source.split('void testDownloadTotalTimeout()')[1].split('void testDownloadLimits()')[0]
        self.assertIn('QSignalSpy progress',total)
        self.assertIn('progress.count()',total)
        self.assertNotIn('.contains("total"',total)

    def test_hdr_fixture_contract(self):
        source=(ROOT/'tests/tst_qviewtests.cpp').read_text()
        case=source.split('void WindowBehaviorTests::testHDRFullScreenVisualContinuity()')[1].split('void WindowBehaviorTests::testFullScreenBottomBackgroundHandoff_data()')[0]
        self.assertLess(case.index('ScopedHDRTestDisplay'),case.index('MainWindow window'))
        self.assertIn('persistentHDRSurfaceReady',case)
        self.assertIn('bottomPixelErrors <= 2',case)
        self.assertNotIn('QSKIP("SDR',case)
        color=source.split('void HDRPolicyTests::testHDRBrightnessNativeColorFidelity()')[1].split('// TC-HDR-UNIT-FIRST-VISIBLE-FINAL')[0]
        self.assertLess(color.index('ScopedHDRTestDisplay'),color.index('QWidget viewport'))
        helper=(ROOT/'tests/scoped_hdr_test_display.h').read_text()
        for name in ('FOVELLE_TEST_DISPLAY_HEADROOM','FOVELLE_TEST_DISPLAY_CURRENT_HEADROOM','qunsetenv'):
            self.assertIn(name,helper)

    def test_workflow_gate_contract(self):
        for name in ('build.yml','release.yml','release-compatibility.yml'):
            workflow=(ROOT/'.github/workflows'/name).read_text()
            part=workflow
            self.assertIn('FOVELLE_REQUIRE_PORTABLE_HDR=ON',part)
            self.assertIn('Upload portable HDR execution evidence',part)
            self.assertIn('if: always()',part)
            self.assertRegex(part,r'(?m)^\s+build/test-results/portable-hdr/\s*$')
        runner=(ROOT/'tests/run_portable_hdr.py').read_text()
        self.assertIn('start_new_session=True',runner)
        self.assertIn('os.killpg',runner)

if __name__=='__main__':unittest.main()

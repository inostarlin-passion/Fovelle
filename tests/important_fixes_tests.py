#!/usr/bin/env python3
"""Source contracts and isolated build.sh process regressions for AC L1–B2."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]


class ImportantFixes(unittest.TestCase):
    def source(self, path):
        return (ROOT / path).read_text()

    def registered(self, name):
        self.assertIn(name, self.source('tests/fast_test_manifest.json'))
        self.assertIn(name, self.source('tests/tst_qviewtests.cpp'))
        self.assertIn('NAME FovelleImportantFixes', self.source('tests/CMakeLists.txt'))

    def test_static_l1(self):
        self.assertIn('QvAsync::Ticket ticket;', self.source('src/qvimageloader.h'))
        self.assertIn('cancelUnwantedJobs();', self.source('src/qvimageloader.cpp'))
        self.registered('testLoaderSupersessionCancellation')

    def test_static_l2(self):
        self.assertIn('nextJobGeneration', self.source('src/qvimageloader.cpp'))
        self.assertIn('QvAsync::cancel(it->ticket)', self.source('src/qvimageloader.cpp'))
        self.registered('testLoaderCancellationLifecycle')

    def test_static_l3(self):
        source = self.source('src/asyncwork.cpp')
        self.assertIn('foregroundPool()', source)
        self.assertIn('setMaxThreadCount(2)', source)
        self.assertIn('foregroundPool().clear()', source)
        self.assertIn('QElapsedTimer', source)
        self.registered('testLoaderForegroundIsolation')

    def test_static_l4(self):
        source = self.source('src/asyncwork.cpp')
        self.assertIn('waitForPool(pool(), remaining())', source)
        self.assertIn('waitForPool(foregroundPool(), remaining())', source)
        self.assertIn('sharedComplete && foregroundComplete', source)
        self.registered('testLoaderPoolBoundAndDrain')

    def test_static_u1(self):
        body = self.source('src/mainwindow.cpp').split('void MainWindow::undoDelete()')[1].split('void MainWindow::copy()')[0]
        self.assertIn('lastDeletedFiles.top()', body)
        self.assertLess(body.index('if (!success)'), body.index('lastDeletedFiles.pop()'))
        self.registered('testUndoDeleteRetry')

    def test_static_u2(self):
        body = self.source('src/mainwindow.cpp').split('void MainWindow::undoDelete()')[1].split('void MainWindow::copy()')[0]
        self.assertNotIn('isWritable()', body)
        self.assertEqual(body.count('lastDeletedFiles.pop()'), 1)
        self.registered('testUndoDeleteHistory')

    def test_static_e1(self):
        self.assertIn('QImageWriter::supportedImageFormats()', self.source('src/mainwindow.cpp'))
        body = self.source('src/mainwindow.cpp').split('void MainWindow::saveFrameAs()')[1].split('void MainWindow::pause()')[0]
        self.assertNotIn('getNameFilterList()', body)
        self.registered('testFrameExportFormats')

    def test_static_e2(self):
        self.assertIn('Unsupported image format: %1', self.source('src/mainwindow.cpp'))
        self.assertIn('Could not save %1:', self.source('src/mainwindow.cpp'))
        for path in (ROOT / 'i18n').glob('qview_*.ts'):
            messages = {m.findtext('source'): m.findtext('translation')
                        for c in ET.parse(path).getroot().findall('context')
                        if c.findtext('name') == 'MainWindow' for m in c.findall('message')}
            for key in ('Unsupported image format: %1', 'Could not save %1:\n%2',
                        'Failed undoing deletion of %1.'):
                text = messages[key]
                self.assertTrue(text, str(path))
                for placeholder in ('%1', '%2'):
                    self.assertEqual(text.count(placeholder), key.count(placeholder), str(path))
        self.registered('testFrameExportRejected')

    def test_static_e3(self):
        source = self.source('src/mainwindow.cpp')
        self.assertIn('QSaveFile', source)
        self.assertIn('output.commit()', source)
        self.assertIn('[this, frame]', source)
        self.registered('testFrameExportWrite')
        self.registered('testFrameExportSnapshot')

    def test_static_b1(self):
        self.assertIn('exit "$configure_status"', self.source('build.sh'))

    def test_static_b2(self):
        self.assertIn('"${CMAKE_ARGS[@]}"', self.source('build.sh'))
        self.assertIn('important_fixes_tests.py', self.source('tests/fast_test_manifest.json'))

    def run_build(self, configure=0, build=0, args=()):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            binary = root / 'bin'
            binary.mkdir()
            # The stub records real process argv; no shell re-parsing of user data.
            cmake = binary / 'cmake'
            cmake.write_text('#!/usr/bin/env python3\nimport json,os,sys\n'
                             'with open(os.environ["FIX_LOG"],"a") as f: f.write(json.dumps(sys.argv[1:])+"\\n")\n'
                             'sys.exit(int(os.environ["FIX_BUILD" if sys.argv[1]=="--build" else "FIX_CONFIGURE"]))\n')
            cmake.chmod(0o755)
            for name, output in [('uname', 'Darwin'), ('xcrun', '/test SDK')]:
                executable = binary / name
                executable.write_text('#!/bin/bash\nprintf "%s\\n" "' + output + '"\n')
                executable.chmod(0o755)
            env = dict(os.environ, PATH=str(binary) + os.pathsep + os.environ['PATH'],
                       FIX_LOG=str(root / 'calls.jsonl'), FIX_CONFIGURE=str(configure), FIX_BUILD=str(build))
            result = subprocess.run(['bash', str(ROOT / 'build.sh'), *args], cwd=root, env=env,
                                    capture_output=True, text=True, timeout=10)
            calls = [json.loads(line) for line in (root / 'calls.jsonl').read_text().splitlines()]
            return result, calls

    def test_configure_failure(self):
        result, calls = self.run_build(configure=23)
        self.assertEqual(result.returncode, 23, result.stdout + result.stderr)
        self.assertEqual(len(calls), 1)
        self.assertNotEqual(calls[0][0], '--build')

    def test_build_status_and_arguments(self):
        for status in (0, 37):
            with self.subTest(status=status):
                result, calls = self.run_build(build=status, args=('--tidy-fix', '-DTEST_PATH=/path with spaces'))
                self.assertEqual(result.returncode, status, result.stdout + result.stderr)
                self.assertEqual(len(calls), 2)
                self.assertEqual(calls[1][:2], ['--build', 'build'])
                self.assertIn('-DTEST_PATH=/path with spaces', calls[0])
                self.assertIn('-DCMAKE_OSX_SYSROOT=/test SDK', calls[0])
                self.assertIn('-DCMAKE_CXX_CLANG_TIDY=clang-tidy;-fix-errors', calls[0])


if __name__ == '__main__':
    unittest.main(verbosity=2)

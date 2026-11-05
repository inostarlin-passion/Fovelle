#!/usr/bin/env python3
"""Registration and ownership/resource contracts for the 2026-11-05 review."""
import json
import re
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ReviewFixes(unittest.TestCase):
    def source(self, path):
        return (ROOT / path).read_text()

    def registered(self, name):
        manifest = json.loads(self.source('tests/fast_test_manifest.json'))
        functions = {f for group in manifest['groups'] for f in group['functions']}
        self.assertIn(name, functions)
        self.assertIn('void ' + name + '();', self.source('tests/tst_qviewtests.cpp'))
        self.assertIn('::' + name + '()', self.source('tests/review_fixes_acceptance.inc'))
        self.assertIn('FovelleReviewFixes', self.source('tests/CMakeLists.txt'))

    def test_static_d1(self):
        text = self.source('src/mainwindow.cpp')
        self.assertIn('[this, target, permanent, sourceLifetime]', text)
        self.assertIn('deleteTarget(*target, permanent)', text)
        self.assertIn('const bool wasCurrent', text)
        self.registered('testDeleteConfirmationKeepsTarget')

    def test_static_d2(self):
        text = self.source('src/mainwindow.cpp')
        self.assertIn('::lstat(', text)
        self.assertIn('st_ino', text)
        self.assertIn('st_mtimespec.tv_nsec', text)
        self.assertIn('currentTarget != target.identity', text)
        self.registered('testDeleteConfirmationRejectsReplacement')

    def test_static_e1(self):
        text = self.source('src/qvcocoafunctions.mm')
        self.assertIn('RLIMIT_FSIZE', text)
        self.assertIn('setChildProcessModifier', text)
        self.assertIn('setupChildProcess()', text)
        self.assertIn('setrlimit(', text)
        self.registered('testEPSOutputLimit')

    def test_static_e2(self):
        text = self.source('src/qvcocoafunctions.mm')
        self.assertIn('readyReadStandardError', text)
        self.assertIn('diagnosticExceeded', text)
        self.assertNotIn('process.readAllStandardError()', text)
        self.registered('testEPSDiagnosticLimit')

    def test_static_e3(self):
        text = self.source('src/qvcocoafunctions.mm')
        self.assertIn('QvAsync::isCanceled()', text)
        self.assertIn('EPSRendererTimeoutMs', text)
        self.assertIn('QvAsync::unregisterChild(childPid)', text)
        self.registered('testEPSResourceCompatibility')

    def test_static_c1(self):
        text = self.source('src/qvgraphicsview.cpp')
        self.assertIn('imageCore.clipboardImage()', text)
        self.assertIn('copySDRImage', self.source('src/qvimagecore.cpp'))
        self.assertIn('QImage copyImage() const', self.source('src/qvcocoafunctions.mm'))
        self.registered('testClipboardFullResolution')

    def test_static_c2(self):
        text = self.source('src/qvimagecore.cpp')
        self.assertIn('QImage QVImageCore::clipboardImage() const', text)
        self.assertIn('copyHDRImage', text)
        self.assertIn('renderPDFVectorTile', text)
        self.assertIn('loadedMovie.currentImage()', text)
        self.registered('testClipboardRepresentations')

    def test_static_l1(self):
        self.assertNotIn('downloadedArtifacts', self.source('src/mainwindow.cpp'))
        self.assertNotIn('downloadedArtifacts', self.source('src/mainwindow.h'))
        self.assertIn('artifact.directory', self.source('src/mainwindow.cpp'))
        self.registered('testDownloadedSourcesRetire')

    def test_static_l2(self):
        self.assertIn('sourceLifetime', self.source('src/qvimageloader.h'))
        self.assertIn('sourceLifetime', self.source('src/qvimageloader.cpp'))
        self.assertIn('asyncDecoder->sourceLifetime', self.source('src/qvmovie.cpp'))
        self.registered('testSourceLeaseAsyncLifetime')

    def test_static_l3(self):
        self.assertIn('retainSource', self.source('src/qvimageloader.cpp'))
        self.assertIn('SourceMimeData', self.source('src/qvgraphicsview.cpp'))
        text = self.source('src/mainwindow.cpp')
        self.assertTrue(re.search(r'\[filePath,\s*sourceLifetime,\s*provider = openWithItemProvider\]', text))
        self.registered('testSourceLeaseConsumers')


if __name__ == '__main__':
    unittest.main(verbosity=2)

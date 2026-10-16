#!/usr/bin/env python3
"""R1-R20 static architectural checks plus dynamic delivery/fixture gate tests."""
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[1]
class Contracts(unittest.TestCase):
    def test_download_boundary(self):
        source=(ROOT/'src/mainwindow.cpp').read_text()
        route=source.split('void MainWindow::openUrl(')[1].split('void MainWindow::pickUrl')[0]
        self.assertIn('UrlDownload',route)
        self.assertNotIn('QtConcurrent',route)
        self.assertNotIn('QImage::fromData',route)
        body=(ROOT/'src/urldownload.cpp').read_text()
        for contract in ('readyRead','deleteLater','idleTimeoutMs','totalTimeoutMs','maxBytes'):
            self.assertIn(contract,body)
    def test_delivery_contract(self):
        source=(ROOT/'.github/workflows/build.yml').read_text()
        self.assertLess(source.index('macdeployqt'),source.index('ditto -c'))
        self.assertIn('verify-macos-bundle.py',source)
        self.assertIn('FovelleDeploymentArtifact',(ROOT/'tests/CMakeLists.txt').read_text())
    def test_folder_snapshot(self):
        source=(ROOT/'src/qvimagecore.cpp').read_text()
        self.assertNotIn('fileEnumerator.getCompatibleFiles(dirPath)',source)
        self.assertIn('scanAsync',source)
        self.assertIn('folderScanGeneration',source)
    def test_animation_boundary(self):
        source=(ROOT/'src/qvimagecore.cpp').read_text()
        self.assertIn('setAsyncPlayback(true)',source)
        movie=(ROOT/'src/qvmovie.cpp').read_text()
        self.assertIn('cacheByteLimit',movie)
        self.assertIn('cachedBytes',movie)
    def test_shutdown_boundary(self):
        source=(ROOT/'src/qvimageloader.cpp').read_text()
        self.assertNotIn('waitForDone()',source)
        self.assertNotIn('imageThreadPool',source)
        service=(ROOT/'src/asyncwork.cpp').read_text()
        self.assertIn('waitForDone(timeoutMs)',service)
        self.assertIn('std::_Exit',service)
        self.assertIn('QvAsync::finishOrExit()', (ROOT/'src/qvapplication.cpp').read_text())
    def test_portable_gate(self):
        source=(ROOT/'tests/CMakeLists.txt').read_text()
        self.assertIn('FovellePortableHDR',source)
        self.assertIn('run_portable_hdr.py',source)
        for name in ('build.yml','release.yml','release-compatibility.yml'):
            self.assertIn('FOVELLE_REQUIRE_PORTABLE_HDR=ON',(ROOT/'.github/workflows'/name).read_text())

if __name__=='__main__': unittest.main()

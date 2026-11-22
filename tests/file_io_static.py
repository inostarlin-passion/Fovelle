#!/usr/bin/env python3
"""Check the ownership and ordering boundaries for the file I/O fixes."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1] / "src"


def source(name):
    return (ROOT / name).read_text()


def function(text, start, end):
    return text.split(start, 1)[1].split(end, 1)[0]


class FileIoContracts(unittest.TestCase):
    def test_IO03_rename_checks_original_directory_entry(self):
        rename = source("qvrenamedialog.cpp")
        confirmation = function(rename, "void QVRenameDialog::onFinished", "void QVRenameDialog::showEvent")
        self.assertIn("LinkMode::DirectoryEntry", rename.split("void QVRenameDialog::onFinished")[0])
        self.assertIn("LinkMode::DirectoryEntry", confirmation)
        self.assertRegex(confirmation, r"(?s)originalVersion.*?return;.*?QFile::rename.*?emit newFileToOpen")
        self.assertIn("::lstat", source("fileversion.h"))

    def test_IO07_caches_compare_strong_worker_versions(self):
        loader = source("qvimageloader.cpp")
        work = function(loader, "void QVImageLoader::startJob", "void QVImageLoader::jobFinished")
        self.assertRegex(work, r"(?s)QvAsync::run\(.*?initialVersion.*?read\(.*?finalVersion")
        self.assertRegex(loader, r"(?s)void QVImageLoader::queueCachedDelivery.*?QvAsync::run\(.*?fileVersion\(.*?deliverResult")
        completed = function(loader, "void QVImageLoader::jobFinished", "\n}")
        self.assertRegex(completed, r"(?s)changedDuringRead.*?State::Queued.*?startReadyJobs\(\).*?return;.*?deliverResult")
        native = source("qvcocoafunctions.mm")
        eps = function(native, "EPSReadResult readEPS(", "class NativeMetalImageGraph")
        self.assertRegex(eps, r"(?s)sourceVersion.*?rendererVersion.*?lookupEPSRenderCache")
        self.assertRegex(eps, r"(?s)sourceVersion.*?rendererVersion.*?storeEPSRenderCache")
        version = source("fileversion.h")
        for field in ("st_dev", "st_ino", "st_size", "st_mtimespec", "st_ctimespec"):
            self.assertIn(field, version)

    def test_IO09_redirect_and_file_publication_are_ordered(self):
        download = source("urldownload.cpp")
        self.assertIn("download.part", download)
        self.assertRegex(download, r"(?s)reply->url\(\).*?output\.close\(\).*?QFile::rename.*?finish\(State::Succeeded\)")
        self.assertRegex(download, r"(?s)artifact = \{ \};.*?emit succeeded\(delivered\)")
        self.assertIn("mimeTypeForData(content.read(4096))", download)
        self.assertIn('type.name() != "image/x-eps"', download)

    def test_IO13_metadata_is_worker_owned(self):
        core = source("qvimagecore.cpp")
        open_file = function(core, "void QVImageCore::loadFile", "void QVImageCore::markFolderInfoDirty")
        self.assertRegex(open_file, r"(?s)QvAsync::run\(.*?\.isDir\(\).*?QvAsync::Lane::Foreground")
        navigation = function(core, "QVImageCore::GoToFileResult QVImageCore::goToFile", "bool QVImageCore::hasPreviousFile")
        self.assertRegex(navigation, r"(?s)QvAsync::run\(.*?QFileInfo\(.*?\.isFile\(\).*?QvAsync::Lane::Foreground")
        loader = source("qvimageloader.cpp")
        for start, end in (("quint64 QVImageLoader::requestImage", "QByteArray QVImageLoader::fileVersion"),
                           ("void QVImageLoader::setDesiredImages", "void QVImageLoader::queueCachedDelivery"),
                           ("void QVImageLoader::jobFinished", "\n}")):
            segment = function(loader, start, end)
            self.assertNotRegex(segment, r"getFileIdentity|fileVersion\(|\.exists\(|\.lastModified\(")
        self.assertIn("canonicalFilePath()", source("qvfileenumerator.cpp"))
        self.assertNotIn("canonicalFilePath()", source("qvthumbnailstrip.cpp"))
        title = function(source("mainwindow.cpp"), "void MainWindow::buildWindowTitle", "\n}")
        self.assertIn("fileDetails.sourceIsFile", title)
        self.assertIn("fileDetails.sourceFileSize", title)
        self.assertNotRegex(title, r"\.fileInfo\.(isFile|size)\(")


if __name__ == "__main__":
    unittest.main()

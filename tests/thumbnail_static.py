from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ThumbnailWiring(unittest.TestCase):
    def test_build_and_native_overlay(self):
        for name in ("CMakeLists.txt", "tests/CMakeLists.txt", "src/src.pri"):
            self.assertIn("qvthumbnailstrip.cpp", (ROOT / name).read_text())
        cocoa = (ROOT / "src/qvcocoafunctions.mm").read_text()
        self.assertIn("[navigationOverlayLayer addSublayer:thumbnailLayer]", cocoa)
        self.assertIn("setThumbnailOpacity", cocoa)

    def test_background_budget_and_versioned_foreground(self):
        strip = (ROOT / "src/qvthumbnailstrip.cpp").read_text()
        self.assertIn("24 * 1024 * 1024", strip)
        self.assertIn("MaxPreviewJobs = 2", strip)
        self.assertIn("QvAsync::Lane::Shared", strip)
        loader = (ROOT / "src/qvimageloader.cpp").read_text()
        versioned = loader.split("QVImageLoader::requestVersionedImage", 1)[1].split(
            "void QVImageLoader::", 1)[0]
        self.assertIn("QvAsync::Lane::Foreground", versioned)
        self.assertGreaterEqual(versioned.count("fileVersion("), 2)

    def test_native_preview_has_independent_admission(self):
        strip = (ROOT / "src/qvthumbnailstrip.cpp").read_text()
        preview = strip.split("Preview readPreview(", 1)[1].split("class PreviewQueue", 1)[0]
        self.assertLess(preview.index("thumbnailMayReadContent"), preview.index("readThumbnail("))
        self.assertLess(preview.index("readThumbnail("), preview.index("readQtPreview("))
        fallback = strip.split("QImage readQtPreview(", 1)[1].split("Preview readPreview", 1)[0]
        self.assertIn("MaxEstimatedBytes", fallback)
        self.assertLess(fallback.index("fileBytes > MaxSvgSourceBytes"), fallback.index("reader.size()"))
        cocoa = (ROOT / "src/qvcocoafunctions.mm").read_text()
        native = cocoa.split("QVCocoaFunctions::readThumbnail(", 1)[1].split(
            "void QVCocoaFunctions::", 1)[0]
        for gate in ("MaxNativeThumbnailPixels", "MaxRawThumbnailPixels", "MaxThumbnailEPSBytes"):
            self.assertIn(gate, native)
        self.assertIn("kCGImageSourceSubsampleFactor", native)


if __name__ == "__main__":
    unittest.main()

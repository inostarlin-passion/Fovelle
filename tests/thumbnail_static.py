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

    def test_browsing_and_native_transition_wiring(self):
        strip = (ROOT / "src/qvthumbnailstrip.cpp").read_text()
        wheel = strip.split("bool QVThumbnailStrip::handleWheel(", 1)[1].split(
            "bool QVThumbnailStrip::", 1)[0]
        scroll = strip.split("void QVThumbnailStrip::scrollBy(", 1)[1].split(
            "void QVThumbnailStrip::", 1)[0]
        self.assertIn("scrollBy(", wheel)
        for action in ("activate(", "openThumbnail(", "goToFile("):
            self.assertNotIn(action, wheel + scroll)
        cocoa = (ROOT / "src/qvcocoafunctions.mm").read_text()
        progress = cocoa.split("    void setThumbnailTransitionProgress(", 1)[1].split(
            "    void ", 1)[0]
        self.assertIn("thumbnailOutgoingLayer.opacity", progress)
        self.assertIn("thumbnailContentLayer.opacity", progress)
        self.assertNotIn("createNativeImageCGImage", progress)

    def test_large_png_and_webp_admission(self):
        cocoa = (ROOT / "src/qvcocoafunctions.mm").read_text()
        native = cocoa.split("QVCocoaFunctions::readThumbnail(", 1)[1].split(
            "void QVCocoaFunctions::", 1)[0]
        self.assertLess(native.index("readLargePngThumbnail("),
                        native.index("CGImageSourceCreateWithURL("))
        self.assertLess(native.index("fileBytes > MaxThumbnailFileBytes"),
                        native.index("CGImageSourceCreateWithURL("))
        self.assertIn('CFEqual(type, CFSTR("org.webmproject.webp"))', native)
        self.assertIn("MaxWebpThumbnailPixels = 40 * 1000 * 1000", native)
        self.assertIn("readSimpleLosslessWebpThumbnail(", native)
        webp = cocoa.split("QImage readSimpleLosslessWebpThumbnail(", 1)[1].split("\n}", 1)[0]
        self.assertLess(webp.index("fileBytes > MaxWebpFallbackBytes"), webp.index("file.read("))
        self.assertLess(webp.index("config.input.width"), webp.index("WebPDecode("))
        self.assertIn('header.mid(12, 4) != "VP8L"', webp)
        self.assertLess(native.index("sourcePixels > maxPixels"),
                        native.index("CGImageSourceCreateThumbnailAtIndex("))

        png = (ROOT / "src/qvpngthumbnail.cpp").read_text()
        for guard in ("MaxPngFileBytes", "MaxPngPixels", "MaxPngEdge",
                      "png_set_user_limits", "png_set_chunk_malloc_max", "QvAsync::isCanceled()"):
            self.assertIn(guard, png)
        self.assertLess(png.index("png_read_row("), png.index("png_read_end("))
        self.assertIn("PNG_PASS_ROWS", png)
        self.assertNotIn("png_read_image(", png)
        self.assertNotIn("readAll(", png)
        for name in ("CMakeLists.txt", "tests/CMakeLists.txt", "src/src.pri"):
            self.assertIn("qvpngthumbnail.cpp", (ROOT / name).read_text())

        strip = (ROOT / "src/qvthumbnailstrip.cpp").read_text()
        self.assertLess(strip.index("QMutexLocker decodeLock"), strip.index("readThumbnail("))
        self.assertIn("QVImageLoader::fileVersion(path) != version", strip)

    def test_raw_thumbnail_uses_oriented_decode_graph(self):
        cocoa = (ROOT / "src/qvcocoafunctions.mm").read_text()
        raw = cocoa.split("QVCocoaFunctions::readThumbnail(", 1)[1].split(
            "if (isRawImageType(type))", 1)[1].split("UTType *imageType", 1)[0]
        self.assertIn("filter.orientation = sourceOrientation(source, 0)", raw)
        self.assertIn("filter.draftModeEnabled = YES", raw)
        self.assertIn("filter.draftModeEnabled = NO", raw)
        self.assertEqual(raw.count("filter.outputImage"), 2)
        self.assertNotIn("filter.previewImage", raw)


if __name__ == "__main__":
    unittest.main()

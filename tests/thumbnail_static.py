import argparse
from pathlib import Path
import sys
import unittest

from thumbnail_webp_compile_test import cocoa_compile_commands

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
        self.assertIn("QvAsync::Lane::ThumbnailDecode", strip)
        self.assertIn("QvAsync::Lane::ThumbnailMetadata", strip)
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

    def test_keyboard_movement_and_versioned_activation_are_separate(self):
        strip = (ROOT / "src/qvthumbnailstrip.cpp").read_text()
        keys = strip.split("bool QVThumbnailStrip::eventFilter(", 1)[1]
        self.assertIn("moveFocus(code == Qt::Key_Left ? -1 : 1);", keys)
        self.assertRegex(
            keys,
            r"else if \(code == Qt::Key_Return \|\| code == Qt::Key_Enter"
            r" \|\| code == Qt::Key_Space\)\s+activate\(focusSlot\);",
        )
        move = strip.split("void QVThumbnailStrip::moveFocus(", 1)[1].split(
            "void QVThumbnailStrip::", 1
        )[0]
        for action in ("activate(", "openThumbnail(", "openFile(", "goToFile("):
            self.assertNotIn(action, move)
        activate = strip.split("void QVThumbnailStrip::activate(", 1)[1].split(
            "void QVThumbnailStrip::", 1
        )[0]
        self.assertIn("core.openThumbnail(index, path, snapshot, verified)", activate)

    def test_large_png_and_webp_admission(self):
        cocoa = (ROOT / "src/qvcocoafunctions.mm").read_text()
        native = cocoa.split("QVCocoaFunctions::readThumbnail(", 1)[1].split(
            "void QVCocoaFunctions::", 1)[0]
        self.assertLess(native.index("readLargePngThumbnail("),
                        native.index("CGImageSourceCreateWithURL("))
        self.assertLess(native.index("fileBytes > MaxThumbnailFileBytes"),
                        native.index("CGImageSourceCreateWithURL("))
        self.assertIn('CFEqual(type, CFSTR("org.webmproject.webp"))', native)
        self.assertIn("MaxWebpThumbnailPixels = 64 * 1000 * 1000", native)
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
        self.assertIn("isQuickThumbnail(path, fileBytes)", strip)
        self.assertIn("QVImageLoader::fileVersion(path) != version", strip)

    def test_large_tiff_admission_is_format_scoped(self):
        cocoa = (ROOT / "src/qvcocoafunctions.mm").read_text()
        native = cocoa.split("QVCocoaFunctions::readThumbnail(", 1)[1].split(
            "void QVCocoaFunctions::", 1)[0]
        gates = (
            "readLargePngThumbnail(", "fileBytes > MaxThumbnailFileBytes",
            "fileBytes > MaxTiffThumbnailFileBytes", "hasTiffSignature(path)",
            "CGImageSourceCreateWithURL(", 'CFEqual(type, CFSTR("public.tiff"))',
            "largeFile && !tiff", "allowsLargeTiffThumbnail(source, pixels)",
            "CGImageSourceCreateThumbnailAtIndex(",
        )
        for gate in gates:
            self.assertIn(gate, native)
        positions = [native.index(gate) for gate in gates]
        self.assertEqual(positions, sorted(positions))
        self.assertIn("MaxThumbnailFileBytes = 256 * 1024 * 1024", native)
        self.assertIn("MaxTiffThumbnailFileBytes = 512 * 1024 * 1024", native)
        self.assertIn("if (sourcePixels > maxPixels)", native)
        large = cocoa.split("bool allowsLargeTiffThumbnail(", 1)[1].split("\n}", 1)[0]
        for guard in ("MaxTiffPixels = 100 * 1000 * 1000", "MaxTiffEdge = 32768",
                      "kCGImagePropertyIsFloat", "kCGImagePropertyDepth", "[@8 isEqual:depth]",
                      "[@16 isEqual:depth]", "MaxHighDepthBytes",
                      "kCGImagePropertyColorModelRGB", "kCGImagePropertyColorModelGray",
                      "kCGImagePropertyColorModelCMYK"):
            self.assertIn(guard, large)
        strip = (ROOT / "src/qvthumbnailstrip.cpp").read_text()
        guards = ("thumbnailMayReadContent", "isQuickThumbnail(path, fileBytes)", "readThumbnail(",
                  "QVImageLoader::fileVersion(path) != version")
        positions = [strip.index(guard) for guard in guards]
        self.assertEqual(positions, sorted(positions))

    def test_raw_thumbnail_uses_oriented_decode_graph(self):
        cocoa = (ROOT / "src/qvcocoafunctions.mm").read_text()
        raw = cocoa.split("QVCocoaFunctions::readThumbnail(", 1)[1].split(
            "if (isRawImageType(type))", 1)[1].split("UTType *imageType", 1)[0]
        self.assertIn("filter.orientation = sourceOrientation(source, 0)", raw)
        self.assertIn("filter.draftModeEnabled = YES", raw)
        self.assertIn("filter.draftModeEnabled = NO", raw)
        self.assertEqual(raw.count("filter.outputImage"), 2)
        self.assertNotIn("filter.previewImage", raw)


class ThumbnailRepairs(unittest.TestCase):
    def setUp(self):
        self.strip = (ROOT / "src/qvthumbnailstrip.cpp").read_text()
        self.native = (ROOT / "src/qvcocoafunctions.mm").read_text()
        self.async_code = (ROOT / "src/asyncwork.cpp").read_text()

    def test_live_subscription_and_file_version(self):
        self.assertIn("FSEventStreamSetDispatchQueue", self.strip)
        changed = self.strip.split("void QVThumbnailStrip::filesChanged(", 1)[1].split(
            "void QVThumbnailStrip::", 1)[0]
        self.assertIn("invalidate", changed)
        self.assertIn("directoryChangeTimer.start()", changed)
        self.assertIn("checkThumbnailListing()", self.strip)
        self.assertIn("QVImageLoader::fileVersion(path) != version", self.strip)

    def test_decode_uses_two_independent_bounded_slots_and_cancels_orphans(self):
        self.assertIn("bool fastRunning", self.strip)
        self.assertIn("bool slowRunning", self.strip)
        self.assertIn("QvAsync::Lane::ThumbnailDecode", self.strip)
        self.assertIn("QvAsync::cancel(job.ticket)", self.strip)
        self.assertIn("const bool canceled", self.strip)
        self.assertIn("if (!canceled)", self.strip)

    def test_metadata_admission_and_timeout_cancellation(self):
        self.assertIn("ThumbnailMetadata", self.async_code)
        self.assertIn("tryStart(runnable)", self.async_code)
        self.assertIn("QvAsync::trySchedule", self.strip)
        self.assertIn("QvAsync::cancel(validationTicket)", self.strip)
        loader = (ROOT / "src/qvimageloader.cpp").read_text()
        versioned = loader.split("QVImageLoader::requestVersionedImage", 1)[1].split(
            "void QVImageLoader::", 1)[0]
        self.assertGreaterEqual(versioned.count("fileVersion(path) == version"), 2)

    def test_over_budget_raw_is_embedded_only(self):
        raw = self.native.split("QImage readEmbeddedRawStripThumbnail(", 1)[1].split(
            "\n}", 1)[0]
        self.assertIn("kCGImageSourceCreateThumbnailFromImageIfAbsent : @NO", raw)
        self.assertNotIn("kCGImageSourceCreateThumbnailFromImageAlways", raw)
        self.assertIn("imageByApplyingCGOrientation", raw)
        self.assertIn("readEmbeddedRawStripThumbnail(source", self.native)

    def test_native_content_and_geometry_can_change_independently(self):
        self.assertIn("thumbnailCellsLayer.masksToBounds = YES", self.native)
        self.assertIn("tile.image.cacheKey()", self.native)
        self.assertIn("thumbnailBackgroundKey", self.native)
        self.assertIn("void QVThumbnailStrip::drawContent", self.strip)
        self.assertIn("update(dirty)", self.strip)


class ThumbnailCompileCommands(unittest.TestCase):
    build_dir = None
    webp_source = None

    def test_bundled_webp_header_is_reachable_for_both_targets(self):
        if self.build_dir is None:
            self.skipTest("Configure a CMake build to check generated compile commands")
        expected_header = (self.webp_source / "src/webp/decode.h").resolve(strict=True)

        for target, directory, command in cocoa_compile_commands(self.build_dir):
            with self.subTest(target=target):
                arguments = iter(command)
                include_paths = []
                for arg in arguments:
                    if arg in ("-I", "-isystem", "-idirafter"):
                        include_paths.append(next(arguments))
                    elif arg.startswith("-I"):
                        include_paths.append(arg[2:])
                candidates = (directory / path / "webp/decode.h" for path in include_paths)
                header = next((path.resolve() for path in candidates if path.is_file()), None)
                self.assertEqual(header, expected_header, f"{target} must use the bundled header")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--webp-source", type=Path)
    args, unittest_args = parser.parse_known_args()
    if bool(args.build_dir) != bool(args.webp_source):
        parser.error("--build-dir and --webp-source must be supplied together")

    ThumbnailCompileCommands.build_dir = args.build_dir
    ThumbnailCompileCommands.webp_source = args.webp_source
    unittest.main(argv=[sys.argv[0], *unittest_args])

#!/usr/bin/env python3
"""Color path contracts; pixel correctness lives in the dynamic suite."""
from pathlib import Path
import unittest
ROOT = Path(__file__).resolve().parents[1]
def read(p): return (ROOT / p).read_text()
class ColorContracts(unittest.TestCase):
    def assertIn(self, needle, haystack, msg=None):
        self.assertTrue(needle in haystack, msg or ('missing contract: ' + needle))
    def test_svg_has_one_normalized_source(self):
        self.assertIn('prepareSvg', read('src/qvimageloader.cpp'))
        self.assertIn('vectorImage.colorSpace', read('src/qvgraphicsimageitem.cpp'))
        self.assertIn('renderer->load(image.encodedData)', read('src/qvgraphicsimageitem.cpp'))
    def test_netpbm_and_multi_image(self):
        self.assertIn('isStandardNetpbm', read('src/qvcocoafunctions.mm'))
        self.assertIn('selectedImageIndex', read('src/qvcocoafunctions.mm'))
        self.assertIn('nativeResult.isMultiFrameImage', read('src/qvimageloader.cpp'))
    def test_vector_and_provider_precision(self):
        s = read('src/qvcocoafunctions.mm')
        self.assertIn('createSDRImage', s)
        self.assertIn('QImage::Format_RGBA64_Premultiplied', s)
        self.assertIn('componentBitsForQImage', s)
        self.assertIn('convertedToColorSpace', read('src/qvgraphicsimageitem.cpp'))
    def test_animation_and_source_evidence(self):
        s = read('src/qvimagecore.cpp').split('if (currentFileDetails.isMovieLoaded)')[1].split('if (!currentFileDetails.isMovieLoaded)')[0]
        self.assertIn('loadedHDRImage.reset()', s)
        self.assertIn('isNativeHDRLoaded = false', s)
        self.assertIn('ICCState::Absent', read('src/qvinfodialog.cpp'))
    def test_ctest_registration(self):
        s = read('tests/CMakeLists.txt')
        self.assertIn('FovelleColorRenderingDynamic', s)
        self.assertIn('FovelleColorRenderingStatic', s)
        self.assertIn('FovelleLegacyBt709Backend', s)
        self.assertIn('native_color_fixtures.mm', s)
        self.assertIn('testColorAnimationClearsStaticRepresentations', read('tests/tst_qviewtests.cpp'))
if __name__ == '__main__': unittest.main()

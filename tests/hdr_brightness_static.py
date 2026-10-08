"""Static acceptance checks; six-part case descriptions live in reports/."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / 'src/qvcocoafunctions.mm').read_text()
VIEW = (ROOT / 'src/qvgraphicsview.cpp').read_text()

class BrightnessSourceContracts(unittest.TestCase):
    def test_BR01_timing(self):
        self.assertIn('const qreal delay = rapidSwitch ? 0.0 : 80.0;', SOURCE)
        self.assertIn('std::clamp(totalMilliseconds, 400.0, 800.0)', SOURCE)
        self.assertIn('FOVELLE_HDR_FADE_MS', SOURCE)
        self.assertIn('owner->brightnessStartTime = CACurrentMediaTime()', SOURCE)
        self.assertIn('&& nativeImage && !nativeImage->isHDR()', SOURCE)
    def test_BR02_bezier(self):
        body = SOURCE.split('qreal QVCocoaFunctions::easedHDRTransition')[1].split('qreal QVCocoaFunctions::hdrBrightnessProgress')[0]
        for token in ('0.42', '0.58', 'x < bounded', '48'):
            self.assertIn(token, body)
    def test_BR03_rapid(self):
        self.assertIn('changedAt - lastImageChangeTime < 1.0', SOURCE)
        self.assertIn('rapidSwitch ? 250.0', SOURCE)
    def test_BR04_accessibility(self):
        self.assertIn('if (reduceMotion) return 1.0', SOURCE)
        self.assertIn('NSWorkspace.sharedWorkspace.accessibilityDisplayShouldReduceMotion', SOURCE)
    def test_BR05_highlights(self):
        body = SOURCE.split('static CIImage *hdrBrightnessImage')[1].split('// Background full-resolution renders')[0]
        for token in ('CIDissolveTransition', 'if (progress >= 1.0F) return hdr;',
                      'imageByApplyingGainMap', 'CIToneMapHeadroom'):
            self.assertIn(token, body)
        for token in ('smoothstep', 'ceiling /', 'max(high.r'):
            self.assertNotIn(token, body)
        self.assertIn('kCIContextWorkingColorSpace : (id)outputColorSpace', SOURCE)
    def test_BR06_current_headroom(self):
        body = SOURCE.split('qreal QVCocoaFunctions::displayHeadroomForRendering')[1].split('bool QVCocoaFunctions::isFinalHDRFrameReadyForReveal')[0]
        self.assertIn('Q_UNUSED(potentialHeadroom)', body)
        self.assertIn('std::isfinite(currentHeadroom)', body)
        self.assertIn('headroomMonitor->setInterval(100)', SOURCE)
    def test_BR07_lifecycle(self):
        self.assertIn('owner->applyPresentationTarget(false)', SOURCE)
        self.assertIn('(imageIsHDR && state.transitionProgress < 0.999F)', SOURCE)
        self.assertIn('(nativeSDRPresentation || rendererState.transitionProgress >= 0.999F)', VIEW)
        body = SOURCE.split('void revealAfterPresentation')[1].split('void scheduleHDRPreparation')[0]
        self.assertIn('!presentationState->hdrPrepared', body)
        self.assertIn('gate->generation != frameGeneration', body)
    def test_BR08_cache(self):
        body = SOURCE.split('void schedulePersistentSurfacePreparation')[1].split('int maximumFramesPerSecondForCurrentDisplay')[0]
        self.assertIn('hdrBrightnessDisplayImage(*image, preparedSDRImage, preparedHDRImage', body)
        self.assertIn('owner->persistentSurfaceSerial == surfaceSerial', body)
        self.assertIn('imageIsHDR && finalHeadroom && presentationState->hdrPrepared', SOURCE)

class ColorRegressionSourceContracts(unittest.TestCase):
    def test_CR01_endpoint_identity(self):
        body = SOURCE.split('static CIImage *hdrBrightnessImage')[1].split('static CIImage *hdrDisplayEndpoint')[0]
        self.assertIn('if (progress >= 1.0F) return hdr;', body)
        self.assertNotIn('smoothstep', body)
        self.assertNotIn('CIColorKernel', body)
    def test_CR02_platform_adaptation(self):
        body = SOURCE.split('static CIImage *hdrDisplayEndpoint')[1].split('static QVector<float> sampleLinearHDRImage')[0]
        for value in ('headroom >= contentHeadroom', 'imageByApplyingGainMap',
                      'CIToneMapHeadroom', 'inputSourceHeadroom', 'inputTargetHeadroom'):
            self.assertIn(value, body)
        # Opening preparation, viewport frames, cached surface and image probe
        # must share the same pipeline, rather than using a test-only formula.
        self.assertGreaterEqual(SOURCE.count('hdrBrightnessDisplayImage(*image,'), 4)
    def test_CR03_real_image_oracle(self):
        tests = (ROOT / 'tests/tst_qviewtests.cpp').read_text()
        self.assertIn('testHDRColorFidelity_data()', tests)
        self.assertIn('headroom, 1, true', tests)
        self.assertIn('mismatched_pixels=%3/4096', tests)
        for name in ('gain-map-jpeg', 'processed-dng', 'plain-dng', 'nef'):
            self.assertIn(name, tests)
    def test_CR04_materialized_cache(self):
        self.assertIn('[CIImage imageWithCGImage:impl->persistentImage]', SOURCE)
        tests = (ROOT / 'tests/tst_qviewtests.cpp').read_text()
        self.assertIn('renderer.probePersistentHDRPixels()', tests)
        self.assertIn('Materialized HDR layer changes decoded endpoint colors', tests)

if __name__ == '__main__':
    unittest.main(verbosity=2)

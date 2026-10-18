"""Contracts for asynchronous CI preparation and movie scheduling checks."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

class ActionsFollowupContracts(unittest.TestCase):
    def test_hdr_readiness_and_delayed_coverage(self):
        source = (ROOT / 'tests/zoom_color_acceptance.inc').read_text()
        case = source.split('void ImageCoreAndMovieTests::testHDRPersistentSurfaceMetadata()')[1].split('\nvoid ')[0]
        self.assertIn('window.getIsPixmapLoaded(), 15000', case)
        for token in ('"raw-delayed"', '<< 5500', 'HDR_FIXTURE_READY', 'loadTimer.elapsed() >= loadDelayMs',
                      'entered->tryAcquire(4, 10000)', 'release->release(4)',
                      'QCOMPARE(restored.pixels, baseline.pixels)', 'QCOMPARE(restored.identity, baseline.identity)',
                      'restored.contentHeadroom - content'):
            self.assertIn(token, source)

    def test_speed_observes_source_and_timer(self):
        source = (ROOT / 'tests/remediation_tests.h').read_text()
        case = source.split('void testMovieAsyncPlaybackSpeed()')[1].split('void testMovieAsyncInvalidInput()')[0]
        for token in ('QImageReader', 'nextImageDelay()', 'movie.nextFrameDelay()',
                      'sourceDelays.at(frame)', 'movie.currentImage()', 'sourceFrames.at(frame)',
                      'MovieSpeedTimerObservation', 'observation.valid', 'observation.deliveries',
                      'expectedElapsed - 20', 'errors.count(), 0'):
            self.assertIn(token, case)
        self.assertNotIn('normal.second * 0.85', case)
        self.assertIn('"gui-delayed"', source)
        cmake = (ROOT / 'tests/CMakeLists.txt').read_text()
        self.assertIn('FovelleActionsFollowupStatic', cmake)
        self.assertIn('FovelleRemediationDynamic', cmake)
        self.assertIn('FovelleHDRSurfaceMetadataDynamic', cmake)

if __name__ == '__main__':
    unittest.main()

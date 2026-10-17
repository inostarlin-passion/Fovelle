from pathlib import Path
import unittest
ROOT = Path(__file__).resolve().parents[1]
class HeadroomRestore(unittest.TestCase):
    def test_persistent_identity(self):
        source = (ROOT/'src/qvcocoafunctions.mm').read_text()
        monitor = source.split('QObject::connect(headroomMonitor')[1].split('headroomMonitor->start()')[0]
        self.assertTrue('persistentSurfaceHeadroom' in monitor, 'Legacy monitor must compare surface identity')
        self.assertTrue('The compositor adapts the tagged full-HDR CGImage' in monitor)
        schedule = source.split('void schedulePersistentSurfacePreparation()')[1].split('int maximumFramesPerSecondForCurrentDisplay')[0]
        self.assertTrue('persistentSurfaceHeadroom = state.displayRenderingHeadroom' in schedule)
        for term in ('maximumPersistentBytes', 'persistentSurfaceSerial == surfaceSerial', '#ifdef FOVELLE_HDR_SURFACE_TESTING'):
            self.assertTrue(term in schedule, term)
        cmake=(ROOT/'tests/CMakeLists.txt').read_text()
        self.assertTrue('FovelleHDRHeadroomRestoreDynamic' in cmake)
        self.assertTrue('FOVELLE_HDR_SURFACE_TESTING' in cmake)
if __name__ == '__main__': unittest.main()

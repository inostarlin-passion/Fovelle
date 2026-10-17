from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[1]
class HDRSurfaceMetadata(unittest.TestCase):
    def test_task_owned_metadata(self):
        source=(ROOT/'src/qvcocoafunctions.mm').read_text()
        schedule=source.split('void schedulePersistentSurfacePreparation()')[1].split('int maximumFramesPerSecondForCurrentDisplay')[0]
        for token in ('effectiveHDRHeadroom(', 'surfaceContentHeadroom', 'CGImageCreateCopyWithContentHeadroom(', '@available(macOS 15.0', 'CGImageRelease(surface)', 'persistentSurfaceSerial == surfaceSerial'):
            self.assertTrue(token in schedule, token)
        install=source.split('void installPersistentSurface(')[1].split('void schedulePersistentSurfacePreparation()')[0]
        self.assertTrue('persistentImageLayer.contentsHeadroom' in install)
        self.assertTrue('CGImageGetContentHeadroom(surface)' in install)
        self.assertTrue('@available(macOS 26.0' in install)
        self.assertNotIn('= state.targetHeadroom', install.replace('state.layerContentsHeadroom = state.targetHeadroom;', ''))
        self.assertTrue('source = [preparedHDRImage retain]' in schedule)
        self.assertTrue('FovelleHDRSurfaceMetadataDynamic' in (ROOT/'tests/CMakeLists.txt').read_text())
if __name__=='__main__': unittest.main()

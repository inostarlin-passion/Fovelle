from pathlib import Path
import unittest
ROOT = Path(__file__).resolve().parents[1]

class HDRInfoOutputContracts(unittest.TestCase):
    def test_normal_hdr_information_path(self):
        view = (ROOT / 'src/qvgraphicsview.cpp').read_text()
        self.assertTrue('QVGraphicsView::fileInfoColorInformation() const' in view,
                        'Dedicated normal-HDR information query is missing')
        body = view.split('QVGraphicsView::fileInfoColorInformation() const')[1].split('void QVGraphicsView::ensureHDRRenderer')[0]
        for token in ('colorInformation()', 'info.hasOutput', 'isNativeHDRLoaded', 'hdrRendererActive',
                      'state.imageActive', 'state.firstFramePresented', 'outputColorSpaceName.isEmpty()',
                      'info.outputSpace = state.outputColorSpaceName', 'info.preview = false'):
            self.assertTrue(token in body, token)
        self.assertNotIn('Extended Linear Display P3', body)
        self.assertNotIn('setHDRPresentationActive', body)
        live = view.split('QVGraphicsView::colorInformation() const')[1].split('QVGraphicsView::fileInfoColorInformation')[0]
        self.assertTrue('!loadedPixmapItem->isVisible()' in live)
        main = (ROOT / 'src/mainwindow.cpp').read_text()
        provider = main.split('void MainWindow::ensureInfoDialog()')[1].split('void MainWindow::showEvent')[0]
        self.assertTrue('view->fileInfoColorInformation()' in provider)
        cmake = (ROOT / 'tests/CMakeLists.txt').read_text()
        self.assertTrue('FovelleHDRInfoOutputStatic' in cmake)
        self.assertTrue('FovelleHDRInfoOutputDynamic' in cmake)

if __name__ == '__main__': unittest.main()

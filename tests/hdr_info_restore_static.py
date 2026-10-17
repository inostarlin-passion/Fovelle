from pathlib import Path
import unittest
ROOT = Path(__file__).resolve().parents[1]
class HDRInfoRestoreContracts(unittest.TestCase):
    def test_hidden_owner_recovery(self):
        source = (ROOT/'src/mainwindow.cpp').read_text()
        self.assertTrue('info->installEventFilter(this)' in source, 'Info hide observation missing')
        self.assertTrue('watched == info' in source and 'QEvent::Hide' in source)
        self.assertTrue('void MainWindow::restoreImageFocusAfterInfo()' in source)
        body = source.split('void MainWindow::restoreImageFocusAfterInfo()')[1].split('void MainWindow::showEvent')[0]
        for token in ('isClosing', 'getIsApplicationQuitting()', 'isVisible()', 'isMinimized()',
                      'Qt::ApplicationActive', 'info->isVisible()', 'activeWindow()',
                      'active != this', 'active != info', 'isActiveWindow()',
                      'activateWindow()', 'setHDRPresentationActive(true)', 'QTimer::singleShot(0, this'):
            self.assertTrue(token in body, token)
        self.assertNotIn('raise()', body)
        self.assertNotIn('processEvents', body)
        view = (ROOT/'src/qvgraphicsview.cpp').read_text()
        setter = view.split('void QVGraphicsView::setHDRPresentationActive(')[1].split('void QVGraphicsView::setHDRNavigationOverlay')[0]
        handoff = setter.index('hdrRenderer->setPresentationActive(active, true)')
        self.assertTrue('hdrPresentationTimer->start()' in setter[handoff:])
        same = setter.split('hdrPresentationActive = active')[0]
        self.assertTrue('hdrPresentationTimer->start()' in same)
        cmake = (ROOT/'tests/CMakeLists.txt').read_text()
        self.assertTrue('FovelleHDRInfoRestoreStatic' in cmake)
        self.assertTrue('FovelleHDRInfoRestoreDynamic' in cmake)
if __name__ == '__main__': unittest.main()

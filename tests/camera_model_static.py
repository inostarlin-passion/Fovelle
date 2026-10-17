"""Static contracts complement real ImageIO and UI acceptance tests."""
from pathlib import Path
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]

class CameraModelContracts(unittest.TestCase):
    def test_metadata_pipeline(self):
        cocoa = (ROOT / 'src/qvcocoafunctions.mm').read_text()
        self.assertIn('kCGImagePropertyTIFFModel', cocoa)
        body = cocoa.split('QString sourceCameraModel(')[1].split('QSize sourcePixelSize')[0]
        for term in ('CFDictionaryGetTypeID()', 'CFStringGetTypeID()', 'CFRelease(properties)', '.trimmed()'):
            self.assertIn(term, body)
        self.assertNotIn('kCGImagePropertyTIFFMake', body)
        for name in ('qvcocoafunctions.h', 'qvimageloader.h', 'qvimageloader.cpp',
                     'qvimagecore.h', 'qvimagecore.cpp', 'mainwindow.cpp'):
            self.assertIn('cameraModel', (ROOT / 'src' / name).read_text())
        dialog = (ROOT / 'src/qvinfodialog.cpp').read_text()
        self.assertIn('cameraModelLabel->setText', dialog)
        self.assertNotIn('CGImageSource', dialog)
        self.assertNotIn('QImageReader', dialog)
        cmake = (ROOT / 'tests/CMakeLists.txt').read_text()
        self.assertIn('FovelleCameraModelStatic', cmake)
        self.assertIn('FovelleCameraModelDynamic', cmake)

    def test_translations(self):
        expected = {'template':'', 'qview_zh_Hans':'拍摄机型:', 'qview_zh_Hant':'拍攝機型:',
                    'qview_es':'Modelo de cámara:', 'qview_ja':'撮影機種:'}
        for file, text in expected.items():
            root = ET.parse(ROOT / 'i18n' / (file + '.ts')).getroot()
            context = next(c for c in root.findall('context') if c.findtext('name') == 'QVInfoDialog')
            messages = [m for m in context.findall('message') if m.findtext('source') == 'Camera model:']
            self.assertEqual(len(messages), 1, file)
            if text:
                self.assertEqual(messages[0].findtext('translation'), text)
                self.assertNotEqual(messages[0].find('translation').get('type'), 'unfinished')

if __name__ == '__main__':
    unittest.main()

#!/usr/bin/env python3
from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[1]
class VectorPerformanceContracts(unittest.TestCase):
    def test_profile_not_in_paint(self):
        s=(ROOT/'src/qvgraphicsimageitem.cpp').read_text().split('void QVGraphicsImageItem::paint(')[1]
        self.assertFalse('getIccProfileForWindow' in s, 'system ICC lookup remains in paint')
        self.assertFalse('fromIccProfile' in s, 'ICC parsing remains in paint')
    def test_conversion_not_in_paint(self):
        s=(ROOT/'src/qvgraphicsimageitem.cpp').read_text();paint=s.split('void QVGraphicsImageItem::paint(')[1]
        self.assertFalse('convertedToColorSpace' in paint, 'color conversion remains in paint')
        self.assertTrue('prepareVectorDisplay' in s, 'worker display preparation missing')
    def test_target_and_queue(self):
        s=(ROOT/'src/qvgraphicsimageitem.cpp').read_text()
        self.assertTrue('request.targetColorSpace' in s)
        self.assertTrue('result.tile.outputSpace == displayColorSpace' in s)
        self.assertTrue('pendingAsyncRequest = request' in s)
    def test_registration(self):
        s=(ROOT/'tests/CMakeLists.txt').read_text()
        self.assertTrue('FovelleVectorPerformanceDynamic' in s)
        self.assertTrue('FovelleVectorPerformanceStatic' in s)
if __name__=='__main__':unittest.main()

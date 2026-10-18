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
    def test_timing_contract(self):
        s=(ROOT/'tests/vector_performance_acceptance.inc').read_text()
        self.assertIn('const auto cpuStart = currentThreadCpuTimeNanoseconds()', s)
        self.assertIn('const auto cpuEnd = currentThreadCpuTimeNanoseconds()', s)
        self.assertIn('(*cpuEnd - *cpuStart) / 1e6', s)
        self.assertIn('measureVectorPaint([&] { item.paint(', s)
        self.assertIn('measureVectorPaint([&] { painter.drawImage(', s)
        self.assertIn('qMax(16.67, 4 * vectorPercentile(reference, .95))', s)
        self.assertIn('vectorPercentile(cold, .95) <= budget', s)
        self.assertIn('p95 <= budget', s)
        for key in ('clock', 'wall_cold_p95_ms', 'wall_paint_p95_ms', 'wall_reference_p95_ms'):
            self.assertIn('"' + key + '"', s)
        cmake=(ROOT/'tests/CMakeLists.txt').read_text()
        self.assertIn('testVectorPaintTimingClockIsolation testVectorPresentationFrameBudget', cmake)
    def test_registration(self):
        s=(ROOT/'tests/CMakeLists.txt').read_text()
        self.assertTrue('FovelleVectorPerformanceDynamic' in s)
        self.assertTrue('FovelleVectorPerformanceStatic' in s)
if __name__=='__main__':unittest.main()

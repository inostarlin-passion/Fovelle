#!/usr/bin/env python3
"""Adversarial tests for mandatory fixture/result acceptance predicates."""
import json
import tempfile
import unittest
from pathlib import Path
from run_portable_hdr import FIXTURES,validate_fixtures,validate_xml
class GateTests(unittest.TestCase):
    def test_real_fixture_manifest(self): validate_fixtures()
    def test_absent_and_corrupt_fixture_fail(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);(root/'manifest.json').write_bytes((FIXTURES/'manifest.json').read_bytes())
            with self.assertRaises(FileNotFoundError):validate_fixtures(root)
            for name in ('gain-map.jpg','bayer.dng'):(root/name).write_bytes(b'corrupt')
            with self.assertRaises(ValueError):validate_fixtures(root)
    def test_missing_skip_fail_and_empty_execution_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            path=Path(temporary)/'result.xml'
            for kind in ('skip','fail','xfail'):
                path.write_text(f'<TestCase><TestFunction name="required"><Incident type="{kind}"/></TestFunction></TestCase>')
                with self.assertRaises(ValueError):validate_xml(path,['required'])
            for text in ('<TestCase/>','<TestCase><TestFunction name="required"/></TestCase>'):
                path.write_text(text)
                with self.assertRaises(ValueError):validate_xml(path,['required'])
            path.write_text('<TestCase><TestFunction name="required"><Incident type="pass"/></TestFunction></TestCase>')
            validate_xml(path,['required'])
if __name__=='__main__':unittest.main()

#!/usr/bin/env python3
"""Static acceptance: UI entry, all translations, and build registration.
Purpose: prevent incomplete UI/resource integration.
Preconditions: repository files exist. Inputs: UI, TS, CMake and qmake files.
Steps: parse controls/catalogs; check every required context/key and placeholder.
Expected: all five UI languages and both build systems expose the feature.
Postconditions: no repository or user settings changes.
"""
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET
ROOT = Path(__file__).resolve().parents[1]
KEYS = {
    'QVOptionsDialog': ['Keep zoom level when switching images'],
    'QVInfoDialog': ['Source color space:', 'Embedded ICC profile:',
                     'Current output color space:', 'Unknown', 'Not embedded',
                     'Invalid profile', 'Unsupported profile',
                     'Unspecified (assumed sRGB for display)', 'No output',
                     'Preview: %1', 'Decoder: %1', 'Embedded ICC: %1',
                     'RAW (camera color space)', 'Document-defined color spaces',
                     'Container: %1', 'Embedded ICC (%1 bytes)', 'Unspecified',
                     'Conflicting color declarations'],
}
def main():
    results = []
    cases = json.loads((ROOT/'tests/zoom_color_cases.json').read_text())
    implementations = (ROOT/'tests/tst_qviewtests.cpp').read_text() + (ROOT/'tests/zoom_color_acceptance.inc').read_text()
    for case in cases:
        assert all(case.get(key) for key in ['purpose', 'preconditions', 'input', 'steps', 'expected', 'postconditions', 'test'])
        for method in re.findall(r'::([A-Za-z0-9_]+)', case['test']):
            assert '::' + method + '(' in implementations, (case['id'], method)
    options = ET.parse(ROOT/'src/qvoptionsdialog.ui')
    assert options.find('.//widget[@name="keepZoomCheckbox"]') is not None
    info = ET.parse(ROOT/'src/qvinfodialog.ui')
    for name in ['sourceColorSpaceLabel', 'iccProfileLabel', 'outputColorSpaceLabel']:
        assert info.find(f'.//widget[@name="{name}"]') is not None
    for path in sorted((ROOT/'i18n').glob('qview_*.ts')):
        tree = ET.parse(path)
        for context, keys in KEYS.items():
            entries = {m.findtext('source'): m.find('translation')
                       for c in tree.findall('context') if c.findtext('name') == context
                       for m in c.findall('message')}
            for key in keys:
                value = entries.get(key)
                assert value is not None, (path.name, context, key)
                text = ''.join(value.itertext()).strip()
                assert text and value.get('type') not in ('unfinished', 'vanished', 'obsolete'), (path.name, key)
                assert sorted(re.findall(r'%[1-9n]', key)) == sorted(re.findall(r'%[1-9n]', text)), (path.name, key)
        results.append(path.name)
    for file in ['CMakeLists.txt', 'tests/CMakeLists.txt', 'src/src.pri']:
        assert 'colorinformation.cpp' in (ROOT/file).read_text(), file
    print(json.dumps({'status': 'passed', 'catalogs': results, 'english': 'source keys',
                      'checks': ['UI controls', 'translations/placeholders', 'build registration']}, ensure_ascii=False))
if __name__ == '__main__':
    main()

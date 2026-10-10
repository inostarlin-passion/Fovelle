#!/usr/bin/env python3
"""KZ-P1 static: active zoom preference must not be reset during startup.
Purpose: detect a restored setting remaining in removed-preference migration.
Preconditions: readable source tree. Input: migration, registry, UI, main.
Steps: inspect the removed-defaults block, active binding, and startup order.
Expected: navresetszoom is active/default-true, bound inversely, never reset.
Postconditions: no source or preference writes.
"""
from pathlib import Path
import re
import json
root = Path(__file__).resolve().parents[1]
cases = json.loads((root/'tests/keep_zoom_persistence_cases.json').read_text())
implementations = (root/'tests/zoom_color_acceptance.inc').read_text()
for case in cases:
    assert all(case.get(key) for key in ['purpose', 'preconditions', 'input', 'steps', 'expected', 'postconditions', 'test'])
    for method in re.findall(r'::([A-Za-z0-9_]+)', case['test']):
        assert '::' + method + '(' in implementations, (case['id'], method)
settings = (root/'src/settingsmanager.cpp').read_text()
block = re.search(r'const QHash<QString, QVariant> removedPreferenceDefaults\s*\{(.*?)\n    \};', settings, re.S)
assert block, 'migration block not found'
assert '"navresetszoom"' not in block.group(1), 'active Keep zoom level preference is reset at every startup'
assert re.search(r'settingsLibrary\.insert\(\s*"navresetszoom"\s*,\s*\{\s*true\s*,', settings)
options = (root/'src/qvoptionsdialog.cpp').read_text()
assert re.search(r'modifySetting\(\s*QStringLiteral\(\s*"navresetszoom"\s*\)\s*,\s*!checked\s*\)', options)
main = (root/'src/main.cpp').read_text()
assert main.index('SettingsManager::migrateOldSettings();') < main.index('QVApplication app(argc, argv);')
print('PASS: active zoom preference is preserved by startup migration; default and inverse binding remain')

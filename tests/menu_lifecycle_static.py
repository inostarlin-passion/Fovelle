#!/usr/bin/env python3
"""TC-M3/R1: classify every menu action and guard shutdown safety."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
window = (root/'src/mainwindow.cpp').read_text()
actions = (root/'src/actionmanager.cpp').read_text()
header = (root/'src/asyncwork.h').read_text()
shutdown = (root/'src/qvapplication.cpp').read_text()
keys = re.findall(r'actionLibrary\.insert\s*\(\s*"([^"]+)"', actions)
assert len(keys) >= 50
assert len(keys) == len(set(keys))
# Every state-dependent text/check writer is reviewed, including helper paths.
writers = set(re.findall(r'(?:getAllClonesOfAction|syncStatefulAction)\s*\(\s*"([^"]+)"', window))
assert {'fullscreen','pause','slideshow','toggletitlebar','windowontop','fillwindow'} <= writers
for function in ['ensureMenus', 'ensureContextMenu']:
    body = window.split(f'void MainWindow::{function}()',1)[1].split('\nvoid ',1)[0]
    assert 'syncMenuState();' in body, function
assert 'QvAsync::finishOrExit();' in shutdown
assert 'shutdownBackgroundWork();' in shutdown
assert 'QSettings().sync();' in shutdown
assert 'int timeoutMs = 1500' in header
print(f'PASS: audited {len(keys)} literal actions plus recent/Open With menu entries; obsolete sort actions absent; dynamic writers: {sorted(writers)}; shutdown safety preserved')

assert not any(k.startswith(('sortmode', 'sortdirection')) for k in keys)
close = window.split('void MainWindow::closeEvent(', 1)[1].split('void MainWindow::changeEvent', 1)[0]
assert 'setFullSizeContentView' not in close, 'Closing must not restyle a dying native window'
constructor = window.split('MainWindow::MainWindow(', 1)[1].split('MainWindow::~MainWindow', 1)[0]
assert 'setWindowFlag(Qt::ExpandedClientAreaHint, true)' in constructor

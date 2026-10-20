#!/usr/bin/env python3
"""S1/S2: launch ordering and bounded, owner-independent shutdown contract."""
from pathlib import Path
r = Path(__file__).resolve().parents[1]
w = (r/'src/mainwindow.cpp').read_text()
h = (r/'src/mainwindow.h').read_text()
a = (r/'src/qvapplication.cpp').read_text()
constructor = w.split('MainWindow::MainWindow(', 1)[1].split('MainWindow::~MainWindow', 1)[0]
show = w.split('void MainWindow::showEvent(', 1)[1].split('void MainWindow::closeEvent', 1)[0]
assert constructor.rfind('ensureMenus();') > constructor.index('restoreGeometry(')
assert 'ensureMenus' not in show
assert 'new MainWindow(nullptr, windowSessionState, true)' in a
print('PASS S1: menus constructed before show without queued menu work')
stop = w.split('void MainWindow::shutdownBackgroundWork()', 1)[1].split('void MainWindow::ensureMenus()', 1)[0]
request = w.split('void MainWindow::requestPopulateOpenWithMenu()', 1)[1].split('void MainWindow::populateOpenWithMenu', 1)[0]
assert 'QvAsync::cancel(openWithTicket)' in stop
assert 'waitFor' not in stop
assert 'backgroundThreadPool' not in h and 'QFutureWatcher' not in h
assert 'QvAsync::run(' in request and 'QPointer<MainWindow>' in request
assert 'provider = openWithItemProvider' in request
assert 'QSettings().sync();' in a and 'QvAsync::finishOrExit();' in a
assert 'int timeoutMs = 1500' in (r/'src/asyncwork.h').read_text()
print('PASS S2: cancellation, weak owner, application drain and persistence retained')

// TC-M1/M2/M3: actual state precedes lazy menu construction. Scoped options
// and stack windows restore all state even if a Qt assertion returns early.
void WindowBehaviorTests::testLateMenuState_data()
{
    QTest::addColumn<bool>("hidden");
    QTest::addColumn<bool>("deferred");
    for (bool hidden : {false, true})
        for (bool deferred : {false, true})
            QTest::newRow(qPrintable(QString("hidden=%1-deferred=%2").arg(hidden).arg(deferred)))
                << hidden << deferred;
}

void WindowBehaviorTests::testLateMenuState()
{
    QFETCH(bool, hidden);
    QFETCH(bool, deferred);
    ScopedOptionValues options({{"titlebarhidden", false}, {"slideshowtimer", 3600}});
    const bool quit = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);
    const auto restoreQuit = qScopeGuard([quit] { qvApp->setQuitOnLastWindowClosed(quit); });
    MainWindow window(nullptr, {}, deferred);
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.show();
    window.setTitlebarHidden(hidden, false);
    window.toggleWindowOnTop();
    window.toggleSlideshow();
    window.setFillWindow(true);
    // Force the normal first-keypress lazy path without changing window state.
    QKeyEvent key(QEvent::ShortcutOverride, Qt::Key_F13, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &key);
    QMenu *context = window.getContextMenuForTesting();
    QVERIFY(context);
    auto &manager = qvApp->getActionManager();
    const auto check = [&](const QString &key, const QString &text, bool checked) {
        const auto actions = manager.getAllClonesOfAction(key, &window);
        QVERIFY2(actions.size() >= 3, qPrintable(key)); // main, virtual, context
        for (QAction *action : actions) {
            if (!text.isEmpty()) QCOMPARE(action->text(), text);
            if (action->isCheckable()) QCOMPARE(action->isChecked(), checked);
            auto *menu = qobject_cast<QMenu *>(action->parent());
            const bool contextIcon = menu && menu->property("isContextMenu").toBool();
            QCOMPARE(action->icon().isNull(), !contextIcon);
        }
    };
    check("toggletitlebar", hidden ? MainWindow::tr("Show Title&bar") : MainWindow::tr("Hide Title&bar"), false);
    check("slideshow", MainWindow::tr("Stop S&lideshow"), false);
    check("windowontop", {}, true);
    check("fillwindow", {}, true);
    // Audit every remaining action against its static template and enable rule.
    const QSet<QString> dynamic {"toggletitlebar", "fullscreen", "pause", "slideshow", "windowontop", "fillwindow"};
    for (auto it = manager.getActionLibrary().cbegin(); it != manager.getActionLibrary().cend(); ++it) {
        if (dynamic.contains(it.key()) || it.key().startsWith("sort")) continue;
        for (auto *action : manager.getAllClonesOfAction(it.key(), &window)) {
            QCOMPARE(action->text(), it.value()->text());
            const QString rule = it.value()->data().toStringList().last();
            if (rule == "disable" || rule == "gifdisable" || rule == "undodisable" || rule == "folderdisable")
                QVERIFY(!action->isEnabled());
        }
    }
    MainWindow other;
    other.setAttribute(Qt::WA_DeleteOnClose, false);
    other.show();
    other.setTitlebarHidden(!hidden, false);
    other.getContextMenuForTesting();
    check("toggletitlebar", hidden ? MainWindow::tr("Show Title&bar") : MainWindow::tr("Hide Title&bar"), false);
    window.setTitlebarHidden(!hidden, false);
    window.cancelSlideshow();
    window.toggleWindowOnTop();
    window.setFillWindow(false);
    check("toggletitlebar", !hidden ? MainWindow::tr("Show Title&bar") : MainWindow::tr("Hide Title&bar"), false);
    check("slideshow", MainWindow::tr("Start S&lideshow"), false);
    check("windowontop", {}, false);
    check("fillwindow", {}, false);
    window.close();
    other.close();
}

void WindowBehaviorTests::testLateFullscreenMenu()
{
    ScopedOptionValues options({{"titlebarhidden", false}});
    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.show();
    window.toggleFullScreen();
    QTRY_VERIFY_WITH_TIMEOUT(window.isFullScreen(), 5000);
    window.getContextMenuForTesting();
    auto actions = qvApp->getActionManager().getAllClonesOfAction("fullscreen", &window);
    QVERIFY(actions.size() >= 3);
    bool allExit = true;
    for (auto *action : actions) allExit &= action->text() == MainWindow::tr("Exit F&ull Screen");
    window.toggleFullScreen();
    QTRY_VERIFY_WITH_TIMEOUT(!window.isFullScreen(), 5000);
    for (auto *action : actions) QCOMPARE(action->text(), MainWindow::tr("Enter F&ull Screen"));
    window.close();
    QVERIFY(allExit);
}

void WindowBehaviorTests::testLatePauseMenu()
{
    QTemporaryDir dir;
    const QString path = createBase64Image(dir, "late-menu", "png", tinyAnimatedPngBase64);
    QVERIFY(!path.isEmpty());
    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.show();
    window.openFile(path);
    QTRY_VERIFY_WITH_TIMEOUT(window.getIsMovieLoaded(), 5000);
    window.pause();
    window.getContextMenuForTesting();
    const auto actions = qvApp->getActionManager().getAllClonesOfAction("pause", &window);
    QVERIFY(actions.size() >= 3);
    for (auto *action : actions) {
        QCOMPARE(action->text(), MainWindow::tr("Res&ume"));
        QVERIFY(action->isEnabled());
        auto *menu = qobject_cast<QMenu *>(action->parent());
        QCOMPARE(action->icon().isNull(), !(menu && menu->property("isContextMenu").toBool()));
    }
    window.pause();
    for (auto *action : actions) QCOMPARE(action->text(), MainWindow::tr("Pa&use"));
    window.close();
}

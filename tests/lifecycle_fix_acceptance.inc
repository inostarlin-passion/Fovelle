// D1: no event dispatch between factory return and readiness assertions.
void WindowBehaviorTests::testStartupMenusReadyBeforeEvents()
{
    const bool quit = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);
    const auto restore = qScopeGuard([quit] { qvApp->setQuitOnLastWindowClosed(quit); });
    auto *window = QVApplication::newWindow();
    window->setAttribute(Qt::WA_DeleteOnClose, false);
    const auto cleanup = qScopeGuard([window] { window->close(); delete window; });
    auto &manager = qvApp->getActionManager();
    const auto actions = manager.getAllClonesOfAction("fullscreen", window);
    QCOMPARE(actions.size(), 2); // window menu and virtual keyboard actions
    QVERIFY(!window->actions().isEmpty());
    window->hide();
    window->show();
    QCOMPARE(manager.getAllClonesOfAction("fullscreen", window).size(), 2);
}

// D4: deterministic single-flight and stale-result suppression, with cleanup
// releasing the worker even when an assertion exits the test early.
void FeatureTests::testOpenWithLatestRequest()
{
    const bool quit = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);
    const auto restore = qScopeGuard([quit] { qvApp->setQuitOnLastWindowClosed(quit); });
    QTemporaryDir dir;
    const QString a = createTestImage(dir, "query-a", Qt::red);
    const QString b = createTestImage(dir, "query-b", Qt::blue);
    auto gate = std::make_shared<QSemaphore>();
    auto nextGate = std::make_shared<QSemaphore>();
    auto calls = std::make_shared<std::atomic<int>>(0);
    const auto cleanup = qScopeGuard([gate, nextGate] {
        gate->release();
        nextGate->release();
        QvAsync::drain(5000);
    });
    MainWindow window(nullptr, {}, true, [gate, nextGate, calls, a](const QString &path) {
        ++*calls;
        if (path == a)
            gate->acquire();
        else
            nextGate->acquire();
        OpenWith::OpenWithItem item;
        item.name = path;
        return QList<OpenWith::OpenWithItem>{item};
    });
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.show();
    window.openFile(a);
    QTRY_VERIFY_WITH_TIMEOUT(window.getIsPixmapLoaded(), 5000);
    window.requestPopulateOpenWithMenu();
    QTRY_COMPARE_WITH_TIMEOUT(calls->load(), 1, 1000);
    window.openFile(b);
    QTRY_COMPARE_WITH_TIMEOUT(window.getCurrentFileDetails().fileInfo.absoluteFilePath(), b, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(window.getIsPixmapLoaded(), 5000);
    window.requestPopulateOpenWithMenu();
    window.requestPopulateOpenWithMenu();
    QCOMPARE(calls->load(), 1);
    gate->release();
    const auto actions = qvApp->getActionManager().getAllClonesOfAction("openwith0", &window);
    QVERIFY(!actions.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(calls->load(), 2, 5000);
    // B is still blocked: the obsolete A completion must not touch the menu.
    QVERIFY(actions.first()->text() != a);
    nextGate->release();
    QTRY_COMPARE_WITH_TIMEOUT(actions.first()->text(), b, 5000);
    QCOMPARE(calls->load(), 2);
    window.close();
}

// D5: actual MainWindow worker participates in the application drain.
void FeatureTests::testOpenWithShutdownProcess()
{
    QTemporaryDir dir;
    for (int delay : {30, 10000}) {
        QProcess child;
        auto env = QProcessEnvironment::systemEnvironment();
        env.insert("FOVELLE_OPENWITH_SHUTDOWN_PROBE", dir.path());
        env.insert("FOVELLE_READ_SHUTDOWN_DELAY_MS", QString::number(delay));
        env.insert("FOVELLE_DISABLE_AUTO_UPDATE_CHECK", "1");
        child.setProcessEnvironment(env);
        QElapsedTimer timer;
        timer.start();
        child.start(QCoreApplication::applicationFilePath(), {});
        const bool done = child.waitForFinished(5000);
        if (!done) { child.kill(); child.waitForFinished(); }
        QVERIFY2(done, child.readAll().constData());
        QCOMPARE(child.exitStatus(), QProcess::NormalExit);
        QCOMPARE(child.exitCode(), delay > 1500 ? 75 : 0);
        QVERIFY(timer.elapsed() < 4000);
        QSettings saved(dir.filePath("saved.ini"), QSettings::IniFormat);
        QCOMPARE(saved.value("saved").toBool(), true);
        qInfo() << "OPENWITH_PROCESS delay_ms=" << delay << "exit=" << child.exitCode()
                << "wall_ms=" << timer.elapsed();
    }
}

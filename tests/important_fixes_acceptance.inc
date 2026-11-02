// Each AC and its six-part specification is documented in reports/test_case_specification.md.
namespace {
struct DecodeGate
{
    std::atomic<bool> entered{ false }, release{ false }, canceled{ false };
};
QVImageLoader::Result gatedRead(const std::shared_ptr<DecodeGate> &gate, const QString &path)
{
    gate->entered = true;
    while (!gate->release && !QvAsync::isCanceled())
        QThread::msleep(2);
    gate->canceled = QvAsync::isCanceled();
    QFileInfo info(path);
    QVImageLoader::Result result;
    result.absoluteFilePath = path;
    result.fileSize = info.size();
    result.lastModified = info.lastModified();
    return result;
}
void closeFixAlerts(MainWindow &window)
{
    for (auto *message : window.findChildren<QMessageBox *>())
        message->done(QMessageBox::Ok);
}
bool fixAlertVisible(MainWindow &window)
{
    for (auto *message : window.findChildren<QMessageBox *>())
        if (message->isVisible() && !message->text().isEmpty())
            return true;
    return false;
}
QFileDialog *openFrameDialog(MainWindow &window)
{
    window.saveFrameAs();
    return window.findChild<QFileDialog *>();
}
void chooseFrameFile(QFileDialog *dialog, const QString &path)
{
    dialog->hide();
    QMetaObject::invokeMethod(dialog, "fileSelected", Qt::DirectConnection, Q_ARG(QString, path));
}
} // namespace

void ImageLoaderTests::testLoaderSupersessionCancellation()
{
    QTemporaryDir dir;
    auto gate = std::make_shared<DecodeGate>();
    const auto cleanup = qScopeGuard([gate] {
        gate->release = true;
        QvAsync::drain(3000);
    });
    QVImageLoader loader(nullptr, [gate](const QString &path, int, bool) {
        if (path.endsWith("old"))
            return gatedRead(gate, path);
        QFileInfo info(path);
        QVImageLoader::Result r;
        r.absoluteFilePath = path;
        r.fileSize = info.size();
        r.lastModified = info.lastModified();
        return r;
    });
    QSignalSpy ready(&loader, &QVImageLoader::imageReady);
    loader.requestImage(dir.filePath("old"));
    QTRY_VERIFY_WITH_TIMEOUT(gate->entered.load(), 1000);
    const auto latest = loader.requestImage(dir.filePath("latest"));
    QTRY_VERIFY_WITH_TIMEOUT(gate->canceled.load(), 1000);
    QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 1000);
    QCOMPARE(ready.first().first().toULongLong(), latest);
    QCoreApplication::processEvents();
    QCOMPARE(ready.size(), 1);
}

void ImageLoaderTests::testLoaderCancellationLifecycle()
{
    QTemporaryDir dir;
    for (int mode = 0; mode < 3; ++mode) {
        auto gate = std::make_shared<DecodeGate>();
        auto calls = std::make_shared<std::atomic<int>>(0);
        const auto cleanup = qScopeGuard([gate] {
            gate->release = true;
            QvAsync::drain(3000);
        });
        const QString path = dir.filePath(QString::number(mode));
        QVImageLoader loader(nullptr, [gate, calls, path](const QString &p, int, bool) {
            if (p == path && calls->fetch_add(1) == 0)
                return gatedRead(gate, p);
            QFileInfo info(p);
            QVImageLoader::Result r;
            r.absoluteFilePath = p;
            r.fileSize = info.size();
            r.lastModified = info.lastModified();
            return r;
        });
        QSignalSpy ready(&loader, &QVImageLoader::imageReady);
        if (mode == 0)
            loader.requestImage(path);
        else
            loader.setDesiredImages({ { path, 1 } });
        QTRY_VERIFY_WITH_TIMEOUT(gate->entered.load(), 1000);
        if (mode == 0)
            loader.clear();
        else if (mode == 1)
            loader.setDesiredImages({ });
        else {
            loader.requestImage(dir.filePath("other"));
            QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 1000);
            QVERIFY(!gate->canceled);
            gate->release = true;
            QVERIFY(QvAsync::drain(3000));
            continue;
        }
        QTRY_VERIFY_WITH_TIMEOUT(gate->canceled.load(), 1000);
        QCOMPARE(ready.size(), 0);
        const auto id = loader.requestImage(path);
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 1000);
        QCOMPARE(ready.first().first().toULongLong(), id);
    }
}

void ImageLoaderTests::testLoaderForegroundIsolation()
{
    auto gate = std::make_shared<QSemaphore>();
    auto entered = std::make_shared<std::atomic<int>>(0);
    const auto cleanup = qScopeGuard([gate] {
        gate->release(4);
        QvAsync::drain(3000);
    });
    for (int i = 0; i < 4; ++i)
        QvAsync::schedule([gate, entered] {
            ++*entered;
            gate->acquire();
        });
    QTRY_COMPARE_WITH_TIMEOUT(entered->load(), 4, 1000);
    QTemporaryDir dir;
    const QString path = createTestImage(dir, "isolated", Qt::red);
    QVERIFY(!path.isEmpty());
    auto obsoleteCalls = std::make_shared<std::atomic<int>>(0);
    QVImageLoader obsolete(nullptr, [obsoleteCalls](const QString &, int, bool) {
        ++*obsoleteCalls;
        return QVImageLoader::Result{ };
    });
    obsolete.setDesiredImages({ { dir.filePath("obsolete"), 1 } });
    obsolete.setDesiredImages({ });
    QVImageLoader loader;
    QSignalSpy ready(&loader, &QVImageLoader::imageReady);
    loader.setDesiredImages({ { path, 1 } });
    loader.requestImage(path);
    QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 1000);
    gate->release(4);
    QVERIFY(QvAsync::drain(3000));
    QCOMPARE(obsoleteCalls->load(), 0);
    QCoreApplication::processEvents();
    loader.requestImage(path, true);
    QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 2, 1000);
}

void ImageLoaderTests::testLoaderPoolBoundAndDrain()
{
    auto gate = std::make_shared<QSemaphore>();
    auto entered = std::make_shared<std::atomic<int>>(0);
    auto queuedCalls = std::make_shared<std::atomic<int>>(0);
    const auto cleanup = qScopeGuard([gate] {
        gate->release(2);
        QvAsync::drain(3000);
    });
    for (int i = 0; i < 2; ++i)
        QvAsync::schedule(
                [gate, entered] {
                    ++*entered;
                    gate->acquire();
                },
                0, QvAsync::Lane::Foreground);
    QTRY_COMPARE_WITH_TIMEOUT(entered->load(), 2, 1000);
    QvAsync::schedule([queuedCalls] { ++*queuedCalls; }, 0, QvAsync::Lane::Foreground);
    QTest::qWait(100);
    QCOMPARE(queuedCalls->load(), 0);
    QElapsedTimer elapsed;
    elapsed.start();
    const bool complete = QvAsync::drain(100);
    const auto duration = elapsed.elapsed();
    QVERIFY(!complete);
    QVERIFY(duration < 600);
    gate->release(2);
    QVERIFY(QvAsync::drain(3000));
    QCOMPARE(queuedCalls->load(), 0);
    QVERIFY(!QvAsync::isCanceled());
    auto delivered = std::make_shared<std::atomic<int>>(0);
    for (auto lane : { QvAsync::Lane::Shared, QvAsync::Lane::Foreground })
        QvAsync::run([] { return 1; }, [delivered](int value) { *delivered += value; }, 0, lane);
    QTRY_COMPARE_WITH_TIMEOUT(delivered->load(), 2, 1000);
}

void WindowBehaviorTests::testUndoDeleteRetry()
{
    const bool quit = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);
    const auto restoreQuit = qScopeGuard([quit] { qvApp->setQuitOnLastWindowClosed(quit); });
    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.show();
    const auto closeWindow = qScopeGuard([&] {
        closeFixAlerts(window);
        window.close();
    });
    QTemporaryDir dir;
    const QString source = createTestImage(dir, "trash", Qt::red);
    const QString target = createTestImage(dir, "original", Qt::blue);
    const QImage original(target), trashed(source);
    window.lastDeletedFiles.push({ source, target });
    window.undoDelete();
    QCOMPARE(window.lastDeletedFiles.size(), 1);
    QCOMPARE(QImage(target), original);
    QCOMPARE(QImage(source), trashed);
    QVERIFY(fixAlertVisible(window));
    closeFixAlerts(window);
    QVERIFY(QFile::remove(target));
    window.undoDelete();
    QCOMPARE(window.lastDeletedFiles.size(), 0);
    QVERIFY(!QFile::exists(source));
    QCOMPARE(QImage(target), trashed);
}

void WindowBehaviorTests::testUndoDeleteHistory()
{
    const bool quit = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);
    const auto restoreQuit = qScopeGuard([quit] { qvApp->setQuitOnLastWindowClosed(quit); });
    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.show();
    const auto closeWindow = qScopeGuard([&] {
        closeFixAlerts(window);
        window.close();
    });
    QTemporaryDir dir;
    const QString first = createTestImage(dir, "first", Qt::red);
    const QString second = createTestImage(dir, "second", Qt::green);
    const QString firstTarget = dir.filePath("restored.png");
    const QString secondTarget = dir.filePath("missing/restored.png");
    window.lastDeletedFiles.push({ first, firstTarget });
    window.lastDeletedFiles.push({ second, secondTarget });
    window.undoDelete();
    QCOMPARE(window.lastDeletedFiles.size(), 2);
    QVERIFY(QFile::exists(first));
    QVERIFY(QFile::exists(second));
    closeFixAlerts(window);
    QVERIFY(QDir().mkpath(QFileInfo(secondTarget).absolutePath()));
    QVERIFY(QFile::setPermissions(second, QFileDevice::ReadOwner | QFileDevice::ReadUser));
    window.undoDelete();
    QCOMPARE(window.lastDeletedFiles.size(), 1);
    QVERIFY(QFile::exists(secondTarget));
    QVERIFY(!QFile::exists(second));
    QVERIFY(QFile::exists(first));
    QVERIFY(!QFile::exists(firstTarget));
    window.undoDelete();
    QCOMPARE(window.lastDeletedFiles.size(), 0);
    QVERIFY(QFile::exists(firstTarget));
    QVERIFY(!QFile::exists(first));
    window.undoDelete();
    QCOMPARE(window.lastDeletedFiles.size(), 0);
}

void WindowBehaviorTests::testFrameExportFormats()
{
    const bool nativeDisabled = QCoreApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    const auto restore = qScopeGuard(
            [=] { QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, nativeDisabled); });
    const bool quit = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);
    const auto restoreQuit = qScopeGuard([quit] { qvApp->setQuitOnLastWindowClosed(quit); });
    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.show();
    const auto closeWindow = qScopeGuard([&] {
        closeFixAlerts(window);
        window.close();
    });
    window.openFile(QStringLiteral(FOVELLE_TEST_FIXTURE_DIR) + "/remediation.gif");
    QTRY_VERIFY_WITH_TIMEOUT(window.getIsMovieLoaded(), 3000);
    auto *dialog = openFrameDialog(window);
    QVERIFY(dialog);
    const QByteArray writers = QImageWriter::supportedImageFormats().join(' ').toLower();
    const QRegularExpression extension("\\*\\.([a-zA-Z0-9]+)");
    bool png = false;
    for (const auto &filter : dialog->nameFilters()) {
        auto matches = extension.globalMatch(filter);
        while (matches.hasNext()) {
            const auto ext = matches.next().captured(1).toLatin1().toLower();
            QVERIFY2(writers.split(' ').contains(ext), ext.constData());
            png |= ext == "png";
        }
    }
    QVERIFY(png);
    QCOMPARE(dialog->defaultSuffix(), QString("png"));
    dialog->reject();
}

void WindowBehaviorTests::testFrameExportRejected()
{
    const bool nativeDisabled = QCoreApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    const auto restore = qScopeGuard(
            [=] { QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, nativeDisabled); });
    const bool quit = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);
    const auto restoreQuit = qScopeGuard([quit] { qvApp->setQuitOnLastWindowClosed(quit); });
    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.show();
    const auto closeWindow = qScopeGuard([&] {
        closeFixAlerts(window);
        window.close();
    });
    QTemporaryDir dir;
    const QString path = dir.filePath("existing.svg");
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("sentinel");
    }
    window.openFile(QStringLiteral(FOVELLE_TEST_FIXTURE_DIR) + "/remediation.gif");
    QTRY_VERIFY_WITH_TIMEOUT(window.getIsMovieLoaded(), 3000);
    auto *dialog = openFrameDialog(window);
    QVERIFY(dialog);
    chooseFrameFile(dialog, path);
    QVERIFY(fixAlertVisible(window));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("sentinel"));
    file.close();
    closeFixAlerts(window);
    const QString emptyTarget = dir.filePath("empty.png");
    {
        QFile sentinel(emptyTarget);
        QVERIFY(sentinel.open(QIODevice::WriteOnly));
        sentinel.write("sentinel");
    }
    QVERIFY(!window.saveFrameImage(QImage(), emptyTarget));
    QVERIFY(fixAlertVisible(window));
    QFile sentinel(emptyTarget);
    QVERIFY(sentinel.open(QIODevice::ReadOnly));
    QCOMPARE(sentinel.readAll(), QByteArray("sentinel"));
}

void WindowBehaviorTests::testFrameExportWrite()
{
    const bool nativeDisabled = QCoreApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    const auto restore = qScopeGuard(
            [=] { QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, nativeDisabled); });
    const bool quit = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);
    const auto restoreQuit = qScopeGuard([quit] { qvApp->setQuitOnLastWindowClosed(quit); });
    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.show();
    const auto closeWindow = qScopeGuard([&] {
        closeFixAlerts(window);
        window.close();
    });
    QTemporaryDir dir;
    const QString targetDir = dir.filePath("directory.png");
    QVERIFY(QDir().mkpath(targetDir));
    {
        QFile file(targetDir + "/sentinel");
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("unchanged");
    }
    window.openFile(QStringLiteral(FOVELLE_TEST_FIXTURE_DIR) + "/remediation.gif");
    QTRY_VERIFY_WITH_TIMEOUT(window.getIsMovieLoaded(), 3000);
    auto *dialog = openFrameDialog(window);
    QVERIFY(dialog);
    chooseFrameFile(dialog, targetDir);
    QVERIFY(fixAlertVisible(window));
    QFile sentinel(targetDir + "/sentinel");
    QVERIFY(sentinel.open(QIODevice::ReadOnly));
    QCOMPARE(sentinel.readAll(), QByteArray("unchanged"));
    closeFixAlerts(window);
    delete dialog;
    dialog = openFrameDialog(window);
    QVERIFY(dialog);
    const QImage expected = window.graphicsView->getLoadedMovie().currentImage();
    const QString target = dir.filePath("frame.PNG");
    chooseFrameFile(dialog, target);
    QVERIFY(!fixAlertVisible(window));
    QCOMPARE(QImage(target).convertToFormat(QImage::Format_RGBA8888),
             expected.convertToFormat(QImage::Format_RGBA8888));
}

void WindowBehaviorTests::testFrameExportSnapshot()
{
    const bool nativeDisabled = QCoreApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    const auto restore = qScopeGuard(
            [=] { QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, nativeDisabled); });
    const bool quit = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);
    const auto restoreQuit = qScopeGuard([quit] { qvApp->setQuitOnLastWindowClosed(quit); });
    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.show();
    const auto closeWindow = qScopeGuard([&] {
        closeFixAlerts(window);
        window.close();
    });
    QTemporaryDir dir;
    window.openFile(QStringLiteral(FOVELLE_TEST_FIXTURE_DIR) + "/remediation.gif");
    QTRY_VERIFY_WITH_TIMEOUT(window.getIsMovieLoaded(), 3000);
    auto *dialog = openFrameDialog(window);
    QVERIFY(dialog);
    const QImage expected = window.graphicsView->getLoadedMovie().currentImage();
    window.nextFrame();
    QVERIFY(window.graphicsView->getLoadedMovie().currentImage() != expected);
    const QString target = dir.filePath("snapshot.png");
    chooseFrameFile(dialog, target);
    QCOMPARE(QImage(target).convertToFormat(QImage::Format_RGBA8888),
             expected.convertToFormat(QImage::Format_RGBA8888));
}

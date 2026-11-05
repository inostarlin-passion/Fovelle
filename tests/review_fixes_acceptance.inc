// Atomic acceptance cases and their six-part specifications are in
// reports/test_case_specification.md (2026-11-05).
#include <csignal>
#include <cerrno>
namespace {
struct ReviewWindowScope
{
    MainWindow &window;
    bool quit = qvApp->quitOnLastWindowClosed();
    explicit ReviewWindowScope(MainWindow &value) : window(value)
    {
        qvApp->setQuitOnLastWindowClosed(false);
        window.setAttribute(Qt::WA_DeleteOnClose, false);
    }
    ~ReviewWindowScope()
    {
        closeFixAlerts(window);
        window.close();
        qvApp->setQuitOnLastWindowClosed(quit);
    }
};

class ReviewRenderer
{
public:
    QTemporaryDir directory;
    QByteArray previous = qgetenv("FOVELLE_GHOSTSCRIPT");
    bool hadPrevious = qEnvironmentVariableIsSet("FOVELLE_GHOSTSCRIPT");
    QString source;
    explicit ReviewRenderer(const QByteArray &body)
    {
        source = directory.filePath("test.eps");
        QFile eps(source);
        if (!eps.open(QIODevice::WriteOnly))
            qFatal("Cannot create EPS fixture");
        eps.write("%!PS-Adobe-3.0 EPSF-3.0\n%%BoundingBox: 0 0 20 20\nshowpage\n");
        eps.close();
        const auto path = directory.filePath("renderer");
        QFile script(path);
        if (!script.open(QIODevice::WriteOnly))
            qFatal("Cannot create renderer fixture");
        script.write("#!" + QStandardPaths::findExecutable("python3").toUtf8()
                     + "\nimport sys,os,time,pathlib,resource\n"
                       "p=pathlib.Path(next(a.split('=',1)[1] for a in sys.argv if "
                       "a.startswith('-sOutputFile=')))\n"
                       "src=pathlib.Path(sys.argv[-1])\n"
                       "src.with_suffix('.started').write_text(str(os.getpid())+'\\n'+str(p))\n"
                       "src.with_suffix('.limit').write_text(str(resource.getrlimit(resource."
                       "RLIMIT_FSIZE)[0]))\n"
                     + body);
        script.close();
        QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        qputenv("FOVELLE_GHOSTSCRIPT", path.toUtf8());
    }
    ~ReviewRenderer()
    {
        if (hadPrevious)
            qputenv("FOVELLE_GHOSTSCRIPT", previous);
        else
            qunsetenv("FOVELLE_GHOSTSCRIPT");
    }
    QString marker(const QString &suffix) const { return directory.filePath("test." + suffix); }
    QByteArray readMarker(const QString &suffix) const
    {
        QFile file(marker(suffix));
        if (!file.open(QIODevice::ReadOnly))
            return { };
        return file.readAll();
    }
    bool reaped() const
    {
        const auto pid = readMarker("started").split('\n').value(0).toInt();
        return pid > 0 && ::kill(pid, 0) == -1 && errno == ESRCH;
    }
};

QImage reviewPattern(const QSize &size)
{
    QImage image(size, QImage::Format_RGBA8888);
    image.setColorSpace(QColorSpace::SRgb);
    for (int y = 0; y < size.height(); ++y) {
        auto *row = image.scanLine(y);
        for (int x = 0; x < size.width(); ++x) {
            row[4 * x] = uchar((x * 3 + y) % 256);
            row[4 * x + 1] = uchar((y * 7 + x) % 256);
            row[4 * x + 2] = uchar((x + y * 11) % 256);
            row[4 * x + 3] = uchar((x + y) % 2 ? 128 : 255);
        }
    }
    return image;
}
} // namespace

void WindowBehaviorTests::testDeleteConfirmationKeepsTarget()
{
    for (const bool permanent : { true, false }) {
        QTemporaryDir directory;
        const auto a = createTestImage(directory, "confirmed-A", Qt::red);
        const auto b = createTestImage(directory, "visible-B", Qt::blue);
        MainWindow window;
        ReviewWindowScope scope(window);
        const auto trashCleanup = qScopeGuard([&] {
            for (const auto &deleted : std::as_const(window.lastDeletedFiles))
                QFile::remove(deleted.pathInTrash);
        });
        window.openFile(a);
        QTRY_VERIFY_WITH_TIMEOUT(window.getIsPixmapLoaded(), 2000);
        window.openFile(b);
        window.askDeleteFile(permanent);
        QPointer<QMessageBox> confirmation = window.findChild<QMessageBox *>();
        QVERIFY(confirmation);
        QVERIFY(confirmation->text().contains("confirmed-A"));
        QEventLoop loop;
        QTimer poll;
        connect(&poll, &QTimer::timeout, &loop, [&] {
            if (window.getCurrentFileDetails().fileInfo.absoluteFilePath() == b) {
                poll.stop();
                confirmation->button(QMessageBox::Yes)->click();
                loop.quit();
            }
        });
        poll.start(10);
        QTimer::singleShot(3000, &loop, &QEventLoop::quit);
        loop.exec();
        QVERIFY(!QFile::exists(a));
        QVERIFY(QFile::exists(b));
        QCOMPARE(window.getCurrentFileDetails().fileInfo.absoluteFilePath(), b);
        QVERIFY(window.getIsPixmapLoaded());
        QCOMPARE(window.lastDeletedFiles.size(), permanent ? 0 : 1);
        if (!permanent)
            QCOMPARE(window.lastDeletedFiles.top().previousPath, a);
    }
}

void WindowBehaviorTests::testDeleteConfirmationRejectsReplacement()
{
    QTemporaryDir directory;
    const auto a = createTestImage(directory, "target", Qt::red);
    MainWindow window;
    ReviewWindowScope scope(window);
    window.openFile(a);
    QTRY_VERIFY_WITH_TIMEOUT(window.getIsPixmapLoaded(), 2000);
    for (const bool replace : { true, false }) {
        const auto target = MainWindow::captureDeleteTarget(a);
        QVERIFY(target);
        if (replace) {
            QVERIFY(QFile::rename(a, a + ".old"));
            QImage other(31, 27, QImage::Format_RGB32);
            other.fill(Qt::green);
            QVERIFY(other.save(a, "PNG"));
        } else {
            QFile changed(a);
            QVERIFY(changed.open(QIODevice::Append));
            changed.write("different");
        }
        window.deleteTarget(*target, true);
        QVERIFY(QFile::exists(a));
        QVERIFY(fixAlertVisible(window));
        closeFixAlerts(window);
    }
    window.askDeleteFile(true);
    QMessageBox *confirmation = nullptr;
    for (auto *message : window.findChildren<QMessageBox *>()) {
        if (message->standardButtons() == (QMessageBox::Yes | QMessageBox::No))
            confirmation = message;
    }
    QVERIFY(confirmation);
    confirmation->done(QMessageBox::No);
    QVERIFY(QFile::exists(a));
}

void WindowBehaviorTests::testEPSOutputLimit()
{
    for (const bool slow : { false, true }) {
        ReviewRenderer renderer(
                QByteArray("with p.open('wb') as f:\n")
                + (slow ? QByteArray(" f.truncate(256*1024*1024-1)\n time.sleep(.1)\n")
                        : QByteArray())
                + " try: f.truncate(256*1024*1024+1)\n"
                  " except OSError:\n"
                  "  src.with_suffix('.peak').write_text(str(p.stat().st_size))\n"
                  "  raise\n"
                  "src.with_suffix('.after').write_text('escaped limit')\n"
                  "time.sleep(3)\n");
        QElapsedTimer timer;
        timer.start();
        const auto result = QVCocoaFunctions::readImageWithImageIO(renderer.source);
        QVERIFY(!result.errorString.isEmpty());
        QVERIFY(timer.elapsed() < 2500);
        QCOMPARE(renderer.readMarker("limit").trimmed().toLongLong(), 256LL * 1024 * 1024);
        QVERIFY(QFile::exists(renderer.marker("peak")));
        QVERIFY(renderer.readMarker("peak").toLongLong() <= 256LL * 1024 * 1024);
        QVERIFY(!QFile::exists(renderer.marker("after")));
        QVERIFY(renderer.reaped());
        const auto output = QString::fromUtf8(renderer.readMarker("started")).section('\n', 1);
        QVERIFY(!QFile::exists(QFileInfo(output).absolutePath()));
    }
}

void WindowBehaviorTests::testEPSDiagnosticLimit()
{
    for (const bool fast : { true, false }) {
        ReviewRenderer renderer(
                QByteArray("sys.stderr.write('E'*(4*1024*1024));sys.stderr.flush()\n")
                + (fast ? QByteArray() : QByteArray("time.sleep(3)\n"))
                + "src.with_suffix('.after').write_text('completed')\n");
        QElapsedTimer timer;
        timer.start();
        const auto result = QVCocoaFunctions::readImageWithImageIO(renderer.source);
        QVERIFY(result.errorString.contains("diagnostic", Qt::CaseInsensitive));
        QVERIFY(result.errorString.size() < 65536 + 256);
        QVERIFY(timer.elapsed() < 2500);
        QVERIFY(renderer.reaped());
        if (!fast)
            QVERIFY(!QFile::exists(renderer.marker("after")));
    }
}

void WindowBehaviorTests::testEPSResourceCompatibility()
{
    QTemporaryDir directory;
    const auto path = directory.filePath("valid.eps");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("%!PS-Adobe-3.0 EPSF-3.0\n%%BoundingBox: 0 0 20 20\n1 0 0 setrgbcolor 0 0 20 20 "
               "rectfill showpage\n");
    file.close();
    const auto valid = QVCocoaFunctions::readImageWithImageIO(path);
    QVERIFY2(valid.errorString.isEmpty(), qPrintable(valid.errorString));
    QVERIFY(valid.vectorImage.isValid());
    QVERIFY(valid.image.convertedToColorSpace(QColorSpace::SRgb).pixelColor(10, 10).redF() > .9);
    {
        ReviewRenderer renderer("sys.stderr.write('short useful failure');sys.exit(3)\n");
        const auto result = QVCocoaFunctions::readImageWithImageIO(renderer.source);
        QVERIFY(result.errorString.contains("short useful failure"));
        QVERIFY(renderer.reaped());
    }
    {
        ReviewRenderer renderer("time.sleep(40)\n");
        std::atomic<bool> delivered{ false };
        const auto ticket = QvAsync::run(
                [source = renderer.source] {
                    return QVCocoaFunctions::readImageWithImageIO(source);
                },
                [&](const auto &) { delivered = true; });
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(renderer.marker("started")), 2000);
        QvAsync::cancel(ticket);
        QVERIFY(QvAsync::drain(1500));
        QCoreApplication::processEvents();
        QVERIFY(!delivered);
        QVERIFY(renderer.reaped());
        const auto output = QString::fromUtf8(renderer.readMarker("started")).section('\n', 1);
        QVERIFY(!QFile::exists(QFileInfo(output).absolutePath()));
    }
    {
        ReviewRenderer renderer("time.sleep(40)\n");
        QElapsedTimer timer;
        timer.start();
        const auto result = QVCocoaFunctions::readImageWithImageIO(renderer.source);
        QVERIFY(result.errorString.contains("timed out"));
        QVERIFY(timer.elapsed() >= 29000 && timer.elapsed() < 35000);
        QVERIFY(renderer.reaped());
    }
}

void WindowBehaviorTests::testClipboardFullResolution()
{
    QTemporaryDir directory;
    const auto path = directory.filePath("full.png");
    const auto expected = reviewPattern(QSize(4000, 3000));
    QVERIFY(expected.save(path));
    MainWindow window;
    ReviewWindowScope scope(window);
    window.openFile(path);
    QTRY_VERIFY_WITH_TIMEOUT(window.getIsPixmapLoaded(), 5000);
    QVERIFY(QFile::remove(path)); // Copy must use the loaded authoritative snapshot.
    std::unique_ptr<QMimeData> mime(window.graphicsView->getMimeData());
    const auto image = qvariant_cast<QImage>(mime->imageData());
    QCOMPARE(image.size(), expected.size());
    for (const int y : { 0, 2045, 2047, 2048, 2049, 2999 }) {
        for (const int x : { 0, 2045, 2047, 2048, 2049, 3999 }) {
            const auto actual = image.pixelColor(x, y);
            const auto wanted = expected.pixelColor(x, y);
            QVERIFY(qAbs(actual.red() - wanted.red()) <= 2);
            QVERIFY(qAbs(actual.green() - wanted.green()) <= 2);
            QVERIFY(qAbs(actual.blue() - wanted.blue()) <= 2);
            QCOMPARE(actual.alpha(), wanted.alpha());
        }
    }
    QImage precise(4, 3, QImage::Format_RGBA64);
    precise.fill(QColor::fromRgbF(.1234, .4, .6, .5));
    precise.setColorSpace(QColorSpace::DisplayP3);
    const auto native = QVCocoaFunctions::createSDRImage(precise, true);
    QCOMPARE(QVCocoaFunctions::copySDRImage(native), precise);
}

void WindowBehaviorTests::testClipboardRepresentations()
{
    QTemporaryDir directory;
    TestableImageCore core;
    QVImageLoader::Result result;
    result.absoluteFilePath = directory.filePath("plain.xpm");
    result.image = reviewPattern(QSize(40, 30));
    core.loadPixmap(result);
    QCOMPARE(core.clipboardImage().size(), result.image.size());
    const auto svgPath = directory.filePath("vector.svg");
    QFile svg(svgPath);
    QVERIFY(svg.open(QIODevice::WriteOnly));
    svg.write("<svg xmlns='http://www.w3.org/2000/svg' width='800' height='600'><rect width='800' "
              "height='600' fill='red'/></svg>");
    svg.close();
    core.loadFile(svgPath);
    QTRY_VERIFY_WITH_TIMEOUT(core.getCurrentFileDetails().isVectorLoaded, 3000);
    QCOMPARE(core.clipboardImage().size(), QSize(800, 600));
    QVERIFY(core.clipboardImage().pixelColor(400, 300).red() > 240);
    const auto gif = QString::fromUtf8(FOVELLE_TEST_FIXTURE_DIR) + "/remediation.gif";
    core.loadFile(gif);
    QTRY_VERIFY_WITH_TIMEOUT(core.getCurrentFileDetails().isMovieLoaded, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(core.getLoadedMovie().currentFrameNumber() >= 0, 2000);
    core.setPaused(true);
    core.jumpToNextFrame();
    QCOMPARE(core.clipboardImage(), core.getLoadedMovie().currentImage());
    const auto hdrPath = QString::fromUtf8(FOVELLE_TEST_FIXTURE_DIR) + "/portable-hdr/gain-map.jpg";
    core.loadFile(hdrPath);
    QTRY_VERIFY_WITH_TIMEOUT(core.getCurrentFileDetails().isNativeHDRLoaded, 5000);
    QCOMPARE(core.clipboardImage().size(), core.getCurrentFileDetails().baseImageSize);
    const auto epsPath = directory.filePath("vector.eps");
    QFile eps(epsPath);
    QVERIFY(eps.open(QIODevice::WriteOnly));
    eps.write("%!PS-Adobe-3.0 EPSF-3.0\n%%BoundingBox: 0 0 80 60\n0 1 0 setrgbcolor 0 0 80 60 "
              "rectfill showpage\n");
    eps.close();
    core.loadFile(epsPath);
    QTRY_VERIFY_WITH_TIMEOUT(core.getCurrentFileDetails().isVectorLoaded, 3000);
    QCOMPARE(core.clipboardImage().size(), QSize(80, 60));
    core.closeImage();
    QVERIFY(core.clipboardImage().isNull());
    QApplication::clipboard()->setText("review clipboard sentinel");
    MainWindow empty;
    ReviewWindowScope scope(empty);
    empty.copy();
    QCOMPARE(QApplication::clipboard()->text(), QString("review clipboard sentinel"));
    QApplication::clipboard()->clear();
}

void WindowBehaviorTests::testDownloadedSourcesRetire()
{
    RemediationHttpServer server;
    QImage expected = reviewPattern(QSize(32, 24));
    QBuffer buffer(&server.body);
    QVERIFY(buffer.open(QIODevice::WriteOnly));
    QVERIFY(expected.save(&buffer, "PNG"));
    MainWindow window;
    ReviewWindowScope scope(window);
    QStringList paths;
    for (int i = 0; i < 3; ++i) {
        window.openUrl(server.url());
        QTRY_VERIFY_WITH_TIMEOUT(
                window.getIsPixmapLoaded()
                        && !paths.contains(
                                window.getCurrentFileDetails().fileInfo.absoluteFilePath()),
                3000);
        const auto current = window.getCurrentFileDetails().fileInfo.absoluteFilePath();
        QVERIFY(QFile::exists(current));
        for (const auto &previous : std::as_const(paths))
            QTRY_VERIFY_WITH_TIMEOUT(!QFile::exists(previous), 2000);
        paths.append(current);
    }
    QTemporaryDir directory;
    window.openFile(createTestImage(directory, "local", Qt::blue));
    QTRY_VERIFY_WITH_TIMEOUT(window.getCurrentFileDetails().fileInfo.fileName().startsWith("local"),
                             3000);
    for (const auto &path : std::as_const(paths))
        QTRY_VERIFY_WITH_TIMEOUT(!QFile::exists(path), 2000);
}

void WindowBehaviorTests::testSourceLeaseAsyncLifetime()
{
    for (const bool destroy : { false, true }) {
        auto lease = std::make_shared<QTemporaryDir>();
        const auto directory = lease->path();
        const auto path = lease->filePath("source.png");
        QVERIFY(reviewPattern(QSize(20, 20)).save(path));
        auto gate = std::make_shared<DecodeGate>();
        auto readable = std::make_shared<std::atomic<bool>>(false);
        const auto release = qScopeGuard([gate] { gate->release = true; });
        auto loader = std::make_unique<QVImageLoader>(
                nullptr, [gate, readable](const QString &source, int, bool) {
                    gate->entered = true;
                    while (!gate->release)
                        QThread::msleep(2);
                    *readable = QFile::exists(source);
                    return QVImageLoader::Result{ };
                });
        QSignalSpy delivered(loader.get(), &QVImageLoader::imageReady);
        loader->requestImage(path, false, lease);
        lease.reset();
        QTRY_VERIFY_WITH_TIMEOUT(gate->entered.load(), 2000);
        if (destroy)
            loader.reset();
        else
            loader->clear();
        QVERIFY(QFile::exists(path));
        gate->release = true;
        QVERIFY(QvAsync::drain(1500));
        QCoreApplication::processEvents();
        QVERIFY(readable->load());
        QCOMPARE(delivered.size(), 0);
        QTRY_VERIFY_WITH_TIMEOUT(!QFile::exists(directory), 2000);
    }
    auto lease = std::make_shared<QTemporaryDir>();
    const auto directory = lease->path();
    const auto gif = lease->filePath("movie.gif");
    QVERIFY(QFile::copy(QString::fromUtf8(FOVELLE_TEST_FIXTURE_DIR) + "/remediation.gif", gif));
    auto entered = std::make_shared<QSemaphore>();
    auto release = std::make_shared<QSemaphore>();
    const auto unblock = qScopeGuard([release] { release->release(4); });
    for (int i = 0; i < 4; ++i)
        QvAsync::schedule([entered, release] {
            entered->release();
            release->acquire();
        });
    QTRY_COMPARE_WITH_TIMEOUT(entered->available(), 4, 2000);
    QVMovie movie;
    movie.setFileName(gif, lease);
    movie.setAsyncPlayback(true);
    movie.start();
    lease.reset();
    movie.setFileName("");
    QVERIFY(QFile::exists(gif)); // The canceled queued decoder still owns its work closure.
    release->release(4);
    QVERIFY(QvAsync::drain(1500));
    QTRY_VERIFY_WITH_TIMEOUT(!QFile::exists(directory), 2000);
}

void WindowBehaviorTests::testSourceLeaseConsumers()
{
    for (const bool hdr : { false, true }) {
        auto lease = std::make_shared<QTemporaryDir>();
        const auto directory = lease->path();
        const auto path = lease->filePath(hdr ? "source.jpg" : "source.png");
        if (hdr)
            QVERIFY(QFile::copy(QString::fromUtf8(FOVELLE_TEST_FIXTURE_DIR)
                                        + "/portable-hdr/gain-map.jpg",
                                path));
        else
            QVERIFY(reviewPattern(QSize(40, 30)).save(path));
        QVImageLoader loader;
        QVImageLoader::Result loaded;
        connect(&loader, &QVImageLoader::imageReady, &loader,
                [&](quint64, const auto &result) { loaded = result; });
        loader.requestImage(path, false, lease);
        lease.reset();
        QTRY_VERIFY_WITH_TIMEOUT(hdr ? loaded.hdrImage != nullptr : loaded.sdrImage != nullptr,
                                 3000);
        auto native = loaded.sdrImage;
        auto nativeHDR = loaded.hdrImage;
        loaded = { };
        loader.clear();
        QVERIFY(QFile::exists(path));
        QVERIFY(!(hdr ? QVCocoaFunctions::copyHDRImage(nativeHDR)
                      : QVCocoaFunctions::copySDRImage(native))
                         .isNull());
        native.reset();
        nativeHDR.reset();
        QTRY_VERIFY_WITH_TIMEOUT(!QFile::exists(directory), 2000);
    }

    RemediationHttpServer server;
    QBuffer buffer(&server.body);
    QVERIFY(buffer.open(QIODevice::WriteOnly));
    QVERIFY(reviewPattern(QSize(40, 30)).save(&buffer, "PNG"));
    auto entered = std::make_shared<std::atomic<bool>>(false);
    auto release = std::make_shared<std::atomic<bool>>(false);
    auto readable = std::make_shared<std::atomic<bool>>(false);
    const auto unblock = qScopeGuard([release] { *release = true; });
    MainWindow window(nullptr, { }, false, [entered, release, readable](const QString &source) {
        *entered = true;
        while (!*release)
            QThread::msleep(2);
        *readable = QFile::exists(source);
        return QList<OpenWith::OpenWithItem>{ };
    });
    ReviewWindowScope scope(window);
    window.openUrl(server.url());
    QTRY_VERIFY_WITH_TIMEOUT(window.getIsPixmapLoaded(), 3000);
    const auto download = window.getCurrentFileDetails().fileInfo.absoluteFilePath();
    std::unique_ptr<QMimeData> mime(window.graphicsView->getMimeData());
    QTemporaryDir local;
    window.openFile(createTestImage(local, "local", Qt::blue));
    QTRY_VERIFY_WITH_TIMEOUT(window.getCurrentFileDetails().fileInfo.fileName().startsWith("local"),
                             2000);
    QVERIFY(QFile::exists(download));
    mime.reset();
    QTRY_VERIFY_WITH_TIMEOUT(!QFile::exists(download), 2000);

    window.openUrl(server.url());
    QTRY_VERIFY_WITH_TIMEOUT(
            window.getCurrentFileDetails().fileInfo.absoluteFilePath().contains("FovelleDownload-"),
            2000);
    const auto secondDownload = window.getCurrentFileDetails().fileInfo.absoluteFilePath();
    window.requestPopulateOpenWithMenu();
    QTRY_VERIFY_WITH_TIMEOUT(entered->load(), 2000);
    window.openFile(createTestImage(local, "another-local", Qt::green));
    QTRY_VERIFY_WITH_TIMEOUT(
            window.getCurrentFileDetails().fileInfo.fileName().startsWith("another-local"), 2000);
    QVERIFY(QFile::exists(secondDownload)); // Only Open With consumes this retired source.
    *release = true;
    QTRY_VERIFY_WITH_TIMEOUT(readable->load(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(!QFile::exists(secondDownload), 2000);

    const auto before = QDir(QDir::tempPath()).entryList({ "FovelleDownload-*" }, QDir::Dirs);
    server.status = "404 Not Found";
    UrlDownload failed;
    QSignalSpy failure(&failed, &UrlDownload::failed);
    failed.start(server.url());
    QTRY_COMPARE_WITH_TIMEOUT(failure.size(), 1, 2000);
    server.stalled = true;
    UrlDownload canceled;
    canceled.start(server.url());
    canceled.cancel();
    QCOMPARE(QDir(QDir::tempPath()).entryList({ "FovelleDownload-*" }, QDir::Dirs), before);
}

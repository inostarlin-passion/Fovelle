#pragma once
// R1-R20 behavioral regressions. Included in the isolated Qt test executable.
#include "urldownload.h"
#include "asyncwork.h"
#include <QTcpServer>
#include <QTcpSocket>
#include <QNetworkReply>
#include <QCryptographicHash>
#include <QSemaphore>

class RemediationHttpServer : public QTcpServer
{
public:
    QByteArray body;
    QByteArray status = "200 OK";
    QByteArray headers;
    bool stalled = false;
    bool dripping = false;
    bool chunked = false;
    explicit RemediationHttpServer(QObject *parent = nullptr) : QTcpServer(parent)
    {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections()) {
                auto *socket = nextPendingConnection();
                socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                    socket->readAll();
                    if (socket->property("answered").toBool())
                        return;
                    socket->setProperty("answered", true);
                    if (stalled)
                        return;
                    socket->write("HTTP/1.1 " + status + "\r\nConnection: close\r\n" + headers
                                  + (chunked ? QByteArray("Transfer-Encoding: chunked\r\n")
                                             : QByteArray())
                                  + "\r\n");
                    if (dripping) {
                        socket->write("x");
                        socket->flush();
                        auto *timer = new QTimer(socket);
                        timer->setTimerType(Qt::PreciseTimer);
                        connect(timer, &QTimer::timeout, socket, [socket] {
                            socket->write("x");
                            socket->flush();
                        });
                        timer->start(30);
                    } else {
                        socket->write(chunked ? QByteArray::number(body.size(), 16) + "\r\n" + body
                                                      + "\r\n0\r\n\r\n"
                                              : body);
                        socket->disconnectFromHost();
                    }
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
        if (!listen(QHostAddress::LocalHost))
            qFatal("Cannot start loopback HTTP fixture");
    }
    QUrl url(const QString &suffix = "png") const
    {
        return QUrl(QString("http://127.0.0.1:%1/image.%2").arg(serverPort()).arg(suffix));
    }
};

// Observe the same frame's timer before its single-shot event is consumed.
// Polling isActive() can instead observe an overdue timer or a later frame.
class MovieTimerObservation : public QObject
{
public:
    QVMovie &movie;
    bool &pauseOnFrame;
    int deliveries = 0;
    int interval = -1;
    int remaining = -1;
    bool valid = true;

    MovieTimerObservation(QVMovie &movie, bool &pauseOnFrame)
        : movie(movie), pauseOnFrame(pauseOnFrame)
    {
    }

    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (event->type() == QEvent::Timer) {
            auto *timer = qobject_cast<QTimer *>(object);
            interval = timer->interval();
            remaining = timer->remainingTime();
            valid = valid && timer->isActive() && remaining >= 0 && remaining <= interval
                    && interval <= movie.nextFrameDelay();
            ++deliveries;
            pauseOnFrame = true;
        }
        return false;
    }
};

class RemediationTests : public QObject
{
    Q_OBJECT
private slots:
    void testDownloadThreadAffinity()
    {
        RemediationHttpServer server;
        server.body = "original bytes";
        UrlDownload download;
        QSignalSpy done(&download, &UrlDownload::succeeded);
        connect(&download, &UrlDownload::progress, &download, [&] {
            QCOMPARE(QThread::currentThread(), download.thread());
            for (auto *reply : download.findChildren<QNetworkReply *>())
                QCOMPARE(reply->thread(), download.thread());
        });
        download.start(server.url());
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 2000);
        const auto artifact = qvariant_cast<UrlDownload::Artifact>(done.at(0).at(0));
        QFile file(artifact.path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), server.body);
    }
    void testDownloadPreservesSources()
    {
        QImage tagged(4, 4, QImage::Format_ARGB32);
        tagged.fill(Qt::red);
        tagged.setColorSpace(QColorSpace::DisplayP3);
        tagged.setText("Author", "Fovelle regression");
        QByteArray taggedBytes;
        QBuffer taggedBuffer(&taggedBytes);
        taggedBuffer.open(QIODevice::WriteOnly);
        QVERIFY(tagged.save(&taggedBuffer, "png"));
        QList<QPair<QByteArray, QString>> sources{
            { taggedBytes, "png" },
            { QByteArray::fromBase64(tinyAnimatedPngBase64), "png" },
            { "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"20\" height=\"10\"><rect "
              "width=\"20\" height=\"10\" fill=\"red\"/></svg>",
              "svg" }
        };
        for (const QString &name : { QString("gain-map.jpg"), QString("bayer.dng") }) {
            QFile fixture(QString::fromUtf8(FOVELLE_TEST_FIXTURE_DIR) + "/portable-hdr/" + name);
            QVERIFY(fixture.open(QIODevice::ReadOnly));
            sources.append({ fixture.readAll(), name.section('.', -1) });
        }
        for (const auto &source : sources) {
            RemediationHttpServer server;
            server.body = source.first;
            UrlDownload download;
            QSignalSpy done(&download, &UrlDownload::succeeded);
            download.start(server.url(source.second));
            QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 2000);
            auto artifact = qvariant_cast<UrlDownload::Artifact>(done.at(0).at(0));
            QFile file(artifact.path);
            QVERIFY(file.open(QIODevice::ReadOnly));
            QCOMPARE(file.readAll(), source.first);
            QVImageLoader loader;
            QSignalSpy loaded(&loader, &QVImageLoader::imageReady);
            // This is a format-preservation check, not a cold-start benchmark.
            // ImageIO/Core Image can initialize codecs and kernels on first use.
            const int decodeTimeout = source.second == "svg" ? 5000 : 15000;
            QElapsedTimer decodeTimer;
            decodeTimer.start();
            qInfo() << "URL_SOURCE_DECODE" << source.second << "bytes=" << source.first.size()
                    << "budget_ms=" << decodeTimeout;
            loader.requestImage(artifact.path);
            QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, decodeTimeout);
            const auto result = qvariant_cast<QVImageLoader::Result>(loaded.at(0).at(1));
            qInfo() << "URL_SOURCE_DECODE_COMPLETE" << source.second
                    << "wait_ms=" << decodeTimer.elapsed()
                    << "decode_ms=" << result.decodeMilliseconds;
            QVERIFY(!result.errorData);
            if (source.second == "svg")
                QVERIFY(result.vectorImage.isValid());
            else if (source.first == QByteArray::fromBase64(tinyAnimatedPngBase64)) {
                QVMovie movie(artifact.path);
                QCOMPARE(movie.frameCount(), 2);
            } else if (source.second == "png") {
                const QImage image(artifact.path);
                QCOMPARE(image.colorSpace(), QColorSpace(QColorSpace::DisplayP3));
                QCOMPARE(image.text("Author"), QString("Fovelle regression"));
            } else {
                QVERIFY(result.hdrImage);
                QCOMPARE(result.sourceColor.isRaw, source.second == "dng");
            }
        }
    }
    void testWindowUrlAnimationAndArtifactLifetime()
    {
        RemediationHttpServer server;
        server.body = QByteArray::fromBase64(tinyAnimatedPngBase64);
        QString downloadedPath;
        {
            MainWindow window;
            window.setAttribute(Qt::WA_DeleteOnClose, false);
            window.resize(640, 480);
            window.show();
            window.openUrl(server.url());
            QTRY_VERIFY_WITH_TIMEOUT(window.getIsMovieLoaded(), 4000);
            auto *view = window.findChild<QVGraphicsView *>("graphicsView");
            QVERIFY(view);
            auto *core = view->findChild<QVImageCore *>();
            QVERIFY(core);
            QCOMPARE(core->getLoadedMovie().frameCount(), 2);
            downloadedPath = window.getCurrentFileDetails().fileInfo.absoluteFilePath();
            QVERIFY(QFile::exists(downloadedPath));
            QFile original(downloadedPath);
            QVERIFY(original.open(QIODevice::ReadOnly));
            QCOMPARE(original.readAll(), server.body);
            window.close();
        }
        QVERIFY(!QFile::exists(downloadedPath));
    }
    void testDownloadCleanup()
    {
        QString path;
        for (int i = 0; i < 10; ++i) {
            RemediationHttpServer server;
            server.body = "bytes";
            UrlDownload download;
            QSignalSpy done(&download, &UrlDownload::succeeded);
            download.start(server.url());
            QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 2000);
            path = qvariant_cast<UrlDownload::Artifact>(done.at(0).at(0)).path;
            QVERIFY(QFile::exists(path));
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QCOMPARE(download.findChildren<QNetworkReply *>().size(), 0);
            done.clear();
        }
        QVERIFY(!QFile::exists(path));
        RemediationHttpServer server;
        server.stalled = true;
        UrlDownload download;
        QSignalSpy canceled(&download, &UrlDownload::canceled);
        download.start(server.url());
        download.cancel();
        QCOMPARE(canceled.count(), 1);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCOMPARE(download.findChildren<QNetworkReply *>().size(), 0);
    }
    void testDownloadHttpFailureCleanup()
    {
        const auto directories = [] {
            return QDir(QDir::tempPath())
                    .entryList({ "FovelleDownload-*" }, QDir::Dirs | QDir::NoDotAndDotDot);
        };
        const auto baseline = directories();
        RemediationHttpServer server;
        server.status = "404 Not Found";
        server.body = "error body";
        UrlDownload download;
        QSignalSpy failed(&download, &UrlDownload::failed);
        download.start(server.url());
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 2000);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCOMPARE(download.findChildren<QNetworkReply *>().size(), 0);
        QCOMPARE(directories(), baseline);
    }
    void testDownloadIdleTimeout()
    {
        RemediationHttpServer server;
        server.stalled = true;
        UrlDownload::Policy policy;
        policy.idleTimeoutMs = 100;
        policy.totalTimeoutMs = 1000;
        UrlDownload download(policy);
        QSignalSpy failed(&download, &UrlDownload::failed);
        QElapsedTimer timer;
        timer.start();
        download.start(server.url());
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 1500);
        QVERIFY(timer.elapsed() < 800);
        QVERIFY(failed.at(0).at(0).toString().contains("idle", Qt::CaseInsensitive));
    }
    void testDownloadTranslatedFailure()
    {
#ifdef FOVELLE_TRANSLATIONS_DIR
        QTranslator translator;
        QVERIFY(translator.load(QString::fromUtf8(FOVELLE_TRANSLATIONS_DIR) + "/qview_zh_Hans.qm"));
        QCoreApplication::installTranslator(&translator);
        const auto restore = qScopeGuard([&] { QCoreApplication::removeTranslator(&translator); });
        RemediationHttpServer server;
        server.stalled = true;
        UrlDownload::Policy policy;
        policy.idleTimeoutMs = 50;
        UrlDownload download(policy);
        QSignalSpy failed(&download, &UrlDownload::failed);
        download.start(server.url());
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 1000);
        QCOMPARE(failed.at(0).at(0).toString(), QString::fromUtf8("下载等待数据超时"));
#else
        QCOMPARE(QCoreApplication::translate("UrlDownload", "Download idle timeout"),
                 QString("Download idle timeout"));
#endif
    }
    void testScopedHDRDisplayRestoresEnvironment()
    {
        const bool had = qEnvironmentVariableIsSet("FOVELLE_TEST_DISPLAY_HEADROOM");
        const QByteArray original = qgetenv("FOVELLE_TEST_DISPLAY_HEADROOM");
        const bool hadCurrent = qEnvironmentVariableIsSet("FOVELLE_TEST_DISPLAY_CURRENT_HEADROOM");
        const QByteArray originalCurrent = qgetenv("FOVELLE_TEST_DISPLAY_CURRENT_HEADROOM");
        const auto restore = qScopeGuard([&] {
            if (had)
                qputenv("FOVELLE_TEST_DISPLAY_HEADROOM", original);
            else
                qunsetenv("FOVELLE_TEST_DISPLAY_HEADROOM");
            if (hadCurrent)
                qputenv("FOVELLE_TEST_DISPLAY_CURRENT_HEADROOM", originalCurrent);
            else
                qunsetenv("FOVELLE_TEST_DISPLAY_CURRENT_HEADROOM");
        });
        const QList<QPair<bool, QByteArray>> values{
            { false, { } }, { true, { } }, { true, "1" }, { true, "2.75" }
        };
        for (const auto &potentialBefore : values)
            for (const auto &currentBefore : values) {
                if (potentialBefore.first)
                    qputenv("FOVELLE_TEST_DISPLAY_HEADROOM", potentialBefore.second);
                else
                    qunsetenv("FOVELLE_TEST_DISPLAY_HEADROOM");
                if (currentBefore.first)
                    qputenv("FOVELLE_TEST_DISPLAY_CURRENT_HEADROOM", currentBefore.second);
                else
                    qunsetenv("FOVELLE_TEST_DISPLAY_CURRENT_HEADROOM");
                {
                    ScopedHDRTestDisplay display;
                    QCOMPARE(qgetenv("FOVELLE_TEST_DISPLAY_HEADROOM"), QByteArray("4"));
                    QCOMPARE(qgetenv("FOVELLE_TEST_DISPLAY_CURRENT_HEADROOM"), QByteArray("4"));
                }
                QCOMPARE(qEnvironmentVariableIsSet("FOVELLE_TEST_DISPLAY_HEADROOM"),
                         potentialBefore.first);
                QCOMPARE(qgetenv("FOVELLE_TEST_DISPLAY_HEADROOM"), potentialBefore.second);
                QCOMPARE(qEnvironmentVariableIsSet("FOVELLE_TEST_DISPLAY_CURRENT_HEADROOM"),
                         currentBefore.first);
                QCOMPARE(qgetenv("FOVELLE_TEST_DISPLAY_CURRENT_HEADROOM"), currentBefore.second);
            }
    }
    void testDownloadProgressKeepsIdleAlive()
    {
        RemediationHttpServer server;
        server.dripping = true;
        UrlDownload::Policy policy;
        policy.idleTimeoutMs = 350;
        policy.totalTimeoutMs = 3000;
        UrlDownload download(policy);
        QSignalSpy failed(&download, &UrlDownload::failed);
        QSignalSpy progress(&download, &UrlDownload::progress);
        download.start(server.url());
        QTRY_VERIFY_WITH_TIMEOUT(progress.count() >= 2, 1500);
        QTest::qWait(policy.idleTimeoutMs * 2);
        QCOMPARE(failed.count(), 0);
        QVERIFY(progress.count() >= 3);
        download.cancel();
    }
    void testDownloadTotalTimeout()
    {
        RemediationHttpServer server;
        server.dripping = true;
        UrlDownload::Policy policy;
        policy.idleTimeoutMs = 2000;
        policy.totalTimeoutMs = 600;
        QVERIFY(policy.totalTimeoutMs < policy.idleTimeoutMs);
        UrlDownload download(policy);
        QSignalSpy failed(&download, &UrlDownload::failed);
        QSignalSpy progress(&download, &UrlDownload::progress);
        QElapsedTimer timer;
        timer.start();
        download.start(server.url());
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 3000);
        QVERIFY2(progress.count() >= 2,
                 "The total-deadline case must observe actual streaming progress");
        QVERIFY(download.receivedBytes() > 0);
        QVERIFY(timer.elapsed() < 2000);
        QCOMPARE(failed.at(0).at(0).toString(),
                 QCoreApplication::translate("UrlDownload", "Download total timeout"));
    }
    void testDownloadLimits()
    {
        for (bool chunked : { false, true })
            for (int length : { 1024, 1025 }) {
                RemediationHttpServer server;
                server.body = QByteArray(length, 'x');
                server.chunked = chunked;
                UrlDownload::Policy policy;
                policy.maxBytes = 1024;
                UrlDownload download(policy);
                QSignalSpy done(&download, &UrlDownload::succeeded);
                QSignalSpy failed(&download, &UrlDownload::failed);
                download.start(server.url());
                QTRY_VERIFY_WITH_TIMEOUT(done.count() + failed.count() == 1, 2000);
                QCOMPARE(done.count(), length == 1024 ? 1 : 0);
                QCOMPARE(failed.count(), length == 1025 ? 1 : 0);
                QVERIFY(download.receivedBytes() <= 1024);
            }
        RemediationHttpServer server;
        server.headers = "Content-Length: 1025\r\n";
        UrlDownload::Policy policy;
        policy.maxBytes = 1024;
        UrlDownload download(policy);
        QSignalSpy failed(&download, &UrlDownload::failed);
        download.start(server.url());
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 2000);
        QCOMPARE(download.receivedBytes(), 0);
    }
    void testDownloadProgressAndCancel()
    {
        RemediationHttpServer server;
        server.dripping = true;
        UrlDownload download;
        QSignalSpy done(&download, &UrlDownload::succeeded);
        QSignalSpy failed(&download, &UrlDownload::failed);
        QSignalSpy canceled(&download, &UrlDownload::canceled);
        download.start(server.url());
        QTest::qWait(80);
        download.cancel();
        download.cancel();
        QTest::qWait(100);
        QCOMPARE(canceled.count(), 1);
        QCOMPARE(done.count(), 0);
        QCOMPARE(failed.count(), 0);
        // Exercise the actual MainWindow route; cancel must not open a native error dialog.
        MainWindow window;
        window.setAttribute(Qt::WA_DeleteOnClose, false);
        window.openUrl(server.url());
        auto *job = window.findChild<UrlDownload *>();
        QVERIFY(job);
        job->cancel();
        QTest::qWait(30);
        QVERIFY(!QApplication::activeModalWidget());
    }
    void testFolderScanAsync()
    {
        QTemporaryDir directory;
        for (const QString &name : { "10.png", "2.png", "1.png" })
            QVERIFY(QImage(2, 2, QImage::Format_ARGB32).save(directory.filePath(name)));
        QVFileEnumerator enumerator;
        bool finished = false;
        enumerator.scanAsync(directory.path(), this, [&](auto files) {
            QCOMPARE(QThread::currentThread(), thread());
            QCOMPARE(files.size(), 3);
            QCOMPARE(QFileInfo(files.at(0).absoluteFilePath).fileName(), QString("1.png"));
            QCOMPARE(QFileInfo(files.at(1).absoluteFilePath).fileName(), QString("2.png"));
            finished = true;
        });
        QVERIFY(!finished);
        QTRY_VERIFY_WITH_TIMEOUT(finished, 3000);
    }
    void testFolderScanGeneration()
    {
        QTemporaryDir a, b;
        QVERIFY(QImage(2, 2, QImage::Format_ARGB32).save(a.filePath("a.png")));
        QVERIFY(QImage(2, 2, QImage::Format_ARGB32).save(b.filePath("b.png")));
        QVImageCore core;
        core.loadFile(a.path());
        core.loadFile(b.path());
        QTRY_COMPARE_WITH_TIMEOUT(core.getCurrentFileDetails().fileInfo.absoluteFilePath(),
                                  b.filePath("b.png"), 4000);
        QTest::qWait(100);
        QCOMPARE(core.getCurrentFileDetails().folderFileInfoList.getBaseDir(), b.path());
        core.closeImage();
        QTest::qWait(50);
        QVERIFY(!core.hasFileOrPendingLoad());
    }
    void testDirectoryOpenAndNavigation()
    {
        QTemporaryDir directory;
        QVERIFY(QImage(2, 2, QImage::Format_ARGB32).save(directory.filePath("1.png")));
        QVERIFY(QImage(2, 2, QImage::Format_ARGB32).save(directory.filePath("2.png")));
        QVImageCore core;
        core.loadFile(directory.path());
        QTRY_VERIFY_WITH_TIMEOUT(core.getCurrentFileDetails().isPixmapLoaded, 4000);
        QTRY_COMPARE_WITH_TIMEOUT(core.getCurrentFileDetails().folderFileInfoList.size(), 2, 4000);
        core.goToFile(Qv::GoToFileMode::Next);
        QTRY_COMPARE_WITH_TIMEOUT(core.getCurrentFileDetails().fileInfo.fileName(),
                                  QString("2.png"), 4000);
    }
    void testMovieBoundedCache()
    {
        QTemporaryDir dir;
        const auto path = createBase64Image(dir, "movie", "png", tinyAnimatedPngBase64);
        QVMovie movie(path);
        movie.setCacheMode(QVMovie::CacheAll);
        movie.setCacheByteLimit(16);
        QVERIFY(movie.jumpToFrame(0));
        const auto first = movie.currentImage();
        QVERIFY(movie.jumpToFrame(1));
        QVERIFY(movie.cachedBytes() <= 16);
        QVERIFY(movie.jumpToFrame(0));
        QCOMPARE(movie.currentImage(), first);
        QVERIFY(movie.cachedBytes() <= 16);
        movie.setCacheByteLimit(1);
        QVERIFY(movie.jumpToFrame(1));
        QCOMPARE(movie.cachedBytes(), quint64(0));
        QVMovie gif(QString::fromUtf8(FOVELLE_TEST_FIXTURE_DIR) + "/remediation.gif");
        gif.setCacheMode(QVMovie::CacheAll);
        gif.setCacheByteLimit(1024);
        QCOMPARE(gif.frameCount(), 4);
        QVERIFY(gif.jumpToFrame(0));
        const auto initial = gif.currentImage();
        QVERIFY(gif.jumpToFrame(3));
        QVERIFY(gif.cachedBytes() <= 1024);
        QVERIFY(gif.jumpToFrame(0));
        QCOMPARE(gif.currentImage(), initial);
    }
    void testMoviePauseResume_data()
    {
        QTest::addColumn<bool>("async");
        QTest::addColumn<bool>("apng");
        QTest::addColumn<bool>("boundedCache");
        for (bool async : {false, true})
            for (bool apng : {false, true})
                for (bool cache : {false, true})
                    QTest::newRow(qPrintable(QString("async=%1-apng=%2-cache=%3")
                            .arg(async).arg(apng).arg(cache))) << async << apng << cache;
    }
    void testMoviePauseResume()
    {
        QFETCH(bool, async);
        QFETCH(bool, apng);
        QFETCH(bool, boundedCache);
        QTemporaryDir dir;
        const auto path = apng
            ? createBase64Image(dir, "resume", "png", tinyAnimatedPngBase64)
            : QString::fromUtf8(FOVELLE_TEST_FIXTURE_DIR) + "/remediation.gif";
        QVMovie reference(path);
        reference.setCacheMode(QVMovie::CacheAll);
        QVMovie movie(path);
        movie.setAsyncPlayback(async);
        movie.setSpeed(50);
        movie.setCacheMode(boundedCache ? QVMovie::CacheAll : QVMovie::CacheNone);
        movie.setCacheByteLimit(1);
        QSignalSpy errors(&movie, &QVMovie::error);
        QSignalSpy frames(&movie, &QVMovie::frameChanged);
        // Pause from publication, before the next timer can be armed.
        bool pauseOnFrame = true;
        connect(&movie, &QVMovie::frameChanged, &movie, [&](int) {
            if (pauseOnFrame)
                movie.setPaused(true);
        });
        movie.start();
        QTRY_COMPARE_WITH_TIMEOUT(movie.state(), QVMovie::Paused, 1000);
        for (int round = 0; round < 2; ++round) {
            const int previous = movie.currentFrameNumber();
            const int count = frames.count();
            const QImage image = movie.currentImage();
            movie.setPaused(true);
            QTest::qWait(120);
            QCOMPARE(frames.count(), count);
            QCOMPARE(movie.currentImage(), image);
            QElapsedTimer elapsed;
            elapsed.start();
            movie.setPaused(false);
            // The next publication pauses again: detect exact frame order.
            QTRY_COMPARE_WITH_TIMEOUT(frames.count(), count + 1, 1000);
            QVERIFY(elapsed.elapsed() < 1000);
            const int expected = (previous + 1) % movie.frameCount();
            QCOMPARE(movie.currentFrameNumber(), expected);
            QVERIFY(reference.jumpToFrame(expected));
            QCOMPARE(movie.currentImage(), reference.currentImage());
            QCOMPARE(movie.property("decoderCreations").toInt(), async ? 1 : 0);
            QVERIFY(movie.cachedBytes() <= 1);
        }
        auto *timer = movie.findChild<QTimer *>();
        QVERIFY(timer);
        MovieTimerObservation observation(movie, pauseOnFrame);
        timer->installEventFilter(&observation);
        const int count = frames.count();
        const int previous = movie.currentFrameNumber();
        pauseOnFrame = false;
        movie.setPaused(false);
        movie.setPaused(false); // repeated resume must not enqueue duplicate work
        QTRY_COMPARE_WITH_TIMEOUT(observation.deliveries, 1, 1000);
        QVERIFY(observation.valid);
        QTRY_COMPARE_WITH_TIMEOUT(movie.state(), QVMovie::Paused, 1000);
        QCOMPARE(frames.count(), count + 2);
        const int expected = (previous + 2) % movie.frameCount();
        QCOMPARE(movie.currentFrameNumber(), expected);
        QVERIFY(reference.jumpToFrame(expected));
        QCOMPARE(movie.currentImage(), reference.currentImage());
        QCOMPARE(movie.property("decoderCreations").toInt(), async ? 1 : 0);
        QVERIFY(movie.cachedBytes() <= 1);
        movie.stop();
        QCOMPARE(errors.count(), 0);
    }
    void testMovieResumeDelayedDispatch_data()
    {
        QTest::addColumn<bool>("async");
        QTest::newRow("async-queue") << true;
        QTest::newRow("gui-dispatch") << false;
    }
    void testMovieResumeDelayedDispatch()
    {
        QFETCH(bool, async);
        const auto path = QString::fromUtf8(FOVELLE_TEST_FIXTURE_DIR) + "/remediation.gif";
        QVMovie movie(path);
        movie.setAsyncPlayback(async);
        movie.setSpeed(50);
        QSignalSpy frames(&movie, &QVMovie::frameChanged);
        QSignalSpy errors(&movie, &QVMovie::error);
        bool pauseOnFrame = true;
        connect(&movie, &QVMovie::frameChanged, &movie, [&](int) {
            if (pauseOnFrame)
                movie.setPaused(true);
        });
        movie.start();
        QTRY_COMPARE_WITH_TIMEOUT(movie.state(), QVMovie::Paused, 1000);
        const int previous = movie.currentFrameNumber();
        const int count = frames.count();
        auto *timer = movie.findChild<QTimer *>();
        QVERIFY(timer);
        MovieTimerObservation observation(movie, pauseOnFrame);
        timer->installEventFilter(&observation);
        auto entered = std::make_shared<QSemaphore>();
        auto release = std::make_shared<QSemaphore>();
        const auto unblock = qScopeGuard([release] { release->release(4); });
        if (async) {
            for (int i = 0; i < 4; ++i)
                QvAsync::schedule([entered, release] {
                    entered->release();
                    release->acquire();
                });
            QTRY_COMPARE_WITH_TIMEOUT(entered->available(), 4, 2000);
        }
        pauseOnFrame = false;
        movie.setPaused(false);
        // Intentionally do not pump events: reproduce queue latency or an
        // overdue GUI timer independently of the hosted runner's speed.
        QThread::msleep(static_cast<unsigned long>(movie.nextFrameDelay() + 80));
        if (async) {
            QCOMPARE(frames.count(), count);
            release->release(4);
        } else {
            QVERIFY(timer->isActive());
            QCOMPARE(timer->remainingTime(), 0);
        }
        QTRY_COMPARE_WITH_TIMEOUT(observation.deliveries, 1, 2000);
        QVERIFY(observation.valid);
        if (async) {
            QCOMPARE(observation.remaining, 0);
            QCOMPARE(observation.interval, 0);
        }
        QTRY_COMPARE_WITH_TIMEOUT(movie.state(), QVMovie::Paused, 1000);
        QCOMPARE(frames.count(), count + 2);
        const int expected = (previous + 2) % movie.frameCount();
        QCOMPARE(movie.currentFrameNumber(), expected);
        QVMovie reference(path);
        reference.setCacheMode(QVMovie::CacheAll);
        QVERIFY(reference.jumpToFrame(expected));
        QCOMPARE(movie.currentImage(), reference.currentImage());
        QCOMPARE(errors.count(), 0);
        qInfo() << "DELAYED_RESUME" << "async=" << async << "interval=" << observation.interval
                << "remaining=" << observation.remaining;
        movie.stop();
    }
    void testMoviePauseQueuedRead_data()
    {
        QTest::addColumn<QString>("action");
        for (const auto &action : {"completed", "inflight", "stop", "replace", "jump", "destroy",
                                   "pending-stop", "pending-replace", "pending-jump", "pending-destroy"})
            QTest::newRow(action) << QString::fromLatin1(action);
    }
    void testMoviePauseQueuedRead()
    {
        QFETCH(QString, action);
        auto entered = std::make_shared<QSemaphore>();
        auto release = std::make_shared<QSemaphore>();
        const auto unblock = qScopeGuard([release] { release->release(4); });
        for (int i = 0; i < 4; ++i)
            QvAsync::schedule([entered, release] {
                entered->release();
                release->acquire();
            });
        QTRY_COMPARE_WITH_TIMEOUT(entered->available(), 4, 2000);
        QTemporaryDir dir;
        const auto gif = QString::fromUtf8(FOVELLE_TEST_FIXTURE_DIR) + "/remediation.gif";
        auto movie = std::make_unique<QVMovie>(gif);
        movie->setCacheMode(QVMovie::CacheAll);
        movie->setAsyncPlayback(true);
        movie->setSpeed(50);
        QSignalSpy frames(movie.get(), &QVMovie::frameChanged);
        QSignalSpy errors(movie.get(), &QVMovie::error);
        movie->start();
        movie->setPaused(true);
        QCOMPARE(movie->currentFrameNumber(), -1);
        if (action == "inflight")
            movie->setPaused(false);
        else if (action == "stop")
            movie->stop();
        else if (action == "replace") {
            const auto path = createBase64Image(dir, "new", "png", tinyAnimatedPngBase64);
            movie->setFileName(path);
        } else if (action == "jump")
            QVERIFY(movie->jumpToFrame(2));
        else if (action == "destroy")
            movie.reset();
        release->release(4);
        // Sentinel verifies that the pool can progress after being released.
        auto completed = std::make_shared<std::atomic<bool>>(false);
        QvAsync::schedule([completed] { completed->store(true); });
        QTRY_VERIFY_WITH_TIMEOUT(completed->load(), 2000);
        QTest::qWait(120);
        if (action == "completed" || action.startsWith("pending-")) {
            QTRY_COMPARE_WITH_TIMEOUT(movie->property("readCompletions").toInt(), 1, 1000);
            QCOMPARE(movie->state(), QVMovie::Paused);
            QCOMPARE(frames.count(), 0);
            if (action == "pending-stop")
                movie->stop();
            else if (action == "pending-replace")
                movie->setFileName(createBase64Image(dir, "new", "png", tinyAnimatedPngBase64));
            else if (action == "pending-jump")
                QVERIFY(movie->jumpToFrame(2));
            else if (action == "pending-destroy")
                movie.reset();
        }
        if (action.endsWith("destroy")) {
            QCOMPARE(frames.count(), 0);
            return;
        }
        if (action == "completed") {
            QCOMPARE(movie->state(), QVMovie::Paused);
            QCOMPARE(frames.count(), 0);
            QCOMPARE(movie->currentFrameNumber(), -1);
        }
        if (action == "stop" || action == "replace")
            QCOMPARE(frames.count(), 0);
        if (action != "inflight") {
            const int count = frames.count();
            connect(movie.get(), &QVMovie::frameChanged, movie.get(), [&](int) {
                movie->setPaused(true);
            });
            movie->start();
            QTRY_COMPARE_WITH_TIMEOUT(frames.count(), count + 1, 1000);
            const int expected = action.endsWith("jump") ? 3 : 0;
            QCOMPARE(movie->currentFrameNumber(), expected);
            QVMovie reference(movie->fileName());
            reference.setCacheMode(QVMovie::CacheAll);
            QVERIFY(reference.jumpToFrame(expected));
            QCOMPARE(movie->currentImage(), reference.currentImage());
        }
        if (action == "completed" || action == "inflight")
            QCOMPARE(movie->property("decoderCreations").toInt(), 1);
        movie->stop();
        const int count = frames.count();
        QTest::qWait(120);
        QCOMPARE(frames.count(), count);
        QCOMPARE(errors.count(), 0);
    }
    void testMovieAsyncPlayback()
    {
        QTemporaryDir dir;
        const auto path = createBase64Image(dir, "movie", "png", tinyAnimatedPngBase64);
        QVMovie movie(path);
        movie.setAsyncPlayback(true);
        QSignalSpy frames(&movie, &QVMovie::frameChanged);
        movie.start();
        QCOMPARE(frames.count(), 0);
        QTRY_VERIFY_WITH_TIMEOUT(frames.count() >= 2, 2000);
        movie.stop();
        const int count = frames.count();
        QTest::qWait(250);
        QCOMPARE(frames.count(), count);
        movie.setFileName(path);
        movie.start();
        QTRY_VERIFY_WITH_TIMEOUT(frames.count() > count, 2000);
    }
    void testMovieAsyncPlaybackSpeed()
    {
        const auto measure = [](int speed) {
            QVMovie movie(QString::fromUtf8(FOVELLE_TEST_FIXTURE_DIR) + "/remediation.gif");
            movie.setAsyncPlayback(true);
            movie.setSpeed(speed);
            QSignalSpy frames(&movie, &QVMovie::frameChanged);
            QElapsedTimer timer;
            timer.start();
            movie.start();
            while (frames.count() < 20 && timer.elapsed() < 3000)
                QTest::qWait(5);
            movie.stop();
            return qMakePair(frames.count() >= 20, timer.elapsed());
        };
        const auto normal = measure(100);
        const auto fast = measure(200);
        QVERIFY(normal.first);
        QVERIFY(fast.first);
        QVERIFY(normal.second >= 500);
        QVERIFY2(fast.second < normal.second * 0.85,
                 "Async decoding must retain playback-speed scaling");
    }
    void testMovieAsyncInvalidInput()
    {
        QTemporaryDir dir;
        QVMovie movie(dir.filePath("absent.gif"));
        movie.setAsyncPlayback(true);
        QSignalSpy failed(&movie, &QVMovie::error);
        QSignalSpy frames(&movie, &QVMovie::frameChanged);
        movie.start();
        QTRY_COMPARE_WITH_TIMEOUT(movie.state(), QVMovie::NotRunning, 1000);
        QCOMPARE(failed.count(), 1);
        QCOMPARE(frames.count(), 0);
        QCOMPARE(movie.cachedBytes(), quint64(0));
    }
    void testMovieQueuedSourceReplacement()
    {
        auto entered = std::make_shared<QSemaphore>();
        auto release = std::make_shared<QSemaphore>();
        const auto unblock = qScopeGuard([release] { release->release(4); });
        for (int i = 0; i < 4; ++i)
            QvAsync::schedule([entered, release] {
                entered->release();
                release->acquire();
            });
        QTRY_COMPARE_WITH_TIMEOUT(entered->available(), 4, 2000);
        QTemporaryDir dir;
        const auto oldPath = createBase64Image(dir, "old", "png", tinyAnimatedPngBase64);
        QImage replacement(3, 3, QImage::Format_ARGB32);
        replacement.fill(Qt::cyan);
        const auto newPath = dir.filePath("new.png");
        QVERIFY(replacement.save(newPath));
        QVMovie movie(oldPath);
        movie.setAsyncPlayback(true);
        QSignalSpy frames(&movie, &QVMovie::frameChanged);
        movie.start();
        movie.setFileName(newPath);
        movie.start();
        bool heartbeat = false;
        QTimer::singleShot(0, this, [&] { heartbeat = true; });
        QTRY_VERIFY_WITH_TIMEOUT(heartbeat, 500);
        QCOMPARE(frames.count(), 0);
        release->release(4);
        QTRY_VERIFY_WITH_TIMEOUT(frames.count() > 0, 2000);
        QCOMPARE(movie.currentImage().pixelColor(1, 1), QColor(Qt::cyan));
        movie.stop();
    }
    void testLoaderNonblockingShutdown()
    {
        auto release = std::make_shared<std::atomic<bool>>(false);
        auto entered = std::make_shared<std::atomic<bool>>(false);
        int deliveries = 0;
        auto *loader =
                new QVImageLoader(nullptr, [release, entered](const QString &path, int, bool) {
                    entered->store(true);
                    while (!release->load())
                        QThread::msleep(5);
                    QVImageLoader::Result result;
                    result.absoluteFilePath = path;
                    return result;
                });
        connect(loader, &QVImageLoader::imageReady, this, [&] { ++deliveries; });
        loader->requestImage("/nonexistent/test.png");
        QTRY_VERIFY_WITH_TIMEOUT(entered->load(), 1000);
        QElapsedTimer timer;
        timer.start();
        delete loader;
        const auto elapsed = timer.elapsed();
        release->store(true);
        QVERIFY(elapsed < 100);
        QTest::qWait(100);
        QCOMPARE(deliveries, 0);
    }
    void testPortableHDR()
    {
        const QString root = QString::fromUtf8(FOVELLE_TEST_FIXTURE_DIR) + "/portable-hdr/";
        for (const QString &name : { QString("gain-map.jpg"), QString("bayer.dng") }) {
            const auto result = QVCocoaFunctions::readImageWithImageIO(root + name, 128);
            QVERIFY2(result.errorString.isEmpty(), qPrintable(result.errorString));
            QVERIFY2(result.hdrImage != nullptr, qPrintable(name));
            QCOMPARE(result.isRaw, name.endsWith("dng"));
            if (name.endsWith("jpg"))
                QVERIFY(result.hdrMetadata.hasAppleGainMap || result.hdrMetadata.hasISOGainMap);
            const auto pixels = QVCocoaFunctions::probeHDRBrightnessImage(result.hdrImage, 8, 1);
            QCOMPARE(pixels.size(), 64 * 64 * 4);
            for (float value : pixels)
                QVERIFY(std::isfinite(value));
        }
    }
    void testShutdownProcess()
    {
        QTemporaryDir dir;
        for (const int delay : { 30, 10000 }) {
            QProcess child;
            auto environment = QProcessEnvironment::systemEnvironment();
            environment.insert("FOVELLE_READ_SHUTDOWN_PROBE", dir.path());
            environment.insert("FOVELLE_READ_SHUTDOWN_DELAY_MS", QString::number(delay));
            child.setProcessEnvironment(environment);
            QElapsedTimer timer;
            timer.start();
            child.start(QCoreApplication::applicationFilePath(), { });
            QVERIFY(child.waitForFinished(5000));
            QCOMPARE(child.exitStatus(), QProcess::NormalExit);
            QCOMPARE(child.exitCode(), delay > 1500 ? 75 : 0);
            QVERIFY(timer.elapsed() < 4000);
            QSettings settings(dir.filePath("saved.ini"), QSettings::IniFormat);
            QCOMPARE(settings.value("saved").toBool(), true);
        }
    }
};

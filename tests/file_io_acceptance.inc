#include "qvrenamedialog.h"
#include <fcntl.h>
#include <sys/stat.h>

namespace {
bool writeIoFixture(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
            && file.write(bytes) == bytes.size() && file.flush();
}

QByteArray ioImageBytes(const QColor &color, const char *format)
{
    QImage image(12, 9, QImage::Format_RGB32);
    image.fill(color);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, format))
        return { };
    return bytes;
}

QByteArray ioEpsBytes(const char *rgb)
{
    return QByteArray("%!PS-Adobe-3.0 EPSF-3.0\n%%BoundingBox: 0 0 32 32\n") + rgb
            + " setrgbcolor\n0 0 32 32 rectfill\nshowpage\n%%EOF\n";
}

bool replaceIoFixture(const QString &path, const QByteArray &replacement)
{
    struct stat info;
    const auto source = QFile::encodeName(path);
    const QString swap = path + ".replacement";
    const auto temporary = QFile::encodeName(swap);
    if (::stat(source.constData(), &info) != 0 || info.st_size != replacement.size()
        || !writeIoFixture(swap, replacement))
        return false;
    const timespec times[]{ info.st_atimespec, info.st_mtimespec };
    return ::utimensat(AT_FDCWD, temporary.constData(), times, 0) == 0
            && ::rename(temporary.constData(), source.constData()) == 0
            && QFileInfo(path).lastModified().toMSecsSinceEpoch()
            == info.st_mtimespec.tv_sec * 1000 + info.st_mtimespec.tv_nsec / 1000000;
}

QColor ioCenter(const QImage &image)
{
    return image.isNull() ? QColor() : image.pixelColor(image.width() / 2, image.height() / 2);
}

struct ForegroundIoGate
{
    struct State
    {
        QSemaphore entered;
        QSemaphore release;
    };
    const std::shared_ptr<State> state = std::make_shared<State>();
    ForegroundIoGate()
    {
        for (int i = 0; i < 2; ++i) {
            QvAsync::schedule(
                    [shared = state] {
                        shared->entered.release();
                        shared->release.acquire();
                    },
                    0, QvAsync::Lane::Foreground);
        }
    }
    ~ForegroundIoGate() { unblock(); }
    bool entered() const { return state->entered.tryAcquire(2, 3000); }
    void unblock() { state->release.release(2); }
};
} // namespace

void WindowBehaviorTests::testRenameKeepsConfirmedIdentity_data()
{
    QTest::addColumn<bool>("replaceAfterOpening");
    QTest::newRow("replacement") << true;
    QTest::newRow("unchanged") << false;
}

void WindowBehaviorTests::testRenameKeepsConfirmedIdentity()
{
    QFETCH(bool, replaceAfterOpening);
    QTemporaryDir directory;
    const auto source = createTestImage(directory, "original", Qt::red);
    const auto moved = directory.filePath("moved.png");
    const auto target = directory.filePath("renamed.png");
    QVERIFY(!source.isEmpty());
    MainWindow window;
    ReviewWindowScope scope(window);
    window.show();
    window.openFile(source);
    QTRY_VERIFY_WITH_TIMEOUT(window.getIsPixmapLoaded(), 5000);
    window.rename();
    auto *dialog = window.findChild<QVRenameDialog *>();
    QVERIFY(dialog);
    QSignalSpy opened(dialog, &QVRenameDialog::newFileToOpen);
    dialog->setTextValue("renamed.png");
    if (replaceAfterOpening) {
        QVERIFY(QFile::rename(source, moved));
        QVERIFY(writeIoFixture(source, ioImageBytes(Qt::blue, "PNG")));
    }
    dialog->done(QDialog::Accepted);
    if (replaceAfterOpening) {
        QCOMPARE(opened.size(), 0);
        QCOMPARE(ioCenter(QImage(source)), QColor(Qt::blue));
        QCOMPARE(ioCenter(QImage(moved)), QColor(Qt::red));
        QVERIFY(!QFile::exists(target));
        QTRY_VERIFY_WITH_TIMEOUT(fixAlertVisible(window), 2000);
    } else {
        QCOMPARE(opened.size(), 1);
        QVERIFY(!QFile::exists(source));
        QCOMPARE(ioCenter(QImage(target)), QColor(Qt::red));
        QTRY_COMPARE_WITH_TIMEOUT(window.getCurrentFileDetails().fileInfo.absoluteFilePath(),
                                  target, 5000);
        QVERIFY(window.getIsPixmapLoaded());
    }
}

void ImageLoaderTests::testCacheDetectsSameMetadataReplacement_data()
{
    QTest::addColumn<bool>("eps");
    QTest::newRow("bitmap") << false;
    QTest::newRow("eps-reload") << true;
}

void ImageLoaderTests::testCacheDetectsSameMetadataReplacement()
{
    QFETCH(bool, eps);
    QTemporaryDir directory;
    const auto path = directory.filePath(eps ? "sample.eps" : "sample.bmp");
    const auto original = eps ? ioEpsBytes("1 0 0") : ioImageBytes(Qt::red, "BMP");
    const auto replacement = eps ? ioEpsBytes("0 0 1") : ioImageBytes(Qt::blue, "BMP");
    QVERIFY(!original.isEmpty());
    QVERIFY(writeIoFixture(path, original));
    const auto originalVersion = QVImageLoader::fileVersion(path);
    QVERIFY(!originalVersion.isEmpty());
    QVImageLoader loader;
    QSignalSpy ready(&loader, &QVImageLoader::imageReady);
    loader.requestImage(path);
    loader.setDesiredImages({ { path, 0 } });
    QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 5000);
    const auto first = qvariant_cast<QVImageLoader::Result>(ready.first().last());
    QVERIFY2(!first.errorData, first.errorData ? qPrintable(first.errorData->errorString) : "");
    const auto red = ioCenter(first.image);
    QVERIFY(red.isValid() && red.red() > red.blue());
    QVERIFY(replaceIoFixture(path, replacement));
    const auto newVersion = QVImageLoader::fileVersion(path);
    QVERIFY(newVersion != originalVersion);
    loader.requestImage(path, eps);
    QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 2, 5000);
    const auto second = qvariant_cast<QVImageLoader::Result>(ready.last().last());
    QVERIFY(!second.errorData);
    QCOMPARE(second.fileVersionStamp, newVersion);
    QCOMPARE(second.vectorImage.isValid(), eps);
    const auto blue = ioCenter(second.image);
    QVERIFY(blue.isValid() && blue.blue() > blue.red());
}

void ImageLoaderTests::testCacheRetriesReplacementDuringRead()
{
    QTemporaryDir directory;
    const auto path = directory.filePath("sample.bmp");
    QVERIFY(writeIoFixture(path, ioImageBytes(Qt::red, "BMP")));
    auto entered = std::make_shared<QSemaphore>();
    auto release = std::make_shared<QSemaphore>();
    auto calls = std::make_shared<std::atomic<int>>(0);
    const auto unblock = qScopeGuard([release] { release->release(); });
    QVImageLoader loader(nullptr, [entered, release, calls](const QString &file, int, bool) {
        QFileInfo info(file);
        QVImageLoader::Result result;
        result.absoluteFilePath = file;
        result.fileSize = info.size();
        result.lastModified = info.lastModified();
        result.image = QImage(file);
        if (++*calls == 1) {
            entered->release();
            release->tryAcquire(1, 4000);
        }
        return result;
    });
    QSignalSpy ready(&loader, &QVImageLoader::imageReady);
    const auto request = loader.requestImage(path);
    QVERIFY(entered->tryAcquire(1, 2000));
    QVERIFY(replaceIoFixture(path, ioImageBytes(Qt::blue, "BMP")));
    release->release();
    QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 5000);
    QCOMPARE(calls->load(), 2);
    QCOMPARE(ready.first().first().toULongLong(), request);
    const auto result = qvariant_cast<QVImageLoader::Result>(ready.first().last());
    QCOMPARE(ioCenter(result.image), QColor(Qt::blue));
    QCOMPARE(result.fileVersionStamp, QVImageLoader::fileVersion(path));
}

void WindowBehaviorTests::testDownloadUsesFinalImageFormat_data()
{
    QTest::addColumn<bool>("redirect");
    QTest::addColumn<bool>("eps");
    QTest::newRow("redirect-to-png") << true << false;
    QTest::newRow("png-at-eps-url") << false << false;
    QTest::newRow("genuine-eps") << false << true;
}

void WindowBehaviorTests::testDownloadUsesFinalImageFormat()
{
    QFETCH(bool, redirect);
    QFETCH(bool, eps);
    RemediationHttpServer endpoint;
    endpoint.body = eps ? ioEpsBytes("1 0 0") : ioImageBytes(Qt::red, "PNG");
    endpoint.headers =
            eps ? "Content-Type: application/postscript\r\n" : "Content-Type: image/png\r\n";
    RemediationHttpServer alias;
    alias.status = "302 Found";
    alias.headers = "Location: " + endpoint.url("png").toEncoded() + "\r\n";
    QString path;
    {
        MainWindow window;
        ReviewWindowScope scope(window);
        window.show();
        window.openUrl(redirect ? alias.url("eps") : endpoint.url("eps"));
        QTRY_VERIFY_WITH_TIMEOUT(window.getIsPixmapLoaded()
                                         || window.getCurrentFileDetails().errorData.has_value(),
                                 5000);
        const auto &details = window.getCurrentFileDetails();
        QVERIFY2(!details.errorData,
                 details.errorData ? qPrintable(details.errorData->errorString) : "");
        QCOMPARE(details.isVectorLoaded, eps);
        QCOMPARE(details.fileInfo.suffix(), eps ? QString("eps") : QString("png"));
        path = details.fileInfo.absoluteFilePath();
        QFile source(path);
        QVERIFY(source.open(QIODevice::ReadOnly));
        QCOMPARE(source.readAll(), endpoint.body);
        source.close();
        window.close();
    }
    QTRY_VERIFY_WITH_TIMEOUT(!QFile::exists(path), 5000);
}

void WindowBehaviorTests::testOpenAndNavigationYieldForFileProbe_data()
{
    QTest::addColumn<bool>("navigate");
    QTest::newRow("superseded-open") << false;
    QTest::newRow("missing-neighbor") << true;
}

void WindowBehaviorTests::testOpenAndNavigationYieldForFileProbe()
{
    QFETCH(bool, navigate);
    QTemporaryDir directory;
    const auto a = createTestImage(directory, "1-a", Qt::red);
    const auto b = createTestImage(directory, "2-b", Qt::green);
    const auto c = createTestImage(directory, "3-c", Qt::blue);
    QVERIFY(!a.isEmpty() && !b.isEmpty() && !c.isEmpty());
    MainWindow window;
    ReviewWindowScope scope(window);
    window.show();
    window.openFile(a);
    QTRY_VERIFY_WITH_TIMEOUT(window.getIsPixmapLoaded(), 5000);
    auto *loader = window.findChild<QVImageLoader *>();
    QVERIFY(loader);
    QTRY_COMPARE_WITH_TIMEOUT(window.getCurrentFileDetails().folderFileInfoList.size(), 3, 5000);
    ForegroundIoGate gate;
    QVERIFY(gate.entered());
    QSignalSpy started(loader, &QVImageLoader::loadStarted);
    QSignalSpy delivered(loader, &QVImageLoader::imageReady);
    if (navigate) {
        QVERIFY(QFile::remove(b));
        window.nextFile();
    } else {
        window.openFile(b);
        window.openFile(c);
    }
    bool heartbeat = false;
    QTimer::singleShot(0, &window, [&] { heartbeat = true; });
    QTRY_VERIFY_WITH_TIMEOUT(heartbeat, 1000);
    QCOMPARE(window.getCurrentFileDetails().fileInfo.absoluteFilePath(), a);
    QCOMPARE(started.size(), 0);
    gate.unblock();
    QTRY_COMPARE_WITH_TIMEOUT(window.getCurrentFileDetails().fileInfo.absoluteFilePath(), c, 5000);
    QCOMPARE(delivered.size(), 1);
}

void WindowBehaviorTests::testAsyncDirectoryOpen()
{
    QTemporaryDir directory;
    const auto first = createTestImage(directory, "1-first", Qt::red);
    QVERIFY(!first.isEmpty());
    QVERIFY(!createTestImage(directory, "2-last", Qt::blue).isEmpty());
    QVERIFY(writeIoFixture(directory.filePath("0-note.txt"), "note"));
    MainWindow window;
    ReviewWindowScope scope(window);
    window.show();
    window.openFile(directory.path());
    QTRY_VERIFY_WITH_TIMEOUT(window.getIsPixmapLoaded(), 5000);
    QCOMPARE(window.getCurrentFileDetails().fileInfo.absoluteFilePath(), first);
}

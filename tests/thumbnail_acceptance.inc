#include "qvthumbnailstrip.h"

namespace {
struct ThumbnailWindow
{
    QTemporaryDir dir;
    MainWindow window;
    QVGraphicsView *view = window.findChild<QVGraphicsView *>("graphicsView");
    QVThumbnailStrip *strip = window.findChild<QVThumbnailStrip *>();
    bool originalQuit = qvApp->quitOnLastWindowClosed();
    QString first;
    QString next;

    explicit ThumbnailWindow(int count = 3)
    {
        qvApp->setQuitOnLastWindowClosed(false);
        window.setAttribute(Qt::WA_DeleteOnClose, false);
        window.resize(800, 500);
        window.show();
        window.activateWindow();
        first = createTestImage(dir, "01", Qt::red, QSize(120, 80));
        if (count > 1)
            next = createTestImage(dir, "02", Qt::green, QSize(120, 80));
        if (count > 2)
            createTestImage(dir, "03", Qt::blue, QSize(120, 80));
        window.openFile(first);
    }

    ~ThumbnailWindow()
    {
        window.close();
        qvApp->setQuitOnLastWindowClosed(originalQuit);
    }

    void hover(const bool settle = true)
    {
        QVERIFY(QTest::qWaitForWindowActive(&window, 1000));
        if (settle)
            QTest::qWait(100);
        sendMouseMove(view->viewport(),
                      QPoint(view->viewport()->width() / 2, view->viewport()->height() - 3));
    }

    QPoint nextCenter() const
    {
        for (const auto &cell : strip->cells()) {
            if (cell.index == 1)
                return cell.rect.center().toPoint();
        }
        return { -1, -1 };
    }
};
} // namespace

void WindowBehaviorTests::testThumbnailCenter()
{
    const QRectF viewport(0, 0, 800, 500);
    for (int center : { 0, 8 }) {
        const auto cells = QVThumbnailStrip::layout(viewport, 9, center, false);
        QCOMPARE(cells.size(), 9);
        QCOMPARE(cells.at(4).rect.center().x(), viewport.center().x());
        QCOMPARE(cells.at(4).index, center);
        QCOMPARE(cells.at(center == 0 ? 3 : 5).index, -1);
    }
}

void WindowBehaviorTests::testThumbnailCompact()
{
    for (int width : { 223, 224, 271, 272 }) {
        const auto cells = QVThumbnailStrip::layout(QRectF(0, 0, width, 160), 2, 0, false);
        QCOMPARE(cells.size(), width == 223 ? 0 : 3);
        if (!cells.isEmpty())
            QCOMPARE(cells.at(1).rect.width(), width < 272 ? 56.0 : 72.0);
    }
}

void WindowBehaviorTests::testThumbnailLoop()
{
    const auto cells = QVThumbnailStrip::layout(QRectF(0, 0, 800, 500), 2, 0, true);
    QCOMPARE(cells.size(), 3);
    QCOMPARE(cells.at(0).index, -1);
    QCOMPARE(cells.at(1).index, 0);
    QCOMPARE(cells.at(2).index, 1);
}

void WindowBehaviorTests::testThumbnailHover()
{
    ThumbnailWindow fixture;
    QTRY_VERIFY(fixture.strip->available());
    // Let the main image finish its deferred fit before measuring the overlay.
    QTest::qWait(350);
    const QSize before = fixture.view->viewport()->size();
    fixture.hover();
    QTRY_COMPARE(fixture.strip->opacity(), 1.0);
    QCOMPARE(fixture.view->viewport()->size(), before);
    QCOMPARE(fixture.strip->cells().at(fixture.strip->cells().size() / 2).index, 0);
}

void WindowBehaviorTests::testThumbnailHide()
{
    ThumbnailWindow fixture;
    QTRY_VERIFY(fixture.strip->available());
    fixture.hover();
    QTRY_COMPARE(fixture.strip->opacity(), 1.0);
    sendMouseMove(fixture.view->viewport(), QPoint(400, 220));
    QTRY_COMPARE(fixture.strip->opacity(), 0.0);
    QVERIFY(!fixture.strip->hitTest(fixture.nextCenter()));
}

void WindowBehaviorTests::testThumbnailClick()
{
    ThumbnailWindow fixture;
    QTRY_VERIFY(fixture.strip->available());
    fixture.hover();
    QTRY_COMPARE(fixture.strip->opacity(), 1.0);
    QVERIFY(fixture.nextCenter().x() >= 0);
    QTest::mouseClick(fixture.view->viewport(), Qt::LeftButton, Qt::NoModifier,
                      fixture.nextCenter());
    QTRY_COMPARE(fixture.window.getCurrentFileDetails().fileInfo.absoluteFilePath(), fixture.next);
    QTRY_COMPARE(fixture.strip->cells().at(fixture.strip->cells().size() / 2).index, 1);
}

void WindowBehaviorTests::testThumbnailInvalidPress()
{
    ThumbnailWindow fixture;
    QTRY_VERIFY(fixture.strip->available());
    fixture.hover();
    QTRY_COMPARE(fixture.strip->opacity(), 1.0);
    const QPoint pos = fixture.nextCenter();
    QTest::mousePress(fixture.view->viewport(), Qt::LeftButton, Qt::NoModifier, pos);
    qvApp->invalidateFolderListings();
    QTest::mouseRelease(fixture.view->viewport(), Qt::LeftButton, Qt::NoModifier, pos);
    QTRY_VERIFY(fixture.strip->available());
    QCOMPARE(fixture.window.getCurrentFileDetails().fileInfo.absoluteFilePath(), fixture.first);
}

void WindowBehaviorTests::testThumbnailVersion()
{
    QTemporaryDir dir;
    const QString path = createTestImage(dir, "changed", Qt::red, QSize(120, 80));
    const auto version = QVImageLoader::fileVersion(path);
    QVERIFY(!version.isEmpty());
    QFile file(path);
    QVERIFY(file.open(QIODevice::Append));
    QCOMPARE(file.write("changed"), qint64(7));
    file.close();
    QVImageLoader loader;
    QSignalSpy ready(&loader, &QVImageLoader::imageReady);
    loader.requestVersionedImage(path, version);
    QTRY_COMPARE(ready.size(), 1);
    QVERIFY(qvariant_cast<QVImageLoader::Result>(ready.first().at(1)).versionMismatch);
}

void WindowBehaviorTests::testThumbnailKeyboard()
{
    ThumbnailWindow fixture;
    QTRY_VERIFY(fixture.strip->available());
    fixture.strip->focusStrip();
    QTRY_COMPARE(fixture.strip->opacity(), 1.0);
    QTest::keyClick(fixture.view->viewport(), Qt::Key_Right);
    QTest::keyClick(fixture.view->viewport(), Qt::Key_Return);
    QTRY_COMPARE(fixture.window.getCurrentFileDetails().fileInfo.absoluteFilePath(), fixture.next);
    QTest::keyClick(fixture.view->viewport(), Qt::Key_Escape);
    QTRY_COMPARE(fixture.strip->opacity(), 0.0);
    QVERIFY(fixture.window.isVisible());
}

void WindowBehaviorTests::testThumbnailPreview()
{
    ThumbnailWindow fixture;
    QTRY_VERIFY(fixture.strip->available());
    fixture.hover();
    QTRY_VERIFY(fixture.strip->previewCount() == 3);
    for (const auto &cell : fixture.strip->cells()) {
        if (cell.index < 0 || cell.currentProxy)
            continue;
        const qreal dpr = fixture.view->viewport()->devicePixelRatioF();
        QVERIFY(cell.preview.width() <= std::ceil(cell.rect.width() * dpr));
        QVERIFY(cell.preview.height() <= std::ceil(cell.rect.height() * dpr));
    }
}

void WindowBehaviorTests::testThumbnailSingle()
{
    ThumbnailWindow fixture(1);
    QTRY_COMPARE(fixture.window.getCurrentFileDetails().folderFileInfoList.size(), 1);
    fixture.hover();
    QTest::qWait(260);
    QCOMPARE(fixture.strip->opacity(), 0.0);
}

void WindowBehaviorTests::testThumbnailPreference()
{
    ScopedOptionValues options({ { "thumbnailhoverenabled", false } });
    ThumbnailWindow fixture;
    QTRY_VERIFY(fixture.strip->available());
    fixture.hover();
    QTest::qWait(260);
    QCOMPARE(fixture.strip->opacity(), 0.0);
    fixture.strip->focusStrip();
    QTRY_COMPARE(fixture.strip->opacity(), 1.0);
}

void WindowBehaviorTests::testThumbnailBackendHandoff()
{
    ThumbnailWindow fixture;
    QTRY_VERIFY(fixture.strip->available());
    if (!fixture.view->usesNativeHDRNavigationOverlay())
        QSKIP("Native Metal presentation is unavailable.");
    const QString vectorPath = fixture.dir.filePath("04.svg");
    QFile vector(vectorPath);
    QVERIFY(vector.open(QIODevice::WriteOnly));
    QVERIFY(vector.write("<svg xmlns='http://www.w3.org/2000/svg' width='200' height='120'>"
                         "<rect width='200' height='120' fill='blue'/></svg>")
            > 0);
    vector.close();
    qvApp->invalidateFolderListings();

    fixture.hover();
    QTRY_COMPARE(fixture.strip->opacity(), 1.0);
    QTRY_VERIFY(fixture.view->nativeMetalRendererDiagnostics().nativeThumbnailVisible);
    QVERIFY(fixture.view->nativeMetalRendererDiagnostics().thumbnailUploadCount > 0);
    fixture.strip->focusStrip();
    fixture.window.openFile(vectorPath);
    QTRY_COMPARE(fixture.window.getCurrentFileDetails().fileInfo.absoluteFilePath(), vectorPath);
    QTRY_VERIFY(!fixture.view->usesNativeHDRNavigationOverlay());
    QTRY_VERIFY(!fixture.view->nativeMetalRendererDiagnostics().nativeThumbnailVisible);
    QTRY_COMPARE(fixture.strip->opacity(), 1.0);
    QVERIFY(!fixture.strip->grab().isNull());
}

void WindowBehaviorTests::testThumbnailSupersededValidation()
{
    ThumbnailWindow fixture;
    QTRY_VERIFY(fixture.strip->available());
    const QString last =
            fixture.window.getCurrentFileDetails().folderFileInfoList.at(2).absoluteFilePath;
    fixture.hover();
    QTRY_COMPARE(fixture.strip->opacity(), 1.0);
    QTest::mouseClick(fixture.view->viewport(), Qt::LeftButton, Qt::NoModifier,
                      fixture.nextCenter());
    fixture.window.openFile(last);
    QTRY_COMPARE(fixture.window.getCurrentFileDetails().fileInfo.absoluteFilePath(), last);
    QTest::qWait(120);
    QCOMPARE(fixture.window.getCurrentFileDetails().fileInfo.absoluteFilePath(), last);
}

void WindowBehaviorTests::testThumbnailVersionChangesDuringRead()
{
    QTemporaryDir dir;
    const QString path = createTestImage(dir, "changes-during-read", Qt::red, QSize(120, 80));
    const auto version = QVImageLoader::fileVersion(path);
    QVERIFY(!version.isEmpty());
    std::atomic<bool> wrote = false;
    QVImageLoader loader(nullptr, [&wrote](const QString &target, int, bool) {
        QFile file(target);
        wrote.store(file.open(QIODevice::Append) && file.write("modified") == 8);
        QVImageLoader::Result result;
        result.absoluteFilePath = target;
        result.image = QImage(16, 16, QImage::Format_RGB32);
        return result;
    });
    QSignalSpy ready(&loader, &QVImageLoader::imageReady);
    loader.requestVersionedImage(path, version);
    QTRY_COMPARE(ready.size(), 1);
    QVERIFY(wrote.load());
    const auto result = qvariant_cast<QVImageLoader::Result>(ready.first().at(1));
    QVERIFY(result.versionMismatch);
    QVERIFY(result.image.isNull());
}

void WindowBehaviorTests::testThumbnailPreferenceDoesNotRescan()
{
    ScopedOptionValues options({ { "thumbnailhoverenabled", true } });
    ThumbnailWindow fixture;
    QTRY_VERIFY(fixture.strip->available());
    const auto revision = fixture.view->thumbnailImageCore().thumbnailListVersion();
    auto &actions = qvApp->getActionManager();
    auto *action = actions.getAction("thumbnailhover");
    QVERIFY(action);
    ActionManager::actionTriggered(action, &fixture.window);
    QVERIFY(!qvApp->getSettingsManager().getBoolean("thumbnailhoverenabled"));
    QCOMPARE(fixture.view->thumbnailImageCore().thumbnailListVersion(), revision);
    ActionManager::actionTriggered(action, &fixture.window);
    QVERIFY(qvApp->getSettingsManager().getBoolean("thumbnailhoverenabled"));
    QCOMPARE(fixture.view->thumbnailImageCore().thumbnailListVersion(), revision);
}

void WindowBehaviorTests::testThumbnailFadeReversal()
{
    if (QVCocoaFunctions::reduceThumbnailMotion())
        QSKIP("The host has Reduce Motion enabled.");
    ThumbnailWindow fixture;
    QTRY_VERIFY(fixture.strip->available());
    fixture.hover();
    QTRY_COMPARE(fixture.strip->opacity(), 1.0);
    sendMouseMove(fixture.view->viewport(), QPoint(400, 220));
    QTRY_VERIFY(fixture.strip->opacity() < 1.0 && fixture.strip->opacity() > 0.0);
    fixture.hover(false);
    QTRY_COMPARE(fixture.strip->opacity(), 1.0);
}

void WindowBehaviorTests::testThumbnailFullscreenTransition()
{
    ThumbnailWindow fixture;
    QTRY_VERIFY(fixture.strip->available());
    fixture.hover();
    QTRY_COMPARE(fixture.strip->opacity(), 1.0);
    const QPoint oldHit = fixture.nextCenter();
    fixture.window.beginNativeFullScreenTransition();
    fixture.hover();
    QTest::qWait(100);
    QCOMPARE(fixture.strip->opacity(), 0.0);
    QVERIFY(!fixture.strip->hitTest(oldHit));
    fixture.window.endNativeFullScreenTransition();
}

void WindowBehaviorTests::testThumbnailVersionedCacheReuse()
{
    QTemporaryDir dir;
    const QString path = createTestImage(dir, "cache", Qt::red, QSize(120, 80));
    const auto version = QVImageLoader::fileVersion(path);
    QVERIFY(!version.isEmpty());
    std::atomic<int> reads = 0;
    QVImageLoader loader(nullptr, [&reads](const QString &target, int, bool) {
        ++reads;
        QVImageLoader::Result result;
        result.absoluteFilePath = target;
        result.fileSize = QFileInfo(target).size();
        result.lastModified = QFileInfo(target).lastModified();
        result.image = QImage(16, 16, QImage::Format_RGB32);
        return result;
    });
    QSignalSpy ready(&loader, &QVImageLoader::imageReady);
    loader.requestImage(path);
    loader.setDesiredImages({ { path, 0 } });
    QTRY_COMPARE(ready.size(), 1);
    QCOMPARE(qvariant_cast<QVImageLoader::Result>(ready.first().at(1)).fileVersionStamp, version);
    loader.requestVersionedImage(path, version);
    QTRY_COMPARE(ready.size(), 2);
    QVERIFY(!qvariant_cast<QVImageLoader::Result>(ready.last().at(1)).versionMismatch);
    QCOMPARE(reads.load(), 1);
}

void WindowBehaviorTests::testThumbnailFocusRefreshesDirtyListing()
{
    ScopedOptionValues options({ { "thumbnailhoverenabled", false } });
    ThumbnailWindow fixture;
    QTRY_VERIFY(fixture.strip->available());
    qvApp->invalidateFolderListings();
    QVERIFY(!fixture.strip->available());
    fixture.strip->focusStrip();
    QTRY_COMPARE(fixture.strip->opacity(), 1.0);
    QCOMPARE(fixture.strip->cells().at(fixture.strip->cells().size() / 2).index, 0);
}

void WindowBehaviorTests::testThumbnailNativeHDR()
{
    ThumbnailWindow fixture;
    QTRY_VERIFY(fixture.strip->available());
    const QString hdrPath = fixture.dir.filePath("04-hdr.jpg");
    QVERIFY(QFile::copy(QStringLiteral(FOVELLE_TEST_FIXTURE_DIR "/portable-hdr/gain-map.jpg"),
                        hdrPath));
    qvApp->invalidateFolderListings();
    fixture.window.openFile(hdrPath);
    QTRY_COMPARE(fixture.window.getCurrentFileDetails().fileInfo.absoluteFilePath(), hdrPath);
    QTRY_VERIFY(fixture.strip->available());
    QVERIFY(fixture.window.getCurrentFileDetails().isNativeHDRLoaded);
    QVERIFY(fixture.view->usesNativeHDRNavigationOverlay());
    fixture.hover();
    QTRY_COMPARE(fixture.strip->opacity(), 1.0);
    QVERIFY(fixture.view->nativeMetalRendererDiagnostics().nativeThumbnailVisible);
    QCOMPARE(fixture.window.getCurrentFileDetails().fileInfo.absoluteFilePath(), hdrPath);
}

void WindowBehaviorTests::testThumbnailDragOwnership()
{
    ThumbnailWindow fixture;
    QTRY_VERIFY(fixture.strip->available());
    auto *viewport = fixture.view->viewport();
    const QPoint bottom(viewport->width() / 2, viewport->height() - 3);
    QTest::mousePress(viewport, Qt::LeftButton, Qt::NoModifier, viewport->rect().center());
    sendMouseMove(viewport, bottom, Qt::LeftButton);
    QTest::qWait(260);
    QCOMPARE(fixture.strip->opacity(), 0.0);
    QTest::mouseRelease(viewport, Qt::LeftButton, Qt::NoModifier, bottom);
    fixture.hover();
    QTRY_COMPARE(fixture.strip->opacity(), 1.0);
    QCOMPARE(fixture.window.getCurrentFileDetails().fileInfo.absoluteFilePath(), fixture.first);
}

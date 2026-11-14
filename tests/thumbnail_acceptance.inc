#include "qvthumbnailstrip.h"

#include <QtEndian>
#include <cstring>

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
        window.resize(1000, 600);
        window.show();
        window.activateWindow();
        first = createTestImage(dir, "01", Qt::red, QSize(120, 80));
        if (count > 1)
            next = createTestImage(dir, "02", Qt::green, QSize(120, 80));
        if (count > 2)
            createTestImage(dir, "03", Qt::blue, QSize(120, 80));
        for (int i = 4; i <= count; ++i)
            createTestImage(dir, QStringLiteral("%1").arg(i, 2, 10, QLatin1Char('0')), Qt::yellow,
                            QSize(120, 80));
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

    QPoint nextCenter() const { return cellCenter(1); }

    QPoint cellCenter(const int index) const
    {
        for (const auto &cell : strip->cells()) {
            if (cell.index == index)
                return cell.rect.center().toPoint();
        }
        return { -1, -1 };
    }
};
bool sendThumbnailWheel(ThumbnailWindow &fixture, const QPoint &pos, const QPoint &pixel,
                        const QPoint &angle, const Qt::ScrollPhase phase = Qt::NoScrollPhase)
{
    auto *viewport = fixture.view->viewport();
    QWheelEvent event(pos, viewport->mapToGlobal(pos), pixel, angle, Qt::NoButton, Qt::NoModifier,
                      phase, false);
    event.ignore();
    QCoreApplication::sendEvent(viewport, &event);
    return event.isAccepted();
}
} // namespace

void WindowBehaviorTests::testThumbnailCenter()
{
    const QRectF viewport(0, 0, 1600, 500);
    for (int center : { 0, 8 }) {
        const auto cells = QVThumbnailStrip::layout(viewport, 9, center, false);
        QCOMPARE(cells.size(), 9);
        QCOMPARE(cells.at(4).rect.size(), QSizeF(144, 104));
        QCOMPARE(cells.at(4).rect.center().x(), viewport.center().x());
        QCOMPARE(cells.at(4).index, center);
        QCOMPARE(cells.at(center == 0 ? 3 : 5).index, -1);
    }
}

void WindowBehaviorTests::testThumbnailCompact()
{
    for (int width : { 391, 392, 487, 488 }) {
        const auto cells = QVThumbnailStrip::layout(QRectF(0, 0, width, 160), 2, 0, false);
        QCOMPARE(cells.size(), width == 391 ? 0 : 3);
        if (!cells.isEmpty())
            QCOMPARE(cells.at(1).rect.size(), width < 488 ? QSizeF(112, 80) : QSizeF(144, 104));
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

void WindowBehaviorTests::testThumbnailKeyboardScroll_data()
{
    QTest::addColumn<int>("key");
    QTest::addColumn<int>("step");
    QTest::newRow("left") << int(Qt::Key_Left) << -1;
    QTest::newRow("right") << int(Qt::Key_Right) << 1;
}

void WindowBehaviorTests::testThumbnailKeyboardScroll()
{
    QFETCH(int, key);
    QFETCH(int, step);
    ScopedOptionValues options({ { "loopfoldersenabled", false } });
    ThumbnailWindow fixture(17);
    QVERIFY(QTest::qWaitForWindowActive(&fixture.window, 1000));
    auto &core = fixture.view->thumbnailImageCore();
    QTRY_COMPARE(core.getCurrentFileDetails().folderFileInfoList.size(), 17);
    const auto pathAt = [&](int index) {
        return core.getCurrentFileDetails().folderFileInfoList.at(index).absoluteFilePath;
    };
    const auto original = pathAt(8);
    fixture.window.openFile(original);
    QTRY_COMPARE(core.getCurrentFileDetails().loadedIndexInFolder, 8);
    QTRY_VERIFY(fixture.strip->available());
    fixture.strip->focusStrip();
    QTRY_COMPARE(fixture.strip->opacity(), 1.0);
    QCOMPARE(fixture.strip->browseIndex(), 8.0);

    auto *viewport = fixture.view->viewport();
    bool crossedEdge = false;
    for (int distance = 1; distance <= 7; ++distance) {
        const int target = 8 + distance * step;
        const bool visible = fixture.strip->panelRect().contains(fixture.cellCenter(target));
        const qreal before = fixture.strip->browseIndex();
        QTest::keyClick(viewport, Qt::Key(key));
        QCOMPARE(core.getCurrentFileDetails().fileInfo.absoluteFilePath(), original);
        QVERIFY(!core.thumbnailNavigationBusy());
        if (visible) {
            QCOMPARE(fixture.strip->browseIndex(), before);
        } else {
            crossedEdge = true;
            QCOMPARE(fixture.strip->browseIndex(), qreal(target));
            QVERIFY(qAbs(fixture.cellCenter(target).x() - fixture.strip->panelRect().center().x())
                    <= 1);
        }
    }
    QVERIFY(crossedEdge);
    const auto target = pathAt(8 + 7 * step);
    QTest::keyClick(viewport, Qt::Key_Return);
    QTRY_COMPARE(core.getCurrentFileDetails().fileInfo.absoluteFilePath(), target);
}

void WindowBehaviorTests::testThumbnailKeyboardBounds_data()
{
    QTest::addColumn<bool>("loop");
    QTest::addColumn<int>("key");
    QTest::addColumn<int>("start");
    QTest::addColumn<int>("target");
    QTest::newRow("first-closed") << false << int(Qt::Key_Left) << 1 << 0;
    QTest::newRow("last-closed") << false << int(Qt::Key_Right) << 15 << 16;
    QTest::newRow("first-wraps") << true << int(Qt::Key_Left) << 1 << 16;
    QTest::newRow("last-wraps") << true << int(Qt::Key_Right) << 15 << 0;
}

void WindowBehaviorTests::testThumbnailKeyboardBounds()
{
    QFETCH(bool, loop);
    QFETCH(int, key);
    QFETCH(int, start);
    QFETCH(int, target);
    ScopedOptionValues options({ { "loopfoldersenabled", loop } });
    ThumbnailWindow fixture(17);
    QVERIFY(QTest::qWaitForWindowActive(&fixture.window, 1000));
    auto &core = fixture.view->thumbnailImageCore();
    QTRY_COMPARE(core.getCurrentFileDetails().folderFileInfoList.size(), 17);
    const auto &listing = core.getCurrentFileDetails().folderFileInfoList;
    const auto original = listing.at(start).absoluteFilePath;
    const auto destination = listing.at(target).absoluteFilePath;
    fixture.window.openFile(original);
    QTRY_COMPARE(core.getCurrentFileDetails().loadedIndexInFolder, start);
    QTRY_VERIFY(fixture.strip->available());
    QCOMPARE(core.thumbnailLoopEnabled(), loop);
    fixture.strip->focusStrip();
    QTRY_COMPARE(fixture.strip->opacity(), 1.0);

    auto *viewport = fixture.view->viewport();
    QTest::keyClick(viewport, Qt::Key(key));
    const qreal before = fixture.strip->browseIndex();
    QTest::keyClick(viewport, Qt::Key(key));
    QCOMPARE(core.getCurrentFileDetails().fileInfo.absoluteFilePath(), original);
    QVERIFY(!core.thumbnailNavigationBusy());
    if (!loop)
        QCOMPARE(fixture.strip->browseIndex(), before);
    QVERIFY(fixture.strip->panelRect().contains(fixture.cellCenter(target)));
    QTest::keyClick(viewport, Qt::Key_Return);
    QTRY_COMPARE(core.getCurrentFileDetails().fileInfo.absoluteFilePath(), destination);
}

void WindowBehaviorTests::testThumbnailKeyboardAfterWheel()
{
    ScopedOptionValues options({ { "loopfoldersenabled", false } });
    ThumbnailWindow fixture(17);
    QVERIFY(QTest::qWaitForWindowActive(&fixture.window, 1000));
    QTRY_VERIFY(fixture.strip->available());
    QCOMPARE(fixture.window.getCurrentFileDetails().folderFileInfoList.size(), 17);
    fixture.strip->focusStrip();
    QTRY_COMPARE(fixture.strip->opacity(), 1.0);
    QVERIFY(sendThumbnailWheel(fixture, fixture.strip->panelRect().center(), { }, QPoint(-720, 0)));
    QCOMPARE(fixture.strip->browseIndex(), 6.0);
    QCOMPARE(fixture.cellCenter(0), QPoint(-1, -1));
    const auto target =
            fixture.window.getCurrentFileDetails().folderFileInfoList.at(7).absoluteFilePath;
    QTest::keyClick(fixture.view->viewport(), Qt::Key_Right);
    QTest::keyClick(fixture.view->viewport(), Qt::Key_Return);
    QTRY_COMPARE(fixture.window.getCurrentFileDetails().fileInfo.absoluteFilePath(), target);
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

void WindowBehaviorTests::testThumbnailContent_data()
{
    QTest::addColumn<QString>("fixtureKind");
    for (const auto *name : { "jpeg", "png", "raw", "eps", "svg", "ppm", "corrupt" })
        QTest::newRow(name) << QString::fromLatin1(name);
}

void WindowBehaviorTests::testThumbnailContent()
{
    QFETCH(QString, fixtureKind);
    ThumbnailWindow fixture(1);
    QString path;
    QColor expected;
    if (fixtureKind == "jpeg" || fixtureKind == "png") {
        path = fixture.dir.filePath("02." + fixtureKind);
        const QSize dimensions = fixtureKind == "jpeg" ? QSize(6000, 4000) : QSize(2500, 2000);
        expected = fixtureKind == "jpeg" ? QColor(Qt::green) : QColor(Qt::red);
        QImage input(dimensions, QImage::Format_RGB32);
        input.fill(expected);
        QVERIFY(input.save(path));
    } else if (fixtureKind == "raw") {
        path = fixture.dir.filePath("02.dng");
        QVERIFY(QFile::copy(QStringLiteral(FOVELLE_TEST_FIXTURE_DIR "/portable-hdr/bayer.dng"),
                            path));
    } else if (fixtureKind == "eps") {
        path = createEPSVectorImage(fixture.dir, QStringLiteral("02"));
        QVERIFY(!path.isEmpty());
    } else {
        path = fixture.dir.filePath(fixtureKind == "corrupt" ? "02.png" : "02." + fixtureKind);
        QByteArray bytes;
        if (fixtureKind == "svg") {
            expected = Qt::blue;
            bytes = "<svg xmlns='http://www.w3.org/2000/svg' width='6000' height='4000'>"
                    "<rect width='6000' height='4000' fill='blue'/></svg>";
        } else if (fixtureKind == "ppm") {
            expected = Qt::green;
            bytes = "P6\n2 2\n255\n" + QByteArray::fromHex("00ff0000ff0000ff0000ff00");
        } else {
            bytes = "Not a PNG image";
        }
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(bytes), bytes.size());
    }

    qvApp->invalidateFolderListings();
    fixture.view->thumbnailImageCore().refreshThumbnailListing();
    QTRY_VERIFY(fixture.strip->available());
    fixture.hover();
    const auto target = [&] {
        for (const auto &cell : fixture.strip->cells()) {
            if (cell.path == path)
                return cell;
        }
        return QVThumbnailStrip::Cell{ };
    };
    QTRY_VERIFY_WITH_TIMEOUT(!target().version.isEmpty(), 10000);
    const auto cell = target();
    const auto &preview = cell.preview;
    if (fixtureKind == "corrupt") {
        QVERIFY(preview.isNull());
        QVERIFY(cell.rect.isValid());
        return;
    }

    QVERIFY2(!preview.isNull(), qPrintable(path));
    const qreal dpr = fixture.view->viewport()->devicePixelRatioF();
    QVERIFY(preview.width() <= std::ceil(cell.rect.width() * dpr));
    QVERIFY(preview.height() <= std::ceil(cell.rect.height() * dpr));
    const QColor center = preview.pixelColor(preview.rect().center());
    QVERIFY(center.alpha() > 0);
    if (expected.isValid()) {
        QVERIFY(std::abs(center.red() - expected.red()) <= 12);
        QVERIFY(std::abs(center.green() - expected.green()) <= 12);
        QVERIFY(std::abs(center.blue() - expected.blue()) <= 12);
    }
    if (fixtureKind == "eps") {
        QVERIFY(center.lightness() < 32);
        const QColor square = preview.pixelColor(preview.width() / 4, preview.height() / 2);
        QVERIFY(square.lightness() > 224);
    }
}

void WindowBehaviorTests::testThumbnailTransition()
{
    if (QVCocoaFunctions::reduceThumbnailMotion())
        QSKIP("Reduce Motion is enabled on the test host.");
    ThumbnailWindow fixture;
    QTRY_VERIFY(fixture.strip->available());
    fixture.hover();
    QTRY_COMPARE(fixture.strip->opacity(), 1.0);
    fixture.window.nextFile();
    QTRY_VERIFY(fixture.strip->transitionProgress() > 0 && fixture.strip->transitionProgress() < 1);
    QCOMPARE(fixture.window.getCurrentFileDetails().fileInfo.absoluteFilePath(), fixture.next);
    QVERIFY(!fixture.strip->hitTest(fixture.nextCenter()));
    QTRY_COMPARE(fixture.strip->transitionProgress(), 1.0);
    QVERIFY(qAbs(fixture.cellCenter(1).x() - fixture.strip->panelRect().center().x()) <= 1);
}

void WindowBehaviorTests::testThumbnailWheelBrowse()
{
    ThumbnailWindow fixture(8);
    QTRY_VERIFY(fixture.strip->available());
    fixture.hover();
    QTRY_COMPARE(fixture.strip->opacity(), 1.0);
    const qreal zoom = fixture.view->getZoomLevel();
    const int horizontal = fixture.view->horizontalScrollBar()->value();
    const int vertical = fixture.view->verticalScrollBar()->value();
    const QPoint pos = fixture.strip->panelRect().center();
    QVERIFY(sendThumbnailWheel(fixture, pos, { }, QPoint(0, -120)));
    QCOMPARE(fixture.strip->browseIndex(), 1.0);
    QCOMPARE(fixture.window.getCurrentFileDetails().fileInfo.absoluteFilePath(), fixture.first);
    QCOMPARE(fixture.view->getZoomLevel(), zoom);
    QCOMPARE(fixture.view->horizontalScrollBar()->value(), horizontal);
    QCOMPARE(fixture.view->verticalScrollBar()->value(), vertical);
}

void WindowBehaviorTests::testThumbnailScrollMomentum()
{
    ThumbnailWindow fixture(8);
    QTRY_VERIFY(fixture.strip->available());
    fixture.hover();
    QTRY_COMPARE(fixture.strip->opacity(), 1.0);
    const QPoint pos = fixture.strip->panelRect().center();
    const QPoint outside(fixture.view->viewport()->width() / 2, 160);
    QVERIFY(sendThumbnailWheel(fixture, pos, { }, { }, Qt::ScrollBegin));
    QVERIFY(sendThumbnailWheel(fixture, pos, QPoint(-76, 0), { }, Qt::ScrollUpdate));
    QCOMPARE(fixture.strip->browseIndex(), 0.5);
    QVERIFY(sendThumbnailWheel(fixture, outside, { }, { }, Qt::ScrollEnd));
    QVERIFY(sendThumbnailWheel(fixture, outside, QPoint(-76, 0), { }, Qt::ScrollMomentum));
    QCOMPARE(fixture.strip->browseIndex(), 1.0);
    QCOMPARE(fixture.window.getCurrentFileDetails().fileInfo.absoluteFilePath(), fixture.first);
}

void WindowBehaviorTests::testThumbnailWheelOutside()
{
    ScopedOptionValues options(
            { { "viewporthorizontalscrollaction", int(Qv::ViewportScrollAction::Navigate) } });
    ThumbnailWindow fixture;
    QTRY_VERIFY(fixture.strip->available());
    fixture.hover();
    QTRY_COMPARE(fixture.strip->opacity(), 1.0);
    sendThumbnailWheel(fixture, QPoint(400, 140), { }, QPoint(-120, 0));
    QTRY_COMPARE(fixture.window.getCurrentFileDetails().fileInfo.absoluteFilePath(), fixture.next);
}

void WindowBehaviorTests::testThumbnailScrollBounds()
{
    ThumbnailWindow fixture(8);
    QTRY_VERIFY(fixture.strip->available());
    fixture.hover();
    QTRY_COMPARE(fixture.strip->opacity(), 1.0);
    const QPoint pos = fixture.strip->panelRect().center();
    QVERIFY(sendThumbnailWheel(fixture, pos, { }, QPoint(-12000, 0)));
    QCOMPARE(fixture.strip->browseIndex(), 7.0);
    QVERIFY(sendThumbnailWheel(fixture, pos, { }, QPoint(-120, 0)));
    QCOMPARE(fixture.strip->browseIndex(), 7.0);
    QVERIFY(sendThumbnailWheel(fixture, pos, { }, QPoint(12000, 0)));
    QCOMPARE(fixture.strip->browseIndex(), 0.0);
    QCOMPARE(fixture.window.getCurrentFileDetails().fileInfo.absoluteFilePath(), fixture.first);
}

void WindowBehaviorTests::testThumbnailClickAfterScroll()
{
    ThumbnailWindow fixture(8);
    QTRY_VERIFY(fixture.strip->available());
    fixture.hover();
    QTRY_COMPARE(fixture.strip->opacity(), 1.0);
    const auto target =
            fixture.window.getCurrentFileDetails().folderFileInfoList.at(5).absoluteFilePath;
    QCOMPARE(fixture.cellCenter(5), QPoint(-1, -1));
    QVERIFY(sendThumbnailWheel(fixture, fixture.strip->panelRect().center(), { }, QPoint(-600, 0)));
    const QPoint pos = fixture.cellCenter(5);
    QVERIFY(fixture.strip->panelRect().contains(pos));
    QTest::mouseClick(fixture.view->viewport(), Qt::LeftButton, Qt::NoModifier, pos);
    QTRY_COMPARE(fixture.window.getCurrentFileDetails().fileInfo.absoluteFilePath(), target);
    QTRY_COMPARE(fixture.strip->transitionProgress(), 1.0);
    QCOMPARE(fixture.strip->browseIndex(), 5.0);
}

void WindowBehaviorTests::testThumbnailNativeBrowse()
{
    ThumbnailWindow fixture(8);
    QTRY_VERIFY(fixture.strip->available());
    fixture.hover();
    QTRY_COMPARE(fixture.strip->opacity(), 1.0);
    const QPoint pos = fixture.strip->panelRect().center();
    QVERIFY(sendNativeGesture(fixture.view, Qt::PanNativeGesture, pos, 0, QPointF(-76, 0)));
    QCOMPARE(fixture.strip->browseIndex(), 0.5);
    QVERIFY(sendNativeGesture(fixture.view, Qt::SwipeNativeGesture, pos, 180));
    QCOMPARE(fixture.strip->browseIndex(), 1.5);
    QCOMPARE(fixture.window.getCurrentFileDetails().fileInfo.absoluteFilePath(), fixture.first);
}

void WindowBehaviorTests::testThumbnailReopenRecenters()
{
    ThumbnailWindow fixture(8);
    QTRY_VERIFY(fixture.strip->available());
    fixture.hover();
    QTRY_COMPARE(fixture.strip->opacity(), 1.0);
    QVERIFY(sendThumbnailWheel(fixture, fixture.strip->panelRect().center(), { }, QPoint(-240, 0)));
    QCOMPARE(fixture.strip->browseIndex(), 2.0);
    sendMouseMove(fixture.view->viewport(), QPoint(400, 140));
    QTRY_COMPARE(fixture.strip->opacity(), 0.0);
    fixture.hover();
    QTRY_COMPARE(fixture.strip->opacity(), 1.0);
    QCOMPARE(fixture.strip->browseIndex(), 0.0);
}

void WindowBehaviorTests::testThumbnailTransitionResize()
{
    if (QVCocoaFunctions::reduceThumbnailMotion())
        QSKIP("Reduce Motion is enabled on the test host.");
    ThumbnailWindow fixture(8);
    QTRY_VERIFY(fixture.strip->available());
    fixture.hover();
    QTRY_COMPARE(fixture.strip->opacity(), 1.0);
    const int initialWidth = fixture.strip->panelRect().width();
    fixture.window.nextFile();
    QTRY_VERIFY(fixture.strip->transitionProgress() < 1);
    fixture.window.resize(680, 600);
    QTRY_VERIFY(fixture.strip->panelRect().width() < initialWidth);
    QVERIFY(fixture.strip->transitionProgress() < 1);
    QTRY_COMPARE(fixture.strip->transitionProgress(), 1.0);
}

void WindowBehaviorTests::testThumbnailTransitionRetarget()
{
    if (QVCocoaFunctions::reduceThumbnailMotion())
        QSKIP("Reduce Motion is enabled on the test host.");
    ThumbnailWindow fixture;
    QTRY_VERIFY(fixture.strip->available());
    fixture.hover();
    QTRY_COMPARE(fixture.strip->opacity(), 1.0);
    const auto third =
            fixture.window.getCurrentFileDetails().folderFileInfoList.at(2).absoluteFilePath;
    fixture.window.nextFile();
    QTRY_VERIFY(fixture.strip->transitionProgress() < 1);
    fixture.window.nextFile();
    QTRY_COMPARE(fixture.window.getCurrentFileDetails().fileInfo.absoluteFilePath(), third);
    QVERIFY(fixture.strip->transitionProgress() < 1);
    QTRY_COMPARE(fixture.strip->transitionProgress(), 1.0);
    QCOMPARE(fixture.strip->browseIndex(), 2.0);
}

namespace {
QVThumbnailStrip::Cell thumbnailCell(const ThumbnailWindow &fixture, const QString &path)
{
    for (const auto &cell : fixture.strip->cells()) {
        if (cell.path == path)
            return cell;
    }
    return { };
}

void revealNewThumbnail(ThumbnailWindow &fixture)
{
    qvApp->invalidateFolderListings();
    fixture.view->thumbnailImageCore().refreshThumbnailListing();
    QTRY_VERIFY(fixture.strip->available());
    fixture.hover();
}

QImage orientationLuma(const QImage &input)
{
    QImage image = input.scaled(32, 32, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    if (image.colorSpace().isValid())
        image = image.convertedToColorSpace(QColorSpace::SRgb);
    return image.convertToFormat(QImage::Format_Grayscale8);
}

double orientationCorrelation(const QImage &a, const QImage &b)
{
    if (a.isNull() || b.isNull())
        return -1;
    double sumA = 0, sumB = 0, sumAA = 0, sumBB = 0, sumAB = 0;
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x) {
            const double pa = a.constScanLine(y)[x];
            const double pb = b.constScanLine(y)[x];
            sumA += pa;
            sumB += pb;
            sumAA += pa * pa;
            sumBB += pb * pb;
            sumAB += pa * pb;
        }
    }
    const int count = a.width() * a.height();
    const double scale = std::sqrt((count * sumAA - sumA * sumA) * (count * sumBB - sumB * sumB));
    return scale > 0 ? (count * sumAB - sumA * sumB) / scale : -1;
}

void verifyRawThumbnailOrientation(ThumbnailWindow &fixture, const QString &path,
                                   const QImage &preview)
{
    QVERIFY(!preview.isNull());
    fixture.window.openFile(path);
    QTRY_COMPARE_WITH_TIMEOUT(fixture.window.getCurrentFileDetails().fileInfo.absoluteFilePath(),
                              path, 15000);
    const auto main = fixture.view->thumbnailImageCore().getLoadedPixmap().toImage();
    QVERIFY(!main.isNull());
    const qreal ratio = qreal(main.width()) / main.height();
    QVERIFY(qAbs(ratio - qreal(preview.width()) / preview.height()) < 0.04);
    const auto mainLuma = orientationLuma(main);
    const auto previewLuma = orientationLuma(preview);
    const double aligned = orientationCorrelation(mainLuma, previewLuma);
    double alternative = -1;
    for (int angle : { 0, 90, 180, 270 }) {
        const auto rotated = previewLuma.transformed(QTransform().rotate(angle));
        if (angle != 0)
            alternative = qMax(alternative, orientationCorrelation(mainLuma, rotated));
        alternative = qMax(alternative,
                           orientationCorrelation(
                                   mainLuma, rotated.transformed(QTransform::fromScale(-1, 1))));
    }
    QVERIFY2(aligned > 0.80 && aligned - alternative > 0.10,
             qPrintable(QString("aligned=%1 best_alternative=%2").arg(aligned).arg(alternative)));
}

bool writeOrientedFixtureDng(const QString &path, quint16 orientation)
{
    QFile source(QStringLiteral(FOVELLE_TEST_FIXTURE_DIR "/portable-hdr/bayer.dng"));
    if (!source.open(QIODevice::ReadOnly))
        return false;
    auto bytes = source.readAll();
    if (bytes.size() < 10 || bytes.left(4) != QByteArray("II*\0", 4))
        return false;
    const qsizetype directory = qFromLittleEndian<quint32>(bytes.constData() + 4);
    if (directory > bytes.size() - 2)
        return false;
    const int count = qFromLittleEndian<quint16>(bytes.constData() + directory);
    if (bytes.size() - directory - 2 < count * 12)
        return false;
    for (int i = 0; i < count; ++i) {
        const qsizetype field = directory + 2 + i * 12;
        if (qFromLittleEndian<quint16>(bytes.constData() + field) == 274
            && qFromLittleEndian<quint16>(bytes.constData() + field + 2) == 3
            && qFromLittleEndian<quint32>(bytes.constData() + field + 4) == 1) {
            qToLittleEndian(orientation, bytes.data() + field + 8);
            QFile output(path);
            return output.open(QIODevice::WriteOnly) && output.write(bytes) == bytes.size();
        }
    }
    return false;
}
} // namespace

void WindowBehaviorTests::testThumbnailLongPng()
{
    ThumbnailWindow fixture(1);
    const QString path = fixture.dir.filePath("02-long.png");
    const QSize dimensions(3000, 24000);
    {
        QImage original(dimensions, QImage::Format_Grayscale8);
        QVERIFY(!original.isNull());
        for (int y = 0; y < original.height(); ++y)
            std::memset(original.scanLine(y), y < original.height() / 2 ? 32 : 224,
                        original.width());
        QVERIFY(original.save(path));
    }
    revealNewThumbnail(fixture);
    QTRY_VERIFY_WITH_TIMEOUT(!thumbnailCell(fixture, path).version.isEmpty(), 15000);
    const auto cell = thumbnailCell(fixture, path);
    const auto &image = cell.preview;
    QVERIFY(!image.isNull());
    const qreal dpr = fixture.view->viewport()->devicePixelRatioF();
    QVERIFY(image.height() <= std::ceil(cell.rect.height() * dpr));
    QVERIFY(image.width() <= std::ceil(cell.rect.width() * dpr));
    QVERIFY(qAbs(qreal(image.width()) / image.height()
                 - qreal(dimensions.width()) / dimensions.height())
            < 0.012);
    QVERIFY(image.pixelColor(image.width() / 2, image.height() / 4).lightness() < 64);
    QVERIFY(image.pixelColor(image.width() / 2, 3 * image.height() / 4).lightness() > 192);
}

void WindowBehaviorTests::testThumbnailRawOrientation_data()
{
    QTest::addColumn<int>("orientation");
    QTest::newRow("exif6") << 6;
    QTest::newRow("exif8") << 8;
}

void WindowBehaviorTests::testThumbnailRawOrientation()
{
    QFETCH(int, orientation);
    ThumbnailWindow fixture(1);
    const QString path = fixture.dir.filePath("02-rotated.dng");
    QVERIFY(writeOrientedFixtureDng(path, quint16(orientation)));
    revealNewThumbnail(fixture);
    QTRY_VERIFY_WITH_TIMEOUT(!thumbnailCell(fixture, path).version.isEmpty(), 15000);
    verifyRawThumbnailOrientation(fixture, path, thumbnailCell(fixture, path).preview);
}

void WindowBehaviorTests::testThumbnailReportedSample_data()
{
    QTest::addColumn<QByteArray>("variable");
    QTest::addColumn<QSize>("expectedSize");
    QTest::newRow("jaguar-tiff") << QByteArray("FOVELLE_REPORTED_THUMBNAIL_TIFF")
                                 << QSize(11409, 8705);
    QTest::newRow("fireshot-png") << QByteArray("FOVELLE_REPORTED_THUMBNAIL_PNG")
                                  << QSize(3352, 24126);
    QTest::newRow("sony-arw") << QByteArray("FOVELLE_REPORTED_THUMBNAIL_ARW") << QSize(4672, 7008);
    QTest::newRow("nikon-nef") << QByteArray("FOVELLE_REPORTED_THUMBNAIL_NEF") << QSize(4256, 2832);
}

void WindowBehaviorTests::testThumbnailReportedSample()
{
    QFETCH(QByteArray, variable);
    QFETCH(QSize, expectedSize);
    const QString original = qEnvironmentVariable(variable.constData());
    if (original.isEmpty())
        QSKIP("Set the reported-sample path to run this local regression.");
    QVERIFY2(QVCocoaFunctions::thumbnailMayReadContent(original), qPrintable(original));
    ThumbnailWindow fixture(1);
    const QString path = fixture.dir.filePath("02." + QFileInfo(original).suffix());
    const bool tiff = variable.endsWith("_TIFF");
    const auto version = QVImageLoader::fileVersion(original);
    QVERIFY(!version.isEmpty());
    if (tiff)
        QVERIFY(QFile::link(original, path));
    else
        QVERIFY(QFile::copy(original, path));
    revealNewThumbnail(fixture);
    QTRY_VERIFY_WITH_TIMEOUT(!thumbnailCell(fixture, path).version.isEmpty(), tiff ? 60000 : 15000);
    const auto cell = thumbnailCell(fixture, path);
    const QImage preview = cell.preview;
    QVERIFY(!preview.isNull());
    QVERIFY(qAbs(qreal(preview.width()) / preview.height()
                 - qreal(expectedSize.width()) / expectedSize.height())
            < 0.025);
    if (variable.endsWith("_PNG")) {
        const auto luma = orientationLuma(preview);
        int darkest = 255, brightest = 0;
        for (int y = 0; y < luma.height(); ++y) {
            for (int x = 0; x < luma.width(); ++x) {
                darkest = qMin(darkest, int(luma.constScanLine(y)[x]));
                brightest = qMax(brightest, int(luma.constScanLine(y)[x]));
            }
        }
        QVERIFY(brightest - darkest > 32);
    } else if (tiff) {
        // Downsampled from an independent sips export, with ICC converted to sRGB.
        const auto bytes = QByteArray::fromHex(
                "969bbac29b8a564d807889b68f52343884838167603e313b7171666b48434148"
                "5f6b6066484742404f515072886b572e4f4848596f68593a6b4c5453513e5f5d");
        QCOMPARE(bytes.size(), 64);
        const QImage reference(reinterpret_cast<const uchar *>(bytes.constData()), 8, 8, 8,
                               QImage::Format_Grayscale8);
        const auto observed = preview.scaled(8, 8, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                                      .convertToFormat(QImage::Format_Grayscale8);
        const auto correlation = orientationCorrelation(reference, observed);
        qInfo() << "Jaguar thumbnail" << preview.size() << "luma correlation" << correlation;
        QVERIFY(correlation > 0.90);
        QCOMPARE(preview.colorSpace(), QColorSpace(QColorSpace::SRgb));
        const qreal dpr = fixture.view->viewport()->devicePixelRatioF();
        QVERIFY(preview.width() <= qCeil(cell.rect.width() * dpr));
        QVERIFY(preview.height() <= qCeil(cell.rect.height() * dpr));
        QVERIFY(!cell.currentProxy);
        QCOMPARE(fixture.window.getCurrentFileDetails().fileInfo.absoluteFilePath(), fixture.first);
        QCOMPARE(QVImageLoader::fileVersion(original), version);
    } else {
        verifyRawThumbnailOrientation(fixture, path, preview);
    }
}

namespace {
QByteArray pngFixtureChunk(const QByteArray &type, const QByteArray &payload)
{
    QByteArray chunk(4, '\0');
    qToBigEndian(quint32(payload.size()), chunk.data());
    chunk.append(type);
    chunk.append(payload);
    const auto crc = crc32(0, reinterpret_cast<const Bytef *>(chunk.constData() + 4),
                           uInt(chunk.size() - 4));
    char checksum[4];
    qToBigEndian(quint32(crc), checksum);
    chunk.append(checksum, sizeof(checksum));
    return chunk;
}

bool writeThumbnailPng(const QString &path, const QSize &size, bool adam7,
                       const QByteArray &variant)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    const auto write = [&file](const QByteArray &bytes) {
        return file.write(bytes) == bytes.size();
    };
    QByteArray header(13, '\0');
    qToBigEndian(quint32(size.width()), header.data());
    qToBigEndian(quint32(size.height()), header.data() + 4);
    header[8] = adam7 ? 16 : 8;
    header[9] = adam7 ? 6 : 2;
    header[12] = adam7 ? 1 : 0;
    if (!write(QByteArray::fromHex("89504e470d0a1a0a")) || !write(pngFixtureChunk("IHDR", header)))
        return false;
    if (variant == "file-limit")
        return file.resize(2LL * 1024 * 1024 * 1024 + 1);
    if (variant == "pixel-limit" || variant == "edge-limit")
        return write(pngFixtureChunk("IEND", { }));
    if (adam7
        && (!write(pngFixtureChunk(
                    "eXIf",
                    QByteArray::fromHex("49492a0008000000010012010300010000000600000000000000")))
            || !write(pngFixtureChunk("cICP", QByteArray::fromHex("0c0d0001")))))
        return false;

    z_stream stream{ };
    if (deflateInit(&stream, Z_BEST_SPEED) != Z_OK)
        return false;
    const auto cleanup = qScopeGuard([&stream] { deflateEnd(&stream); });
    QByteArray compressed(64 * 1024, '\0');
    const auto feed = [&](QByteArray &row, const int flush) {
        stream.next_in = reinterpret_cast<Bytef *>(row.data());
        stream.avail_in = uInt(row.size());
        do {
            stream.next_out = reinterpret_cast<Bytef *>(compressed.data());
            stream.avail_out = uInt(compressed.size());
            const int status = deflate(&stream, flush);
            if (status != Z_OK && status != Z_STREAM_END)
                return false;
            const int written = int(compressed.size()) - int(stream.avail_out);
            if (written > 0 && !write(pngFixtureChunk("IDAT", compressed.left(written))))
                return false;
            if (status == Z_STREAM_END)
                return true;
        } while (stream.avail_in != 0 || stream.avail_out == 0);
        return flush != Z_FINISH;
    };
    constexpr int starts[7][2] = { { 0, 0 }, { 4, 0 }, { 0, 4 }, { 2, 0 },
                                   { 0, 2 }, { 1, 0 }, { 0, 1 } };
    constexpr int strides[7][2] = { { 8, 8 }, { 8, 8 }, { 4, 8 }, { 4, 4 },
                                    { 2, 4 }, { 2, 2 }, { 1, 2 } };
    for (int pass = 0; pass < (adam7 ? 7 : 1); ++pass) {
        const int startX = adam7 ? starts[pass][0] : 0;
        const int startY = adam7 ? starts[pass][1] : 0;
        const int stepX = adam7 ? strides[pass][0] : 1;
        const int stepY = adam7 ? strides[pass][1] : 1;
        QByteArray upper(1, '\0'), lower(1, '\0');
        for (int x = startX; x < size.width(); x += stepX) {
            upper.append(adam7 ? QByteArray::fromHex("a00040002000ffff")
                               : QByteArray::fromHex("ff0000"));
            lower.append(adam7 ? QByteArray::fromHex("20004000a0008080")
                               : QByteArray::fromHex("0000ff"));
        }
        for (int y = startY; y < size.height(); y += stepY) {
            if (!feed(y < size.height() / 2 ? upper : lower, Z_NO_FLUSH))
                return false;
        }
    }
    QByteArray end;
    return feed(end, Z_FINISH) && (variant == "truncated" || write(pngFixtureChunk("IEND", { })));
}

void verifyThumbnailBounds(const ThumbnailWindow &fixture, const QVThumbnailStrip::Cell &cell,
                           const QSize &source)
{
    const QImage &image = cell.preview;
    QVERIFY(!image.isNull());
    QVERIFY(!cell.currentProxy);
    const qreal dpr = fixture.view->viewport()->devicePixelRatioF();
    const QSize limits(qCeil(cell.rect.width() * dpr), qCeil(cell.rect.height() * dpr));
    QVERIFY(image.width() <= limits.width());
    QVERIFY(image.height() <= limits.height());
    if (source.isValid()) {
        const QSize ideal = source.scaled(limits, Qt::KeepAspectRatio);
        QVERIFY(qAbs(image.width() - ideal.width()) <= 2);
        QVERIFY(qAbs(image.height() - ideal.height()) <= 2);
    }
    QCOMPARE(image.colorSpace(), QColorSpace(QColorSpace::SRgb));
    QCOMPARE(fixture.window.getCurrentFileDetails().fileInfo.absoluteFilePath(), fixture.first);
}
} // namespace

void WindowBehaviorTests::testThumbnailLargeWebp_data()
{
    QTest::addColumn<QString>("fixtureName");
    QTest::newRow("lossless-vp8l") << QStringLiteral("thumbnail-large-alpha.webp");
    QTest::newRow("lossy-vp8x") << QStringLiteral("thumbnail-large-lossy-alpha.webp");
}

void WindowBehaviorTests::testThumbnailLargeWebp()
{
    QFETCH(QString, fixtureName);
    ThumbnailWindow fixture(1);
    const QString path = fixture.dir.filePath("02-large.webp");
    QVERIFY(QFile::copy(QStringLiteral(FOVELLE_TEST_FIXTURE_DIR "/") + fixtureName, path));
    revealNewThumbnail(fixture);
    QTRY_VERIFY(!thumbnailCell(fixture, path).version.isEmpty());
    const auto cell = thumbnailCell(fixture, path);
    QVERIFY(!cell.preview.isNull());
    verifyThumbnailBounds(fixture, cell, QSize(4200, 2400));
    const int x = cell.preview.width() / 2;
    const QColor upper = cell.preview.pixelColor(x, cell.preview.height() / 4);
    QVERIFY(upper.red() > 240 && upper.green() < 15 && upper.blue() < 15);
    QCOMPARE(upper.alpha(), 255);
    QCOMPARE(cell.preview.pixelColor(x, cell.preview.height() * 3 / 4).alpha(), 0);
}

void WindowBehaviorTests::testThumbnailStreamingPng_data()
{
    QTest::addColumn<bool>("adam7");
    QTest::newRow("rgb8") << false;
    QTest::newRow("oriented-adam7-rgba16") << true;
}

void WindowBehaviorTests::testThumbnailStreamingPng()
{
    QFETCH(bool, adam7);
    ThumbnailWindow fixture(1);
    const QString path = fixture.dir.filePath("02-large.png");
    const QSize source(8201, 12201);
    QVERIFY(writeThumbnailPng(path, source, adam7, { }));
    revealNewThumbnail(fixture);
    QTRY_VERIFY_WITH_TIMEOUT(!thumbnailCell(fixture, path).version.isEmpty(), 30000);
    const auto cell = thumbnailCell(fixture, path);
    const auto &image = cell.preview;
    QVERIFY(!image.isNull());
    verifyThumbnailBounds(fixture, cell, adam7 ? source.transposed() : source);
    if (adam7) {
        const int y = image.height() / 2;
        const QColor left = image.pixelColor(image.width() / 4, y);
        const QColor right = image.pixelColor(image.width() * 3 / 4, y);
        QVERIFY(left.blue() > 145 && left.red() < 35);
        QVERIFY(right.red() > 170 && right.blue() < 35);
        QVERIFY(qAbs(left.alpha() - 128) <= 1);
        QCOMPARE(right.alpha(), 255);
    } else {
        const int x = image.width() / 2;
        QCOMPARE(image.pixelColor(x, image.height() / 4), QColor(Qt::red));
        QCOMPARE(image.pixelColor(x, image.height() * 3 / 4), QColor(Qt::blue));
    }
}

void WindowBehaviorTests::testThumbnailStreamingPngRejected_data()
{
    QTest::addColumn<QByteArray>("variant");
    QTest::addColumn<QSize>("source");
    QTest::newRow("file-limit") << QByteArray("file-limit") << QSize(8201, 12201);
    QTest::newRow("pixel-limit") << QByteArray("pixel-limit") << QSize(40000, 40000);
    QTest::newRow("edge-limit") << QByteArray("edge-limit") << QSize(65537, 1000);
    QTest::newRow("truncated") << QByteArray("truncated") << QSize(8201, 12201);
}

void WindowBehaviorTests::testThumbnailStreamingPngRejected()
{
    QFETCH(QByteArray, variant);
    QFETCH(QSize, source);
    ThumbnailWindow fixture(2);
    const QString path = fixture.dir.filePath("03-rejected.png");
    QVERIFY(writeThumbnailPng(path, source, false, variant));
    revealNewThumbnail(fixture);
    QTRY_VERIFY_WITH_TIMEOUT(!thumbnailCell(fixture, path).version.isEmpty(), 30000);
    QVERIFY(thumbnailCell(fixture, path).preview.isNull());
    QTRY_VERIFY(!thumbnailCell(fixture, fixture.next).preview.isNull());
}

void WindowBehaviorTests::testThumbnailCorpus_data()
{
    QTest::addColumn<QString>("original");
    const QString base = qEnvironmentVariable("FOVELLE_THUMBNAIL_CORPUS");
    if (base.isEmpty()) {
        QTest::newRow("unconfigured") << QString();
        return;
    }
    for (const QString &name :
         { QStringLiteral("sdr_test"), QStringLiteral("hdr_test"), QStringLiteral("mix_test"),
           QStringLiteral("用于UI截图的示例图片") }) {
        const QDir dir(QDir(base).filePath(name));
        const auto files = dir.entryList(QDir::Files);
        if (files.isEmpty())
            QTest::newRow(qPrintable(name)) << dir.absolutePath();
        for (const QString &file : files) {
            QTest::newRow(qPrintable(name + '/' + file)) << dir.absoluteFilePath(file);
        }
    }
}

void WindowBehaviorTests::testThumbnailCorpus()
{
    QFETCH(QString, original);
    if (original.isEmpty())
        QSKIP("Set FOVELLE_THUMBNAIL_CORPUS to the mounted sample directory.");
    const QByteArray originalVersion = QVImageLoader::fileVersion(original);
    QVERIFY(!originalVersion.isEmpty());
    ThumbnailWindow fixture(1);
    const QString name = QFileInfo(original).fileName();
    const QString path = fixture.dir.filePath("02-" + name);
    QVERIFY(QFile::link(original, path));
    qvApp->invalidateFolderListings();
    fixture.view->thumbnailImageCore().refreshThumbnailListing();
    QTRY_VERIFY(fixture.strip->available());
    fixture.strip->focusStrip();
    QTRY_COMPARE(fixture.strip->opacity(), 1.0);
    QTRY_VERIFY_WITH_TIMEOUT(!thumbnailCell(fixture, path).version.isEmpty(), 60000);
    const auto cell = thumbnailCell(fixture, path);
    qInfo() << name << cell.preview.size();
    QVERIFY(!cell.preview.isNull());

    QSize expected;
    QByteArray lumaHex;
    if (name == "5.webp") {
        expected = QSize(4275, 2451);
        lumaHex = "33251b44534c473d3b2d1c48675d574f5c21202650743f3e38103e1727871a29"
                  "292c65313e592e505e755f5995957d835267706f7d8d6a350e1c1a1a2c251a08";
    } else if (name == "9.png") {
        expected = QSize(23094, 31390);
        lumaHex = "a2a09d9b97979a95aea8a5aba7a39e97adbcaaa7aeada8a39fabb3a0afacab949"
                  "d9bac8e72a69e8d9492966849739382778893846d5e786660626d81706d635e";
    } else if (name == "macOS 2.webp") {
        expected = QSize(6016, 6016);
        lumaHex = "ceb4a1b1aa9c91bebabaa87876a8b7bd96985161654e887a2f2c318a8b2a32a03"
                  "46fb9c4c3a666a1b2a1d4b9b4e1be79cf8ac5a9a1ddcfc5ba84a5998fc0bc7b";
    }
    verifyThumbnailBounds(fixture, cell, expected);
    if (!lumaHex.isEmpty()) {
        // ImageIO reference exported independently from the preview queue.
        const auto bytes = QByteArray::fromHex(lumaHex);
        QCOMPARE(bytes.size(), 64);
        const QImage reference(reinterpret_cast<const uchar *>(bytes.constData()), 8, 8, 8,
                               QImage::Format_Grayscale8);
        const QImage actual =
                cell.preview.scaled(8, 8, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                        .convertToFormat(QImage::Format_Grayscale8);
        const double correlation = orientationCorrelation(actual, reference);
        QVERIFY2(correlation > 0.9, qPrintable(QString::number(correlation)));
    }
    QCOMPARE(QVImageLoader::fileVersion(original), originalVersion);
}

namespace {
constexpr qint64 MiB = 1024 * 1024;

bool writeStripedTiff(const QString &path, const QSize &size, const int bits, const int channels,
                      const int orientation, const bool little, const qint64 fileBytes)
{
    QByteArray header(little ? "II" : "MM");
    const auto appendNumber = [little, &header](auto value) {
        char bytes[sizeof(value)];
        if (little)
            qToLittleEndian(value, bytes);
        else
            qToBigEndian(value, bytes);
        header.append(bytes, sizeof(value));
    };
    constexpr quint16 TagCount = 12;
    constexpr quint32 DataOffset = 8 + 2 + TagCount * 12 + 4;
    const quint32 countsOffset = DataOffset + size.height() * 4;
    const quint32 bitsOffset = countsOffset + size.height() * 4;
    const quint32 stripeOffset = bitsOffset + channels * 2;
    const quint32 rowBytes = size.width() * channels * (bits / 8);
    const auto entry = [&appendNumber](quint16 tag, quint16 type, quint32 count, quint32 value) {
        appendNumber(tag);
        appendNumber(type);
        appendNumber(count);
        if (type == 3 && count == 1) {
            appendNumber(quint16(value));
            appendNumber(quint16(0));
        } else {
            appendNumber(value);
        }
    };

    appendNumber(quint16(42));
    appendNumber(quint32(8));
    appendNumber(TagCount);
    entry(256, 4, 1, size.width());
    entry(257, 4, 1, size.height());
    entry(258, 3, channels, channels == 1 ? bits : bitsOffset);
    entry(259, 3, 1, 1); // Uncompressed.
    entry(262, 3, 1, channels == 4 ? 5 : channels == 3 ? 2 : 1); // CMYK, RGB, gray.
    entry(273, 4, size.height(), DataOffset);
    entry(274, 3, 1, orientation);
    entry(277, 3, 1, channels);
    entry(278, 4, 1, 1); // One scanline per strip.
    entry(279, 4, size.height(), countsOffset);
    entry(284, 3, 1, 1); // Interleaved samples.
    entry(339, 3, 1, 1); // Unsigned samples.
    appendNumber(quint32(0)); // No next IFD.
    // All strips in each half share one scanline. Trailing padding exercises the
    // encoded-file limit without writing hundreds of MiB into the test directory.
    for (int y = 0; y < size.height(); ++y)
        appendNumber(stripeOffset + (y < size.height() / 2 ? 0 : rowBytes));
    for (int y = 0; y < size.height(); ++y)
        appendNumber(rowBytes);
    for (int c = 0; c < channels; ++c)
        appendNumber(quint16(bits));
    if (channels == 4) {
        QByteArray dark(rowBytes, char(0));
        for (int x = 0; x < dark.size(); x += channels)
            dark[x + 3] = char(220);
        header += dark;
        header += QByteArray(rowBytes, char(0));
    } else {
        header += QByteArray(rowBytes, char(32));
        header += QByteArray(rowBytes, char(224));
    }
    QFile file(path);
    // ImageIO also checks the nominal extent of uncompressed strips against
    // the file length, even when the strip table reuses the same scanline.
    const qint64 nominalBytes = stripeOffset + qint64(rowBytes) * size.height();
    return file.open(QIODevice::WriteOnly) && file.write(header) == header.size()
            && file.resize(qMax(fileBytes, qMax(nominalBytes, qint64(header.size()))));
}
} // namespace

void WindowBehaviorTests::testThumbnailLargeTiff_data()
{
    QTest::addColumn<QSize>("source");
    QTest::addColumn<int>("channels");
    QTest::addColumn<int>("orientation");
    QTest::addColumn<bool>("little");
    QTest::addColumn<qint64>("bytes");
    QTest::newRow("ordinary") << QSize(128, 88) << 3 << 1 << true << qint64(0);
    QTest::newRow("file-only") << QSize(640, 480) << 3 << 1 << true << (256 * MiB + 1);
    QTest::newRow("pixels-only-rotated") << QSize(10000, 6401) << 1 << 6 << true << qint64(0);
    QTest::newRow("both-cmyk-big-endian") << QSize(12000, 7500) << 4 << 1 << false << (512 * MiB);
}

void WindowBehaviorTests::testThumbnailLargeTiff()
{
    QFETCH(QSize, source);
    QFETCH(int, channels);
    QFETCH(int, orientation);
    QFETCH(bool, little);
    QFETCH(qint64, bytes);
    ThumbnailWindow fixture(1);
    const auto path = fixture.dir.filePath("02.tiff");
    QVERIFY(writeStripedTiff(path, source, 8, channels, orientation, little, bytes));
    revealNewThumbnail(fixture);
    QTRY_VERIFY_WITH_TIMEOUT(!thumbnailCell(fixture, path).version.isEmpty(), 30000);
    const auto cell = thumbnailCell(fixture, path);
    QVERIFY(!cell.preview.isNull());
    const QSize oriented = orientation == 6 ? source.transposed() : source;
    verifyThumbnailBounds(fixture, cell, source.width() >= 640 ? oriented : QSize());
    const QImage luma = orientationLuma(cell.preview);
    const auto dark = orientation == 6 ? luma.constScanLine(16)[24] : luma.constScanLine(8)[16];
    const auto light = orientation == 6 ? luma.constScanLine(16)[8] : luma.constScanLine(24)[16];
    QVERIFY(light - dark > 50);
}

void WindowBehaviorTests::testThumbnailLargeTiffRejected_data()
{
    QTest::addColumn<QSize>("source");
    QTest::addColumn<int>("bits");
    QTest::addColumn<qint64>("bytes");
    QTest::newRow("file-limit") << QSize(640, 480) << 8 << (512 * MiB + 1);
    QTest::newRow("pixel-limit") << QSize(10001, 10000) << 8 << qint64(0);
    QTest::newRow("edge-limit") << QSize(32769, 10) << 8 << (256 * MiB + 1);
    QTest::newRow("sample-depth") << QSize(640, 480) << 16 << (256 * MiB + 1);
    QTest::newRow("jpeg-with-tiff-extension") << QSize() << 0 << (256 * MiB + 1);
    QTest::newRow("broken-directory") << QSize() << 0 << (256 * MiB + 2);
}

void WindowBehaviorTests::testThumbnailLargeTiffRejected()
{
    QFETCH(QSize, source);
    QFETCH(int, bits);
    QFETCH(qint64, bytes);
    ThumbnailWindow fixture(2);
    const auto path = fixture.dir.filePath("03.tiff");
    if (source.isValid()) {
        QVERIFY(writeStripedTiff(path, source, bits, 1, 1, true, bytes));
    } else {
        QImage jpeg(640, 480, QImage::Format_RGB32);
        jpeg.fill(Qt::yellow);
        if (bytes == 256 * MiB + 1)
            QVERIFY(jpeg.save(path, "JPEG"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Append));
        if (bytes == 256 * MiB + 2)
            QCOMPARE(file.write(QByteArray::fromHex("49492a00ffffffff")), 8);
        QVERIFY(file.resize(bytes));
    }
    revealNewThumbnail(fixture);
    QTRY_VERIFY_WITH_TIMEOUT(!thumbnailCell(fixture, path).version.isEmpty(), 30000);
    QVERIFY(thumbnailCell(fixture, path).preview.isNull());
    QTRY_VERIFY(!thumbnailCell(fixture, fixture.next).preview.isNull());
}

void WindowBehaviorTests::testThumbnailGalleryLargePng_data()
{
    QTest::addColumn<QString>("name");
    QTest::newRow("largest-file") << QStringLiteral("014 Muse on Pegasus - Odilon Redon 1907.png");
    QTest::newRow("largest-area") << QStringLiteral("032 Nativity - Athos Bulcao 1999.png");
}

void WindowBehaviorTests::testThumbnailGalleryLargePng()
{
    QFETCH(QString, name);
    const auto gallery = qEnvironmentVariable("FOVELLE_THUMBNAIL_GALLERY");
    if (gallery.isEmpty())
        QSKIP("Set FOVELLE_THUMBNAIL_GALLERY to the mounted gallery directory.");
    const auto original = QDir(gallery).filePath(name);
    const auto version = QVImageLoader::fileVersion(original);
    QVERIFY(!version.isEmpty());
    ThumbnailWindow fixture(1);
    const auto path = fixture.dir.filePath("02.png");
    QVERIFY(QFile::link(original, path));
    revealNewThumbnail(fixture);
    QTRY_VERIFY_WITH_TIMEOUT(!thumbnailCell(fixture, path).version.isEmpty(), 120000);
    verifyThumbnailBounds(fixture, thumbnailCell(fixture, path), QImageReader(path).size());
    QCOMPARE(QVImageLoader::fileVersion(original), version);
}

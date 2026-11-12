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
    QVERIFY(QFile::copy(original, path));
    revealNewThumbnail(fixture);
    QTRY_VERIFY_WITH_TIMEOUT(!thumbnailCell(fixture, path).version.isEmpty(), 15000);
    const QImage preview = thumbnailCell(fixture, path).preview;
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
    } else {
        verifyRawThumbnailOrientation(fixture, path, preview);
    }
}

#include <QDropEvent>
#include <QSplitter>
#include <sys/sysctl.h>
#include "native_color_fixtures.h"

namespace {
struct CompareFixture
{
    QTemporaryDir directory;
    MainWindow window;
    QString first;
    QString second;

    CompareFixture()
    {
        QDir(directory.path()).mkdir("other");
        first = writeImage("a.bmp", { 2000, 1250 }, Qt::red);
        second = writeImage("other/b.bmp", { 1000, 800 }, Qt::blue);
        window.resize(960, 660);
        window.show();
    }

    QString writeImage(const QString &name, QSize size, QColor color)
    {
        QImage image(size, QImage::Format_RGB32);
        image.fill(color);
        const QString path = directory.filePath(name);
        return image.save(path) ? path : QString();
    }

    QString writeSquareSvg(const QString &name, int edge)
    {
        QFile file(directory.filePath(name));
        const auto svg =
                QStringLiteral("<svg xmlns='http://www.w3.org/2000/svg' width='%1' height='%1'>"
                               "<rect width='100%' height='100%' fill='blue'/></svg>")
                        .arg(edge)
                        .toUtf8();
        return file.open(QIODevice::WriteOnly) && file.write(svg) == svg.size() ? file.fileName()
                                                                                : QString();
    }

    QAction *action(const QString &key)
    {
        const auto actions = qvApp->getActionManager().getAllClonesOfAction(key, &window);
        return actions.isEmpty() ? nullptr : actions.first();
    }

    QVGraphicsView *reference() { return window.findChild<QVGraphicsView *>("graphicsView"); }
    QVGraphicsView *candidate() { return window.findChild<QVGraphicsView *>("comparisonView"); }
    QSplitter *splitter() { return window.findChild<QSplitter *>("comparisonSplitter"); }

    bool select(const QString &path)
    {
        auto *view = candidate();
        if (!view)
            return false;
        QMimeData mime;
        mime.setUrls({ QUrl::fromLocalFile(path) });
#if QT_VERSION >= QT_VERSION_CHECK(6, 12, 0)
        const QPointF dropPoint(10, 10);
#else
        const QPoint dropPoint(10, 10);
#endif
        QDragEnterEvent enter(dropPoint, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(view->viewport(), &enter);
        QDropEvent drop({ 10, 10 }, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(view->viewport(), &drop);
        return enter.isAccepted() && drop.isAccepted();
    }

    bool ready(const QString &path)
    {
        auto *view = candidate();
        return view && view->getCurrentFileDetails().isPixmapLoaded
                && view->getCurrentFileDetails().fileInfo.absoluteFilePath() == path;
    }

    QString feedback()
    {
        auto *label = window.findChild<QLabel *>("comparisonStatus");
        return label ? label->text() : QString();
    }
};

#define OPEN_COMPARISON(fixture)                       \
    QVERIFY(!(fixture).first.isEmpty());               \
    (fixture).window.openFile((fixture).first);        \
    QTRY_VERIFY((fixture).window.getIsPixmapLoaded()); \
    QVERIFY((fixture).action("compare"));              \
    (fixture).action("compare")->trigger();            \
    QTRY_VERIFY((fixture).candidate())

#define PREPARE_PAIR(fixture)                    \
    OPEN_COMPARISON(fixture);                    \
    QVERIFY((fixture).select((fixture).second)); \
    QTRY_VERIFY((fixture).ready((fixture).second))
} // namespace

void WindowBehaviorTests::testCompareEntry()
{
    CompareFixture fixture;
    QVERIFY(fixture.action("compare"));
    QVERIFY(!fixture.action("compare")->isEnabled());
    fixture.window.openFile(fixture.first);
    QTRY_VERIFY(fixture.window.getIsPixmapLoaded());
    fixture.reference()->zoomAbsolute(1.5, Qv::CalculateViewportCenterPos);
    fixture.action("compare")->trigger();
    QTRY_VERIFY(fixture.candidate());
    QVERIFY(fixture.action("compare")->isChecked());
    QVERIFY(!fixture.action("compareswap")->isEnabled());
    QVERIFY(!fixture.action("nextfile")->isEnabled());
    QTest::keyClick(fixture.reference(), Qt::Key_Escape);
    QTRY_VERIFY(!fixture.candidate());
    QVERIFY(fixture.window.isVisible());
    QCOMPARE(fixture.reference()->getZoomLevel(), 1.5);
}

void WindowBehaviorTests::testCompareLoad()
{
    CompareFixture fixture;
    PREPARE_PAIR(fixture);
    QCOMPARE(fixture.reference()->getCurrentFileDetails().fileInfo.absoluteFilePath(),
             fixture.first);
    QVERIFY(fixture.reference()->mapToGlobal(QPoint()).x() + fixture.reference()->width()
            <= fixture.candidate()->mapToGlobal(QPoint()).x());
    QVERIFY(fixture.candidate()->isImageAtFit());
}

void WindowBehaviorTests::testCompareZoom()
{
    CompareFixture fixture;
    PREPARE_PAIR(fixture);
    auto *a = fixture.reference();
    auto *b = fixture.candidate();
    a->zoomAbsolute(2.0);
    b->zoomAbsolute(1.0);
    a->zoomRelative(1.25);
    QCOMPARE(a->getZoomLevel(), 2.5);
    QCOMPARE(b->getZoomLevel(), 1.25);
    a->zoomRelative(100.0);
    QCOMPARE(a->getZoomLevel(), 64.0);
    QCOMPARE(b->getZoomLevel(), 32.0);
    a->zoomRelative(0.5);
    QCOMPARE(a->getZoomLevel(), 32.0);
    QCOMPARE(b->getZoomLevel(), 16.0);
}

void WindowBehaviorTests::testCompareIndependent()
{
    CompareFixture fixture;
    PREPARE_PAIR(fixture);
    QVERIFY(fixture.action("comparezoom")->isChecked());
    fixture.action("comparezoom")->trigger();
    QVERIFY(fixture.action("comparepan")->isChecked());
    auto *a = fixture.reference();
    auto *b = fixture.candidate();
    a->zoomAbsolute(2.0);
    b->zoomAbsolute(1.0);
    const auto aTransform = a->viewportTransform();
    b->zoomRelative(2.0);
    QCOMPARE(a->viewportTransform(), aTransform);
    QCOMPARE(b->getZoomLevel(), 2.0);
}

void WindowBehaviorTests::testComparePan()
{
    CompareFixture fixture;
    PREPARE_PAIR(fixture);
    auto *a = fixture.reference();
    auto *b = fixture.candidate();
    a->zoomAbsolute(2.0, Qv::CalculateViewportCenterPos);
    b->zoomAbsolute(2.0, Qv::CalculateViewportCenterPos);
    auto *source = a->horizontalScrollBar();
    auto *target = b->horizontalScrollBar();
    source->setValue(source->maximum() - 20);
    const int initial = target->value();
    source->triggerAction(QAbstractSlider::SliderToMaximum);
    QCOMPARE(target->value() - initial, 10);
    QTest::keyClick(a, Qt::Key_Left);
    QVERIFY(target->value() < initial + 10);
    b->setCalculatedZoomMode(Qv::CalculatedZoomMode::ZoomToFit);
    QCOMPARE(target->minimum(), target->maximum());
    const int sourceBefore = source->value();
    QTest::keyClick(b, Qt::Key_Left);
    QCOMPARE(source->value(), sourceBefore);
    b->zoomAbsolute(2.0, Qv::CalculateViewportCenterPos);
    fixture.action("comparepan")->trigger();
    const int unlinked = target->value();
    source->triggerAction(QAbstractSlider::SliderToMinimum);
    QCOMPARE(target->value(), unlinked);
}

void WindowBehaviorTests::testCompareSwap()
{
    CompareFixture fixture;
    PREPARE_PAIR(fixture);
    fixture.splitter()->setSizes({ 610, 330 });
    auto *a = fixture.reference();
    auto *b = fixture.candidate();
    a->zoomAbsolute(1.25, Qv::CalculateViewportCenterPos);
    b->zoomAbsolute(1.75, Qv::CalculateViewportCenterPos);
    b->setFocus();
    QTest::keyClick(b, Qt::Key_Right);
    const QPointF old = b->mapToScene(b->viewport()->rect().center());
    fixture.action("compareswap")->trigger();
    QCOMPARE(fixture.candidate(), b);
    QCOMPARE(b->getZoomLevel(), 1.75);
    QVERIFY(b->mapToGlobal(QPoint()).x() < a->mapToGlobal(QPoint()).x());
    QVERIFY(b->hasFocus());
    fixture.action("compareswap")->trigger();
    QVERIFY(a->mapToGlobal(QPoint()).x() < b->mapToGlobal(QPoint()).x());
    QVERIFY(QLineF(old, b->mapToScene(b->viewport()->rect().center())).length() <= 1.1);
}

void WindowBehaviorTests::testCompareReplacement()
{
    CompareFixture fixture;
    PREPARE_PAIR(fixture);
    const QString brokenPath = fixture.directory.filePath("broken.ppm");
    QFile broken(brokenPath);
    QVERIFY(broken.open(QIODevice::WriteOnly));
    const QByteArray header("P6\n20 10\n255\n");
    QCOMPARE(broken.write(header), header.size());
    broken.close();
    QCOMPARE(QImageReader(brokenPath).size(), QSize(20, 10));
    QVERIFY(fixture.select(brokenPath));
    QTRY_COMPARE(fixture.feedback(), QStringLiteral("Unable to display this image."));
    QVERIFY(fixture.ready(fixture.second));
    const QString latest = fixture.writeImage("d.bmp", { 640, 500 }, Qt::green);
    QVERIFY(fixture.select(brokenPath));
    QVERIFY(fixture.select(latest));
    fixture.action("compareswap")->trigger();
    QTRY_VERIFY(fixture.ready(latest));
    QVERIFY(fixture.candidate()->mapToGlobal(QPoint()).x()
            < fixture.reference()->mapToGlobal(QPoint()).x());
}

void WindowBehaviorTests::testCompareExitWhileLoading()
{
    CompareFixture fixture;
    OPEN_COMPARISON(fixture);
    QVERIFY(fixture.select(fixture.second));
    fixture.action("compare")->trigger();
    QTRY_VERIFY(!fixture.candidate());
    fixture.action("compare")->trigger();
    QTRY_VERIFY(fixture.candidate());
    QVERIFY(QvAsync::drain(5000));
    QCoreApplication::processEvents();
    QVERIFY(!fixture.candidate()->getCurrentFileDetails().isPixmapLoaded);
    fixture.window.openFile(fixture.second);
    QTRY_COMPARE(fixture.reference()->getCurrentFileDetails().fileInfo.absoluteFilePath(),
                 fixture.second);
    QVERIFY(!fixture.candidate());
}

void WindowBehaviorTests::testCompareLargeCandidate()
{
    CompareFixture fixture;
    PREPARE_PAIR(fixture);
    const QString path = fixture.writeSquareSvg("other/large-candidate.svg", 80000);
    QVERIFY(!path.isEmpty());
    QVERIFY(fixture.select(path));
    QTRY_VERIFY2(fixture.ready(path), qPrintable(fixture.feedback()));
    const auto &details = fixture.candidate()->getCurrentFileDetails();
    QVERIFY(details.isVectorLoaded);
    QCOMPARE(details.loadedPixmapSize, QSize(80000, 80000));
    QVERIFY(fixture.feedback().isEmpty());
}

void WindowBehaviorTests::testCompareLargeReference()
{
    CompareFixture fixture;
    fixture.first = fixture.writeSquareSvg("large-reference.svg", 80000);
    OPEN_COMPARISON(fixture);
    QVERIFY(fixture.select(fixture.second));
    QTRY_VERIFY2(fixture.ready(fixture.second), qPrintable(fixture.feedback()));
    const auto &reference = fixture.reference()->getCurrentFileDetails();
    QCOMPARE(reference.fileInfo.absoluteFilePath(), fixture.first);
    QVERIFY(reference.isVectorLoaded);
    QCOMPARE(reference.loadedPixmapSize, QSize(80000, 80000));
    QCOMPARE(fixture.candidate()->getCurrentFileDetails().loadedPixmapSize, QSize(1000, 800));
    QVERIFY(fixture.feedback().isEmpty());
}

void WindowBehaviorTests::testCompareAnimatedCandidate()
{
    CompareFixture fixture;
    PREPARE_PAIR(fixture);
    const QString path = QFINDTESTDATA("data/remediation.gif");
    QVERIFY(!path.isEmpty());
    QImageReader reader(path);
    QVERIFY(reader.supportsAnimation());
    QCOMPARE(reader.imageCount(), 4);
    QVERIFY(fixture.select(path));
    QTRY_COMPARE(fixture.feedback(),
                 QStringLiteral("Animated images cannot be used for comparison."));
    QVERIFY(fixture.ready(fixture.second));
}

void WindowBehaviorTests::testCompareNativeLayout()
{
    CompareFixture fixture;
    const QString sample = QFINDTESTDATA("data/portable-hdr/gain-map.jpg");
    QVERIFY(!sample.isEmpty());
    OPEN_COMPARISON(fixture);
    QVERIFY(fixture.select(sample));
    QTRY_VERIFY(fixture.ready(sample));
    auto *b = fixture.candidate();
    QVERIFY(b->getCurrentFileDetails().isNativeHDRLoaded);
    QTRY_VERIFY(!b->nativeMetalRendererDiagnostics().nativeViewportFrame.isEmpty());
    const auto before = b->nativeMetalRendererDiagnostics();
    fixture.action("compareswap")->trigger();
    fixture.splitter()->setSizes({ 530, 410 });
    QTRY_COMPARE(b->nativeMetalRendererDiagnostics().nativeViewportFrame.x(),
                 qreal(b->viewport()->mapTo(&fixture.window, QPoint()).x()));
    const auto after = b->nativeMetalRendererDiagnostics();
    QVERIFY(after.imageActive);
    QVERIFY(after.nativeViewportFrame.x() < before.nativeViewportFrame.x());
    QCOMPARE(after.geometryGeneration, before.geometryGeneration);
    QVERIFY(b->mapToGlobal(QPoint()).x() < fixture.reference()->mapToGlobal(QPoint()).x());
    QVERIFY(fixture.ready(sample));
}

void WindowBehaviorTests::testCompareGestureOwnership()
{
    CompareFixture fixture;
    PREPARE_PAIR(fixture);
    auto *a = fixture.reference();
    auto *b = fixture.candidate();
    fixture.action("comparezoom")->trigger();
    a->zoomAbsolute(1.0);
    b->zoomAbsolute(1.0);
    QVERIFY(sendNativeGesture(a, Qt::BeginNativeGesture, QPoint(10, 10)));
    QVERIFY(sendNativeGesture(b, Qt::ZoomNativeGesture, QPoint(10, 10), 0.25));
    QCOMPARE(a->getZoomLevel(), 1.25);
    QCOMPARE(b->getZoomLevel(), 1.0);
    fixture.action("compareswap")->trigger();
    QVERIFY(sendNativeGesture(b, Qt::ZoomNativeGesture, QPoint(10, 10), 0.5));
    QCOMPARE(a->getZoomLevel(), 1.25);
    QCOMPARE(b->getZoomLevel(), 1.0);
    QVERIFY(sendNativeGesture(b, Qt::EndNativeGesture, QPoint(10, 10)));
    QVERIFY(sendNativeGesture(b, Qt::BeginNativeGesture, QPoint(10, 10)));
    QVERIFY(sendNativeGesture(b, Qt::ZoomNativeGesture, QPoint(10, 10), 0.5));
    QVERIFY(sendNativeGesture(b, Qt::EndNativeGesture, QPoint(10, 10)));
    QCOMPARE(a->getZoomLevel(), 1.25);
    QCOMPARE(b->getZoomLevel(), 1.5);
}

void WindowBehaviorTests::testCompareZoomCommands()
{
    CompareFixture fixture;
    PREPARE_PAIR(fixture);
    auto *a = fixture.reference();
    auto *b = fixture.candidate();
    a->setFocus();
    QVERIFY(a->isImageAtFit());
    QVERIFY(b->isImageAtFit());
    fixture.window.toggleFitAnd100();
    QCOMPARE(a->getZoomLevel(), 1.0);
    QCOMPARE(b->getZoomLevel(), 1.0);
    fixture.action("comparezoom")->trigger();
    b->setFocus();
    fixture.window.toggleFitAnd100();
    QCOMPARE(a->getZoomLevel(), 1.0);
    QVERIFY(b->isImageAtFit());
}

void WindowBehaviorTests::testCompareRemove()
{
    CompareFixture fixture;
    PREPARE_PAIR(fixture);
    const QString latest = fixture.writeImage("d.bmp", { 640, 500 }, Qt::green);
    QVERIFY(fixture.select(latest));
    auto *remove = fixture.window.findChild<QPushButton *>("comparisonRemove");
    QVERIFY(remove && remove->isEnabled());
    remove->click();
    QVERIFY(QvAsync::drain(5000));
    QCoreApplication::processEvents();
    QVERIFY(!fixture.candidate()->getCurrentFileDetails().isPixmapLoaded);
    QVERIFY(!fixture.action("compareswap")->isEnabled());
    QCOMPARE(fixture.reference()->getCurrentFileDetails().fileInfo.absoluteFilePath(),
             fixture.first);
}

void WindowBehaviorTests::testCompareMaximized16KHDR()
{
    // This fixture can use over 12 GiB. Keep it off small test hosts.
    constexpr quint64 GiB = 1024ULL * 1024 * 1024;
    quint64 physicalBytes = 0;
    size_t length = sizeof(physicalBytes);
    if (sysctlbyname("hw.memsize", &physicalBytes, &length, nullptr, 0) != 0
        || physicalBytes < 32 * GiB)
        QSKIP("The two 16K HDR fixtures run on hosts with at least 32 GiB of physical memory");
    ScopedHDRTestDisplay display(2.0F);
    CompareFixture fixture;
    const auto portable = QFINDTESTDATA("data/portable-hdr/gain-map.jpg");
    QVERIFY(!portable.isEmpty());
    const QSize sourceSize(16384, 9216);
    fixture.first = fixture.directory.filePath("a-16k-hdr.jpg");
    fixture.second = fixture.directory.filePath("other/b-16k-hdr.jpg");
    QVERIFY(scaleComparisonHDRJpeg(portable, fixture.first, sourceSize));
    QVERIFY(scaleComparisonHDRJpeg(portable, fixture.second, sourceSize, true));

    fixture.window.showMaximized();
    fixture.window.raise();
    fixture.window.activateWindow();
    QTRY_VERIFY(fixture.window.isMaximized());
    fixture.window.openFile(fixture.first);
    QTRY_VERIFY_WITH_TIMEOUT(fixture.window.getIsPixmapLoaded(), 30000);
    auto *reference = fixture.reference();
    const auto &a = reference->getCurrentFileDetails();
    QVERIFY(a.isNativeHDRLoaded);
    QCOMPARE(a.fileInfo.absoluteFilePath(), fixture.first);
    QCOMPARE(a.loadedPixmapSize, sourceSize);

    fixture.window.toggleComparison();
    QTRY_VERIFY(fixture.candidate());
    auto *candidate = fixture.candidate();
    QVERIFY(fixture.select(fixture.second));
    QTRY_VERIFY2_WITH_TIMEOUT(fixture.ready(fixture.second), qPrintable(fixture.feedback()), 30000);
    const auto &b = candidate->getCurrentFileDetails();
    QVERIFY(b.isNativeHDRLoaded);
    QCOMPARE(b.loadedPixmapSize, sourceSize);
    QCOMPARE(a.loadedPixmapSize, sourceSize);
    QVERIFY(reference->thumbnailImageCore().getLoadedHDRImage()
            != candidate->thumbnailImageCore().getLoadedHDRImage());
    QCOMPARE(a.fileInfo.absoluteFilePath(), fixture.first);
    QVERIFY(fixture.window.isMaximized());

    for (auto *view : { reference, candidate }) {
        QTRY_VERIFY_WITH_TIMEOUT(view->nativeMetalRendererDiagnostics().firstFramePresented, 30000);
        const auto diagnostics = view->nativeMetalRendererDiagnostics();
        QVERIFY(diagnostics.imageActive);
        QVERIFY(diagnostics.usesRGBA16Float);
        QVERIFY(diagnostics.wantsExtendedDynamicRangeContent);
        QVERIFY(diagnostics.contentHeadroom > 1.0F);
    }
    QVERIFY(fixture.feedback().isEmpty());
}

void WindowBehaviorTests::testCompareMaximizedHDRSameFile()
{
    CompareFixture fixture;
    const auto portable = QFINDTESTDATA("data/portable-hdr/gain-map.jpg");
    QVERIFY(!portable.isEmpty());
    const QSize fullSize(8064, 6048);
    fixture.first = fixture.directory.filePath("hdr.jpg");
    QVERIFY(scaleComparisonHDRJpeg(portable, fixture.first, fullSize));
    fixture.window.showMaximized();
    QTRY_VERIFY(fixture.window.isMaximized());

    OPEN_COMPARISON(fixture);
    QVERIFY(fixture.select(fixture.first));
    QTRY_VERIFY2_WITH_TIMEOUT(fixture.ready(fixture.first), qPrintable(fixture.feedback()), 15000);
    for (auto *view : { fixture.reference(), fixture.candidate() }) {
        const auto &details = view->getCurrentFileDetails();
        QVERIFY(details.isNativeHDRLoaded);
        QCOMPARE(details.loadedPixmapSize, fullSize);
        QCOMPARE(details.fileInfo.absoluteFilePath(), fixture.first);
    }
}

void WindowBehaviorTests::testCompareLargeEncodedFile()
{
    CompareFixture fixture;
    OPEN_COMPARISON(fixture);
    const QSize size(1600, 1200);
    const QString path = fixture.writeImage("other/full.jpg", size, Qt::blue);
    QVERIFY(!path.isEmpty());
    constexpr qint64 encodedBytes = 65 * 1024 * 1024;
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadWrite));
    QVERIFY(file.resize(encodedBytes));
    file.close();

    QVERIFY(fixture.select(path));
    QTRY_VERIFY2(fixture.ready(path), qPrintable(fixture.feedback()));
    const auto &details = fixture.candidate()->getCurrentFileDetails();
    QVERIFY(details.isNativeSDRLoaded);
    QCOMPARE(details.loadedPixmapSize, size);
    QCOMPARE(details.sourceFileSize, encodedBytes);
    QVERIFY(fixture.feedback().isEmpty());
}

void WindowBehaviorTests::testCompareConcurrentWindows()
{
    constexpr int edge = 12500;
    CompareFixture fixtures[3];
    QString paths[3];
    for (int i = 0; i < 3; ++i) {
        auto &fixture = fixtures[i];
        OPEN_COMPARISON(fixture);
        paths[i] = fixture.writeSquareSvg("other/large-candidate.svg", edge);
        QVERIFY(!paths[i].isEmpty());
        QVERIFY(fixture.select(paths[i]));
        QTRY_VERIFY2(fixture.ready(paths[i]), qPrintable(fixture.feedback()));
    }

    for (int i = 0; i < 3; ++i) {
        auto &fixture = fixtures[i];
        QVERIFY(fixture.ready(paths[i]));
        const auto &details = fixture.candidate()->getCurrentFileDetails();
        QVERIFY(details.isVectorLoaded);
        QCOMPARE(details.loadedPixmapSize, QSize(edge, edge));
        QVERIFY(fixture.feedback().isEmpty());
    }
}

void WindowBehaviorTests::testCompareInitialEqualWidths_data()
{
    QTest::addColumn<int>("windowWidth");
    QTest::newRow("even") << 960;
    QTest::newRow("odd") << 961;
    QTest::newRow("narrow") << 690;
}

void WindowBehaviorTests::testCompareInitialEqualWidths()
{
    QFETCH(int, windowWidth);
    CompareFixture fixture;
    fixture.window.setStyleSheet("QPushButton#comparisonChoose { min-width: 295px; }");
    fixture.window.openFile(fixture.first);
    QTRY_VERIFY(fixture.window.getIsPixmapLoaded());
    fixture.window.resize(windowWidth, 660);
    fixture.action("compare")->trigger();
    QTRY_VERIFY(fixture.candidate());
    auto *splitter = fixture.splitter();
    QTRY_VERIFY(splitter->widget(1)->minimumSizeHint().width()
                > splitter->widget(0)->minimumSizeHint().width());
    QCoreApplication::processEvents();
    const auto widths = splitter->sizes();
    QCOMPARE(widths.size(), 2);
    QCOMPARE(widths[0] + widths[1],
             splitter->contentsRect().width() - splitter->handle(1)->width());
    QVERIFY2(qAbs(widths[0] - widths[1]) <= 1,
             qPrintable(QString("%1 vs %2").arg(widths[0]).arg(widths[1])));
}

void WindowBehaviorTests::testCompareDividerCenter()
{
    CompareFixture fixture;
    OPEN_COMPARISON(fixture);
    auto *splitter = fixture.splitter();
    auto *handle = splitter->handle(1);
    const int leftEnd = splitter->widget(0)->geometry().right() + 1;
    const int rightStart = splitter->widget(1)->geometry().left();
    QCOMPARE(handle->x(), leftEnd);
    QCOMPARE(handle->x() + handle->width(), rightStart);
    QVERIFY(handle->width() >= 6);

    auto palette = handle->palette();
    const QColor stroke(34, 220, 190);
    const QColor track(10, 20, 30);
    palette.setColor(QPalette::Mid, stroke);
    palette.setColor(QPalette::Window, track);
    handle->setPalette(palette);
    const auto image = handle->grab().toImage();
    const int x = image.width() / 2;
    const int y = image.height() / 2;
    QCOMPARE(image.pixelColor(x - 1, y), stroke);
    QCOMPARE(image.pixelColor(x, y), stroke);
    QCOMPARE(image.pixelColor(0, y), track);
    QCOMPARE(image.pixelColor(image.width() - 1, y), track);
}

void WindowBehaviorTests::testCompareDividerDragLayer()
{
    CompareFixture fixture;
    fixture.first = QFINDTESTDATA("data/portable-hdr/gain-map.jpg");
    OPEN_COMPARISON(fixture);
    auto *splitter = fixture.splitter();
    auto *handle = splitter->handle(1);
    const auto initial = splitter->sizes();
    QTRY_VERIFY(fixture.reference()->nativeMetalRendererDiagnostics().imageActive);
    const QRectF nativeImage =
            fixture.reference()->nativeMetalRendererDiagnostics().nativeViewportFrame;
    QVERIFY(!nativeImage.isEmpty());
    const QPoint grabbed(1, handle->height() / 2);
    const QPoint moved = grabbed - QPoint(70, 0);
    QTest::mousePress(handle, Qt::LeftButton, Qt::NoModifier, grabbed);
    sendMouseMove(handle, moved, Qt::LeftButton);
    const auto preview = nativeComparisonGuide(fixture.window.windowHandle());
    QVERIFY(!preview.frame.isEmpty());
    QVERIFY(preview.frame.intersects(nativeImage));
    QVERIFY(preview.overlappingLayers > 0);
    QVERIFY(preview.aboveOverlappingLayers);
    QCOMPARE(splitter->sizes(), initial);
    const QPoint oldOrigin = handle->mapTo(&fixture.window, QPoint());
    QCOMPARE(preview.frame.center().x(), oldOrigin.x() - 70 + handle->width() / 2.0);
    QTest::mouseRelease(handle, Qt::LeftButton, Qt::NoModifier, moved);
    QVERIFY(nativeComparisonGuide(fixture.window.windowHandle()).frame.isEmpty());
    QCOMPARE(qreal(handle->mapTo(&fixture.window, QPoint()).x()) + handle->width() / 2.0,
             preview.frame.center().x());
}

void WindowBehaviorTests::testCompareDividerCancellation()
{
    CompareFixture fixture;
    OPEN_COMPARISON(fixture);
    auto *splitter = fixture.splitter();
    auto *handle = splitter->handle(1);
    splitter->setSizes({ 610, 340 });
    const auto before = splitter->sizes();
    QVERIFY(before[0] > before[1] + 20);
    const QPoint grab = handle->rect().center();
    const QPoint moved = grab - QPoint(40, 0);
    QTest::mousePress(handle, Qt::LeftButton, Qt::NoModifier, grab);
    sendMouseMove(handle, moved, Qt::LeftButton);
    QVERIFY(!nativeComparisonGuide(fixture.window.windowHandle()).frame.isEmpty());
    QEvent canceled(QEvent::UngrabMouse);
    QApplication::sendEvent(handle, &canceled);
    QVERIFY(nativeComparisonGuide(fixture.window.windowHandle()).frame.isEmpty());
    QTest::mouseRelease(handle, Qt::LeftButton, Qt::NoModifier, moved);
    QCOMPARE(splitter->sizes(), before);
    QTest::mouseDClick(handle, Qt::LeftButton, Qt::NoModifier, grab);
    const auto reset = splitter->sizes();
    QVERIFY(qAbs(reset[0] - reset[1]) <= 1);
}

void WindowBehaviorTests::testCompareExitRestoresTransform_data()
{
    QTest::addColumn<bool>("changeZoom");
    QTest::newRow("same-recorded-zoom") << false;
    QTest::newRow("different-recorded-zoom") << true;
}

void WindowBehaviorTests::testCompareExitRestoresTransform()
{
    QFETCH(bool, changeZoom);
    CompareFixture fixture;
    fixture.window.openFile(fixture.first);
    QTRY_VERIFY(fixture.window.getIsPixmapLoaded());
    auto *view = fixture.reference();
    view->rotateImage(90);
    view->mirrorImage();
    view->zoomAbsolute(1.75, Qv::CalculateViewportCenterPos);
    view->horizontalScrollBar()->setValue(view->horizontalScrollBar()->value() + 70);
    view->verticalScrollBar()->setValue(view->verticalScrollBar()->value() + 50);
    QCoreApplication::processEvents();
    const QTransform expected = view->transform();
    const QPointF center = view->mapToScene(view->viewport()->rect().center());
    fixture.action("compare")->trigger();
    QTRY_VERIFY(fixture.candidate());
    QVERIFY(fixture.select(fixture.second));
    QTRY_VERIFY(fixture.ready(fixture.second));
    if (changeZoom)
        view->zoomAbsolute(2.0, Qv::CalculateViewportCenterPos);
    fixture.candidate()->setFocus();
    QTRY_VERIFY(fixture.candidate()->hasFocus());
    QTest::keyClick(fixture.candidate(), Qt::Key_Escape);
    QTRY_VERIFY(!fixture.candidate());
    QVERIFY(fixture.window.isVisible());
    QTRY_VERIFY(view->hasFocus());
    QCOMPARE(view->getZoomLevel(), 1.75);
    QCOMPARE(view->transform(), expected);
    QVERIFY(QLineF(center, view->mapToScene(view->viewport()->rect().center())).length() <= 1.1);
}

void WindowBehaviorTests::testCompareExitRestoresDragging()
{
    CompareFixture fixture;
    fixture.first = fixture.writeImage("small.bmp", { 500, 350 }, Qt::red);
    fixture.window.openFile(fixture.first);
    QTRY_VERIFY(fixture.window.getIsPixmapLoaded());
    auto *view = fixture.reference();
    QVERIFY(view->viewport()->width() > 500);
    view->zoomAbsolute(3.0, Qv::CalculateViewportCenterPos);
    auto *horizontal = view->horizontalScrollBar();
    QVERIFY(horizontal->maximum() - horizontal->value() >= 64);

    fixture.action("compare")->trigger();
    QTRY_VERIFY(fixture.candidate());
    QCOMPARE(view->getZoomLevel(), 3.0);
    QTest::keyClick(view, Qt::Key_Escape);
    QTRY_VERIFY(!fixture.candidate());
    QVERIFY(fixture.window.isVisible());

    auto *canvas = view->viewport();
    const QPoint start = canvas->rect().center();
    const QPoint end = start - QPoint(64, 0);
    const int before = horizontal->value();
    QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, start);
    sendMouseMove(canvas, end, Qt::LeftButton);
    QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, end);
    QVERIFY(horizontal->value() > before);
}

void WindowBehaviorTests::testCompareExitRestoresZ()
{
    CompareFixture fixture;
    fixture.window.openFile(fixture.first);
    QTRY_VERIFY(fixture.window.getIsPixmapLoaded());
    auto *view = fixture.reference();
    view->setCalculatedZoomMode(Qv::CalculatedZoomMode::ZoomToFit, true);
    view->fitOrConstrainImage();
    QTRY_VERIFY(view->isImageAtFit());
    const qreal fit = view->getZoomLevel();
    const QTransform fitted = view->transform();
    QVERIFY(fit < 1.0);

    fixture.action("compare")->trigger();
    QTRY_VERIFY(fixture.candidate());
    view->zoomAbsolute(fit, Qv::CalculateViewportCenterPos);
    QTest::keyClick(view, Qt::Key_Escape);
    QTRY_VERIFY(!fixture.candidate());
    QTRY_VERIFY(view->hasFocus());
    QVERIFY(fixture.action("togglefitand100")->isEnabled());

    QTest::keyClick(view, Qt::Key_Z);
    QCOMPARE(view->getZoomLevel(), 1.0);
    QVERIFY(view->transform() != fitted);
    QVERIFY(view->horizontalScrollBar()->maximum() > view->horizontalScrollBar()->minimum());
    QTest::keyClick(view, Qt::Key_Z);
    QVERIFY(view->isImageAtFit());
    QCOMPARE(view->getZoomLevel(), fit);
    QCOMPARE(view->transform(), fitted);
}

#undef OPEN_COMPARISON
#undef PREPARE_PAIR

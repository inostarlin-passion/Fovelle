#include <QDropEvent>
#include <QSplitter>

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
    const QString brokenPath = fixture.directory.filePath("broken.bmp");
    QFile broken(brokenPath);
    QVERIFY(broken.open(QIODevice::WriteOnly));
    broken.write("broken");
    broken.close();
    QVERIFY(fixture.select(brokenPath));
    QTRY_VERIFY(fixture.feedback().contains("Unable", Qt::CaseInsensitive));
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

void WindowBehaviorTests::testCompareAdmission()
{
    CompareFixture fixture;
    PREPARE_PAIR(fixture);
    const QString path = fixture.directory.filePath("oversize.svg");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("<svg xmlns='http://www.w3.org/2000/svg' width='80000' height='80000'/>");
    file.close();
    QVERIFY(fixture.select(path));
    QTRY_VERIFY(fixture.feedback().contains("budget", Qt::CaseInsensitive));
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

void WindowBehaviorTests::testCompareBudgetPreview()
{
    CompareFixture fixture;
    const QString large = fixture.writeImage("large.jpg", { 9000, 5000 }, Qt::green);
    QVERIFY(!large.isEmpty());
    OPEN_COMPARISON(fixture);
    QVERIFY(fixture.select(large));
    QTRY_VERIFY(fixture.ready(large));
    auto *view = fixture.candidate();
    QVERIFY(view->getCurrentFileDetails().isComparisonPreview);
    QCOMPARE(view->getCurrentFileDetails().loadedPixmapSize, QSize(9000, 5000));
    QVERIFY(view->isImageAtFit());
    QCOMPARE(fixture.feedback(), QStringLiteral("SDR preview"));
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

#undef OPEN_COMPARISON
#undef PREPARE_PAIR

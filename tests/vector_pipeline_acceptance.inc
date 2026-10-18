void ImageLoaderTests::testVectorDisplayTargetSwitch_data()
{
    QTest::addColumn<bool>("pdf");
    QTest::newRow("svg") << false;
    QTest::newRow("pdf-from-eps-path") << true;
}
void ImageLoaderTests::testVectorDisplayTargetSwitch()
{
    QFETCH(bool, pdf);
    Qv::VectorImageData vector;
    vector.logicalSize = QSizeF(16, 16);
    vector.colorSpace = QColorSpace::DisplayP3;
    vector.format = pdf ? Qv::VectorImageFormat::Pdf : Qv::VectorImageFormat::Svg;
    vector.encodedData = pdf
            ? createColorP3Pdf()
            : QByteArray("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16'><rect "
                         "width='16' height='16' fill='red'/></svg>");
    QVGraphicsImageItem item;
    QVERIFY(item.setVectorImage(vector));
    QImage canvas(96, 96, QImage::Format_ARGB32_Premultiplied);
    QStyleOptionGraphicsItem option;
    option.exposedRect = QRectF(0, 0, 16, 16);
    const auto paint = [&] {
        canvas.fill(Qt::transparent);
        QPainter painter(&canvas);
        painter.scale(6, 6);
        item.paint(&painter, &option);
    };
    // The first request may finish on its worker but cannot publish until the
    // GUI event loop runs. Only the final target is allowed to become visible.
    item.setDisplayColorSpace(QColorSpace::SRgb);
    paint();
    item.setDisplayColorSpace(QColorSpace::DisplayP3);
    paint();
    item.setDisplayColorSpace(QColorSpace::AdobeRgb);
    paint();
    for (QColorSpace::NamedColorSpace target :
         { QColorSpace::AdobeRgb, QColorSpace::SRgb, QColorSpace::DisplayP3 }) {
        item.setDisplayColorSpace(target);
        paint();
        QElapsedTimer timeout;
        timeout.start();
        while ((item.vectorDisplaySpace() != QColorSpace(target)
                || item.hasPendingVectorRefinement())
               && timeout.elapsed() < 5000) {
            QTest::qWait(2);
            paint();
        }
        QCOMPARE(item.vectorDisplaySpace(), QColorSpace(target));
        QVERIFY(!item.hasPendingVectorRefinement());
        QImage source(1, 1, QImage::Format_RGBA64_Premultiplied);
        source.fill(Qt::red);
        source.setColorSpace(QColorSpace::DisplayP3);
        const QColor expected = source.convertedToColorSpace(target).pixelColor(0, 0);
        const QColor actual = canvas.pixelColor(48, 48);
        QVERIFY(qAbs(actual.red() - expected.red()) <= 2);
        QVERIFY(qAbs(actual.green() - expected.green()) <= 2);
        QVERIFY(qAbs(actual.blue() - expected.blue()) <= 2);
    }
    QCOMPARE(item.vectorGuiColorConversions(), quint64(0));
    QVERIFY(item.vectorWorkerColorConversions() > 0);
}
void ImageLoaderTests::testVectorRequestBurstAndDestruction()
{
    QTemporaryDir directory;
    const auto vector = performanceVectorFixture(directory, false);
    QVERIFY(vector.isValid());
    QImage canvas(640, 480, QImage::Format_ARGB32_Premultiplied);
    QVGraphicsImageItem item;
    QVERIFY(item.setVectorImage(vector));
    item.setVectorInteractionActive(true);
    QStyleOptionGraphicsItem option;
    const auto paint = [&](QVGraphicsImageItem &image, double scale, double x) {
        option.exposedRect = QRectF(x, 100, canvas.width() / scale, canvas.height() / scale);
        QPainter painter(&canvas);
        painter.scale(scale, scale);
        painter.translate(-x, -100);
        image.paint(&painter, &option);
    };
    for (int i = 0; i < 100; ++i) {
        paint(item, 1.0 + i * .004, 100 + i);
        QVERIFY(item.vectorRequestDepth() <= 2);
    }
    item.setVectorInteractionActive(false);
    QElapsedTimer deadline;
    deadline.start();
    do {
        QTest::qWait(2);
        paint(item, 1.396, 199);
    } while (item.hasPendingVectorRefinement() && deadline.elapsed() < 5000);
    QVERIFY(!item.hasPendingVectorRefinement());
    QVERIFY(item.vectorRenderCount() > 0);
    QVERIFY(item.lastVectorSourceRect().contains(option.exposedRect));
    QVERIFY(item.lastVectorRasterSize().width() / item.lastVectorSourceRect().width() >= 1.396);
    {
        auto destroyed = std::make_unique<QVGraphicsImageItem>();
        QVERIFY(destroyed->setVectorImage(vector));
        paint(*destroyed, 1, 100);
        QVERIFY(destroyed->vectorRequestDepth() > 0);
    }
    QVERIFY(QvAsync::drain(5000));
    QCoreApplication::processEvents();
}

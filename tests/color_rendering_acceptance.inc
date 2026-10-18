// D1–D8: independent pixel references and real production loader/provider paths.
#include "native_color_fixtures.h"
#include "svgcolor.h"
#include <QBuffer>
#include <zlib.h>

static QByteArray colorAuditGzip(const QByteArray &plain)
{
    z_stream stream{ };
    if (deflateInit2(&stream, 6, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        return { };
    QByteArray result(4096, '\0');
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(plain.constData()));
    stream.avail_in = plain.size();
    stream.next_out = reinterpret_cast<Bytef *>(result.data());
    stream.avail_out = result.size();
    const int status = deflate(&stream, Z_FINISH);
    result.resize(stream.total_out);
    deflateEnd(&stream);
    return status == Z_STREAM_END ? result : QByteArray();
}
static QImage colorAuditRenderSvg(const Qv::VectorImageData &vector)
{
    QSvgRenderer renderer(vector.encodedData);
    QImage image(vector.logicalSize.toSize(), QImage::Format_RGBA64_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    renderer.render(&painter);
    painter.end();
    image.setColorSpace(vector.colorSpace);
    return image;
}
void ImageLoaderTests::testColorSvgWideGamut_data()
{
    QTest::addColumn<bool>("compressed");
    QTest::addColumn<bool>("utf16");
    QTest::newRow("svg") << false << false;
    QTest::newRow("svgz") << true << false;
    QTest::newRow("svg-utf16") << false << true;
    QTest::newRow("svgz-utf16") << true << true;
}
void ImageLoaderTests::testColorSvgWideGamut()
{
    QFETCH(bool, compressed);
    QFETCH(bool, utf16);
    QTemporaryDir dir;
    const QByteArray source =
            "<svg xmlns='http://www.w3.org/2000/svg' width='20' height='10'><rect width='10' "
            "height='10' fill='color(display-p3 1 0 0)'/><rect x='10' width='10' height='10' "
            "fill='color(display-p3 0 1 0 / 50%)'/></svg>";
    QFile file(dir.filePath(compressed ? "p3.svgz" : "p3.svg"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QByteArray encoded = source;
    if (utf16) {
        encoded = QByteArray::fromHex("fffe");
        for (const QChar c : QString::fromUtf8(source)) {
            encoded += char(c.unicode() & 255);
            encoded += char(c.unicode() >> 8);
        }
    }
    const QByteArray bytes = compressed ? colorAuditGzip(encoded) : encoded;
    QCOMPARE(file.write(bytes), qint64(bytes.size()));
    file.close();
    auto result = loadImage(file.fileName());
    QVERIFY(result && !result->errorData);
    QVERIFY(result->vectorImage.isValid());
    QCOMPARE(result->vectorImage.colorSpace, QColorSpace(QColorSpace::DisplayP3));
    const QImage actual = colorAuditRenderSvg(result->vectorImage);
    QVERIFY(actual.pixelColor(5, 5).redF() > .99);
    QVERIFY(actual.pixelColor(5, 5).greenF() < .01);
    QVERIFY(qAbs(actual.pixelColor(15, 5).alphaF() - .5) < .005);
    const QColor redInSrgb = actual.convertedToColorSpace(QColorSpace::SRgbLinear).pixelColor(5, 5);
    QVERIFY(redInSrgb.redF() > .99);
}
void ImageLoaderTests::testColorSvgMixedPaints()
{
    QImage embedded(2, 2, QImage::Format_RGB32);
    embedded.fill(Qt::red);
    embedded.setColorSpace(QColorSpace::SRgb);
    QByteArray png;
    QBuffer buffer(&png);
    QVERIFY(buffer.open(QIODevice::WriteOnly));
    QVERIFY(embedded.save(&buffer, "PNG"));
    QByteArray source =
            "<svg xmlns='http://www.w3.org/2000/svg' width='60' "
            "height='10'><style>.r{fill:red}</style><defs><linearGradient id='g'><stop "
            "stop-color='red'/><stop offset='1' "
            "stop-color='#ff0000'/></linearGradient></defs><rect width='10' height='10' "
            "fill='color(display-p3 1 0 0)'/><rect x='10' width='10' height='10' class='r'/><rect "
            "x='20' width='10' height='10' fill='rgb(255,0,0)'/><rect x='30' width='10' "
            "height='10' fill='url(#g)'/><rect x='40' width='10' height='10' "
            "style='fill:#ff0000;fill-opacity:0.5'/><image x='50' width='10' height='10' "
            "href='data:image/png;base64,"
            + png.toBase64() + "'/></svg>";
    QColorSpace space;
    QString error;
    const QByteArray normalized = QvColor::prepareSvg(source, { }, &space, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    Qv::VectorImageData vector;
    vector.format = Qv::VectorImageFormat::Svg;
    vector.encodedData = normalized;
    vector.logicalSize = QSize(60, 10);
    vector.colorSpace = space;
    QImage actual = colorAuditRenderSvg(vector);
    // Independent W3C sRGB -> linear P3 matrix, followed by the sRGB curve.
    // sRGB red in linear P3 is (0.82259287, 0.03319951, 0.01708535).
    const QColor expected = QColor::fromRgbF(.91748756, .20028681, .13856059);
    for (int x : { 15, 25, 35, 45, 55 }) {
        const QColor pixel = actual.pixelColor(x, 5);
        QVERIFY2(qAbs(pixel.redF() - expected.redF()) < .008, qPrintable(QString::number(x)));
        QVERIFY2(qAbs(pixel.greenF() - expected.greenF()) < .008, qPrintable(QString::number(x)));
        QVERIFY2(qAbs(pixel.blueF() - expected.blueF()) < .008, qPrintable(QString::number(x)));
    }
    QVERIFY(qAbs(actual.pixelColor(45, 5).alphaF() - .5) < .005);
    const QByteArray ordinary = "<svg xmlns='http://www.w3.org/2000/svg'/>";
    QCOMPARE(QvColor::prepareSvg(ordinary, { }, &space, &error), ordinary);
    for (const QByteArray value :
         { QByteArray("color(unknown 1 0 0)"), QByteArray("color(display-p3 nan 0 0)") }) {
        QVERIFY(QvColor::prepareSvg("<svg xmlns='http://www.w3.org/2000/svg'><rect fill='" + value
                                            + "'/></svg>",
                                    { }, &space, &error)
                        .isEmpty());
        QVERIFY(!error.isEmpty());
    }
}
void ImageLoaderTests::testColorNetpbm_data()
{
    QTest::addColumn<QByteArray>("magic");
    for (const char *magic : { "P2", "P3", "P5", "P6" })
        QTest::newRow(magic) << QByteArray(magic);
}
void ImageLoaderTests::testColorNetpbm()
{
    QFETCH(QByteArray, magic);
    QTemporaryDir dir;
    const bool rgb = magic == "P3" || magic == "P6";
    const bool binary = magic == "P5" || magic == "P6";
    for (int maximum : { 255, 65535 }) {
        QByteArray data = magic + "\n# standard BT.709\n3 1\n" + QByteArray::number(maximum) + "\n";
        for (int value : { 0, maximum == 255 ? 128 : 32897, maximum })
            for (int c = 0; c < (rgb ? 3 : 1); ++c) {
                if (!binary)
                    data += QByteArray::number(value) + ' ';
                else if (maximum == 255)
                    data += char(value);
                else {
                    data += char(value >> 8);
                    data += char(value & 255);
                }
            }
        QFile file(dir.filePath(QString::number(maximum) + ".wrong"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(data);
        file.close();
        auto result = loadImage(file.fileName());
        QVERIFY(result && !result->errorData);
        QImage actual = result->image.convertedToColorSpace(QColorSpace::SRgb);
        QVERIFY(!actual.isNull());
        const double v = double(maximum == 255 ? 128 : 32897) / maximum;
        const double linear = std::pow((v + .099) / 1.099, 1.0 / .45);
        const double expected = 1.055 * std::pow(linear, 1.0 / 2.4) - .055;
        QVERIFY(qAbs(actual.pixelColor(1, 0).redF() - expected) < .004);
        QVERIFY(actual.pixelColor(0, 0).redF() < .001);
        QVERIFY(actual.pixelColor(2, 0).redF() > .999);
        QVERIFY(result->sourceColor.spaceName.contains("709"));
        if (maximum > 255) {
            QVERIFY(result->image.depth() >= 64);
            const auto *samples = reinterpret_cast<const QRgba64 *>(result->image.constScanLine(0));
            QCOMPARE(samples[1].red(), uint16_t(32897));
        }
    }
    QVERIFY(!QvColor::isStandardNetpbm(QByteArray("PF\n")));
    QVERIFY(!QvColor::isStandardNetpbm(QByteArray("P4\n")));
    for (const QByteArray bad :
         { QByteArray("P6\n2147483647 2147483647\n65535\n"), QByteArray("P6\n1 1\n255\n\x01"),
           QByteArray("P2\n1 1\n255\n256 ") }) {
        QFile file(dir.filePath("bad.pnm"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(bad);
        file.close();
        QString error;
        QVERIFY(QvColor::readStandardNetpbm(file.fileName(), &error).isNull());
        QVERIFY(!error.isEmpty());
    }
}
void ImageLoaderTests::testColorNativeMultiImage()
{
    QTemporaryDir dir;
    for (const QString format : { QString("tiff"), QString("icns") }) {
        const QString path = createColorMultiImage(dir.path(), format);
        QVERIFY2(!path.isEmpty(), qPrintable(format));
        auto result = loadImage(path);
        QVERIFY(result && !result->errorData);
        QVERIFY(result->sdrImage);
        QVERIFY(result->isMultiFrameImage);
        QCOMPARE(result->intrinsicSize,
                 QSize(format == "tiff" ? 16 : 32, format == "tiff" ? 16 : 32));
        const QColor pixel = result->image.pixelColor(2, 2);
        QVERIFY(format == "tiff" ? pixel.redF() > .95 : pixel.greenF() > .95);
        TestableImageCore core;
        core.loadPixmap(*result);
        QVERIFY(!core.getCurrentFileDetails().isMovieLoaded);
    }
}
void ImageLoaderTests::testColorPdfPrecision()
{
    const QByteArray pdf = createColorP3Pdf();
    QVERIFY(!pdf.isEmpty());
    QString error;
    const QImage actual = QVCocoaFunctions::renderPDFVectorTile(
            pdf, QSizeF(16, 16), QRectF(0, 0, 16, 16), QSize(16, 16), &error);
    QVERIFY2(!actual.isNull(), qPrintable(error));
    QCOMPARE(actual.format(), QImage::Format_RGBA32FPx4_Premultiplied);
    QCOMPARE(actual.colorSpace(),
             QColorSpace(QColorSpace::Primaries::DciP3D65, QColorSpace::TransferFunction::Linear));
    const QColor pixel = actual.pixelColor(8, 8);
    QVERIFY(pixel.redF() > .99);
    QVERIFY(pixel.greenF() < .01);
    // Adobe RGB green lies outside P3. An integer P3 intermediate clips its
    // negative red component before a wider destination can recover it.
    const QImage wide = QVCocoaFunctions::renderPDFVectorTile(
            createColorP3Pdf(true), QSizeF(16, 16), QRectF(0, 0, 16, 16), QSize(16, 16), &error);
    QVERIFY2(!wide.isNull(), qPrintable(error));
    QVERIFY(wide.pixelColor(8, 8).redF() < -.01F);
    QTemporaryDir dir;
    const QString path = dir.filePath("p.eps");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("%!PS-Adobe-3.0 EPSF-3.0\n%%BoundingBox: 0 0 16 16\n1 0 0 setrgbcolor 0 0 16 16 "
               "rectfill showpage\n%%EOF\n");
    file.close();
    const auto result = QVCocoaFunctions::readImageWithImageIO(path, 16);
    QVERIFY2(!result.image.isNull(), qPrintable(result.errorString));
    QCOMPARE(result.image.format(), QImage::Format_RGBA32FPx4_Premultiplied);
    QCOMPARE(result.image.colorSpace(),
             QColorSpace(QColorSpace::Primaries::DciP3D65, QColorSpace::TransferFunction::Linear));
}
void ImageLoaderTests::testColorBoundedProvider()
{
    QVERIFY(!QVCocoaFunctions::requiresBoundedSDRProvider(QSize(20000, 20000), 8));
    QVERIFY(QVCocoaFunctions::requiresBoundedSDRProvider(QSize(20000, 20000), 16));
    QVERIFY(QVCocoaFunctions::requiresBoundedSDRProvider(QSize(12000, 12000), 32));
    QVERIFY(!QVCocoaFunctions::requiresBoundedSDRProvider({ }, 16));
    QImage image(2, 1, QImage::Format_RGBA64_Premultiplied);
    image.setColorSpace(QColorSpace::DisplayP3);
    auto *row = reinterpret_cast<QRgba64 *>(image.scanLine(0));
    row[0] = QRgba64::fromRgba64(0x8080, 0x4040, 0x2020, 65535);
    row[1] = QRgba64::fromRgba64(0x8081, 0x4040, 0x2020, 65535);
    auto native = QVCocoaFunctions::createSDRImage(image, false);
    QVERIFY(native);
    const auto actual = QVCocoaFunctions::probeSDRImagePixels(native);
    QCOMPARE(actual.size(), 8);
    QVERIFY(actual[4] > actual[0]);
    QVERIFY(std::abs(actual[0] - 0x8080 / 65535.0F) < .00002F);
    QVERIFY(std::abs(actual[4] - 0x8081 / 65535.0F) < .00002F);
#if QT_VERSION >= QT_VERSION_CHECK(6, 2, 0)
    QImage floating(1, 1, QImage::Format_RGBA32FPx4);
    floating.setColorSpace(QColorSpace::SRgbLinear);
    auto *pixel = reinterpret_cast<float *>(floating.bits());
    pixel[0] = .123456F;
    pixel[1] = .25F;
    pixel[2] = .5F;
    pixel[3] = 1;
    native = QVCocoaFunctions::createSDRImage(floating, false);
    QVERIFY(native);
    const auto floats = QVCocoaFunctions::probeSDRImagePixels(native, QColorSpace::SRgbLinear);
    QCOMPARE(floats.size(), 4);
    QVERIFY(std::abs(floats[0] - .123456F) < .00001F);
#endif
    QImage gray(2, 1, QImage::Format_Grayscale16);
    gray.setColorSpace(QColorSpace::SRgbLinear);
    auto *samples = reinterpret_cast<uint16_t *>(gray.bits());
    samples[0] = 0x8080;
    samples[1] = 0x8081;
    native = QVCocoaFunctions::createSDRImage(gray, false);
    QVERIFY(native);
    const auto grayscale = QVCocoaFunctions::probeSDRImagePixels(native, QColorSpace::SRgbLinear);
    QCOMPARE(grayscale.size(), 8);
    QVERIFY(grayscale[4] > grayscale[0]);
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    gray.setColorSpace(QColorSpace(QPointF(.3127, .3290), QColorSpace::TransferFunction::Linear));
    native = QVCocoaFunctions::createSDRImage(gray, false);
    QVERIFY(native);
    const auto grayProfile = QVCocoaFunctions::probeSDRImagePixels(native, QColorSpace::SRgbLinear);
    QCOMPARE(grayProfile.size(), 8);
    QVERIFY(grayProfile[4] > grayProfile[0]);
    QVERIFY(std::abs(grayProfile[0] - 0x8080 / 65535.0F) < .0001F);
#endif
    QImage alpha(1, 1, QImage::Format_RGBA64);
    alpha.setColorSpace(QColorSpace::DisplayP3);
    reinterpret_cast<QRgba64 *>(alpha.bits())[0] = QRgba64::fromRgba64(32768, 16384, 8192, 32768);
    native = QVCocoaFunctions::createSDRImage(alpha, true);
    QVERIFY(native);
    const auto translucent = QVCocoaFunctions::probeSDRImagePixels(native);
    QCOMPARE(translucent.size(), 4);
    QVERIFY(std::abs(translucent[3] - .5F) < .0001F);
    QVERIFY(std::abs(translucent[0] / translucent[3] - .5F) < .0001F);
    QVERIFY(!QVCocoaFunctions::createSDRImage({ }, false));
}
void ImageLoaderTests::testColorAnimationClearsStaticRepresentations()
{
    const auto hdr = QVCocoaFunctions::readImageWithImageIO(QStringLiteral(FOVELLE_TEST_FIXTURE_DIR)
                                                            + "/portable-hdr/gain-map.jpg");
    QVERIFY(hdr.hdrImage);
    QVImageLoader::Result result;
    result.absoluteFilePath = QStringLiteral(FOVELLE_TEST_FIXTURE_DIR) + "/remediation.gif";
    result.image = QImage(result.absoluteFilePath);
    QVERIFY(!result.image.isNull());
    result.hdrImage = hdr.hdrImage;
    result.hdrMetadata = hdr.hdrMetadata;
    TestableImageCore core;
    core.loadPixmap(result);
    QVERIFY(core.getCurrentFileDetails().isMovieLoaded);
    QVERIFY(!core.getLoadedHDRImage());
    QVERIFY(!core.getLoadedSDRImage());
    QVERIFY(!core.getCurrentFileDetails().isNativeHDRLoaded);
    QVERIFY(!core.getCurrentFileDetails().isNativeSDRLoaded);
    QVERIFY(!core.getCurrentFileDetails().hdrMetadata.decodedToHDR);
    QSignalSpy frames(&core, &QVImageCore::animatedFrameChanged);
    QTRY_VERIFY_WITH_TIMEOUT(frames.count() > 0, 3000);
    core.closeImage();
}
void ImageLoaderTests::testColorUnspecifiedSource()
{
    QvColor::Information information;
    information.hasOutput = true;
    information.source.iccState = QvColor::ICCState::Absent;
    information.source.decoderSpace = "sRGB";
    QVInfoDialog dialog;
    dialog.setColorInfoProvider([&] { return information; });
    auto *label = dialog.findChild<QLabel *>("sourceColorSpaceLabel");
    QVERIFY(label);
    QVERIFY(label->text().contains("assumed sRGB"));
    information.source.iccState = QvColor::ICCState::Invalid;
    dialog.setColorInfoProvider([&] { return information; });
    QVERIFY(!label->text().contains("assumed"));
    information.source.iccState = QvColor::ICCState::Embedded;
    information.source.origin = QvColor::Origin::ICC;
    information.source.spaceName = "Display P3";
    dialog.setColorInfoProvider([&] { return information; });
    QVERIFY(label->text().contains("Embedded ICC"));
}

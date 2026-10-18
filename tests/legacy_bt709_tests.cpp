#include "colorinformation.h"
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>

// The production compatibility backend is selected at compile time. Validate
// its ICC curve with independent transfer-function math, including the toe.
class LegacyBt709Tests : public QObject
{
    Q_OBJECT
private slots:
    void transferCurve_data()
    {
        QTest::addColumn<QByteArray>("magic");
        QTest::addColumn<int>("maximum");
        for (const char *type : { "P2", "P3", "P5", "P6" })
            for (int maximum : { 255, 65535 })
                QTest::newRow(qPrintable(QString::fromLatin1(type) + QString::number(maximum)))
                        << QByteArray(type) << maximum;
    }
    void transferCurve()
    {
        QFETCH(QByteArray, magic);
        QFETCH(int, maximum);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const bool rgb = magic == "P3" || magic == "P6";
        const bool binary = magic == "P5" || magic == "P6";
        const int values[] = { 0,           maximum / 32, int(maximum * .08),
                               maximum / 2, maximum,      maximum == 65535 ? 32897 : 128 };
        QByteArray encoded = magic + "\n6 1\n" + QByteArray::number(maximum) + '\n';
        for (int x = 0; x < 6; ++x)
            for (int c = 0; c < (rgb ? 3 : 1); ++c) {
                const int value = values[(x + c) % 6];
                if (!binary)
                    encoded += QByteArray::number(value) + ' ';
                else {
                    if (maximum > 255)
                        encoded += char(value >> 8);
                    encoded += char(value & 255);
                }
            }
        QFile file(directory.filePath("signal.pnm"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(encoded), qint64(encoded.size()));
        file.close();
        QString error;
        const QImage source = QvColor::readStandardNetpbm(file.fileName(), &error);
        QVERIFY2(!source.isNull(), qPrintable(error));
        QVERIFY(source.colorSpace().isValid());
        const QImage actual = source.convertedToColorSpace(QColorSpace::SRgb);
        QVERIFY(!actual.isNull());
        for (int x = 0; x < 6; ++x)
            for (int c = 0; c < (rgb ? 3 : 1); ++c) {
                const double v = double(values[(x + c) % 6]) / maximum;
                const double linear = v < .081 ? v / 4.5 : std::pow((v + .099) / 1.099, 1 / .45);
                const double expected = linear <= .0031308
                        ? 12.92 * linear
                        : 1.055 * std::pow(linear, 1 / 2.4) - .055;
                const QColor pixel = actual.pixelColor(x, 0);
                const double channel = c == 0 ? pixel.redF()
                        : c == 1              ? pixel.greenF()
                                              : pixel.blueF();
                QVERIFY2(std::abs(channel - expected) < .004,
                         qPrintable(QString("pixel=%1 channel=%2 actual=%3 expected=%4")
                                            .arg(x)
                                            .arg(c)
                                            .arg(channel)
                                            .arg(expected)));
            }
        if (maximum == 65535)
            QCOMPARE(reinterpret_cast<const QRgba64 *>(source.constScanLine(0))[5].red(),
                     uint16_t(32897));
    }
};
QTEST_GUILESS_MAIN(LegacyBt709Tests)
#include "legacy_bt709_tests.moc"

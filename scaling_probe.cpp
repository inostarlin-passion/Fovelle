#include <QGuiApplication>
#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QElapsedTimer>
#include <QDir>
#include <QFileInfo>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace {

QImage renderBilinear(const QImage &source, const QSize &target)
{
    QImage result(target, QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(QRect(QPoint(), target), source);
    return result;
}

QImage renderExpensive(const QImage &source, const QSize &target)
{
    return source.scaled(target, Qt::IgnoreAspectRatio,
                         Qt::SmoothTransformation)
        .convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

struct Metrics
{
    double mae = 0;
    double rmse = 0;
    double psnr = 0;
    double changed1 = 0;
    double changed4 = 0;
    int maxError = 0;
};

Metrics compareImages(const QImage &a, const QImage &b)
{
    long double absoluteSum = 0;
    long double squaredSum = 0;
    qsizetype changed1 = 0;
    qsizetype changed4 = 0;
    int maxError = 0;
    const qsizetype pixelCount = qsizetype(a.width()) * a.height();
    const qsizetype channelCount = pixelCount * 3;
    for (int y = 0; y < a.height(); ++y) {
        const QRgb *pa = reinterpret_cast<const QRgb *>(a.constScanLine(y));
        const QRgb *pb = reinterpret_cast<const QRgb *>(b.constScanLine(y));
        for (int x = 0; x < a.width(); ++x) {
            const int dr = std::abs(qRed(pa[x]) - qRed(pb[x]));
            const int dg = std::abs(qGreen(pa[x]) - qGreen(pb[x]));
            const int db = std::abs(qBlue(pa[x]) - qBlue(pb[x]));
            const int pixelMax = std::max({dr, dg, db});
            absoluteSum += dr + dg + db;
            squaredSum += dr * dr + dg * dg + db * db;
            changed1 += pixelMax >= 1;
            changed4 += pixelMax >= 4;
            maxError = std::max(maxError, pixelMax);
        }
    }
    Metrics m;
    m.mae = double(absoluteSum / channelCount);
    m.rmse = std::sqrt(double(squaredSum / channelCount));
    m.psnr = m.rmse == 0 ? std::numeric_limits<double>::infinity()
                         : 20 * std::log10(255.0 / m.rmse);
    m.changed1 = 100.0 * changed1 / pixelCount;
    m.changed4 = 100.0 * changed4 / pixelCount;
    m.maxError = maxError;
    return m;
}

double median(QVector<double> values)
{
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

double benchmark(const std::function<QImage()> &function, int iterations)
{
    QVector<double> samples;
    samples.reserve(iterations);
    for (int i = 0; i < iterations + 2; ++i) {
        QElapsedTimer timer;
        timer.start();
        QImage result = function();
        result.constBits();
        const double elapsed = timer.nsecsElapsed() / 1000000.0;
        if (i >= 2)
            samples.push_back(elapsed);
    }
    return median(samples);
}

QImage checkerboard(int size)
{
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < size; ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < size; ++x)
            line[x] = ((x + y) & 1) ? qRgb(255, 255, 255) : qRgb(0, 0, 0);
    }
    return image;
}

void probe(const QString &label, QImage source, const QVector<double> &scales,
           QTextStream &out, const QString &outputDir = {})
{
    source = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    for (double scale : scales) {
        QSize target(qMax(1, qRound(source.width() * scale)),
                     qMax(1, qRound(source.height() * scale)));
        QImage bilinear = renderBilinear(source, target);
        QImage expensive = renderExpensive(source, target);
        Metrics metrics = compareImages(bilinear, expensive);
        const int iterations = target.width() * target.height() > 4'000'000 ? 5 : 11;
        const double bilinearMs = benchmark([&]{ return renderBilinear(source, target); }, iterations);
        const double expensiveMs = benchmark([&]{ return renderExpensive(source, target); }, iterations);
        out << label << ',' << source.width() << 'x' << source.height() << ','
            << scale << ',' << target.width() << 'x' << target.height() << ','
            << metrics.mae << ',' << metrics.rmse << ',' << metrics.psnr << ','
            << metrics.changed1 << ',' << metrics.changed4 << ',' << metrics.maxError << ','
            << bilinearMs << ',' << expensiveMs << '\n';
        if (!outputDir.isEmpty() && (qFuzzyCompare(scale, 0.25) || qFuzzyCompare(scale, 0.75))) {
            bilinear.save(outputDir + '/' + label + QString("_%1_bilinear.png").arg(scale));
            expensive.save(outputDir + '/' + label + QString("_%1_expensive.png").arg(scale));
            QImage diff(target, QImage::Format_RGB32);
            for (int y = 0; y < target.height(); ++y) {
                const QRgb *pa = reinterpret_cast<const QRgb *>(bilinear.constScanLine(y));
                const QRgb *pb = reinterpret_cast<const QRgb *>(expensive.constScanLine(y));
                QRgb *pd = reinterpret_cast<QRgb *>(diff.scanLine(y));
                for (int x = 0; x < target.width(); ++x) {
                    const int r = qMin(255, std::abs(qRed(pa[x]) - qRed(pb[x])) * 8);
                    const int g = qMin(255, std::abs(qGreen(pa[x]) - qGreen(pb[x])) * 8);
                    const int b = qMin(255, std::abs(qBlue(pa[x]) - qBlue(pb[x])) * 8);
                    pd[x] = qRgb(r, g, b);
                }
            }
            diff.save(outputDir + '/' + label + QString("_%1_diff8x.png").arg(scale));
        }
    }
}

}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    if (argc != 3)
        return 2;
    const QString inputDir = QString::fromLocal8Bit(argv[1]);
    const QString outputDir = QString::fromLocal8Bit(argv[2]);
    QDir().mkpath(outputDir);
    QTextStream out(stdout);
    out << "file,source,scale,target,mae,rmse,psnr_db,changed_ge1_pct,changed_ge4_pct,max_error,bilinear_ms,expensive_ms\n";
    QDir dir(inputDir);
    const QFileInfoList files = dir.entryInfoList(QDir::Files, QDir::Name);
    for (const QFileInfo &file : files) {
        QImageReader reader(file.absoluteFilePath());
        reader.setAutoTransform(true);
        QImage source = reader.read();
        if (source.isNull()) {
            out << file.fileName() << ",ERROR," << reader.errorString() << '\n';
            continue;
        }
        probe(file.completeBaseName(), source, {0.125, 0.25, 0.5, 0.75, 1.25, 2.0}, out,
              file.fileName() == QStringLiteral("2.png") ? outputDir : QString());
    }
    probe(QStringLiteral("checker1px"), checkerboard(2048), {0.125, 0.25, 0.5, 0.75, 1.25, 2.0}, out,
          outputDir);
    return 0;
}

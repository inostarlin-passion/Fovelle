#ifndef NATIVE_COLOR_FIXTURES_H
#define NATIVE_COLOR_FIXTURES_H
#include <QByteArray>
#include <QSize>
#include <QString>
QString createColorMultiImage(const QString &directory, const QString &format);
QByteArray createColorP3Pdf(bool adobeGreen = false);
bool scaleComparisonHDRJpeg(const QString &source, const QString &path, QSize size,
                            bool mirrored = false);
#endif

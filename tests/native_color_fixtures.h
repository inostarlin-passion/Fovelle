#ifndef NATIVE_COLOR_FIXTURES_H
#define NATIVE_COLOR_FIXTURES_H
#include <QByteArray>
#include <QString>
QString createColorMultiImage(const QString &directory, const QString &format);
QByteArray createColorP3Pdf(bool adobeGreen = false);
#endif

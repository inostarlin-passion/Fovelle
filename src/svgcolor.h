#ifndef SVGCOLOR_H
#define SVGCOLOR_H
#include <QByteArray>
#include <QColorSpace>
#include <QString>
namespace QvColor {
// Return one color-managed source for the preview and every vector tile.
QByteArray prepareSvg(const QByteArray &encoded, const QString &sourcePath, QColorSpace *space,
                      QString *error);
} // namespace QvColor
#endif

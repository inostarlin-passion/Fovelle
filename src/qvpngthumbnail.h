#ifndef QVPNGTHUMBNAIL_H
#define QVPNGTHUMBNAIL_H

#include <QImage>
#include <optional>

// No value means the regular thumbnail path should handle the file. An empty
// image means the PNG requires the streaming path but could not be decoded.
std::optional<QImage> readLargePngThumbnail(const QString &path, int longestEdge,
                                            qint64 nativeFileLimit, qint64 nativePixelLimit);

#endif

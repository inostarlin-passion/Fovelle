#include "colorinformation.h"

#include <QFile>
#include <QMap>
#include <QtEndian>
#include <zlib.h>
#include <cmath>
#include <QImageReader>
#include <limits>

namespace QvColor {
namespace {
constexpr quint32 ProfileLimit = 16 * 1024 * 1024;
constexpr int EntryLimit = 8192;
quint32 be32(const char *p)
{
    return qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(p));
}
quint16 be16(const char *p)
{
    return qFromBigEndian<quint16>(reinterpret_cast<const uchar *>(p));
}
quint32 le32(const char *p)
{
    return qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(p));
}
QByteArray readAt(QFile &file, quint64 offset, quint64 length)
{
    if (offset > quint64(file.size()) || length > quint64(file.size()) - offset
        || length > ProfileLimit || !file.seek(qint64(offset)))
        return { };
    return file.read(qint64(length));
}
void profile(SourceInfo &info, QByteArray data)
{
    info.iccData = std::move(data);
    info.iccState = ICCState::Invalid;
    const auto &bytes = info.iccData;
    if (bytes.size() < 132 || bytes.size() > ProfileLimit || bytes.mid(36, 4) != "acsp"
        || be32(bytes.constData()) != quint32(bytes.size()))
        return;
    const quint32 count = be32(bytes.constData() + 128);
    if (count > quint32((bytes.size() - 132) / 12))
        return;
    for (quint32 i = 0; i < count; ++i) {
        const char *tag = bytes.constData() + 132 + 12 * i;
        const quint32 offset = be32(tag + 4), size = be32(tag + 8);
        if (offset < 132 + 12 * count || offset > quint32(bytes.size())
            || size > quint32(bytes.size()) - offset)
            return;
    }
    // Only pass structurally bounded v2/v4 profiles to Qt. Unsupported
    // versions retain the original bytes without being called malformed.
    const auto version = uchar(bytes.at(8));
    if (version != 2 && version != 4) {
        info.iccState = ICCState::Unsupported;
        return;
    }
    const QColorSpace space = QColorSpace::fromIccProfile(bytes);
    info.iccState = space.isValid() ? ICCState::Embedded : ICCState::Unsupported;
    if (space.isValid()) {
        info.origin = Origin::ICC;
        info.spaceName = QvColor::spaceName(space);
#if QT_VERSION >= QT_VERSION_CHECK(6, 2, 0)
        info.profileDescription = space.description().left(256);
#endif
    }
}

SourceInfo png(QFile &file)
{
    SourceInfo info;
    bool seenICC = false, seenHeader = false, seenPixels = false;
    QString container;
    QColorSpace containerColor;
    quint64 offset = 8;
    for (int i = 0; i < EntryLimit; ++i) {
        const auto header = readAt(file, offset, 8);
        if (header.size() != 8)
            return { };
        const quint32 size = be32(header.constData());
        const auto type = header.mid(4, 4);
        const quint64 end = offset + 12 + quint64(size);
        if (end > quint64(file.size()))
            return { };
        if (!seenHeader && (type != "IHDR" || size != 13))
            return { };
        seenHeader = true;
        if (type == "IDAT")
            seenPixels = true;
        if (type == "iCCP" || type == "sRGB" || type == "cICP") {
            if (size > ProfileLimit)
                return { };
            const auto data = readAt(file, offset + 8, size);
            const auto crcBytes = readAt(file, offset + 8 + size, 4);
            if (quint32(data.size()) != size || crcBytes.size() != 4)
                return { };
            uLong crc = crc32(0, reinterpret_cast<const Bytef *>(type.constData()), 4);
            crc = crc32(crc, reinterpret_cast<const Bytef *>(data.constData()), size);
            if (crc != be32(crcBytes.constData())) {
                if (type == "iCCP") {
                    info.iccState = ICCState::Invalid;
                    return info;
                }
                return { };
            }
            if (type == "iCCP") {
                if (seenICC) {
                    info.iccState = ICCState::Invalid;
                    return info;
                }
                seenICC = true;
                const int separator = data.indexOf('\0');
                if (separator < 1 || separator > 79 || separator + 2 >= data.size()
                    || data.at(separator + 1) != 0) {
                    info.iccState = ICCState::Invalid;
                    return info;
                }
                QByteArray decoded(int(ProfileLimit), Qt::Uninitialized);
                uLongf length = ProfileLimit;
                const int status = uncompress(
                        reinterpret_cast<Bytef *>(decoded.data()), &length,
                        reinterpret_cast<const Bytef *>(data.constData() + separator + 2),
                        uLong(data.size() - separator - 2));
                if (status == Z_BUF_ERROR)
                    return { }; // Limited, not verified absent.
                if (status != Z_OK) {
                    info.iccState = ICCState::Invalid;
                    return info;
                }
                decoded.resize(qsizetype(length));
                decoded.squeeze(); // Do not retain a 16 MiB allocation per cached small profile.
                profile(info, std::move(decoded));
            } else if (type == "sRGB") {
                if (size != 1 || uchar(data[0]) > 3)
                    return { };
                if (container.isEmpty()) {
                    container = QStringLiteral("sRGB");
                    containerColor = QColorSpace::SRgb;
                }
            } else {
                if (size != 4 || uchar(data[2]) != 0 || uchar(data[3]) > 1)
                    return { };
                const int primaries = uchar(data[0]), transfer = uchar(data[1]);
                if (primaries == 1 && transfer == 13 && uchar(data[3]) == 1) {
                    container = QStringLiteral("sRGB");
                    containerColor = QColorSpace::SRgb;
                } else if (primaries == 12 && transfer == 13 && uchar(data[3]) == 1) {
                    container = QStringLiteral("Display P3");
                    containerColor = QColorSpace::DisplayP3;
                } else {
                    container = QStringLiteral("cICP (%1, %2, %3, %4)")
                                        .arg(primaries)
                                        .arg(transfer)
                                        .arg(uchar(data[2]))
                                        .arg(uchar(data[3]));
                    containerColor = { };
                }
            }
        }
        if (type == "IEND") {
            if (size != 0 || !seenPixels || end != quint64(file.size()))
                return { };
            if (!seenICC)
                info.iccState = ICCState::Absent;
            if (!container.isEmpty()) {
                if (info.iccState == ICCState::Embedded && containerColor.isValid())
                    info.conflictingDeclarations =
                            QColorSpace::fromIccProfile(info.iccData) != containerColor;
                // PNG cICP takes precedence over ICC; sRGB is only a fallback.
                if (info.origin != Origin::ICC || container.startsWith("cICP")
                    || containerColor.isValid()) {
                    info.spaceName = container;
                    info.origin = Origin::Container;
                }
            }
            return info;
        }
        offset = end;
    }
    return { };
}

SourceInfo jpeg(QFile &file)
{
    SourceInfo info;
    QMap<int, QByteArray> parts;
    int total = 0;
    bool seenFrame = false;
    quint64 offset = 2, accumulated = 0;
    for (int i = 0; i < EntryLimit; ++i) {
        auto marker = readAt(file, offset, 2);
        if (marker.size() != 2 || uchar(marker[0]) != 0xff)
            return { };
        if (uchar(marker[1]) == 0xff) {
            ++offset;
            continue;
        }
        const auto code = uchar(marker[1]);
        if (code == 0xda || code == 0xd9) {
            if (!seenFrame)
                return { };
            if (code == 0xda) {
                const auto length = readAt(file, offset + 2, 2);
                if (length.size() != 2 || be16(length.constData()) < 2
                    || offset + 2 + be16(length.constData()) > quint64(file.size()))
                    return { };
            }
            if (parts.isEmpty()) {
                info.iccState = ICCState::Absent;
                return info;
            }
            if (parts.size() != total) {
                info.iccState = ICCState::Invalid;
                return info;
            }
            QByteArray data;
            for (int n = 1; n <= total; ++n) {
                if (!parts.contains(n)) {
                    info.iccState = ICCState::Invalid;
                    return info;
                }
                data += parts.value(n);
            }
            profile(info, std::move(data));
            return info;
        }
        if (code == 0x01 || (code >= 0xd0 && code <= 0xd7)) {
            offset += 2;
            continue;
        }
        const auto length = readAt(file, offset + 2, 2);
        if (length.size() != 2)
            return { };
        const quint16 size = be16(length.constData());
        if (size < 2 || offset + 2 + size > quint64(file.size()))
            return { };
        if (code >= 0xc0 && code <= 0xcf && code != 0xc4 && code != 0xc8 && code != 0xcc)
            seenFrame = true;
        if (code == 0xe2) {
            const auto data = readAt(file, offset + 4, size - 2);
            if (data.startsWith(QByteArray("ICC_PROFILE\0", 12))) {
                if (data.size() < 14) {
                    info.iccState = ICCState::Invalid;
                    return info;
                }
                const int n = uchar(data[12]), count = uchar(data[13]);
                if (!n || !count || n > count || (total && count != total) || parts.contains(n)) {
                    info.iccState = ICCState::Invalid;
                    return info;
                }
                total = count;
                accumulated += quint64(data.size() - 14);
                if (accumulated > ProfileLimit)
                    return { };
                parts.insert(n, data.mid(14));
            }
        }
        offset += 2 + size;
    }
    return { };
}
SourceInfo webp(QFile &file)
{
    const auto header = readAt(file, 0, 12);
    if (header.size() != 12)
        return { };
    const quint64 end = 8 + quint64(le32(header.constData() + 4));
    if (end != quint64(file.size()) || end < 12)
        return { };
    SourceInfo info;
    bool found = false, pixels = false;
    quint64 offset = 12;
    for (int i = 0; offset < end && i < EntryLimit; ++i) {
        const auto chunk = readAt(file, offset, 8);
        if (chunk.size() != 8)
            return { };
        const quint32 size = le32(chunk.constData() + 4);
        if (offset + 8 + size + (size & 1) > end)
            return { };
        if (chunk.left(4) == "ICCP") {
            if (size > ProfileLimit)
                return { };
            if (found) {
                info.iccState = ICCState::Invalid;
                return info;
            }
            found = true;
            profile(info, readAt(file, offset + 8, size));
        }
        if (chunk.left(4) == "VP8 " || chunk.left(4) == "VP8L" || chunk.left(4) == "ANMF")
            pixels = true;
        offset += 8 + size + (size & 1);
    }
    if (offset != end || !pixels)
        return { };
    if (!found)
        info.iccState = ICCState::Absent;
    return info;
}
SourceInfo tiff(QFile &file, bool little)
{
    const auto u16 = [little](const char *p) {
        return little ? qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(p)) : be16(p);
    };
    const auto u32 = [little](const char *p) { return little ? le32(p) : be32(p); };
    const auto header = readAt(file, 0, 8);
    if (header.size() != 8 || u16(header.constData() + 2) != 42)
        return { }; // BigTIFF is unknown.
    const quint32 offset = u32(header.constData() + 4);
    const auto countBytes = readAt(file, offset, 2);
    if (countBytes.size() != 2)
        return { };
    const quint16 count = u16(countBytes.constData());
    if (count > EntryLimit)
        return { };
    const auto entries = readAt(file, quint64(offset) + 2, quint64(count) * 12 + 4);
    if (entries.size() != count * 12 + 4)
        return { };
    // Multi-page readers may select a different image. Never label page zero's
    // profile as the selected page without an explicit decoder image index.
    if (u32(entries.constData() + count * 12) != 0)
        return { };
    SourceInfo info;
    bool found = false;
    for (int i = 0; i < count; ++i) {
        const char *entry = entries.constData() + i * 12;
        if (u16(entry) != 34675)
            continue;
        if (found) {
            info.iccState = ICCState::Invalid;
            return info;
        }
        found = true;
        const quint32 size = u32(entry + 4);
        if (size > ProfileLimit)
            return { };
        if (u16(entry + 2) != 7 || size < 132) {
            info.iccState = ICCState::Invalid;
            return info;
        }
        const auto data = readAt(file, u32(entry + 8), size);
        if (quint32(data.size()) != size)
            return { };
        profile(info, data);
    }
    if (!found)
        info.iccState = ICCState::Absent;
    return info;
}
} // namespace
QString spaceName(const QColorSpace &space)
{
    if (!space.isValid())
        return { };
    if (space == QColorSpace(QColorSpace::SRgb))
        return QStringLiteral("sRGB");
    if (space == QColorSpace(QColorSpace::DisplayP3))
        return QStringLiteral("Display P3");
    if (space == QColorSpace(QColorSpace::AdobeRgb))
        return QStringLiteral("Adobe RGB (1998)");
    if (space == QColorSpace(QColorSpace::SRgbLinear))
        return QStringLiteral("Linear sRGB");
    if (space == QColorSpace(QColorSpace::ProPhotoRgb))
        return QStringLiteral("ProPhoto RGB");
#if QT_VERSION >= QT_VERSION_CHECK(6, 2, 0)
    const auto description = space.description().left(256);
    return description.isEmpty() ? QStringLiteral("ICC") : description;
#else
    return QStringLiteral("ICC");
#endif
}
bool isStandardNetpbm(const QByteArray &header)
{
    return header.size() >= 3 && header[0] == 'P'
            && (header[1] == '2' || header[1] == '3' || header[1] == '5' || header[1] == '6')
            && (header[2] == ' ' || header[2] == '\n' || header[2] == '\r' || header[2] == '\t');
}
QColorSpace bt709ColorSpace()
{
    static const QColorSpace space = [] {
        QList<uint16_t> curve;
        curve.reserve(4096);
        for (int i = 0; i < 4096; ++i) {
            const double v = i / 4095.0;
            const double linear = v < .081 ? v / 4.5 : std::pow((v + .099) / 1.099, 1.0 / .45);
            curve.append(uint16_t(qRound(linear * 65535.0)));
        }
#if QT_VERSION >= QT_VERSION_CHECK(6, 1, 0) && !defined(FOVELLE_TEST_LEGACY_BT709)
        QColorSpace result(QColorSpace::Primaries::SRgb, curve);
#else
        // Older Qt accepts matrix/TRC ICC profiles but cannot construct a
        // sampled transfer function directly. Retain Qt's sRGB primaries and
        // replace its three TRC tags with one shared ICC curveType payload.
        QByteArray icc = QColorSpace(QColorSpace::SRgb).iccProfile();
        if (icc.size() < 132)
            return QColorSpace();
        const quint32 tags = be32(icc.constData() + 128);
        if (tags > quint32((icc.size() - 132) / 12))
            return QColorSpace();
        QByteArray samples(12 + 2 * curve.size(), '\0');
        samples.replace(0, 4, "curv");
        qToBigEndian<quint32>(quint32(curve.size()), samples.data() + 8);
        for (int i = 0; i < curve.size(); ++i)
            qToBigEndian<quint16>(curve[i], samples.data() + 12 + 2 * i);
        icc.append((4 - icc.size() % 4) % 4, '\0');
        const quint32 curveOffset = quint32(icc.size());
        icc.append(samples);
        int replaced = 0;
        for (quint32 i = 0; i < tags; ++i) {
            const qsizetype entry = 132 + 12 * qsizetype(i);
            const QByteArray tag = icc.mid(entry, 4);
            if (tag == "rTRC" || tag == "gTRC" || tag == "bTRC") {
                qToBigEndian<quint32>(curveOffset, icc.data() + entry + 4);
                qToBigEndian<quint32>(quint32(samples.size()), icc.data() + entry + 8);
                ++replaced;
            }
        }
        if (replaced != 3)
            return QColorSpace();
        qToBigEndian<quint32>(quint32(icc.size()), icc.data());
        icc.replace(84, 16, QByteArray(16, '\0')); // Do not retain the old profile ID.
        QColorSpace result = QColorSpace::fromIccProfile(icc);
#endif
#if QT_VERSION >= QT_VERSION_CHECK(6, 2, 0)
        result.setDescription(QStringLiteral("BT.709 (Netpbm)"));
#endif
        return result;
    }();
    return space;
}
QImage readStandardNetpbm(const QString &path, QString *error)
{
    error->clear();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = file.errorString();
        return { };
    }
    char delimiter = 0;
    const auto token = [&]() -> QByteArray {
        QByteArray value;
        char c;
        while (file.getChar(&c)) {
            if (c == '#') {
                while (file.getChar(&c) && c != '\n' && c != '\r') { }
                continue;
            }
            if (!QChar::fromLatin1(c).isSpace()) {
                value += c;
                break;
            }
        }
        while (file.getChar(&c)) {
            if (QChar::fromLatin1(c).isSpace()) {
                delimiter = c;
                break;
            }
            if (value.size() >= 32)
                return { };
            value += c;
        }
        return value;
    };
    const QByteArray magic = token();
    bool widthOk = false, heightOk = false, maxOk = false;
    const int width = token().toInt(&widthOk), height = token().toInt(&heightOk),
              maximum = token().toInt(&maxOk);
    const bool rgb = magic == "P3" || magic == "P6";
    const bool binary = magic == "P5" || magic == "P6";
    const quint64 limit = QImageReader::allocationLimit() > 0
            ? quint64(QImageReader::allocationLimit()) * 1024 * 1024
            : 8ULL * 1024 * 1024 * 1024;
    if (!isStandardNetpbm(magic + '\n') || !widthOk || !heightOk || !maxOk || width <= 0
        || height <= 0 || maximum <= 0 || maximum > 65535
        || quint64(width) > limit / quint64(height) / 8
        || quint64(width) * 8 > std::numeric_limits<int>::max()) {
        *error = QStringLiteral("Invalid or oversized Netpbm image");
        return { };
    }
    if (binary && delimiter == '\r' && file.peek(1) == "\n")
        file.getChar(&delimiter);
    QImage image(width, height, QImage::Format_RGBX64);
    if (image.isNull()) {
        *error = QStringLiteral("Netpbm pixels could not be allocated");
        return { };
    }
    const int channels = rgb ? 3 : 1, bytes = maximum > 255 ? 2 : 1;
    for (int y = 0; y < height; ++y) {
        QByteArray row;
        if (binary) {
            const qint64 size = qint64(width) * channels * bytes;
            row = file.read(size);
            if (row.size() != size) {
                *error = QStringLiteral("Truncated Netpbm raster");
                return { };
            }
        }
        auto *pixels = reinterpret_cast<QRgba64 *>(image.scanLine(y));
        qsizetype offset = 0;
        for (int x = 0; x < width; ++x) {
            uint16_t components[3]{ };
            for (int c = 0; c < channels; ++c) {
                bool ok = true;
                int value;
                if (binary) {
                    value = uchar(row[offset++]);
                    if (bytes == 2)
                        value = (value << 8) | uchar(row[offset++]);
                } else
                    value = token().toInt(&ok);
                if (!ok || value < 0 || value > maximum) {
                    *error = QStringLiteral("Invalid Netpbm sample");
                    return { };
                }
                components[c] = uint16_t((quint64(value) * 65535 + maximum / 2) / maximum);
            }
            if (!rgb)
                components[1] = components[2] = components[0];
            pixels[x] = QRgba64::fromRgba64(components[0], components[1], components[2], 65535);
        }
    }
    image.setColorSpace(bt709ColorSpace());
    return image;
}
SourceInfo readSource(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return { };
    const auto header = file.peek(16);
    if (isStandardNetpbm(header)) {
        SourceInfo info;
        info.iccState = ICCState::Absent;
        info.origin = Origin::Container;
        info.spaceName = QStringLiteral("BT.709 (Netpbm)");
        return info;
    }
    if (header.startsWith(QByteArray::fromHex("89504e470d0a1a0a")))
        return png(file);
    if (header.startsWith(QByteArray::fromHex("ffd8")))
        return jpeg(file);
    if (header.size() >= 12 && header.startsWith("RIFF") && header.mid(8, 4) == "WEBP")
        return webp(file);
    if (header.startsWith("II") || header.startsWith("MM"))
        return tiff(file, header.startsWith("II"));
    return { };
}
} // namespace QvColor

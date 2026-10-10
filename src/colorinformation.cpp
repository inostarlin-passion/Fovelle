#include "colorinformation.h"

#include <QFile>
#include <QMap>
#include <QtEndian>
#include <zlib.h>

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
SourceInfo readSource(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return { };
    const auto header = file.peek(16);
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

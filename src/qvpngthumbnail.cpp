#include "qvpngthumbnail.h"

#include "asyncwork.h"

#include <QColorSpace>
#include <QFile>
#include <QScopeGuard>
#include <QTransform>
#include <QtEndian>

#include <cstring>
#include <vector>
#include <png.h>

namespace {
constexpr qint64 MaxPngFileBytes = 2LL * 1024 * 1024 * 1024;
constexpr qint64 MaxPngPixels = 1000LL * 1000 * 1000;
constexpr int MaxPngEdge = 65536;
constexpr int MaxPreviewEdge = 1024;
constexpr int SamplingScale = 4;
constexpr int MaxMetadataChunkBytes = 2 * 1024 * 1024;

struct PngInput
{
    QFile &file;
    qint64 remaining;
};

void readPngBytes(png_structp png, png_bytep output, png_size_t count)
{
    auto &input = *static_cast<PngInput *>(png_get_io_ptr(png));
    if (QvAsync::isCanceled() || count > png_size_t(input.remaining)
        || input.file.read(reinterpret_cast<char *>(output), qint64(count)) != qint64(count))
        png_error(png, "Could not read PNG data");
    input.remaining -= qint64(count);
}

int pngOrientation(png_structp png, png_infop info)
{
    png_uint_32 size = 0;
    png_bytep data = nullptr;
    if (!png_get_eXIf_1(png, info, &size, &data) || size < 8)
        return 1;

    const quint16 order = qFromBigEndian<quint16>(data);
    if (order != 0x4949 && order != 0x4d4d)
        return 1;
    const bool little = order == 0x4949;
    const auto read16 = [little](const uchar *bytes) {
        return little ? qFromLittleEndian<quint16>(bytes) : qFromBigEndian<quint16>(bytes);
    };
    const auto read32 = [little](const uchar *bytes) {
        return little ? qFromLittleEndian<quint32>(bytes) : qFromBigEndian<quint32>(bytes);
    };
    const quint32 offset = read32(data + 4);
    if (read16(data + 2) != 42 || offset < 8 || offset > size - 2)
        return 1;
    const quint32 entries = read16(data + offset);
    if (entries > (size - offset - 2) / 12)
        return 1;
    for (quint32 i = 0; i < entries; ++i) {
        const uchar *entry = data + offset + 2 + i * 12;
        if (read16(entry) == 274 && read16(entry + 2) == 3 && read32(entry + 4) == 1) {
            const int orientation = read16(entry + 8);
            return orientation >= 1 && orientation <= 8 ? orientation : 1;
        }
    }
    return 1;
}

QColorSpace pngColorSpace(png_structp png, png_infop info)
{
    png_byte primaries = 0, transfer = 0, matrix = 0, fullRange = 0;
    if (png_get_cICP(png, info, &primaries, &transfer, &matrix, &fullRange)) {
        if (transfer == 13 && matrix == 0 && fullRange == 1) {
            if (primaries == 1)
                return QColorSpace::SRgb;
            if (primaries == 12)
                return QColorSpace::DisplayP3;
        }
        return { };
    }

    png_charp name = nullptr;
    png_bytep profile = nullptr;
    png_uint_32 length = 0;
    int compression = 0;
    if (png_get_iCCP(png, info, &name, &compression, &profile, &length)) {
        const auto space =
                QColorSpace::fromIccProfile(QByteArray(reinterpret_cast<char *>(profile), length));
        if (space.isValid())
            return space;
    }

    int intent = 0;
    if (png_get_sRGB(png, info, &intent))
        return QColorSpace::SRgb;

    double gamma = 0;
    const bool hasGamma = png_get_gAMA(png, info, &gamma) && gamma > 0.0;
    const auto function =
            hasGamma ? QColorSpace::TransferFunction::Gamma : QColorSpace::TransferFunction::SRgb;
    const float exponent = hasGamma ? float(1.0 / gamma) : 0.0F;
    double wx, wy, rx, ry, gx, gy, bx, by;
    if (png_get_cHRM(png, info, &wx, &wy, &rx, &ry, &gx, &gy, &bx, &by)) {
        const QColorSpace space(QPointF(wx, wy), QPointF(rx, ry), QPointF(gx, gy), QPointF(bx, by),
                                function, exponent);
        if (space.isValid())
            return space;
    }
    return QColorSpace(QColorSpace::Primaries::SRgb, function, exponent);
}

int sampleCenter(const int index, const int sourceExtent, const int sampledExtent)
{
    return int((2LL * index + 1) * sourceExtent / (2LL * sampledExtent));
}

// libpng errors jump back into this frame. The caller owns every C++ object
// whose lifetime must survive the jump.
bool decodePngRows(png_structp png, png_infop info, PngInput &input, const QSize &source,
                   uchar *row, uchar *output, qsizetype stride, const std::vector<int> &sampleX,
                   const std::vector<int> &sampleY, QColorSpace &colorSpace, int &orientation)
{
    if (setjmp(png_jmpbuf(png)))
        return false;

    png_set_read_fn(png, &input, readPngBytes);
    png_set_user_limits(png, MaxPngEdge, MaxPngEdge);
    png_set_chunk_malloc_max(png, MaxMetadataChunkBytes);
    png_set_chunk_cache_max(png, 32);
    png_set_crc_action(png, PNG_CRC_ERROR_QUIT, PNG_CRC_ERROR_QUIT);
    const png_byte skippedChunks[] = "tEXt\0zTXt\0iTXt\0sPLT";
    png_set_keep_unknown_chunks(png, PNG_HANDLE_CHUNK_NEVER, skippedChunks, 4);
    png_read_info(png, info);
    if (png_get_image_width(png, info) != png_uint_32(source.width())
        || png_get_image_height(png, info) != png_uint_32(source.height()))
        return false;

    const int type = png_get_color_type(png, info);
    const int depth = png_get_bit_depth(png, info);
    const bool transparent = png_get_valid(png, info, PNG_INFO_tRNS);
    if (depth == 16)
        png_set_strip_16(png);
    if (type == PNG_COLOR_TYPE_PALETTE)
        png_set_palette_to_rgb(png);
    if (type == PNG_COLOR_TYPE_GRAY && depth < 8)
        png_set_expand_gray_1_2_4_to_8(png);
    if (type == PNG_COLOR_TYPE_GRAY || type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png);
    if (transparent)
        png_set_tRNS_to_alpha(png);
    const bool alpha = transparent || (type & PNG_COLOR_MASK_ALPHA);
    const int channels = alpha ? 4 : 3;

    const bool adam7 = png_get_interlace_type(png, info) == PNG_INTERLACE_ADAM7;
    // Let libpng return compact Adam7 pass rows so previous passes never
    // require a full-resolution deinterlace buffer.
    png_read_update_info(png, info);
    if (png_get_rowbytes(png, info) != png_size_t(source.width()) * channels)
        return false;

    for (int pass = 0; pass < (adam7 ? PNG_INTERLACE_ADAM7_PASSES : 1); ++pass) {
        const int rows = adam7 ? PNG_PASS_ROWS(source.height(), pass) : source.height();
        if (adam7 && PNG_PASS_COLS(source.width(), pass) == 0)
            continue;
        const int firstX = adam7 ? PNG_PASS_START_COL(pass) : 0;
        const int stepX = adam7 ? PNG_PASS_COL_OFFSET(pass) : 1;
        auto targetY = sampleY.cbegin();
        for (int rowIndex = 0; rowIndex < rows; ++rowIndex) {
            if (QvAsync::isCanceled())
                return false;
            const int y = adam7 ? PNG_ROW_FROM_PASS_ROW(rowIndex, pass) : rowIndex;
            while (targetY != sampleY.cend() && *targetY < y)
                ++targetY;
            const bool keepRow = targetY != sampleY.cend() && *targetY == y;
            png_read_row(png, keepRow ? row : nullptr, nullptr);
            if (!keepRow)
                continue;
            uchar *pixel = output + (targetY - sampleY.cbegin()) * stride;
            for (const int x : sampleX) {
                if (x >= firstX && (x - firstX) % stepX == 0) {
                    std::memcpy(pixel, row + channels * ((x - firstX) / stepX), channels);
                    if (!alpha)
                        pixel[3] = 255;
                }
                pixel += 4;
            }
            ++targetY;
        }
    }
    png_read_end(png, info);
    orientation = pngOrientation(png, info);
    colorSpace = pngColorSpace(png, info);
    return colorSpace.isValid();
}
} // namespace

std::optional<QImage> readLargePngThumbnail(const QString &path, const int longestEdge,
                                            const qint64 nativeFileLimit,
                                            const qint64 nativePixelLimit)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    const auto header = file.peek(24);
    if (header.size() < 24
        || png_sig_cmp(reinterpret_cast<const uchar *>(header.constData()), 0, 8) != 0
        || qFromBigEndian<quint32>(header.constData() + 8) != 13 || header.mid(12, 4) != "IHDR")
        return std::nullopt;

    const auto width = qFromBigEndian<quint32>(header.constData() + 16);
    const auto height = qFromBigEndian<quint32>(header.constData() + 20);
    const qint64 fileBytes = file.size();
    const quint64 area = quint64(width) * height;
    if (fileBytes <= nativeFileLimit && area <= quint64(nativePixelLimit))
        return std::nullopt;

    if (!width || !height || width > MaxPngEdge || height > MaxPngEdge || area > MaxPngPixels
        || fileBytes > MaxPngFileBytes || longestEdge <= 0 || QvAsync::isCanceled())
        return QImage();

    const QSize source{ int(width), int(height) };
    const int edge = qMin(longestEdge, MaxPreviewEdge);
    const auto fit = [&source](const int limit) {
        if (qMax(source.width(), source.height()) <= limit)
            return source;
        return source.scaled(limit, limit, Qt::KeepAspectRatio).expandedTo(QSize(1, 1));
    };
    const QSize outputSize = fit(edge);
    QImage sampled(fit(edge * SamplingScale), QImage::Format_RGBA8888);
    if (sampled.isNull())
        return QImage();

    png_structp png = png_create_read_struct(
            PNG_LIBPNG_VER_STRING, nullptr,
            [](png_structp reader, png_const_charp) { png_longjmp(reader, 1); },
            [](png_structp, png_const_charp) { });
    if (!png)
        return QImage();
    png_infop info = png_create_info_struct(png);
    const auto cleanup = qScopeGuard([&] { png_destroy_read_struct(&png, &info, nullptr); });
    if (!info)
        return QImage();

    QByteArray row(source.width() * 4, Qt::Uninitialized);
    PngInput input{ file, fileBytes };
    QColorSpace colorSpace;
    int orientation = 1;
    std::vector<int> sampleX(sampled.width()), sampleY(sampled.height());
    for (int x = 0; x < sampled.width(); ++x)
        sampleX[x] = sampleCenter(x, source.width(), sampled.width());
    for (int y = 0; y < sampled.height(); ++y)
        sampleY[y] = sampleCenter(y, source.height(), sampled.height());
    if (!decodePngRows(png, info, input, source, reinterpret_cast<uchar *>(row.data()),
                       sampled.bits(), sampled.bytesPerLine(), sampleX, sampleY, colorSpace,
                       orientation))
        return QImage();

    sampled.setColorSpace(colorSpace);
    QImage result = sampled.convertToFormat(QImage::Format_RGBA8888_Premultiplied)
                            .scaled(outputSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    switch (orientation) {
    case 2:
        return result.transformed(QTransform::fromScale(-1, 1));
    case 3:
        return result.transformed(QTransform().rotate(180));
    case 4:
        return result.transformed(QTransform::fromScale(1, -1));
    case 5:
        return result.transformed(QTransform(0, 1, 1, 0, 0, 0));
    case 6:
        return result.transformed(QTransform().rotate(90));
    case 7:
        return result.transformed(QTransform(0, -1, -1, 0, 0, 0));
    case 8:
        return result.transformed(QTransform().rotate(270));
    default:
        return result;
    }
}

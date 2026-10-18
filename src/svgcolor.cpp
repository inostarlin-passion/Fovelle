#include "svgcolor.h"
#include <QBuffer>
#include <QColor>
#include <QColorTransform>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QRegularExpression>
#include <QSet>
#include <QCryptographicHash>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <cmath>
#include <zlib.h>

namespace QvColor {
namespace {
constexpr qsizetype MaximumSvgBytes = 64 * 1024 * 1024;
bool paintProperty(const QString &name)
{
    static const QSet<QString> names{ "fill",        "stroke",         "color",      "stop-color",
                                      "flood-color", "lighting-color", "solid-color" };
    return names.contains(name.toLower());
}
double number(QString value, bool *ok)
{
    const bool percent = value.endsWith('%');
    if (percent)
        value.chop(1);
    const double result = value.toDouble(ok) / (percent ? 100.0 : 1.0);
    *ok = *ok && std::isfinite(result);
    return result;
}
QColor parsePaint(const QString &value, QColorSpace *source)
{
    *source = QColorSpace::SRgb;
    static const QRegularExpression modern(
            "^color\\(\\s*(display-p3|srgb|srgb-linear)\\s+([^()]+)\\)$",
            QRegularExpression::CaseInsensitiveOption);
    const auto match = modern.match(value);
    if (match.hasMatch()) {
        QString components = match.captured(2);
        components.replace('/', " / ");
        const auto parts = components.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (parts.size() != 3 && !(parts.size() == 5 && parts[3] == "/"))
            return { };
        double rgba[4] = { 0, 0, 0, 1 };
        for (int i = 0; i < 4; ++i) {
            if (i == 3 && parts.size() == 3)
                break;
            bool ok = false;
            rgba[i] = number(parts[i == 3 ? 4 : i], &ok);
            if (!ok)
                return { };
            rgba[i] = qBound(0.0, rgba[i], 1.0);
        }
        const QString name = match.captured(1).toLower();
        *source = name == "display-p3"  ? QColorSpace::DisplayP3
                : name == "srgb-linear" ? QColorSpace::SRgbLinear
                                        : QColorSpace::SRgb;
        return QColor::fromRgbF(rgba[0], rgba[1], rgba[2], rgba[3]);
    }
    static const QRegularExpression rgb("^(rgb|rgba)\\(\\s*([^()]+)\\)$",
                                        QRegularExpression::CaseInsensitiveOption);
    const auto rgbMatch = rgb.match(value);
    if (rgbMatch.hasMatch()) {
        const auto parts = rgbMatch.captured(2).split(QRegularExpression("\\s*,\\s*"));
        if (parts.size() != 3 && parts.size() != 4)
            return { };
        double rgba[4] = { 0, 0, 0, 1 };
        for (int i = 0; i < parts.size(); ++i) {
            bool ok = false;
            rgba[i] = number(parts[i], &ok);
            if (!ok)
                return { };
            if (i < 3 && !parts[i].endsWith('%'))
                rgba[i] /= 255.0;
            rgba[i] = qBound(0.0, rgba[i], 1.0);
        }
        return QColor::fromRgbF(rgba[0], rgba[1], rgba[2], rgba[3]);
    }
    return QColor(value);
}
struct PaintServers
{
    QVector<QColor> colors;
    QString prefix;
};
QString normalizePaint(QString value, QString *error, PaintServers *servers,
                       const QString &property)
{
    QString important;
    if (value.trimmed().endsWith("!important", Qt::CaseInsensitive)) {
        value = value.left(value.lastIndexOf('!'));
        important = " !important";
    }
    value = value.trimmed();
    // References and inherited values keep their semantics; their declarations
    // are converted at their definition, rather than flattened at each use.
    if (value.startsWith("url(", Qt::CaseInsensitive)) {
        const int end = value.indexOf(')');
        if (end >= 0 && !value.mid(end + 1).trimmed().isEmpty())
            return value.left(end + 1) + ' '
                    + normalizePaint(value.mid(end + 1), error, servers, property) + important;
        return value + important;
    }
    QColorSpace source;
    QColor color = parsePaint(value, &source);
    if (!color.isValid()) {
        if (value.contains(QRegularExpression("\\bcolor\\s*\\(",
                                              QRegularExpression::CaseInsensitiveOption)))
            *error = QStringLiteral("Unsupported or invalid SVG color expression: %1")
                             .arg(value.left(160));
        return value + important;
    }
    color = source.transformationToColorSpace(QColorSpace::DisplayP3).map(color);
    // SVG Tiny solidColor carries intrinsic alpha independently of inherited
    // fill/stroke opacity. Qt supports it, whereas rgba() becomes black.
    if (color.alphaF() < 1.0 && (property == "fill" || property == "stroke")) {
        const QString id = servers->prefix + QString::number(servers->colors.size());
        servers->colors.append(color);
        return "url(#" + id + ')' + important;
    }
    if (color.alphaF() < 1.0) {
        *error = QStringLiteral("Alpha in SVG %1 requires an opacity property").arg(property);
        return { };
    }
    return color.name(QColor::HexRgb) + important;
}
QString normalizeStyle(const QString &css, QString *error, PaintServers *servers)
{
    static const QRegularExpression declarations("(^|[;{])([\\s\\w-]+):([^;{}]+)");
    QString result;
    qsizetype offset = 0;
    auto matches = declarations.globalMatch(css);
    while (matches.hasNext()) {
        const auto match = matches.next();
        result += css.mid(offset, match.capturedStart(3) - offset);
        result += paintProperty(match.captured(2).trimmed())
                ? normalizePaint(match.captured(3), error, servers,
                                 match.captured(2).trimmed().toLower())
                : match.captured(3);
        offset = match.capturedEnd(3);
    }
    return result + css.mid(offset);
}
QString normalizeImage(const QString &href, const QString &path, QString *error)
{
    if (href.startsWith('#'))
        return href;
    QImage image;
    if (href.startsWith("data:", Qt::CaseInsensitive)) {
        const int separator = href.indexOf(',');
        if (separator < 0 || !href.left(separator).endsWith(";base64", Qt::CaseInsensitive)) {
            *error = QStringLiteral(
                    "Wide-color SVG images require a base64 image data URL or a local image");
            return { };
        }
        image.loadFromData(QByteArray::fromBase64(href.mid(separator + 1).toLatin1()));
    } else if (!href.contains("://")) {
        image.load(QFileInfo(path).dir().absoluteFilePath(href));
    }
    if (image.isNull()) {
        *error = QStringLiteral("An image in the wide-color SVG could not be color converted");
        return { };
    }
    if (!image.colorSpace().isValid())
        image.setColorSpace(QColorSpace::SRgb);
    image = image.convertedToColorSpace(QColorSpace::DisplayP3);
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "PNG")) {
        *error = QStringLiteral("An SVG image could not be normalized");
        return { };
    }
    return "data:image/png;base64," + QString::fromLatin1(png.toBase64());
}
} // namespace
QByteArray prepareSvg(const QByteArray &encoded, const QString &sourcePath, QColorSpace *space,
                      QString *error)
{
    error->clear();
    *space = QColorSpace::SRgb;
    QByteArray xml = encoded;
    if (encoded.startsWith(QByteArray::fromHex("1f8b"))) {
        z_stream stream{ };
        if (inflateInit2(&stream, 15 + 16) != Z_OK) {
            *error = "SVG decompressor initialization failed";
            return { };
        }
        stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(encoded.constData()));
        stream.avail_in = encoded.size();
        xml.clear();
        int status;
        do {
            char block[32768];
            stream.next_out = reinterpret_cast<Bytef *>(block);
            stream.avail_out = sizeof(block);
            status = inflate(&stream, Z_NO_FLUSH);
            xml.append(block, sizeof(block) - stream.avail_out);
        } while (status == Z_OK && xml.size() <= MaximumSvgBytes);
        inflateEnd(&stream);
        if (status != Z_STREAM_END || xml.size() > MaximumSvgBytes) {
            *error = "Compressed SVG is invalid or exceeds the decoded size limit";
            return { };
        }
    }
    if (xml.size() > MaximumSvgBytes) {
        *error = "SVG exceeds the decoded size limit";
        return { };
    }
    const QString text = QString::fromUtf8(xml);
    const QRegularExpression modernColor("\\bcolor\\s*\\(",
                                         QRegularExpression::CaseInsensitiveOption);
    bool wide = text.contains(modernColor);
    bool images = text.contains("<image");
    // XML permits UTF-16. Byte-oriented UTF-8 scanning cannot see its CSS
    // tokens; let the XML reader detect the encoding before choosing a path.
    if (xml.contains('\0')) {
        QXmlStreamReader scan(xml);
        while (!scan.atEnd()) {
            scan.readNext();
            if (scan.isStartElement()) {
                images = images || scan.name() == QLatin1String("image");
                for (const auto &attribute : scan.attributes())
                    wide = wide || attribute.value().toString().contains(modernColor);
            } else if (scan.isCharacters())
                wide = wide || scan.text().toString().contains(modernColor);
        }
        if (scan.hasError()) {
            *error = scan.errorString();
            return { };
        }
    }
    if (!wide && !images)
        return encoded.startsWith(QByteArray::fromHex("1f8b")) ? xml : encoded;
    if (wide)
        *space = QColorSpace::DisplayP3;
    QXmlStreamReader reader(xml);
    QByteArray output;
    QXmlStreamWriter writer(&output);
    PaintServers servers;
    servers.prefix =
            "_fovelle_color_"
            + QString::fromLatin1(
                    QCryptographicHash::hash(xml, QCryptographicHash::Sha256).toHex().left(24))
            + '_';
    bool inStyle = false;
    int depth = 0;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            ++depth;
            inStyle = reader.name() == QLatin1String("style");
            writer.writeStartElement(reader.qualifiedName().toString());
            for (const auto &ns : reader.namespaceDeclarations()) {
                if (ns.prefix().isEmpty())
                    writer.writeDefaultNamespace(ns.namespaceUri().toString());
                else
                    writer.writeNamespace(ns.namespaceUri().toString(), ns.prefix().toString());
            }
            const QString animationProperty = reader.attributes().value("attributeName").toString();
            for (const auto &attribute : reader.attributes()) {
                const QString name = attribute.name().toString();
                QString value = attribute.value().toString();
                if (wide && paintProperty(name))
                    value = normalizePaint(value, error, &servers, name);
                else if (wide && name == "style")
                    value = normalizeStyle(value, error, &servers);
                else if (reader.name() == QLatin1String("image") && name == "href") {
                    if (wide)
                        value = normalizeImage(value, sourcePath, error);
                    else if (!value.startsWith("data:") && !value.contains("://")
                             && !value.startsWith('#'))
                        value = QFileInfo(sourcePath).dir().absoluteFilePath(value);
                } else if (wide && paintProperty(animationProperty)
                           && (name == "from" || name == "to" || name == "values")) {
                    QStringList values = value.split(';');
                    for (QString &v : values)
                        v = normalizePaint(v, error, &servers, animationProperty);
                    value = values.join(';');
                }
                writer.writeAttribute(attribute.qualifiedName().toString(), value);
            }
        } else if (wide && reader.isCharacters() && inStyle)
            writer.writeCharacters(normalizeStyle(reader.text().toString(), error, &servers));
        else {
            if (reader.isEndElement()) {
                if (depth == 1 && !servers.colors.isEmpty()) {
                    writer.writeStartElement("defs");
                    for (int i = 0; i < servers.colors.size(); ++i) {
                        const QColor color = servers.colors[i];
                        writer.writeStartElement("solidColor");
                        writer.writeAttribute("id", servers.prefix + QString::number(i));
                        writer.writeAttribute("solid-color", color.name(QColor::HexRgb));
                        writer.writeAttribute("solid-opacity",
                                              QString::number(color.alphaF(), 'g', 9));
                        writer.writeEndElement();
                    }
                    writer.writeEndElement();
                }
                --depth;
                inStyle = false;
            }
            writer.writeCurrentToken(reader);
        }
        if (!error->isEmpty())
            return { };
        if (output.size() > MaximumSvgBytes) {
            *error = "Normalized SVG exceeds the size limit";
            return { };
        }
    }
    if (reader.hasError()) {
        *error = reader.errorString();
        return { };
    }
    return output;
}
} // namespace QvColor

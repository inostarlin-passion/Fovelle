#include "actionmanager.h"
#include "qvapplication.h"
#include "qvcocoafunctions.h"

#include <QUrl>
#include <QDebug>
#include <QFile>
#include <QFileIconProvider>
#include <QCollator>
#include <QColorSpace>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QWidget>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <limits>
#include <mutex>

#import <Cocoa/Cocoa.h>
#import <ColorSync/ColorSync.h>
#import <CoreImage/CoreImage.h>
#import <CoreGraphics/CoreGraphics.h>
#import <CoreServices/CoreServices.h>
#import <ImageIO/ImageIO.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

@interface QVHDRPresentationState : NSObject
{
@public
    NSUInteger generation;
    BOOL firstFrameSubmitted;
    BOOL firstFramePresented;
    BOOL hdrPreparationInFlight;
    BOOL hdrPrepared;
    CAMetalLayer *metalLayer;
    CAMetalDisplayLink *displayLink;
}

- (instancetype)initWithMetalLayer:(CAMetalLayer *)layer;
- (void)resetForImage;
- (void)setMetalDisplayLink:(CAMetalDisplayLink *)link;
- (void)invalidate;
@end

@implementation QVHDRPresentationState

- (instancetype)initWithMetalLayer:(CAMetalLayer *)layer
{
    self = [super init];
    if (self)
        metalLayer = [layer retain];
    return self;
}

- (void)resetForImage
{
    ++generation;
    firstFrameSubmitted = NO;
    firstFramePresented = NO;
    hdrPreparationInFlight = NO;
    hdrPrepared = NO;
}

- (void)setMetalDisplayLink:(CAMetalDisplayLink *)link
{
    if (displayLink == link)
        return;
    [displayLink release];
    displayLink = [link retain];
}

- (void)invalidate
{
    [self resetForImage];
    [self setMetalDisplayLink:nil];
    [metalLayer release];
    metalLayer = nil;
}

- (void)dealloc
{
    [metalLayer release];
    [displayLink release];
    [super dealloc];
}

@end

@interface QVHDRDisplayLinkDelegate : NSObject<CAMetalDisplayLinkDelegate>
{
    void (^updateHandler)(CAMetalDisplayLinkUpdate *update);
}

- (instancetype)initWithUpdateHandler:(void (^)(CAMetalDisplayLinkUpdate *update))handler;
- (void)invalidate;
@end

@implementation QVHDRDisplayLinkDelegate

- (instancetype)initWithUpdateHandler:(void (^)(CAMetalDisplayLinkUpdate *update))handler
{
    self = [super init];
    if (self)
        updateHandler = [handler copy];
    return self;
}

- (void)metalDisplayLink:(CAMetalDisplayLink *)link
             needsUpdate:(CAMetalDisplayLinkUpdate *)update
{
    Q_UNUSED(link);
    if (updateHandler)
        updateHandler(update);
}

- (void)invalidate
{
    [updateHandler release];
    updateHandler = nil;
}

- (void)dealloc
{
    [self invalidate];
    [super dealloc];
}

@end

namespace
{
QString QStringFromCFString(CFStringRef value)
{
    if (!value)
        return {};

    const CFIndex length = CFStringGetLength(value);
    const CFIndex maximumSize = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
    QByteArray utf8(static_cast<qsizetype>(maximumSize), Qt::Uninitialized);
    if (!CFStringGetCString(value, utf8.data(), maximumSize, kCFStringEncodingUTF8))
        return {};
    return QString::fromUtf8(utf8.constData());
}

QByteArray normalizedExtension(const QByteArray &extension)
{
    QByteArray normalized = extension.toLower();
    while (normalized.startsWith('.'))
        normalized.remove(0, 1);
    return normalized;
}

bool isRawImageType(CFStringRef typeIdentifier)
{
    if (!typeIdentifier)
        return false;
    UTType *type = [UTType typeWithIdentifier:(NSString *)typeIdentifier];
    return type && [type conformsToType:UTTypeRAWImage];
}

bool isImageType(CFStringRef typeIdentifier)
{
    if (!typeIdentifier)
        return false;
    UTType *type = [UTType typeWithIdentifier:(NSString *)typeIdentifier];
    return type && [type conformsToType:UTTypeImage];
}

QList<QByteArray> typeTags(CFStringRef typeIdentifier, NSString *tagClass)
{
    QList<QByteArray> tags;
    if (!typeIdentifier)
        return tags;

    UTType *type = [UTType typeWithIdentifier:(NSString *)typeIdentifier];
    NSArray<NSString *> *allTags = type.tags[tagClass];
    if (!type || !allTags)
        return tags;

    for (NSString *tag in allTags) {
        const QByteArray normalized = normalizedExtension(QByteArray(tag.UTF8String));
        if (!normalized.isEmpty() && !tags.contains(normalized))
            tags.append(normalized);
    }
    return tags;
}

QList<CFStringRef> imageIOTypeIdentifiers()
{
    QList<CFStringRef> typeIdentifiers;
    CFArrayRef identifiers = CGImageSourceCopyTypeIdentifiers();
    if (!identifiers)
        return typeIdentifiers;

    const CFIndex count = CFArrayGetCount(identifiers);
    for (CFIndex index = 0; index < count; ++index)
    {
        const auto identifier = static_cast<CFStringRef>(CFArrayGetValueAtIndex(identifiers, index));
        if (isImageType(identifier))
            typeIdentifiers.append(static_cast<CFStringRef>(CFRetain(identifier)));
    }

    CFRelease(identifiers);
    return typeIdentifiers;
}

CGColorSpaceRef colorSyncSrgbColorSpace()
{
    ColorSyncProfileRef profile = ColorSyncProfileCreateWithName(kColorSyncSRGBProfile);
    if (profile)
    {
        CGColorSpaceRef colorSpace = CGColorSpaceCreateWithColorSyncProfile(profile, nullptr);
        CFRelease(profile);
        if (colorSpace)
            return colorSpace;
    }

    return CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
}

CGColorSpaceRef colorSyncDisplayP3ColorSpace(const bool extendedLinear)
{
    CGColorSpaceRef displayP3 = nullptr;
    ColorSyncProfileRef profile = ColorSyncProfileCreateWithName(kColorSyncDisplayP3Profile);
    if (profile) {
        displayP3 = CGColorSpaceCreateWithColorSyncProfile(profile, nullptr);
        CFRelease(profile);
    }

    if (!displayP3)
        displayP3 = CGColorSpaceCreateWithName(kCGColorSpaceDisplayP3);

    if (!displayP3 || !extendedLinear)
        return displayP3;

    CGColorSpaceRef extended = CGColorSpaceCreateExtendedLinearized(displayP3);
    CGColorSpaceRelease(displayP3);
    return extended ? extended : CGColorSpaceCreateWithName(kCGColorSpaceExtendedLinearDisplayP3);
}

CIContext *metalCIContext(CGColorSpaceRef workingColorSpace, CGColorSpaceRef outputColorSpace)
{
    if (!workingColorSpace || !outputColorSpace)
        return nil;

    NSDictionary *options = @{
        (id)kCIContextUseSoftwareRenderer : @NO,
        (id)kCIContextWorkingColorSpace : (id)workingColorSpace,
        (id)kCIContextOutputColorSpace : (id)outputColorSpace,
        (id)kCIContextWorkingFormat : @(kCIFormatRGBAh)
    };
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    CIContext *context = device ? [CIContext contextWithMTLDevice:device options:options]
                                : [CIContext contextWithOptions:options];
    [device release];
    return context;
}

QColorSpace qColorSpaceFromCGColorSpace(CGColorSpaceRef colorSpace)
{
    if (!colorSpace)
        return {};

    CFDataRef iccData = CGColorSpaceCopyICCData(colorSpace);
    if (!iccData)
        return {};

    const QByteArray data(
        reinterpret_cast<const char *>(CFDataGetBytePtr(iccData)),
        static_cast<qsizetype>(CFDataGetLength(iccData)));
    const QColorSpace result = QColorSpace::fromIccProfile(data);
    CFRelease(iccData);
    return result;
}

QSize sourcePixelSize(CGImageSourceRef source)
{
    int width = 0;
    int height = 0;
    CFDictionaryRef properties = CGImageSourceCopyPropertiesAtIndex(source, 0, nullptr);
    if (properties)
    {
        if (const auto widthNumber = static_cast<CFNumberRef>(CFDictionaryGetValue(properties, kCGImagePropertyPixelWidth)))
            CFNumberGetValue(widthNumber, kCFNumberIntType, &width);
        if (const auto heightNumber = static_cast<CFNumberRef>(CFDictionaryGetValue(properties, kCGImagePropertyPixelHeight)))
            CFNumberGetValue(heightNumber, kCFNumberIntType, &height);
        CFRelease(properties);
    }

    return QSize(width, height);
}

QSize orientedPixelSize(const QSize &size, const CGImagePropertyOrientation orientation)
{
    if (orientation >= kCGImagePropertyOrientationLeftMirrored
        && orientation <= kCGImagePropertyOrientationLeft) {
        return size.transposed();
    }
    return size;
}

int sourceMaxPixelSize(CGImageSourceRef source)
{
    const QSize size = sourcePixelSize(source);
    const int maxDimension = std::max(size.width(), size.height());
    return maxDimension > 0 ? maxDimension : std::numeric_limits<int>::max();
}

CGImagePropertyOrientation sourceOrientation(CGImageSourceRef source)
{
    CGImagePropertyOrientation orientation = kCGImagePropertyOrientationUp;
    CFDictionaryRef properties = CGImageSourceCopyPropertiesAtIndex(source, 0, nullptr);
    if (!properties)
        return orientation;

    const CFTypeRef rawOrientation = CFDictionaryGetValue(properties, kCGImagePropertyOrientation);
    if (rawOrientation && CFGetTypeID(rawOrientation) == CFNumberGetTypeID())
    {
        int value = static_cast<int>(orientation);
        if (CFNumberGetValue(static_cast<CFNumberRef>(rawOrientation), kCFNumberIntType, &value) && value >= 1 && value <= 8)
            orientation = static_cast<CGImagePropertyOrientation>(value);
    }

    CFRelease(properties);
    return orientation;
}

bool numberFromDictionary(CFDictionaryRef dictionary, CFStringRef key, double &value)
{
    if (!dictionary)
        return false;

    const CFTypeRef rawValue = CFDictionaryGetValue(dictionary, key);
    if (!rawValue || CFGetTypeID(rawValue) != CFNumberGetTypeID())
        return false;

    return CFNumberGetValue(static_cast<CFNumberRef>(rawValue), kCFNumberDoubleType, &value);
}

CFDictionaryRef pngPropertiesForFrame(CGImageSourceRef source, const size_t frameNumber)
{
    CFDictionaryRef properties = CGImageSourceCopyPropertiesAtIndex(source, frameNumber, nullptr);
    if (!properties)
        return nullptr;

    const CFTypeRef pngProperties = CFDictionaryGetValue(properties, kCGImagePropertyPNGDictionary);
    if (!pngProperties || CFGetTypeID(pngProperties) != CFDictionaryGetTypeID())
    {
        CFRelease(properties);
        return nullptr;
    }

    CFRetain(pngProperties);
    CFRelease(properties);
    return static_cast<CFDictionaryRef>(pngProperties);
}

QImage imageFromCGImage(CGImageRef cgImage)
{
    if (!cgImage)
        return {};

    const size_t width = CGImageGetWidth(cgImage);
    const size_t height = CGImageGetHeight(cgImage);
    QImage image(static_cast<int>(width), static_cast<int>(height), QImage::Format_RGBA8888_Premultiplied);
    if (image.isNull())
        return {};

    CGColorSpaceRef colorSpace = CGImageGetColorSpace(cgImage);
    const bool canPreserveSourceSpace = colorSpace
            && CGColorSpaceGetModel(colorSpace) == kCGColorSpaceModelRGB
            && !CGColorSpaceUsesExtendedRange(colorSpace) && !CGColorSpaceIsHDR(colorSpace);
    colorSpace =
            canPreserveSourceSpace ? CGColorSpaceRetain(colorSpace) : colorSyncSrgbColorSpace();
    CGContextRef context = CGBitmapContextCreate(
        image.bits(),
        width,
        height,
        8,
        image.bytesPerLine(),
        colorSpace,
        kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    if (!context)
    {
        CGColorSpaceRelease(colorSpace);
        return {};
    }

    CGContextDrawImage(context, CGRectMake(0, 0, static_cast<CGFloat>(width), static_cast<CGFloat>(height)), cgImage);
    CGContextRelease(context);
    const QColorSpace qColorSpace = qColorSpaceFromCGColorSpace(colorSpace);
    CGColorSpaceRelease(colorSpace);
    if (qColorSpace.isValid())
        image.setColorSpace(qColorSpace);
    return image;
}

QImage imageFromCIImage(CIImage *image, CIContext *context, CGColorSpaceRef outputColorSpace, int largestDimension)
{
    if (!image || !context || !outputColorSpace)
        return {};

    CGRect extent = image.extent;
    if (CGRectIsEmpty(extent) ||
        !std::isfinite(extent.origin.x) || !std::isfinite(extent.origin.y) ||
        !std::isfinite(extent.size.width) || !std::isfinite(extent.size.height))
        return {};

    const CGFloat maxDimension = std::max(CGRectGetWidth(extent), CGRectGetHeight(extent));
    if (largestDimension > 0 && maxDimension > largestDimension)
    {
        const CGFloat scale = static_cast<CGFloat>(largestDimension) / maxDimension;
        image = [image imageByApplyingTransform:CGAffineTransformMakeScale(scale, scale)];
        extent = image.extent;
    }

    CGImageRef cgImage = [context createCGImage:image fromRect:extent format:kCIFormatRGBA8 colorSpace:outputColorSpace];
    if (!cgImage)
        return {};

    QImage result = imageFromCGImage(cgImage);
    CGImageRelease(cgImage);
    return result;
}

CFDictionaryRef thumbnailOptions(CGImageSourceRef source, const int largestDimension,
                                 const bool decodeToHDR)
{
    const int sourceDimension = sourceMaxPixelSize(source);
    const int maxDimension =
            largestDimension > 0 ? std::min(sourceDimension, largestDimension) : sourceDimension;
    CFNumberRef maxPixelSizeNumber = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &maxDimension);
    if (!maxPixelSizeNumber)
        return nullptr;

    CFMutableDictionaryRef options =
            CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
                                      &kCFTypeDictionaryValueCallBacks);
    if (options) {
        CFDictionarySetValue(options, kCGImageSourceCreateThumbnailFromImageAlways, kCFBooleanTrue);
        CFDictionarySetValue(options, kCGImageSourceCreateThumbnailWithTransform, kCFBooleanTrue);
        CFDictionarySetValue(options, kCGImageSourceThumbnailMaxPixelSize, maxPixelSizeNumber);
        if (@available(macOS 14.0, *)) {
            CFDictionarySetValue(options, kCGImageSourceDecodeRequest,
                                 decodeToHDR ? kCGImageSourceDecodeToHDR
                                             : kCGImageSourceDecodeToSDR);
        }
    }
    CFRelease(maxPixelSizeNumber);
    return options;
}

CFDictionaryRef fullResolutionThumbnailOptions(CGImageSourceRef source,
                                               const bool decodeToHDR = false)
{
    // Non-HDR images keep source resolution for later zooming. HDR images keep
    // their full-resolution CIImage graph and only bound the separate SDR
    // fallback proxy.
    return thumbnailOptions(source, 0, decodeToHDR);
}

bool hasAuxiliaryImage(CGImageSourceRef source, CFStringRef auxiliaryType)
{
    CFDictionaryRef auxiliary = CGImageSourceCopyAuxiliaryDataInfoAtIndex(source, 0, auxiliaryType);
    if (!auxiliary)
        return false;
    CFRelease(auxiliary);
    return true;
}

float cgImageContentHeadroom(CGImageRef image)
{
    if (@available(macOS 15.0, *))
        return CGImageGetContentHeadroom(image);
    return 0.0F;
}

float ciImageContentHeadroom(CIImage *image)
{
    if (@available(macOS 15.0, *))
        return image ? image.contentHeadroom : 0.0F;
    return 0.0F;
}

bool maximumCIImageRGBComponent(CIImage *source, CIContext *context,
                                CGColorSpaceRef colorSpace, float &maximum)
{
    if (!source || !context || !colorSpace || CGRectIsEmpty(source.extent))
        return false;

    CIFilter *areaMaximum = [CIFilter filterWithName:@"CIAreaMaximum"];
    [areaMaximum setValue:source forKey:kCIInputImageKey];
    [areaMaximum setValue:[CIVector vectorWithCGRect:source.extent]
                   forKey:kCIInputExtentKey];
    CIImage *reduced = areaMaximum.outputImage;
    if (!reduced)
        return false;

    float pixel[4]{ 0.0F, 0.0F, 0.0F, 0.0F };
    [context render:reduced
            toBitmap:pixel
            rowBytes:sizeof(pixel)
              bounds:CGRectMake(0, 0, 1, 1)
              format:kCIFormatRGBAf
          colorSpace:colorSpace];
    maximum = std::max({ pixel[0], pixel[1], pixel[2] });
    return std::isfinite(maximum);
}

QString colorSpaceName(CGColorSpaceRef colorSpace)
{
    if (!colorSpace)
        return QStringLiteral("unspecified");
    return QStringFromCFString(CGColorSpaceGetName(colorSpace));
}

QString transferFunctionName(CGColorSpaceRef colorSpace, const bool hasGainMap)
{
    if (colorSpace) {
        if (CGColorSpaceIsPQBased(colorSpace))
            return QStringLiteral("PQ");
        if (CGColorSpaceIsHLGBased(colorSpace))
            return QStringLiteral("HLG");
        if (CGColorSpaceUsesExtendedRange(colorSpace))
            return QStringLiteral("extended-linear");
    }
    return hasGainMap ? QStringLiteral("gain-map") : QStringLiteral("ICC/SDR");
}

// macOS 14 removed every system PostScript/EPS conversion path. An embedded
// EPS TIFF/EPSI image is only a low-resolution placement preview and is not the
// document artwork; treating it as the image both loses vector detail and can
// expose decoder-specific TIFF errors. Convert the PostScript program with a
// bounded Ghostscript child process, then rasterize the cropped PDF through
// Core Graphics at the screen-sized resolution requested by the loader.
constexpr quint32 DosEPSMagic = 0xC6D3D0C5;
constexpr int EPSRendererStartTimeoutMs = 5000;
constexpr int EPSRendererTimeoutMs = 30000;
constexpr qsizetype MaxEPSRendererDiagnosticBytes = 64 * 1024;
constexpr quint64 MaxEPSRenderedPixels = 64ULL * 1024ULL * 1024ULL;
constexpr quint64 MaxEPSIntermediatePDFBytes = 256ULL * 1024ULL * 1024ULL;

struct EPSReadResult
{
    bool recognized {false};
    QImage image;
    QSize intrinsicSize;
    QString errorString;
};

bool readLittleEndianUInt32(const QByteArray &data, const int offset, quint32 &value)
{
    if (offset < 0 || data.size() < offset + 4)
        return false;

    value = static_cast<quint32>(static_cast<unsigned char>(data.at(offset)))
            | (static_cast<quint32>(static_cast<unsigned char>(data.at(offset + 1))) << 8)
            | (static_cast<quint32>(static_cast<unsigned char>(data.at(offset + 2))) << 16)
            | (static_cast<quint32>(static_cast<unsigned char>(data.at(offset + 3))) << 24);
    return true;
}

bool isEPSFilename(const QString &filePath)
{
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    return suffix == QStringLiteral("eps")
           || suffix == QStringLiteral("epsf")
           || suffix == QStringLiteral("epsi");
}

bool isDosEPSHeader(const QByteArray &data)
{
    quint32 magic = 0;
    return readLittleEndianUInt32(data, 0, magic) && magic == DosEPSMagic;
}

bool isAsciiEPSHeader(const QByteArray &data)
{
    if (!data.startsWith("%!PS-Adobe"))
        return false;

    const qsizetype headerEnd = data.indexOf('\n');
    const QByteArray header = headerEnd >= 0 ? data.left(headerEnd) : data;
    return header.contains("EPSF");
}

QString ghostscriptExecutable()
{
    const QString configured = QString::fromLocal8Bit(qgetenv("FOVELLE_GHOSTSCRIPT"));
    if (!configured.isEmpty())
        return QFileInfo(configured).isExecutable() ? configured : QString();

    const QString fromPath = QStandardPaths::findExecutable(QStringLiteral("gs"));
    if (!fromPath.isEmpty())
        return fromPath;

    static const QStringList standardPaths {
        QStringLiteral("/opt/homebrew/bin/gs"),
        QStringLiteral("/usr/local/bin/gs"),
        QStringLiteral("/opt/local/bin/gs"),
        QStringLiteral("/opt/sw/bin/gs"),
        QStringLiteral("/Library/TeX/texbin/gs")
    };
    for (const QString &path : standardPaths)
    {
        if (QFileInfo(path).isExecutable())
            return path;
    }
    return {};
}

QString boundedProcessError(QProcess &process)
{
    QByteArray diagnostic = process.readAllStandardError();
    if (diagnostic.size() > MaxEPSRendererDiagnosticBytes)
        diagnostic.truncate(MaxEPSRendererDiagnosticBytes);
    return QString::fromLocal8Bit(diagnostic).simplified();
}

bool convertEPSToPDF(const QString &filePath, const QString &pdfPath,
                     QString &errorString)
{
    const QString executable = ghostscriptExecutable();
    if (executable.isEmpty())
    {
        errorString = QStringLiteral(
            "EPS rendering requires Ghostscript (gs); install Ghostscript or set FOVELLE_GHOSTSCRIPT");
        return false;
    }

    QProcess process;
    process.setProgram(executable);
    process.setArguments({
        QStringLiteral("-q"),
        QStringLiteral("-dSAFER"),
        QStringLiteral("-dBATCH"),
        QStringLiteral("-dNOPAUSE"),
        QStringLiteral("-dEPSCrop"),
        QStringLiteral("-dAutoRotatePages=/None"),
        QStringLiteral("-dALLOWPSTRANSPARENCY"),
        QStringLiteral("-sDEVICE=pdfwrite"),
        QStringLiteral("-dCompatibilityLevel=1.7"),
        QStringLiteral("-sOutputFile=") + pdfPath,
        QStringLiteral("-f"),
        filePath
    });
    process.setWorkingDirectory(QFileInfo(pdfPath).absolutePath());
    process.setStandardOutputFile(QProcess::nullDevice());
    process.start();
    if (!process.waitForStarted(EPSRendererStartTimeoutMs))
    {
        errorString = QStringLiteral("Ghostscript could not be started: %1")
                          .arg(process.errorString());
        return false;
    }
    if (!process.waitForFinished(EPSRendererTimeoutMs))
    {
        process.kill();
        process.waitForFinished();
        errorString = QStringLiteral("Ghostscript exceeded the 30 second EPS rendering limit");
        return false;
    }

    const QString diagnostic = boundedProcessError(process);
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
    {
        errorString = diagnostic.isEmpty()
            ? QStringLiteral("Ghostscript could not interpret the EPS document")
            : QStringLiteral("Ghostscript could not interpret the EPS document: %1").arg(diagnostic);
        return false;
    }

    const QFileInfo pdfInfo(pdfPath);
    if (!pdfInfo.isFile() || pdfInfo.size() <= 0)
    {
        errorString = QStringLiteral("Ghostscript did not produce an EPS rendering");
        return false;
    }
    if (static_cast<quint64>(pdfInfo.size()) > MaxEPSIntermediatePDFBytes)
    {
        errorString = QStringLiteral("Ghostscript EPS rendering exceeded the safety limit");
        return false;
    }
    return true;
}

QImage imageFromPDFPage(const QString &pdfPath, const int requestedLargestDimension,
                        QSize &intrinsicSize, QString &errorString)
{
    const QUrl pdfUrl = QUrl::fromLocalFile(pdfPath);
    CGPDFDocumentRef document = CGPDFDocumentCreateWithURL((CFURLRef)pdfUrl.toNSURL());
    if (!document)
    {
        errorString = QStringLiteral("The Ghostscript EPS rendering is not a valid PDF");
        return {};
    }

    CGPDFPageRef page = CGPDFDocumentGetPage(document, 1);
    if (!page)
    {
        CGPDFDocumentRelease(document);
        errorString = QStringLiteral("The Ghostscript EPS rendering has no page");
        return {};
    }

    CGPDFBox drawingBox = kCGPDFCropBox;
    CGRect pageBox = CGPDFPageGetBoxRect(page, drawingBox);
    if (CGRectIsEmpty(pageBox))
    {
        drawingBox = kCGPDFMediaBox;
        pageBox = CGPDFPageGetBoxRect(page, drawingBox);
    }
    const int rotation = ((CGPDFPageGetRotationAngle(page) % 360) + 360) % 360;
    double logicalWidth = CGRectGetWidth(pageBox);
    double logicalHeight = CGRectGetHeight(pageBox);
    if (rotation == 90 || rotation == 270)
        std::swap(logicalWidth, logicalHeight);
    constexpr double MaximumLogicalDimension =
        static_cast<double>(std::numeric_limits<int>::max());
    if (!(logicalWidth > 0.0) || !(logicalHeight > 0.0)
        || !std::isfinite(logicalWidth) || !std::isfinite(logicalHeight)
        || logicalWidth > MaximumLogicalDimension
        || logicalHeight > MaximumLogicalDimension)
    {
        CGPDFDocumentRelease(document);
        errorString = QStringLiteral("The Ghostscript EPS rendering has an invalid page box");
        return {};
    }

    intrinsicSize = QSize(qMax(1, qCeil(logicalWidth)), qMax(1, qCeil(logicalHeight)));
    const double sourceLargestDimension = std::max(logicalWidth, logicalHeight);
    const int targetLargestDimension = requestedLargestDimension > 0
        ? requestedLargestDimension
        : qMax(intrinsicSize.width(), intrinsicSize.height());
    const double scale = static_cast<double>(targetLargestDimension) / sourceLargestDimension;
    const int targetWidth = qMax(1, qRound(logicalWidth * scale));
    const int targetHeight = qMax(1, qRound(logicalHeight * scale));
    if (static_cast<quint64>(targetWidth) * static_cast<quint64>(targetHeight)
        > MaxEPSRenderedPixels)
    {
        CGPDFDocumentRelease(document);
        errorString = QStringLiteral("EPS rendering dimensions exceed the safety limit");
        return {};
    }

    QImage image(targetWidth, targetHeight, QImage::Format_RGBA8888_Premultiplied);
    if (image.isNull())
    {
        CGPDFDocumentRelease(document);
        errorString = QStringLiteral("EPS rendering pixels could not be allocated");
        return {};
    }
    image.fill(Qt::transparent);

    CGColorSpaceRef colorSpace = colorSyncSrgbColorSpace();
    CGContextRef context = CGBitmapContextCreate(
        image.bits(),
        static_cast<size_t>(targetWidth),
        static_cast<size_t>(targetHeight),
        8,
        static_cast<size_t>(image.bytesPerLine()),
        colorSpace,
        kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    CGColorSpaceRelease(colorSpace);
    if (!context)
    {
        CGPDFDocumentRelease(document);
        errorString = QStringLiteral("EPS rendering context could not be created");
        return {};
    }

    // CGPDFPageGetDrawingTransform fits oversized pages but deliberately does
    // not enlarge a small page into a larger destination. Build the pixel
    // scale explicitly, while retaining its box-origin/rotation transform in
    // logical PDF-point coordinates.
    const CGRect logicalDestination = CGRectMake(0, 0, logicalWidth, logicalHeight);
    CGContextSetInterpolationQuality(context, kCGInterpolationHigh);
    CGContextScaleCTM(context,
                      static_cast<CGFloat>(targetWidth) / logicalWidth,
                      static_cast<CGFloat>(targetHeight) / logicalHeight);
    CGContextConcatCTM(context, CGPDFPageGetDrawingTransform(
        page, drawingBox, logicalDestination, 0, true));
    CGContextDrawPDFPage(context, page);
    CGContextRelease(context);
    CGPDFDocumentRelease(document);
    image.setColorSpace(QColorSpace::SRgb);
    return image;
}

EPSReadResult readEPS(const QString &filePath, const int requestedLargestDimension)
{
    EPSReadResult result;
    const bool extensionSuggestsEPS = isEPSFilename(filePath);
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (extensionSuggestsEPS)
        {
            result.recognized = true;
            result.errorString = QStringLiteral("EPS file could not be opened");
        }
        return result;
    }

    const QByteArray prefix = file.read(64);
    if (!extensionSuggestsEPS && !isDosEPSHeader(prefix) && !isAsciiEPSHeader(prefix))
        return result;
    result.recognized = true;
    file.close();

    QTemporaryDir temporaryDirectory(
        QDir::tempPath() + QStringLiteral("/Fovelle-eps-render-XXXXXX"));
    if (!temporaryDirectory.isValid())
    {
        result.errorString = QStringLiteral("A temporary EPS rendering directory could not be created");
        return result;
    }

    const QString pdfPath = temporaryDirectory.filePath(QStringLiteral("rendered.pdf"));
    if (!convertEPSToPDF(filePath, pdfPath, result.errorString))
        return result;
    result.image = imageFromPDFPage(pdfPath, requestedLargestDimension,
                                    result.intrinsicSize, result.errorString);
    return result;
}

class NativeHDRImage final : public QVCocoaFunctions::HDRImage
{
public:
    NativeHDRImage(CIImage *hdrImage, CIImage *sdrImage,
                   QVCocoaFunctions::HDRMetadata metadata,
                   CIImage *auxiliaryGainMap = nil)
        : hdr([hdrImage retain]), sdr([sdrImage retain]),
          gainMap([auxiliaryGainMap retain]), imageMetadata(std::move(metadata))
    {
    }

    ~NativeHDRImage() override
    {
        [hdr release];
        [sdr release];
        [gainMap release];
    }

    const QVCocoaFunctions::HDRMetadata &metadata() const override { return imageMetadata; }
    CIImage *hdrCIImage() const { return hdr; }
    CIImage *sdrCIImage() const { return sdr; }
    CIImage *gainMapCIImage() const { return gainMap; }

private:
    CIImage *hdr{ nil };
    CIImage *sdr{ nil };
    CIImage *gainMap{ nil };
    QVCocoaFunctions::HDRMetadata imageMetadata;
};

class NativeAnimatedImage final : public QVCocoaFunctions::AnimatedImage
{
public:
    explicit NativeAnimatedImage(const QString &filePath)
    {
        const QByteArray pathData = QFile::encodeName(filePath);
        CFURLRef url = CFURLCreateFromFileSystemRepresentation(
            kCFAllocatorDefault,
            reinterpret_cast<const UInt8 *>(pathData.constData()),
            static_cast<CFIndex>(pathData.size()),
            false);
        if (!url)
            return;

        source = CGImageSourceCreateWithURL(url, nullptr);
        CFRelease(url);
        if (!source)
            return;

        frameTotal = static_cast<int>(CGImageSourceGetCount(source));
        if (frameTotal <= 1)
            return;

        if (const CFDictionaryRef properties = pngPropertiesForFrame(source, 0))
        {
            double loopValue = 0;
            if (numberFromDictionary(properties, kCGImagePropertyAPNGLoopCount, loopValue))
                loops = loopValue == 0 ? -1 : std::max(0, static_cast<int>(std::lround(loopValue)));
            CFRelease(properties);
        }

        valid = true;
    }

    ~NativeAnimatedImage() override
    {
        if (source)
            CFRelease(source);
    }

    bool isValid() const override { return valid; }

    int frameCount() const override { return valid ? frameTotal : 0; }

    int loopCount() const override { return loops; }

    QImage frame(const int frameNumber) const override
    {
        if (!valid || frameNumber < 0 || frameNumber >= frameTotal)
            return {};

        CGImageRef cgImage = CGImageSourceCreateImageAtIndex(source, static_cast<size_t>(frameNumber), nullptr);
        if (!cgImage)
            return {};

        QImage image = imageFromCGImage(cgImage);
        CGImageRelease(cgImage);
        return image;
    }

    int frameDelay(const int frameNumber) const override
    {
        if (!valid || frameNumber < 0 || frameNumber >= frameTotal)
            return 0;

        int delay = 100;
        if (const CFDictionaryRef properties = pngPropertiesForFrame(source, static_cast<size_t>(frameNumber)))
        {
            double seconds = 0;
            if (!numberFromDictionary(properties, kCGImagePropertyAPNGUnclampedDelayTime, seconds) &&
                !numberFromDictionary(properties, kCGImagePropertyAPNGDelayTime, seconds))
            {
                seconds = 0.1;
            }

            if (std::isfinite(seconds))
                delay = std::max(0, static_cast<int>(std::lround(seconds * 1000.0)));
            CFRelease(properties);
        }
        return delay;
    }

private:
    CGImageSourceRef source {nullptr};
    int frameTotal {0};
    int loops {-1};
    bool valid {false};
};

struct HDRFrameFlowState
{
    std::atomic<int> framesInFlight{ 0 };
    std::atomic<quint64> presentedFrameCount{ 0 };
    std::atomic<quint64> missedTargetDeadlineCount{ 0 };
    std::atomic<double> lastPresentedTime{ 0.0 };
    std::atomic<double> lastGPUExecutionMilliseconds{ 0.0 };
    std::atomic<double> lastPresentedIntervalMilliseconds{ 0.0 };
    std::atomic<double> lastRequestToPresentationMilliseconds{ 0.0 };
    std::atomic<bool> firstVisibleFrameUsesFinalHeadroom{ false };
};

// A background full-resolution render can outlive the C++ renderer turn that
// scheduled it.  Main-queue installation therefore resolves the owner through
// this independently retained gate instead of capturing a raw `Impl *`.
struct HDRPersistentSurfaceGate
{
    std::atomic<void *> owner{ nullptr };
};
}

struct QVCocoaFunctions::HDRRenderer::Impl
{
    explicit Impl(QWidget *viewportWidget)
    {
        if (!viewportWidget)
            return;

        nativeView = reinterpret_cast<NSView *>(viewportWidget->winId());
        device = MTLCreateSystemDefaultDevice();
        if (!nativeView || !device)
            return;

        commandQueue = [device newCommandQueue];
        renderQueue = dispatch_queue_create(
                "com.fovelle.hdr-render-encode", DISPATCH_QUEUE_SERIAL);
        persistentSurfaceQueue = dispatch_queue_create(
                "com.fovelle.hdr-persistent-surface",
                dispatch_queue_attr_make_with_qos_class(
                        DISPATCH_QUEUE_SERIAL, QOS_CLASS_USER_INITIATED, 0));
        outputColorSpace = colorSyncDisplayP3ColorSpace(true);
        backgroundColorSpace = colorSyncSrgbColorSpace();
        if (!commandQueue || !renderQueue || !persistentSurfaceQueue
            || !outputColorSpace || !backgroundColorSpace)
            return;

        NSDictionary *contextOptions = @{
            (id)kCIContextUseSoftwareRenderer : @NO,
            (id)kCIContextWorkingColorSpace : (id)outputColorSpace,
            (id)kCIContextOutputColorSpace : (id)outputColorSpace,
            (id)kCIContextWorkingFormat : @(kCIFormatRGBAh),
            // This renderer repeatedly displays one RAW graph. Apple
            // recommends caching intermediates for that interactive use case;
            // disabling it is intended for one-shot exports and allowed RAW
            // tiles to be recomputed after they had already been revealed.
            (id)kCIContextCacheIntermediates : @YES
        };
        context = [[CIContext contextWithMTLDevice:device options:contextOptions] retain];
        persistentContext = [[CIContext contextWithMTLDevice:device
                                                     options:contextOptions] retain];
        if (!context || !persistentContext)
            return;

        nativeView.wantsLayer = YES;
        presentationContainerLayer = [[CALayer layer] retain];
        presentationContainerLayer.frame = nativeView.bounds;
        presentationContainerLayer.autoresizingMask =
                kCALayerWidthSizable | kCALayerHeightSizable;
        // Do not snapshot QNSView's AppKit-managed backing-layer flag here.
        // During attachment it can still be YES and later settle to NO, which
        // leaves this standalone subtree in the opposite coordinate system.
        presentationContainerLayer.geometryFlipped =
                QVCocoaFunctions::persistentHDRLayerGeometryFlipped();
        presentationContainerLayer.opacity = 0.0F;
        presentationContainerLayer.hidden = YES;
        viewportBackgroundLayer = [[CALayer layer] retain];
        viewportBackgroundLayer.opaque = YES;
        viewportBackgroundLayer.frame = nativeView.bounds;
        viewportBackgroundLayer.autoresizingMask =
                kCALayerWidthSizable | kCALayerHeightSizable;
        viewportBackgroundLayer.hidden = YES;

        // The high-frequency path is a materialized half-float CGImage in a
        // regular Core Animation layer.  Once ready, pan/zoom changes only
        // this layer's affine geometry; they don't consume a new drawable.
        // Large images that exceed the bounded cache budget stay on the
        // viewport-sized CAMetalLayer fallback below.
        persistentImageLayer = [[CALayer layer] retain];
        persistentImageLayer.opaque = YES;
        persistentImageLayer.contentsFormat = kCAContentsFormatRGBA16Float;
        persistentImageLayer.contentsGravity = kCAGravityResize;
        persistentImageLayer.minificationFilter = kCAFilterTrilinear;
        persistentImageLayer.magnificationFilter = kCAFilterLinear;
        persistentImageLayer.wantsExtendedDynamicRangeContent = YES;
        persistentImageLayer.hidden = YES;
        persistentImageLayer.opacity = 0.0F;
        if (@available(macOS 15.0, *))
            persistentImageLayer.toneMapMode = CAToneMapModeAutomatic;
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 260000
        if (@available(macOS 26.0, *))
            persistentImageLayer.preferredDynamicRange = CADynamicRangeHigh;
#endif

        metalLayer = [[CAMetalLayer layer] retain];
        metalLayer.device = device;
        metalLayer.pixelFormat = MTLPixelFormatRGBA16Float;
        metalLayer.framebufferOnly = NO;
        metalLayer.opaque = YES;
        metalLayer.colorspace = outputColorSpace;
        metalLayer.wantsExtendedDynamicRangeContent = YES;
        metalLayer.presentsWithTransaction = NO;
        metalLayer.allowsNextDrawableTimeout = YES;
        metalLayer.maximumDrawableCount = 3;
        metalLayer.autoresizingMask = kCALayerWidthSizable | kCALayerHeightSizable;
        if (@available(macOS 15.0, *))
            metalLayer.toneMapMode = CAToneMapModeAutomatic;
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 260000
        if (@available(macOS 26.0, *))
            metalLayer.preferredDynamicRange = CADynamicRangeHigh;
#endif

        // UI which overlaps HDR pixels must belong to the same native layer
        // tree as the drawable. A transparent QWidget is composited from Qt's
        // separate SDR backing store; its nominally transparent corners then
        // reveal that rectangular store instead of the Metal image below.
        navigationOverlayLayer = [[CALayer layer] retain];
        navigationOverlayLayer.geometryFlipped = YES;
        navigationOverlayLayer.frame = nativeView.bounds;
        navigationOverlayLayer.autoresizingMask =
                kCALayerWidthSizable | kCALayerHeightSizable;
        navigationOverlayLayer.zPosition = 1000.0;
        for (int index = 0; index < 2; ++index) {
            navigationBackgroundLayers[index] = [CAShapeLayer layer];
            navigationBackgroundLayers[index].hidden = YES;
            navigationChevronLayers[index] = [CAShapeLayer layer];
            navigationChevronLayers[index].hidden = YES;
            navigationChevronLayers[index].fillColor = nil;
            navigationChevronLayers[index].lineWidth = 4.0;
            navigationChevronLayers[index].lineCap = kCALineCapRound;
            navigationChevronLayers[index].lineJoin = kCALineJoinRound;
            [navigationOverlayLayer addSublayer:navigationBackgroundLayers[index]];
            [navigationOverlayLayer addSublayer:navigationChevronLayers[index]];
        }
        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        metalLayer.frame = nativeView.bounds;
        metalLayer.hidden = YES;
        metalLayer.opacity = 0.0F;
        [nativeView.layer addSublayer:presentationContainerLayer];
        [presentationContainerLayer addSublayer:viewportBackgroundLayer];
        [presentationContainerLayer addSublayer:persistentImageLayer];
        [presentationContainerLayer addSublayer:metalLayer];
        // Keep controls fixed in viewport coordinates while the persistent
        // HDR image layer moves underneath them.
        [nativeView.layer addSublayer:navigationOverlayLayer];
        [CATransaction commit];
        presentationState = [[QVHDRPresentationState alloc] initWithMetalLayer:metalLayer];
        persistentSurfaceGate->owner.store(this);

        if (@available(macOS 14.0, *)) {
            Impl *renderer = this;
            displayLinkDelegate = [[QVHDRDisplayLinkDelegate alloc]
                    initWithUpdateHandler:^(CAMetalDisplayLinkUpdate *update) {
                        renderer->renderDisplayLinkUpdate(update);
                    }];
            displayLink = [[CAMetalDisplayLink alloc] initWithMetalLayer:metalLayer];
            displayLink.delegate = displayLinkDelegate;
            displayLink.preferredFrameLatency = 1.0F;
            // Prefer ProMotion's top cadence while retaining enough latitude
            // for the compositor to select a sustainable variable rate.
            displayLink.preferredFrameRateRange = CAFrameRateRangeMake(80.0, 120.0, 120.0);
            displayLink.paused = YES;
            [displayLink addToRunLoop:NSRunLoop.mainRunLoop
                              forMode:NSRunLoopCommonModes];
            [presentationState setMetalDisplayLink:displayLink];
        }

        // CAMetalDisplayLink supplies the drawable at display cadence. On
        // older macOS releases the native HDR overlay stays unavailable and
        // the existing SDR proxy remains the compatible presentation path.
        state.rendererAvailable = presentationState != nil && displayLink != nil;
        state.usesRGBA16Float = true;
        state.usesExtendedLinearDisplayP3 = CGColorSpaceUsesExtendedRange(outputColorSpace);
        state.usesColorSync = true;
        state.wantsExtendedDynamicRangeContent = metalLayer.wantsExtendedDynamicRangeContent;
        state.clearsEntireDrawableOpaque = metalLayer.opaque;
        state.usesCoreImageManagedIntermediates = true;
        state.cachesIntermediates = true;
        state.usesCAMetalDisplayLink = displayLink != nil;
        state.encodesMetalOffMainThread = renderQueue != nullptr;
        state.usesDisplayLinkInteractionPacing = displayLink != nil;
        state.usesNativeNavigationOverlay = navigationOverlayLayer != nil;
        state.usesPersistentHDRSurface = persistentImageLayer != nil;
        setBackgroundColor(backgroundColor);
    }

    ~Impl()
    {
        persistentSurfaceGate->owner.store(nullptr);
        if (displayLink) {
            displayLink.paused = YES;
            displayLink.delegate = nil;
            [displayLink invalidate];
        }
        [displayLinkDelegate invalidate];
        if (renderQueue)
            dispatch_sync(renderQueue, ^{});
        if (persistentSurfaceQueue)
            dispatch_sync(persistentSurfaceQueue, ^{});
        [presentationState setMetalDisplayLink:nil];
        [presentationState invalidate];
        clearPreparedImages();
        persistentImageLayer.contents = nil;
        if (persistentImage)
            CGImageRelease(persistentImage);
        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        [navigationOverlayLayer removeFromSuperlayer];
        [presentationContainerLayer removeFromSuperlayer];
        [CATransaction commit];
        [presentationState release];
        [displayLink release];
        [displayLinkDelegate release];
        [navigationOverlayLayer release];
        [presentationContainerLayer release];
        [metalLayer release];
        [persistentImageLayer release];
        [viewportBackgroundLayer release];
        [context release];
        [persistentContext release];
        [commandQueue release];
#if !OS_OBJECT_USE_OBJC
        if (renderQueue)
            dispatch_release(renderQueue);
        if (persistentSurfaceQueue)
            dispatch_release(persistentSurfaceQueue);
#endif
        [device release];
        if (outputColorSpace)
            CGColorSpaceRelease(outputColorSpace);
        if (backgroundColorSpace)
            CGColorSpaceRelease(backgroundColorSpace);
    }

    void setBackgroundColor(const QColor &newColor)
    {
        if (!newColor.isValid())
            return;

        backgroundColor = newColor.toRgb();
        state.backgroundRed = backgroundColor.red();
        state.backgroundGreen = backgroundColor.green();
        state.backgroundBlue = backgroundColor.blue();
        ++state.backgroundUpdateCount;

        if (!metalLayer || !backgroundColorSpace)
            return;
        const CGFloat components[]{ backgroundColor.redF(), backgroundColor.greenF(),
                                    backgroundColor.blueF(), 1.0 };
        CGColorRef layerColor = CGColorCreate(backgroundColorSpace, components);
        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        metalLayer.backgroundColor = layerColor;
        viewportBackgroundLayer.backgroundColor = layerColor;
        [CATransaction commit];
        if (layerColor)
            CGColorRelease(layerColor);
    }

    float currentPresentationOpacity() const
    {
        if (!presentationContainerLayer || presentationContainerLayer.hidden)
            return 0.0F;
        CALayer *presented = presentationContainerLayer.presentationLayer;
        return std::clamp<float>(presented ? presented.opacity
                                           : presentationContainerLayer.opacity,
                                 0.0F, 1.0F);
    }

    void setExtendedDynamicRangeEnabled(const bool enabled)
    {
        metalLayer.wantsExtendedDynamicRangeContent = enabled;
        persistentImageLayer.wantsExtendedDynamicRangeContent = enabled;
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 260000
        if (@available(macOS 26.0, *)) {
            const CADynamicRange range = enabled
                    ? CADynamicRangeHigh : CADynamicRangeStandard;
            metalLayer.preferredDynamicRange = range;
            persistentImageLayer.preferredDynamicRange = range;
        }
#endif
        state.wantsExtendedDynamicRangeContent = enabled;
    }

    void finishPresentationTransition(const quint64 transitionGeneration,
                                      const NSUInteger imageGeneration)
    {
        if (transitionGeneration != presentationTransitionGeneration
            || imageGeneration != presentationState->generation)
            return;

        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        [presentationContainerLayer removeAnimationForKey:@"fovelle.hdr.presentation"];
        presentationContainerLayer.opacity = presentationActiveRequested ? 1.0F : 0.0F;
        presentationContainerLayer.hidden = !presentationActiveRequested;
        [CATransaction commit];
        if (!presentationActiveRequested)
            setExtendedDynamicRangeEnabled(false);
        presentationAnimationInFlight = false;
        state.presentationAnimationInFlight = false;
        state.layerOpacity = presentationActiveRequested ? 1.0F : 0.0F;
    }

    void applyPresentationTarget(const bool animated)
    {
        state.presentationActiveRequested = presentationActiveRequested;
        // Enable EDR before fading the HDR surface in.  While fading it out,
        // keep EDR enabled until the surface is fully transparent; disabling
        // it here would clamp the still-visible HDR pixels in a single frame.
        if (presentationActiveRequested)
            setExtendedDynamicRangeEnabled(true);
        if (!image || !presentationState->firstFramePresented) {
            if (!presentationActiveRequested) {
                [CATransaction begin];
                [CATransaction setDisableActions:YES];
                presentationContainerLayer.opacity = 0.0F;
                presentationContainerLayer.hidden = YES;
                [CATransaction commit];
                setExtendedDynamicRangeEnabled(false);
                state.layerOpacity = 0.0F;
            }
            return;
        }

        const float targetOpacity = presentationActiveRequested ? 1.0F : 0.0F;
        const float startOpacity = currentPresentationOpacity();
        const float distance = std::abs(targetOpacity - startOpacity);
        const quint64 transitionGeneration = ++presentationTransitionGeneration;
        const NSUInteger imageGeneration = presentationState->generation;

        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        [presentationContainerLayer removeAnimationForKey:@"fovelle.hdr.presentation"];
        presentationContainerLayer.hidden = NO;
        presentationContainerLayer.opacity = targetOpacity;
        [CATransaction commit];

        if (!animated || distance <= 0.001F) {
            finishPresentationTransition(transitionGeneration, imageGeneration);
            return;
        }

        constexpr CFTimeInterval fullTransitionDuration = 0.45;
        const CFTimeInterval duration = std::max<CFTimeInterval>(
                0.08, fullTransitionDuration * distance);
        CABasicAnimation *animation = [CABasicAnimation animationWithKeyPath:@"opacity"];
        animation.fromValue = @(startOpacity);
        animation.toValue = @(targetOpacity);
        animation.duration = duration;
        animation.timingFunction = [CAMediaTimingFunction
                functionWithName:kCAMediaTimingFunctionEaseInEaseOut];
        [presentationContainerLayer addAnimation:animation
                                          forKey:@"fovelle.hdr.presentation"];
        presentationAnimationInFlight = true;
        state.presentationAnimationInFlight = true;
        state.layerOpacity = startOpacity;
        ++state.presentationTransitionCount;

        const auto gate = persistentSurfaceGate;
        dispatch_after(dispatch_time(
                               DISPATCH_TIME_NOW,
                               static_cast<int64_t>((duration + 0.03) * NSEC_PER_SEC)),
                       dispatch_get_main_queue(), ^{
            auto *owner = static_cast<Impl *>(gate->owner.load());
            if (owner)
                owner->finishPresentationTransition(
                        transitionGeneration, imageGeneration);
        });
    }

    void setPresentationActive(const bool active, const bool animated)
    {
        if (presentationActiveRequested == active && presentationAnimationInFlight)
            return;
        presentationActiveRequested = active;
        applyPresentationTarget(animated);
    }

    CGColorRef navigationColor(const QColor &color) const
    {
        const QColor rgb = color.toRgb();
        const CGFloat components[]{ rgb.redF(), rgb.greenF(), rgb.blueF(), rgb.alphaF() };
        return CGColorCreate(backgroundColorSpace, components);
    }

    void setNavigationOverlay(const int index, const QRectF &viewportRect,
                              const qreal opacity, const bool previous,
                              const bool darkBackground, const bool hovered,
                              const bool pressed, const bool enabled)
    {
        if (!navigationOverlayLayer || index < 0 || index >= 2)
            return;

        CAShapeLayer *backgroundLayer = navigationBackgroundLayers[index];
        CAShapeLayer *chevronLayer = navigationChevronLayers[index];
        const CGFloat boundedOpacity = std::clamp<CGFloat>(opacity, 0.0, 1.0);
        const CGFloat frameWidth = std::max<qreal>(0.0, viewportRect.width());
        const CGFloat frameHeight = std::max<qreal>(0.0, viewportRect.height());
        // The CAMetalLayer subtree is composited in Core Animation's
        // bottom-left coordinate system even though Qt supplies viewport
        // geometry from the top-left. `geometryFlipped` does not change the
        // interpretation of an already assigned sublayer frame here, so map
        // the Y coordinate explicitly. This keeps the native paint surface
        // aligned with the invisible QWidget used for hit testing.
        const CGFloat frameY = CGRectGetHeight(navigationOverlayLayer.bounds)
                - viewportRect.y() - frameHeight;
        const CGRect frame = CGRectMake(viewportRect.x(), frameY,
                                        frameWidth, frameHeight);
        const bool artworkVisible = boundedOpacity > 0.001
                && frame.size.width > 0.0 && frame.size.height > 0.0;
        const bool highlighted = hovered || pressed;

        QColor background;
        if (darkBackground)
            background = QColor(128, 128, 128, highlighted ? 235 : 220);
        else if (highlighted)
            background = QColor(255, 255, 255, 55);
        if (!enabled && background.isValid())
            background.setAlpha(100);

        QColor foreground = darkBackground ? QColor(48, 48, 48) : QColor(96, 96, 96);
        if (!enabled)
            foreground.setAlpha(90);

        CGPathRef backgroundPath = CGPathCreateWithRoundedRect(
                CGRectMake(1.0, 1.0, std::max<CGFloat>(0.0, frame.size.width - 2.0),
                           std::max<CGFloat>(0.0, frame.size.height - 2.0)),
                10.0, 10.0, nullptr);
        CGMutablePathRef chevronPath = CGPathCreateMutable();
        const CGFloat centerX = frame.size.width / 2.0;
        const CGFloat centerY = frame.size.height / 2.0;
        const CGFloat direction = previous ? -1.0 : 1.0;
        CGPathMoveToPoint(chevronPath, nullptr,
                          centerX - direction * 5.0, centerY - 12.0);
        CGPathAddLineToPoint(chevronPath, nullptr,
                             centerX + direction * 7.0, centerY);
        CGPathAddLineToPoint(chevronPath, nullptr,
                             centerX - direction * 5.0, centerY + 12.0);

        CGColorRef backgroundCGColor = background.isValid()
                ? navigationColor(background) : nullptr;
        CGColorRef foregroundCGColor = navigationColor(foreground);
        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        navigationOverlayLayer.frame = nativeView.bounds;
        backgroundLayer.frame = frame;
        backgroundLayer.path = backgroundPath;
        backgroundLayer.fillColor = backgroundCGColor;
        backgroundLayer.opacity = boundedOpacity;
        backgroundLayer.hidden = !artworkVisible || !background.isValid();
        chevronLayer.frame = frame;
        chevronLayer.path = chevronPath;
        chevronLayer.strokeColor = foregroundCGColor;
        chevronLayer.opacity = boundedOpacity;
        chevronLayer.hidden = !artworkVisible;
        [CATransaction commit];

        if (backgroundPath)
            CGPathRelease(backgroundPath);
        if (chevronPath)
            CGPathRelease(chevronPath);
        if (backgroundCGColor)
            CGColorRelease(backgroundCGColor);
        if (foregroundCGColor)
            CGColorRelease(foregroundCGColor);
        ++state.navigationOverlayUpdateCount;
        state.nativeNavigationVisibleCount = 0;
        for (int layerIndex = 0; layerIndex < 2; ++layerIndex) {
            if (!navigationChevronLayers[layerIndex].hidden
                && navigationChevronLayers[layerIndex].opacity > 0.001F)
                ++state.nativeNavigationVisibleCount;
        }
    }

    void clearNavigationOverlays()
    {
        if (!navigationOverlayLayer)
            return;
        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        for (int index = 0; index < 2; ++index) {
            navigationBackgroundLayers[index].hidden = YES;
            navigationChevronLayers[index].hidden = YES;
        }
        [CATransaction commit];
        state.nativeNavigationVisibleCount = 0;
        ++state.navigationOverlayUpdateCount;
    }

    void discardPersistentSurface(const bool keepVisible)
    {
        persistentSurfacePreparationInFlight = false;
        persistentSurfacePreparationGeneration = 0;
        persistentSurfaceReady = false;
        state.persistentHDRSurfaceReady = false;
        state.persistentHDRSurfaceBytes = 0;
        state.persistentHDRSurfacePreparationMilliseconds = 0.0;
        if (keepVisible)
            return;

        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        persistentImageLayer.contents = nil;
        persistentImageLayer.hidden = YES;
        persistentImageLayer.opacity = 0.0F;
        viewportBackgroundLayer.hidden = YES;
        [CATransaction commit];
        if (persistentImage) {
            CGImageRelease(persistentImage);
            persistentImage = nullptr;
        }
    }

    void updatePersistentSurfaceGeometry(const QSize &viewportSize,
                                         const QPolygonF &corners)
    {
        latestViewportSize = viewportSize;
        latestCorners = corners;
        if (!persistentSurfaceReady || !persistentImage
            || viewportSize.isEmpty() || corners.size() < 4)
            return;

        const CGFloat sourceWidth = CGImageGetWidth(persistentImage);
        const CGFloat sourceHeight = CGImageGetHeight(persistentImage);
        if (sourceWidth <= 0.0 || sourceHeight <= 0.0)
            return;

        const QTransform qtTransform = QVCocoaFunctions::persistentHDRLayerTransform(
                QSizeF(sourceWidth, sourceHeight), corners);
        const CGAffineTransform transform = CGAffineTransformMake(
                qtTransform.m11(), qtTransform.m12(),
                qtTransform.m21(), qtTransform.m22(),
                qtTransform.dx(), qtTransform.dy());

        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        viewportBackgroundLayer.frame = nativeView.bounds;
        persistentImageLayer.bounds = CGRectMake(
                0.0, 0.0, sourceWidth, sourceHeight);
        persistentImageLayer.anchorPoint = CGPointZero;
        persistentImageLayer.position = CGPointZero;
        persistentImageLayer.affineTransform = transform;
        navigationOverlayLayer.frame = nativeView.bounds;
        [CATransaction commit];
        if (qEnvironmentVariableIsSet("FOVELLE_HDR_DIAGNOSTIC_LOG")) {
            const CGRect viewBounds = nativeView.bounds;
            const CGRect rootBounds = nativeView.layer.bounds;
            const CGRect containerFrame = presentationContainerLayer.frame;
            const CGRect containerBounds = presentationContainerLayer.bounds;
            const CGRect imageFrame = persistentImageLayer.frame;
            qInfo().nospace()
                    << "FOVELLE_HDR_LAYER view_flipped=" << nativeView.isFlipped
                    << " root_flipped=" << nativeView.layer.geometryFlipped
                    << " container_flipped=" << presentationContainerLayer.geometryFlipped
                    << " viewport=" << viewportSize.width() << "x" << viewportSize.height()
                    << " view_bounds=" << viewBounds.origin.x << "," << viewBounds.origin.y
                    << "," << viewBounds.size.width << "," << viewBounds.size.height
                    << " root_bounds=" << rootBounds.origin.x << "," << rootBounds.origin.y
                    << "," << rootBounds.size.width << "," << rootBounds.size.height
                    << " container_frame=" << containerFrame.origin.x << ","
                    << containerFrame.origin.y << "," << containerFrame.size.width << ","
                    << containerFrame.size.height
                    << " container_bounds=" << containerBounds.origin.x << ","
                    << containerBounds.origin.y << "," << containerBounds.size.width << ","
                    << containerBounds.size.height
                    << " image_frame=" << imageFrame.origin.x << "," << imageFrame.origin.y
                    << "," << imageFrame.size.width << "," << imageFrame.size.height;
        }
        ++state.compositorGeometryUpdateCount;
    }

    void installPersistentSurface(CGImageRef surface,
                                  const NSUInteger generation,
                                  const double preparationMilliseconds)
    {
        if (generation != presentationState->generation
            || generation != persistentSurfacePreparationGeneration)
            return;

        persistentSurfacePreparationInFlight = false;
        state.persistentHDRSurfacePreparationMilliseconds =
                preparationMilliseconds;
        if (!surface)
            return;

        if (persistentImage)
            CGImageRelease(persistentImage);
        persistentImage = CGImageRetain(surface);
        persistentSurfaceReady = true;
        state.persistentHDRSurfaceReady = true;
        state.persistentHDRSurfaceBytes = static_cast<quint64>(
                CGImageGetBytesPerRow(surface)) * CGImageGetHeight(surface);

        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        persistentImageLayer.contents = reinterpret_cast<id>(surface);
        persistentImageLayer.contentsScale = 1.0;
        persistentImageLayer.hidden = NO;
        persistentImageLayer.opacity = 1.0F;
        viewportBackgroundLayer.hidden = NO;
        // The viewport drawable and persistent image contain the same final
        // HDR endpoint.  Switch both visibility and geometry in one Core
        // Animation transaction, then keep the drawable pool idle.
        metalLayer.opacity = 0.0F;
        metalLayer.hidden = YES;
        [CATransaction commit];
        updatePersistentSurfaceGeometry(latestViewportSize, latestCorners);
        state.layerOpacity = currentPresentationOpacity();
        renderPending = false;
        if (displayLink)
            displayLink.paused = YES;
    }

    void schedulePersistentSurfacePreparation()
    {
        if (persistentSurfaceReady || persistentSurfacePreparationInFlight
            || !preparedHDRImage || !persistentContext || !persistentSurfaceQueue)
            return;

        const CGRect extent = CGRectIntegral(preparedHDRImage.extent);
        if (CGRectIsEmpty(extent) || !std::isfinite(extent.size.width)
            || !std::isfinite(extent.size.height))
            return;
        const quint64 width = static_cast<quint64>(extent.size.width);
        const quint64 height = static_cast<quint64>(extent.size.height);
        constexpr quint64 maximumPersistentBytes = 512ULL * 1024ULL * 1024ULL;
        if (width == 0 || height == 0 || width > 16384 || height > 16384
            || width > std::numeric_limits<quint64>::max() / height
            || width * height > maximumPersistentBytes / 8ULL)
            return;

        persistentSurfacePreparationInFlight = true;
        persistentSurfacePreparationGeneration = presentationState->generation;
        const NSUInteger generation = persistentSurfacePreparationGeneration;
        CIImage *source = [preparedHDRImage retain];
        CIContext *surfaceContext = [persistentContext retain];
        CGColorSpaceRef surfaceColorSpace = CGColorSpaceRetain(outputColorSpace);
        const auto gate = persistentSurfaceGate;
        dispatch_async(persistentSurfaceQueue, ^{
            @autoreleasepool {
                const CFTimeInterval started = CACurrentMediaTime();
                CGImageRef surface = [surfaceContext
                        createCGImage:source
                             fromRect:extent
                               format:kCIFormatRGBAh
                           colorSpace:surfaceColorSpace
                             deferred:NO];
                // The CGImage is fully materialized (`deferred:NO`), so the
                // one-shot CI evaluation cache can be released immediately.
                // Retaining it would duplicate a substantial fraction of an
                // 8-byte-per-pixel source while the persistent surface lives.
                [surfaceContext clearCaches];
                const double elapsedMilliseconds =
                        (CACurrentMediaTime() - started) * 1000.0;
                dispatch_async(dispatch_get_main_queue(), ^{
                    auto *owner = static_cast<Impl *>(gate->owner.load());
                    if (owner)
                        owner->installPersistentSurface(
                                surface, generation, elapsedMilliseconds);
                    if (surface)
                        CGImageRelease(surface);
                });
                [source release];
                [surfaceContext release];
                CGColorSpaceRelease(surfaceColorSpace);
            }
        });
    }

    void rebuildDisplayLinkForDrawableResize()
    {
        if (!displayLink || !displayLinkDelegate)
            return;
        if (@available(macOS 14.0, *)) {
            CAMetalDisplayLink *replacement =
                    [[CAMetalDisplayLink alloc] initWithMetalLayer:metalLayer];
            if (!replacement)
                return;

            CAMetalDisplayLink *previous = displayLink;
            previous.paused = YES;
            previous.delegate = nil;
            [previous invalidate];

            replacement.delegate = displayLinkDelegate;
            replacement.preferredFrameLatency = 1.0F;
            replacement.preferredFrameRateRange =
                    CAFrameRateRangeMake(80.0, 120.0, 120.0);
            replacement.paused = YES;
            [replacement addToRunLoop:NSRunLoop.mainRunLoop
                              forMode:NSRunLoopCommonModes];
            displayLink = replacement;
            [presentationState setMetalDisplayLink:displayLink];
            [previous release];
            ++state.displayLinkRebuildCount;
        }
    }

    bool setImage(const HDRImagePtr &newImage)
    {
        const bool presentationFullyVisible = currentPresentationOpacity() >= 0.999F;
        const bool previousMetalPresentationVisible = image
                && presentationState->firstFramePresented
                && presentationFullyVisible
                && metalLayer.opacity >= 0.999F;
        const bool previousPersistentPresentationVisible = image
                && presentationState->firstFramePresented
                && presentationFullyVisible
                && persistentSurfaceReady
                && persistentImageLayer.opacity >= 0.999F;
        const bool retainPreviousPresentation = presentationActiveRequested
                && (previousMetalPresentationVisible
                    || previousPersistentPresentationVisible);
        if (displayLink)
            displayLink.paused = YES;
        if (renderQueue)
            dispatch_sync(renderQueue, ^{});
        renderPending = false;
        pendingViewportSize = {};
        pendingCorners.clear();
        pendingLinearProgress = 0.0;
        pendingRequestTimestamp = 0.0;
        pendingInteractive = false;
        interactiveKeepAliveUntil = 0.0;
        frameFlow = std::make_shared<HDRFrameFlowState>();
        clearPreparedImages();
        // The context is intentionally long-lived for one interactive view,
        // but intermediates from the previous source are no longer reusable.
        // Release them only on image replacement, never on zoom or pan.
        if (context)
            [context clearCaches];
        const auto nativeImage = std::dynamic_pointer_cast<const NativeHDRImage>(newImage);
        image = nativeImage;
        [presentationState resetForImage];
        ++presentationTransitionGeneration;
        presentationAnimationInFlight = false;
        discardPersistentSurface(
                nativeImage && previousPersistentPresentationVisible);
        latestViewportSize = {};
        latestCorners.clear();
        state.imageActive = nativeImage != nullptr;
        state.firstFrameSubmitted = false;
        state.firstFramePresented = false;
        state.hdrPreparationInFlight = false;
        state.hdrPrepared = false;
        state.preparedGeometryActive = false;
        state.bootstrappingEDR = false;
        state.requestedDrawableWidth = 0;
        state.requestedDrawableHeight = 0;
        state.actualTextureWidth = 0;
        state.actualTextureHeight = 0;
        state.drawableGeometryMatches = false;
        state.layerOpacity = 0.0F;
        state.geometryGeneration = presentationState->generation;
        state.geometryResetCount = 0;
        state.renderRequestCount = 0;
        state.coalescedRenderRequestCount = 0;
        state.displayLinkCallbackCount = 0;
        state.displayLinkRebuildCount = 0;
        state.deferredDisplayLinkCallbackCount = 0;
        state.requestedRenderGeneration = 0;
        state.submittedRenderGeneration = 0;
        state.presentedFrameCount = 0;
        state.missedTargetDeadlineCount = 0;
        state.framesInFlight = 0;
        state.displayLinkInteractiveSubmissionCount = 0;
        state.compositorGeometryUpdateCount = 0;
        state.firstVisibleFrameUsesFinalHeadroom = false;
        state.presentationActiveRequested = presentationActiveRequested;
        state.presentationAnimationInFlight = false;
        state.transitionProgress = 0.0F;
        state.targetHeadroom = 1.0F;
        state.renderCount = 0;
        state.lastRenderMilliseconds = 0.0;
        state.lastGPUExecutionMilliseconds = 0.0;
        state.lastPresentedIntervalMilliseconds = 0.0;
        state.lastRequestToPresentationMilliseconds = 0.0;
        if (nativeImage) {
            const HDRMetadata &metadata = nativeImage->metadata();
            state.isRaw = metadata.isRaw;
            state.hasGainMap = metadata.hasAppleGainMap || metadata.hasISOGainMap;
            state.contentHeadroom = metadata.contentHeadroom;
        } else {
            state.isRaw = false;
            state.hasGainMap = false;
            state.contentHeadroom = 1.0F;
        }

        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        [presentationContainerLayer removeAnimationForKey:@"fovelle.hdr.presentation"];
        presentationContainerLayer.hidden = !nativeImage || !retainPreviousPresentation;
        presentationContainerLayer.opacity = nativeImage && retainPreviousPresentation
                ? 1.0F : 0.0F;
        metalLayer.hidden = nativeImage == nullptr;
        metalLayer.opacity = nativeImage && previousMetalPresentationVisible
                ? 1.0F : 0.0F;
        if (!nativeImage) {
            viewportBackgroundLayer.hidden = YES;
            persistentImageLayer.hidden = YES;
            persistentImageLayer.opacity = 0.0F;
        }
        [CATransaction commit];
        state.layerOpacity = nativeImage && retainPreviousPresentation ? 1.0F : 0.0F;
        setExtendedDynamicRangeEnabled(nativeImage && presentationActiveRequested);
        return nativeImage != nullptr && state.rendererAvailable;
    }

    void invalidateGeometry()
    {
        if (!image)
            return;

        renderPending = false;
        interactiveKeepAliveUntil = 0.0;
        if (displayLink)
            displayLink.paused = YES;
        if (renderQueue)
            dispatch_sync(renderQueue, ^{});
        clearPreparedImages();
        [presentationState resetForImage];
        discardPersistentSurface(false);
        state.firstFrameSubmitted = false;
        state.firstFramePresented = false;
        state.hdrPreparationInFlight = false;
        state.hdrPrepared = false;
        state.preparedGeometryActive = false;
        state.transitionProgress = 0.0F;
        state.geometryGeneration = presentationState->generation;
        ++state.geometryResetCount;

        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        [presentationContainerLayer removeAnimationForKey:@"fovelle.hdr.presentation"];
        presentationContainerLayer.hidden = YES;
        presentationContainerLayer.opacity = 0.0F;
        metalLayer.hidden = NO;
        metalLayer.opacity = 1.0F;
        [CATransaction commit];
        ++presentationTransitionGeneration;
        presentationAnimationInFlight = false;
        state.layerOpacity = 0.0F;
    }

    static CIImage *mixImages(CIImage *sdr, CIImage *hdr, const float amount)
    {
        if (!hdr)
            return sdr;
        if (!sdr || amount >= 0.999F)
            return hdr;
        if (amount <= 0.001F)
            return sdr;

        CIFilter *transition = [CIFilter filterWithName:@"CIDissolveTransition"];
        [transition setValue:sdr forKey:kCIInputImageKey];
        [transition setValue:hdr forKey:kCIInputTargetImageKey];
        [transition setValue:@(amount) forKey:kCIInputTimeKey];
        return transition.outputImage ?: hdr;
    }

    void clearPreparedImages()
    {
        [preparedHDRImage release];
        [preparedSDRImage release];
        [preparationTexture release];
        preparedHDRImage = nil;
        preparedSDRImage = nil;
        preparationTexture = nil;
    }

    CIImage *displayImage(const NativeHDRImage &nativeImage, const float targetHeadroom,
                          const float progress)
    {
        CIImage *hdr = nativeImage.hdrCIImage();
        CIImage *sdr = nativeImage.sdrCIImage();
        const HDRMetadata &metadata = nativeImage.metadata();

        if (!sdr)
            return hdr;
        if (state.displayRenderingHeadroom <= 1.001F || progress <= 0.001F)
            return sdr;

        if (metadata.isRaw && !metadata.usesProcessedRawPreview) {
            const float rawAmount = state.displayRenderingHeadroom > 1.001F ? progress : 0.0F;
            return mixImages(sdr, hdr, rawAmount);
        }

        if (metadata.usesProcessedRawPreview && nativeImage.gainMapCIImage()) {
            if (@available(macOS 15.0, *)) {
                // Adaptive-HDR RAW is an SDR base plus an auxiliary gain map.
                // Ask Core Image to reconstruct exactly the headroom available
                // for this display/activation frame. This preserves the full
                // processed preview and avoids feeding a half-resolution gain-
                // map graph through a second viewport-dependent tone-map ROI.
                CIImage *adapted = [sdr imageByApplyingGainMap:nativeImage.gainMapCIImage()
                                                     headroom:std::max(1.0F, targetHeadroom)];
                if (adapted)
                    return adapted;
            }
        }

        if (@available(macOS 15.0, *)) {
            if (metadata.contentHeadroom > 1.0F) {
                CIFilter *toneMap = [CIFilter filterWithName:@"CIToneMapHeadroom"];
                [toneMap setValue:hdr forKey:kCIInputImageKey];
                [toneMap setValue:@(metadata.contentHeadroom) forKey:@"inputSourceHeadroom"];
                [toneMap setValue:@(targetHeadroom) forKey:@"inputTargetHeadroom"];
                if (toneMap.outputImage)
                    return mixImages(sdr, toneMap.outputImage, progress);
            }
        }

        const float fallbackAmount = state.displayRenderingHeadroom > 1.001F ? progress : 0.0F;
        return mixImages(sdr, hdr, fallbackAmount);
    }

    CIImage *preparedDisplayImage(const float targetHeadroom, const float progress)
    {
        if (!preparedSDRImage)
            return preparedHDRImage;
        if (!preparedHDRImage || state.displayRenderingHeadroom <= 1.001F
            || progress <= 0.001F)
            return preparedSDRImage;

        // The decoded HDR endpoint already represents the complete gain-map or
        // RAW graph at its declared content headroom. Reapplying the gain map
        // for every pan at the same endpoint rebuilds an expensive full-frame
        // graph without changing a pixel.
        if (progress >= 0.999F
            && targetHeadroom + 0.001F >= image->metadata().contentHeadroom)
            return preparedHDRImage;

        if (image->metadata().isRaw && !image->metadata().usesProcessedRawPreview)
            return mixImages(preparedSDRImage, preparedHDRImage, progress);

        if (image->metadata().usesProcessedRawPreview && image->gainMapCIImage()) {
            if (@available(macOS 15.0, *)) {
                CIImage *adapted = [preparedSDRImage
                        imageByApplyingGainMap:image->gainMapCIImage()
                                      headroom:std::max(1.0F, targetHeadroom)];
                if (adapted)
                    return adapted;
            }
        }

        if (@available(macOS 15.0, *)) {
            if (image->metadata().contentHeadroom > 1.0F) {
                CIFilter *toneMap = [CIFilter filterWithName:@"CIToneMapHeadroom"];
                [toneMap setValue:preparedHDRImage forKey:kCIInputImageKey];
                [toneMap setValue:@(image->metadata().contentHeadroom)
                           forKey:@"inputSourceHeadroom"];
                [toneMap setValue:@(targetHeadroom) forKey:@"inputTargetHeadroom"];
                if (toneMap.outputImage)
                    return mixImages(preparedSDRImage, toneMap.outputImage, progress);
            }
        }
        return mixImages(preparedSDRImage, preparedHDRImage, progress);
    }

    void syncPresentationDiagnostics()
    {
        state.firstFrameSubmitted = presentationState->firstFrameSubmitted;
        state.firstFramePresented = presentationState->firstFramePresented;
        state.hdrPreparationInFlight = presentationState->hdrPreparationInFlight;
        state.hdrPrepared = presentationState->hdrPrepared;
        state.framesInFlight = frameFlow ? frameFlow->framesInFlight.load() : 0;
        state.frameInFlight = state.framesInFlight > 0;
        state.presentedFrameCount = frameFlow
                ? frameFlow->presentedFrameCount.load() : 0;
        state.missedTargetDeadlineCount = frameFlow
                ? frameFlow->missedTargetDeadlineCount.load() : 0;
        state.lastPresentedIntervalMilliseconds = frameFlow
                ? frameFlow->lastPresentedIntervalMilliseconds.load() : 0.0;
        state.lastGPUExecutionMilliseconds = frameFlow
                ? frameFlow->lastGPUExecutionMilliseconds.load() : 0.0;
        state.lastRequestToPresentationMilliseconds = frameFlow
                ? frameFlow->lastRequestToPresentationMilliseconds.load() : 0.0;
        state.firstVisibleFrameUsesFinalHeadroom = frameFlow
                && frameFlow->firstVisibleFrameUsesFinalHeadroom.load();
        state.displayLinkPaused = displayLink ? displayLink.paused : true;
        NSWindow *window = nativeView.window;
        state.nativeWindowNumber = window
                ? static_cast<int>(window.windowNumber) : 0;
        if (window) {
            // NSWindow uses a bottom-left Cocoa global coordinate system,
            // while screencapture window bounds and Qt global points use the
            // primary display's top-left. Convert the full frame (including
            // title bar), because `screencapture -l` captures that frame.
            const NSRect frame = window.frame;
            NSScreen *primaryScreen = NSScreen.screens.firstObject;
            const CGFloat primaryTop = primaryScreen
                    ? NSMaxY(primaryScreen.frame) : NSMaxY(frame);
            state.nativeWindowGlobalX = static_cast<int>(std::lround(NSMinX(frame)));
            state.nativeWindowGlobalY = static_cast<int>(
                    std::lround(primaryTop - NSMaxY(frame)));
        } else {
            state.nativeWindowGlobalX = 0;
            state.nativeWindowGlobalY = 0;
        }
        state.layerOpacity = currentPresentationOpacity();
        state.presentationActiveRequested = presentationActiveRequested;
        state.presentationAnimationInFlight = presentationAnimationInFlight
                || [presentationContainerLayer
                           animationForKey:@"fovelle.hdr.presentation"] != nil;
        state.persistentHDRSurfaceReady = persistentSurfaceReady;
    }

    CIImage *imageForTexture(CIImage *source, const QSize &viewportSize,
                             const QPolygonF &corners, const CGSize textureSize)
    {
        if (!source || viewportSize.isEmpty() || corners.size() < 4 || textureSize.width <= 0
            || textureSize.height <= 0)
            return nil;

        const CGRect sourceExtent = source.extent;
        if (CGRectIsEmpty(sourceExtent) || sourceExtent.size.width <= 0
            || sourceExtent.size.height <= 0)
            return nil;

        // A drawable can briefly belong to the previous CAMetalLayer pool
        // after a resize. Derive coordinates from the texture that will
        // actually receive the pixels, never from an assumed Retina scale.
        const CGFloat scaleX = textureSize.width / viewportSize.width();
        const CGFloat scaleY = textureSize.height / viewportSize.height();
        const auto destinationPoint = [&](const QPointF &point) {
            return CGPointMake(point.x() * scaleX,
                               (viewportSize.height() - point.y()) * scaleY);
        };
        const CGPoint destinationBottomLeft = destinationPoint(corners.at(3));
        const CGPoint destinationBottomRight = destinationPoint(corners.at(2));
        const CGPoint destinationTopLeft = destinationPoint(corners.at(0));
        const CGFloat a =
                (destinationBottomRight.x - destinationBottomLeft.x) / sourceExtent.size.width;
        const CGFloat b =
                (destinationBottomRight.y - destinationBottomLeft.y) / sourceExtent.size.width;
        const CGFloat c =
                (destinationTopLeft.x - destinationBottomLeft.x) / sourceExtent.size.height;
        const CGFloat d =
                (destinationTopLeft.y - destinationBottomLeft.y) / sourceExtent.size.height;
        const CGFloat tx =
                destinationBottomLeft.x - a * sourceExtent.origin.x - c * sourceExtent.origin.y;
        const CGFloat ty =
                destinationBottomLeft.y - b * sourceExtent.origin.x - d * sourceExtent.origin.y;
        source = [source imageByApplyingTransform:CGAffineTransformMake(a, b, c, d, tx, ty)];

        const CGRect destinationBounds = CGRectMake(0, 0, textureSize.width, textureSize.height);
        // QColor stores these constants in sRGB. Keep that source tag so
        // ColorSync converts the exact Qt theme color into extended-linear P3
        // instead of interpreting gamma-encoded components as linear values.
        CIColor *clearColor = [CIColor colorWithRed:backgroundColor.redF()
                                              green:backgroundColor.greenF()
                                               blue:backgroundColor.blueF()
                                              alpha:1
                                         colorSpace:backgroundColorSpace];
        CIImage *clearImage =
                [[CIImage imageWithColor:clearColor] imageByCroppingToRect:destinationBounds];
        return [[source imageByCroppingToRect:destinationBounds]
                imageByCompositingOverImage:clearImage];
    }

    void revealAfterPresentation(id<CAMetalDrawable> drawable,
                                 id<MTLCommandBuffer> commandBuffer,
                                 const bool finalHeadroom)
    {
        if (presentationState->firstFrameSubmitted
            || !QVCocoaFunctions::isFinalHDRFrameReadyForReveal(
                    state.drawableGeometryMatches, finalHeadroom ? 1.0 : 0.0))
            return;

        presentationState->firstFrameSubmitted = YES;
        const NSUInteger frameGeneration = presentationState->generation;
        QVHDRPresentationState *gate = presentationState;
        const auto ownerGate = persistentSurfaceGate;
        const auto visibleFlow = frameFlow;
        void (^revealLayer)(void) = ^{
            if (gate->generation != frameGeneration || !gate->metalLayer)
                return;
            [CATransaction begin];
            [CATransaction setDisableActions:YES];
            gate->metalLayer.hidden = NO;
            gate->metalLayer.opacity = 1.0F;
            [CATransaction commit];
            gate->firstFramePresented = YES;
            if (visibleFlow)
                visibleFlow->firstVisibleFrameUsesFinalHeadroom.store(finalHeadroom);
            auto *owner = static_cast<Impl *>(ownerGate->owner.load());
            if (owner)
                owner->applyPresentationTarget(true);
        };

        if ([drawable respondsToSelector:@selector(addPresentedHandler:)]) {
            [drawable addPresentedHandler:^(id<MTLDrawable>) {
                dispatch_async(dispatch_get_main_queue(), revealLayer);
            }];
        } else {
            [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer>) {
                dispatch_async(dispatch_get_main_queue(), revealLayer);
            }];
        }

        [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> completedBuffer) {
            if (completedBuffer.status != MTLCommandBufferStatusError)
                return;
            dispatch_async(dispatch_get_main_queue(), ^{
                if (gate->generation == frameGeneration)
                    gate->firstFrameSubmitted = NO;
            });
        }];
    }

    void scheduleHDRPreparation(const QSize &viewportSize, const QPolygonF &corners,
                                const CGSize textureSize)
    {
        if (presentationState->hdrPrepared || presentationState->hdrPreparationInFlight)
            return;

        // An SDR display never needs to evaluate the >1 branch merely to show
        // a compatible result.
        if (state.displayRenderingHeadroom <= 1.001F) {
            presentationState->hdrPrepared = YES;
            return;
        }

        CIImage *sdrSource = image->sdrCIImage();
        CIImage *hdrSource = image->hdrCIImage();
        if (!sdrSource || !hdrSource)
            return;

        clearPreparedImages();
        MTLTextureDescriptor *descriptor = [MTLTextureDescriptor
                texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float
                                              width:static_cast<NSUInteger>(textureSize.width)
                                             height:static_cast<NSUInteger>(textureSize.height)
                                          mipmapped:NO];
        descriptor.storageMode = MTLStorageModePrivate;
        descriptor.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite
                | MTLTextureUsageRenderTarget;
        preparationTexture = [device newTextureWithDescriptor:descriptor];
        id<MTLCommandBuffer> preparationBuffer = [commandQueue commandBuffer];
        if (!preparationTexture || !preparationBuffer) {
            clearPreparedImages();
            return;
        }

        // Cache the high-precision source graphs, before viewport transforms
        // and crops. Caching the final viewport image makes Core Image's
        // intermediate ROI geometry-dependent: after zooming or panning a
        // later evaluation can expose only the tiles resolved for an earlier
        // crop (visible as black bands or stale pixels). The source-space
        // intermediate remains valid while every visible frame is freshly
        // transformed and composited across the entire drawable.
        if (image->metadata().usesProcessedRawPreview) {
            // The DNG HDR graph already contains a half-resolution auxiliary
            // gain map.  Inserting an intermediate *after* applying that map
            // can freeze Core Image's first reduced ROI (4032x3024) into an
            // otherwise 8064x6048 extent, so the next viewport evaluation has
            // valid pixels only in one quadrant.  Both source images were
            // loaded with kCIImageCacheImmediately; retain their complete
            // public representations and let Core Image evaluate the gain-map
            // graph for the current ROI.
            preparedSDRImage = [sdrSource retain];
            preparedHDRImage = [hdrSource retain];
        } else {
            preparedSDRImage = [[sdrSource imageByInsertingIntermediate:YES] retain];
            preparedHDRImage = [[hdrSource imageByInsertingIntermediate:YES] retain];
        }
        if (!preparedSDRImage || !preparedHDRImage) {
            clearPreparedImages();
            return;
        }
        presentationState->hdrPreparationInFlight = YES;
        const NSUInteger preparationGeneration = presentationState->generation;
        QVHDRPresentationState *gate = presentationState;
        const CGRect bounds = CGRectMake(0, 0, textureSize.width, textureSize.height);
        const float fullTargetHeadroom = static_cast<float>(
                QVCocoaFunctions::effectiveHDRHeadroom(
                        state.contentHeadroom, state.displayRenderingHeadroom, 1.0));
        CIImage *finalDisplayImage = preparedDisplayImage(fullTargetHeadroom, 1.0F);
        CIImage *preparedHDRFrame = imageForTexture(
                finalDisplayImage, viewportSize, corners, textureSize);
        if (!preparedHDRFrame) {
            clearPreparedImages();
            presentationState->hdrPreparationInFlight = NO;
            return;
        }

        // Compile the opening ROI and several representative 4x interaction
        // ROIs on the renderer queue. The first *visible* drawable is still
        // the final-headroom image, while shader compilation and full-texture
        // reads cannot block AppKit or leak a partially evaluated frame. RAW
        // gain-map graphs can specialize their ROI on first evaluation; a
        // small cross-shaped warm set prevents the first few drag samples from
        // paying that compilation cost after the user zooms.
        QPolygonF interactionWarmCorners = corners;
        const QPointF viewportCenter(viewportSize.width() / 2.0,
                                     viewportSize.height() / 2.0);
        for (QPointF &corner : interactionWarmCorners)
            corner = viewportCenter + (corner - viewportCenter) * 4.0
                    + QPointF(11.0, 7.0);
        NSMutableArray *interactionWarmFrames = [[NSMutableArray alloc] initWithCapacity:5];
        const QPointF warmOffsets[] = {
            QPointF(0.0, 0.0),
            QPointF(viewportSize.width() * 0.25, 0.0),
            QPointF(-viewportSize.width() * 0.25, 0.0),
            QPointF(0.0, viewportSize.height() * 0.25),
            QPointF(0.0, -viewportSize.height() * 0.25),
        };
        for (const QPointF &offset : warmOffsets) {
            QPolygonF sampleCorners = interactionWarmCorners;
            for (QPointF &corner : sampleCorners)
                corner += offset;
            CIImage *warmFrame = imageForTexture(
                    finalDisplayImage, viewportSize, sampleCorners, textureSize);
            if (warmFrame)
                [interactionWarmFrames addObject:warmFrame];
        }
        NSArray *retainedWarmFrames = [interactionWarmFrames copy];
        [interactionWarmFrames release];

        CIImage *openingFrame = [preparedHDRFrame retain];
        id<MTLTexture> targetTexture = [preparationTexture retain];
        id<MTLCommandBuffer> retainedBuffer = [preparationBuffer retain];
        CIContext *renderContext = [context retain];
        CGColorSpaceRef renderColorSpace = CGColorSpaceRetain(outputColorSpace);
        dispatch_async(renderQueue, ^{
            [renderContext render:openingFrame
                      toMTLTexture:targetTexture
                     commandBuffer:retainedBuffer
                            bounds:bounds
                        colorSpace:renderColorSpace];
            for (CIImage *warmFrame in retainedWarmFrames) {
                [renderContext render:warmFrame
                          toMTLTexture:targetTexture
                         commandBuffer:retainedBuffer
                                bounds:bounds
                            colorSpace:renderColorSpace];
            }
            [retainedBuffer addCompletedHandler:^(id<MTLCommandBuffer> completedBuffer) {
                const BOOL completed =
                        completedBuffer.status == MTLCommandBufferStatusCompleted;
                dispatch_async(dispatch_get_main_queue(), ^{
                    if (gate->generation != preparationGeneration)
                        return;
                    gate->hdrPreparationInFlight = NO;
                    gate->hdrPrepared = completed;
                    if (gate->displayLink)
                        gate->displayLink.paused = NO;
                });
            }];
            [retainedBuffer commit];
            [openingFrame release];
            [retainedWarmFrames release];
            [targetTexture release];
            [retainedBuffer release];
            [renderContext release];
            CGColorSpaceRelease(renderColorSpace);
        });
    }

    void render(const QSize &viewportSize, const QPolygonF &corners,
                const qreal linearProgress, const bool interactive)
    {
        if (!state.rendererAvailable || !image || viewportSize.isEmpty() || corners.size() < 4)
            return;

        latestViewportSize = viewportSize;
        latestCorners = corners;
        if (persistentSurfaceReady) {
            QElapsedTimer geometryTimer;
            geometryTimer.start();
            ++state.renderRequestCount;
            const quint64 geometryGeneration = ++state.requestedRenderGeneration;
            updatePersistentSurfaceGeometry(viewportSize, corners);
            state.submittedRenderGeneration = geometryGeneration;
            state.lastRenderMilliseconds =
                    geometryTimer.nsecsElapsed() / 1000000.0;
            ++state.renderCount;
            renderPending = false;
            pendingInteractive = interactive;
            interactiveKeepAliveUntil = 0.0;
            if (displayLink)
                displayLink.paused = YES;
            return;
        }

        // Resize the CAMetalLayer on the AppKit/Qt thread that observed the
        // viewport change, before CAMetalDisplayLink vends its next drawable.
        // Mutating drawableSize from inside needsUpdate can leave the link
        // waiting on the old drawable pool after scrollbars resize a viewport.
        NSScreen *screen = nativeView.window.screen ?: NSScreen.mainScreen;
        const CGFloat backingScale = nativeView.window
                ? nativeView.window.backingScaleFactor
                : (screen ? screen.backingScaleFactor : 1.0);
        const CGSize requestedSize = CGSizeMake(
                std::max<CGFloat>(1.0, viewportSize.width() * backingScale),
                std::max<CGFloat>(1.0, viewportSize.height() * backingScale));
        state.requestedDrawableWidth = static_cast<int>(
                std::lround(requestedSize.width));
        state.requestedDrawableHeight = static_cast<int>(
                std::lround(requestedSize.height));
        const bool drawableSizeChanged =
                std::abs(metalLayer.drawableSize.width - requestedSize.width) > 0.5
                || std::abs(metalLayer.drawableSize.height - requestedSize.height) > 0.5;
        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        metalLayer.frame = nativeView.bounds;
        metalLayer.contentsScale = backingScale;
        if (drawableSizeChanged)
            metalLayer.drawableSize = requestedSize;
        navigationOverlayLayer.frame = nativeView.bounds;
        [CATransaction commit];
        if (drawableSizeChanged)
            rebuildDisplayLinkForDrawableResize();

        ++state.renderRequestCount;
        if (renderPending)
            ++state.coalescedRenderRequestCount;
        renderPending = true;
        pendingViewportSize = viewportSize;
        pendingCorners = corners;
        pendingLinearProgress = linearProgress;
        pendingInteractive = interactive;
        pendingRequestTimestamp = CACurrentMediaTime();
        // Keep CAMetalDisplayLink running for the short interaction window.
        // Pausing immediately after the first interactive submission creates
        // a pause/wake race: Qt can enqueue many latest-only geometry updates
        // while the link is still parked, leaving one stale frame on screen.
        // The deadline is refreshed by every input sample and expires shortly
        // after the final sample, so idle images still use one-shot rendering.
        interactiveKeepAliveUntil = interactive
                ? pendingRequestTimestamp + 0.16
                : 0.0;
        pendingRenderGeneration = ++state.requestedRenderGeneration;
        if (displayLink && displayLink.paused)
            displayLink.paused = NO;
    }

    void renderDisplayLinkUpdate(CAMetalDisplayLinkUpdate *update)
    {
        ++state.displayLinkCallbackCount;
        syncPresentationDiagnostics();
        const bool keepAlive = pendingInteractive
                && interactiveKeepAliveUntil > CACurrentMediaTime();
        if (!renderPending && !keepAlive)
            return;
        // Do not pause from an idle callback: that callback can race a request
        // that has just resumed the link. A successful submission below pauses
        // atomically with consuming its exact pending generation; the next Qt
        // request then performs the only wake-up.
        // Two frames may overlap CPU encoding, GPU execution and scanout. A
        // one-frame completion gate serialized those stages and capped a fast
        // GPU at roughly 30 fps. Pending geometry is still latest-only.
        if (frameFlow && frameFlow->framesInFlight.load() >= 2) {
            ++state.deferredDisplayLinkCallbackCount;
            return;
        }

        const QSize viewportSize = pendingViewportSize;
        const QPolygonF corners = pendingCorners;
        const qreal linearProgress = pendingLinearProgress;
        const bool interactive = pendingInteractive;
        const CFTimeInterval requestTimestamp = pendingRequestTimestamp;
        const quint64 renderGeneration = pendingRenderGeneration;
        const bool submitted = renderToDrawable(
                update.drawable, viewportSize, corners, linearProgress,
                renderGeneration, requestTimestamp, update.targetTimestamp,
                interactive);
        if (submitted && pendingRenderGeneration == renderGeneration) {
            if (interactive)
                ++state.displayLinkInteractiveSubmissionCount;
            if (keepAlive || (pendingInteractive
                              && interactiveKeepAliveUntil > CACurrentMediaTime())) {
                // A display-link callback must receive a drawable on every
                // cadence while interaction is active. Re-submit the latest
                // geometry until the keep-alive deadline, refreshing the
                // telemetry timestamp so repeated presents are measured as
                // independent frames rather than one growing request.
                renderPending = true;
                pendingRequestTimestamp = CACurrentMediaTime();
                displayLink.paused = NO;
            } else {
                renderPending = false;
                displayLink.paused = YES;
            }
        }
    }

    bool renderToDrawable(id<CAMetalDrawable> drawable,
                          const QSize &viewportSize,
                          const QPolygonF &corners,
                          const qreal linearProgress,
                          const quint64 renderGeneration,
                          const CFTimeInterval requestTimestamp,
                          const CFTimeInterval targetTimestamp,
                          const bool interactive)
    {
        if (!state.rendererAvailable || !image || !drawable
            || viewportSize.isEmpty() || corners.size() < 4)
            return false;

        @autoreleasepool {
            syncPresentationDiagnostics();
            if (presentationState->hdrPreparationInFlight) {
                displayLink.paused = YES;
                return false;
            }

            NSScreen *screen = nativeView.window.screen ?: NSScreen.mainScreen;
            state.displayCurrentHeadroom = screen
                    ? static_cast<float>(screen.maximumExtendedDynamicRangeColorComponentValue)
                    : 1.0F;
            state.displayPotentialHeadroom = screen
                    ? static_cast<float>(
                              screen.maximumPotentialExtendedDynamicRangeColorComponentValue)
                    : 1.0F;
            bool overrideIsValid = false;
            const double overriddenHeadroom =
                    qgetenv("FOVELLE_TEST_DISPLAY_HEADROOM").toDouble(&overrideIsValid);
            state.displayHeadroomOverridden = overrideIsValid;
            if (overrideIsValid) {
                state.displayCurrentHeadroom =
                        static_cast<float>(std::max(1.0, overriddenHeadroom));
                state.displayPotentialHeadroom = state.displayCurrentHeadroom;
            }
            bool currentOverrideIsValid = false;
            const double overriddenCurrentHeadroom =
                    qgetenv("FOVELLE_TEST_DISPLAY_CURRENT_HEADROOM")
                            .toDouble(&currentOverrideIsValid);
            state.displayCurrentHeadroomOverridden = currentOverrideIsValid;
            if (currentOverrideIsValid)
                state.displayCurrentHeadroom =
                        static_cast<float>(std::max(1.0, overriddenCurrentHeadroom));
            state.displayRenderingHeadroom = static_cast<float>(
                    QVCocoaFunctions::displayHeadroomForRendering(
                            state.displayCurrentHeadroom, state.displayPotentialHeadroom,
                            state.contentHeadroom));
            state.bootstrappingEDR = state.displayCurrentHeadroom <= 1.001F
                    && state.displayRenderingHeadroom > 1.001F;
            state.transitionProgress =
                    static_cast<float>(QVCocoaFunctions::easedHDRTransition(linearProgress));
            state.targetHeadroom = static_cast<float>(QVCocoaFunctions::effectiveHDRHeadroom(
                    state.contentHeadroom, state.displayRenderingHeadroom, linearProgress));

            const CGSize requestedSize = CGSizeMake(
                    state.requestedDrawableWidth,
                    state.requestedDrawableHeight);

            [CATransaction begin];
            [CATransaction setDisableActions:YES];
            metalLayer.hidden = NO;
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 260000
            if (@available(macOS 26.0, *)) {
                // contentsHeadroom describes the pixels in the drawable, not
                // the display's potential capability. Mis-tagging a 1.8x RAW
                // image as 16x can cause automatic tone mapping to suppress its
                // real highlights.
                metalLayer.contentsHeadroom = std::max<CGFloat>(1.0, state.targetHeadroom);
                state.layerContentsHeadroom = static_cast<float>(metalLayer.contentsHeadroom);
                state.usesLayerContentsHeadroomTag = true;
            } else {
                // Earlier systems infer range from the extended-linear pixels
                // and EDR layer contract; retain the intended content target in
                // diagnostics without claiming the unavailable CALayer tag.
                state.layerContentsHeadroom = state.targetHeadroom;
                state.usesLayerContentsHeadroomTag = false;
            }
#else
            // SDK15 has no CALayer contentsHeadroom declaration. Earlier
            // systems infer range from the extended-linear pixels and EDR
            // layer contract; keep the intended target in diagnostics.
            state.layerContentsHeadroom = state.targetHeadroom;
            state.usesLayerContentsHeadroomTag = false;
#endif
            [CATransaction commit];

            const bool needsManagedPreparation = state.displayRenderingHeadroom > 1.001F;
            const bool preparedEndpointsAvailable = presentationState->hdrPrepared
                    && preparedSDRImage && preparedHDRImage;

            // A full-resolution RAW or adaptive-HDR graph may evaluate its
            // tiles lazily. Never reveal that first evaluation: it can contain
            // only the already-resolved top-left tiles even though its extent
            // and drawable dimensions are correct. Keep the complete Qt SDR
            // proxy visible while Core Image owns and warms full-frame float
            // intermediates, then make the first visible Metal frame from the
            // prepared graph.
            if (needsManagedPreparation && !preparedEndpointsAvailable) {
                if (presentationState->hdrPrepared) {
                    clearPreparedImages();
                    presentationState->hdrPrepared = NO;
                }
                scheduleHDRPreparation(viewportSize, corners, requestedSize);
                displayLink.paused = YES;
                syncPresentationDiagnostics();
                return false;
            }
            if (!needsManagedPreparation && !presentationState->hdrPrepared)
                scheduleHDRPreparation(viewportSize, corners, requestedSize);

            id<MTLCommandBuffer> commandBuffer = [commandQueue commandBuffer];
            if (!commandBuffer)
                return false;
            const CGSize actualSize = CGSizeMake(drawable.texture.width, drawable.texture.height);
            state.actualTextureWidth = static_cast<int>(drawable.texture.width);
            state.actualTextureHeight = static_cast<int>(drawable.texture.height);
            state.drawableGeometryMatches =
                    state.actualTextureWidth == state.requestedDrawableWidth
                    && state.actualTextureHeight == state.requestedDrawableHeight;
            if (!state.drawableGeometryMatches)
                return false;

            const bool preparedEndpointsActive = presentationState->hdrPrepared
                    && preparedSDRImage && preparedHDRImage;
            state.preparedGeometryActive = preparedEndpointsActive;
            if (needsManagedPreparation && !preparedEndpointsActive)
                return false;
            CIImage *source = preparedEndpointsActive
                    ? preparedDisplayImage(state.targetHeadroom, state.transitionProgress)
                    : displayImage(*image, state.targetHeadroom, state.transitionProgress);
            source = imageForTexture(source, viewportSize, corners, actualSize);
            if (!source)
                return false;

            const CGRect destinationBounds = CGRectMake(
                    0, 0, actualSize.width, actualSize.height);
            const bool finalHeadroom = linearProgress >= 0.999;
            revealAfterPresentation(drawable, commandBuffer, finalHeadroom);
            state.submittedRenderGeneration = renderGeneration;
            const auto submittedFlow = frameFlow;
            const auto presentationCallTimestamp =
                    std::make_shared<std::atomic<double>>(0.0);
            if (submittedFlow)
                submittedFlow->framesInFlight.fetch_add(1);
            [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> completedBuffer) {
                if (submittedFlow) {
                    const double gpuStart = completedBuffer.GPUStartTime;
                    const double gpuEnd = completedBuffer.GPUEndTime;
                    if (gpuEnd >= gpuStart && gpuStart > 0.0) {
                        submittedFlow->lastGPUExecutionMilliseconds.store(
                                (gpuEnd - gpuStart) * 1000.0);
                    }
                    submittedFlow->framesInFlight.fetch_sub(1);
                }
            }];
            if ([drawable respondsToSelector:@selector(addPresentedHandler:)]) {
                const bool presentationLogging =
                        qEnvironmentVariableIsSet("FOVELLE_HDR_DIAGNOSTIC_LOG")
                        || qEnvironmentVariableIsSet("FOVELLE_HDR_PRESENTATION_LOG");
                [drawable addPresentedHandler:^(id<MTLDrawable> presentedDrawable) {
                    if (!submittedFlow)
                        return;
                    const double reportedPresentedTime = presentedDrawable.presentedTime;
                    // Some CAMetalDrawable implementations report zero even
                    // inside addPresentedHandler. The handler invocation itself
                    // is the documented post-presentation observation point,
                    // so use its monotonic timestamp as the auditable fallback.
                    const double presentedTime = reportedPresentedTime > 0.0
                            ? reportedPresentedTime : CACurrentMediaTime();
                    const double previousTime = submittedFlow->lastPresentedTime.exchange(
                            presentedTime);
                    const double intervalMilliseconds = previousTime > 0.0
                            ? (presentedTime - previousTime) * 1000.0 : 0.0;
                    const double requestToPresentationMilliseconds =
                            std::max(0.0, (presentedTime - requestTimestamp) * 1000.0);
                    submittedFlow->lastPresentedIntervalMilliseconds.store(
                            intervalMilliseconds);
                    submittedFlow->lastRequestToPresentationMilliseconds.store(
                            requestToPresentationMilliseconds);
                    const quint64 presentedCount =
                            submittedFlow->presentedFrameCount.fetch_add(1) + 1;
                    const double presentationCallTime =
                            presentationCallTimestamp->load();
                    const bool missedDeadline = targetTimestamp > 0.0
                            && presentationCallTime > targetTimestamp;
                    if (missedDeadline)
                        submittedFlow->missedTargetDeadlineCount.fetch_add(1);
                    if (presentationLogging) {
                        std::fprintf(stderr,
                                "FOVELLE_PRESENT {\"generation\":%llu,"
                                "\"presented_count\":%llu,\"interactive\":%s,"
                                "\"final_headroom\":%s,\"interval_ms\":%.3f,"
                                "\"request_to_present_ms\":%.3f,"
                                "\"missed_target\":%s}\n",
                                static_cast<unsigned long long>(renderGeneration),
                                static_cast<unsigned long long>(presentedCount),
                                interactive ? "true" : "false",
                                finalHeadroom ? "true" : "false",
                                intervalMilliseconds,
                                requestToPresentationMilliseconds,
                                missedDeadline ? "true" : "false");
                    }
                }];
            }
            // Complete encoding, present, and commit before returning from the
            // display-link callback. Running the CI work synchronously on the
            // dedicated serial queue keeps it off the AppKit thread without
            // introducing the post-callback scheduling bubble that misses the
            // display link's targetTimestamp deadline.
            CIImage *encodedSource = source;
            id<CAMetalDrawable> encodedDrawable = drawable;
            id<MTLCommandBuffer> encodedCommandBuffer = commandBuffer;
            CIContext *renderContext = context;
            CGColorSpaceRef renderColorSpace = CGColorSpaceRetain(outputColorSpace);
            dispatch_sync(renderQueue, ^{
                QElapsedTimer timer;
                timer.start();
                [renderContext render:encodedSource
                          toMTLTexture:encodedDrawable.texture
                         commandBuffer:encodedCommandBuffer
                                bounds:destinationBounds
                            colorSpace:renderColorSpace];
                // Presentation is ordered after every Core Image command
                // encoded in this command buffer. A drawable supplied by
                // CAMetalDisplayLink must use ordinary present; targetTimestamp
                // is the deadline by which present needs to be called, not a
                // time to pass to a timed-presentation selector.
                presentationCallTimestamp->store(CACurrentMediaTime());
                [encodedCommandBuffer presentDrawable:encodedDrawable];
                [encodedCommandBuffer commit];
                const double elapsedMilliseconds =
                        timer.nsecsElapsed() / 1000000.0;
                {
                    const std::lock_guard<std::mutex> lock(telemetryMutex);
                    state.lastRenderMilliseconds = elapsedMilliseconds;
                    ++state.renderCount;
                }
                CGColorSpaceRelease(renderColorSpace);
            });
            syncPresentationDiagnostics();
            if (finalHeadroom && presentationState->hdrPrepared)
                schedulePersistentSurfacePreparation();
            return true;
        }
        return false;
    }

    HDRRendererDiagnostics diagnostics()
    {
        const std::lock_guard<std::mutex> lock(telemetryMutex);
        syncPresentationDiagnostics();
        return state;
    }

    NSView *nativeView{ nil };
    CALayer *presentationContainerLayer{ nil };
    CALayer *viewportBackgroundLayer{ nil };
    CALayer *persistentImageLayer{ nil };
    CAMetalLayer *metalLayer{ nil };
    CALayer *navigationOverlayLayer{ nil };
    CAShapeLayer *navigationBackgroundLayers[2]{ nil, nil };
    CAShapeLayer *navigationChevronLayers[2]{ nil, nil };
    CAMetalDisplayLink *displayLink{ nil };
    QVHDRDisplayLinkDelegate *displayLinkDelegate{ nil };
    id<MTLDevice> device{ nil };
    id<MTLCommandQueue> commandQueue{ nil };
    dispatch_queue_t renderQueue{ nullptr };
    dispatch_queue_t persistentSurfaceQueue{ nullptr };
    CIContext *context{ nil };
    CIContext *persistentContext{ nil };
    CGColorSpaceRef outputColorSpace{ nullptr };
    CGColorSpaceRef backgroundColorSpace{ nullptr };
    QColor backgroundColor{ Qv::viewportBackgroundColor(Qv::Theme::Light) };
    QVHDRPresentationState *presentationState{ nil };
    std::shared_ptr<HDRFrameFlowState> frameFlow{ std::make_shared<HDRFrameFlowState>() };
    std::shared_ptr<HDRPersistentSurfaceGate> persistentSurfaceGate{
        std::make_shared<HDRPersistentSurfaceGate>()
    };
    CGImageRef persistentImage{ nullptr };
    bool persistentSurfaceReady{ false };
    bool persistentSurfacePreparationInFlight{ false };
    bool presentationActiveRequested{ true };
    bool presentationAnimationInFlight{ false };
    quint64 presentationTransitionGeneration{ 0 };
    NSUInteger persistentSurfacePreparationGeneration{ 0 };
    QSize latestViewportSize;
    QPolygonF latestCorners;
    id<MTLTexture> preparationTexture{ nil };
    CIImage *preparedSDRImage{ nil };
    CIImage *preparedHDRImage{ nil };
    std::shared_ptr<const NativeHDRImage> image;
    bool renderPending{ false };
    QSize pendingViewportSize;
    QPolygonF pendingCorners;
    qreal pendingLinearProgress{ 0.0 };
    bool pendingInteractive{ false };
    CFTimeInterval pendingRequestTimestamp{ 0.0 };
    CFTimeInterval interactiveKeepAliveUntil{ 0.0 };
    quint64 pendingRenderGeneration{ 0 };
    std::mutex telemetryMutex;
    HDRRendererDiagnostics state;
};

QVCocoaFunctions::HDRRenderer::HDRRenderer(QWidget *viewport)
    : impl(std::make_unique<Impl>(viewport))
{
}

QVCocoaFunctions::HDRRenderer::~HDRRenderer() = default;

bool QVCocoaFunctions::HDRRenderer::isAvailable() const
{
    return impl && impl->state.rendererAvailable;
}

bool QVCocoaFunctions::HDRRenderer::setImage(const HDRImagePtr &image)
{
    return impl && impl->setImage(image);
}

void QVCocoaFunctions::HDRRenderer::setBackgroundColor(const QColor &color)
{
    if (impl)
        impl->setBackgroundColor(color);
}

void QVCocoaFunctions::HDRRenderer::setPresentationActive(const bool active,
                                                          const bool animated)
{
    if (impl)
        impl->setPresentationActive(active, animated);
}

void QVCocoaFunctions::HDRRenderer::invalidateGeometry()
{
    if (impl)
        impl->invalidateGeometry();
}

void QVCocoaFunctions::HDRRenderer::clear()
{
    if (impl)
        impl->setImage({});
}

void QVCocoaFunctions::HDRRenderer::render(const QSize &viewportSize, const QPolygonF &imageCorners,
                                           const qreal transitionProgress,
                                           const bool interactive)
{
    if (impl)
        impl->render(viewportSize, imageCorners, transitionProgress, interactive);
}

void QVCocoaFunctions::HDRRenderer::setNavigationOverlay(
        const int index, const QRectF &viewportRect, const qreal opacity,
        const bool previous, const bool darkBackground, const bool hovered,
        const bool pressed, const bool enabled)
{
    if (impl)
        impl->setNavigationOverlay(index, viewportRect, opacity, previous,
                                   darkBackground, hovered, pressed, enabled);
}

void QVCocoaFunctions::HDRRenderer::clearNavigationOverlays()
{
    if (impl)
        impl->clearNavigationOverlays();
}

QVCocoaFunctions::HDRRendererDiagnostics QVCocoaFunctions::HDRRenderer::diagnostics() const
{
    return impl ? impl->diagnostics() : HDRRendererDiagnostics{};
}

qreal QVCocoaFunctions::easedHDRTransition(const qreal progress)
{
    const qreal bounded = std::clamp(progress, 0.0, 1.0);
    return bounded * bounded * (3.0 - 2.0 * bounded);
}

qreal QVCocoaFunctions::effectiveHDRHeadroom(const qreal contentHeadroom,
                                             const qreal displayHeadroom,
                                             const qreal transitionProgress)
{
    const qreal safeDisplay = std::max(1.0, displayHeadroom);
    const qreal safeContent = contentHeadroom > 0.0 ? std::max(1.0, contentHeadroom) : safeDisplay;
    const qreal available = std::min(safeContent, safeDisplay);
    return 1.0 + (available - 1.0) * easedHDRTransition(transitionProgress);
}

qreal QVCocoaFunctions::resolvedHDRContentHeadroom(const qreal reportedHeadroom,
                                                   const qreal measuredMaximumComponent)
{
    if (std::isfinite(reportedHeadroom) && reportedHeadroom > 0.0)
        return std::max(1.0, reportedHeadroom);
    if (std::isfinite(measuredMaximumComponent) && measuredMaximumComponent > 0.0)
        return std::max(1.0, measuredMaximumComponent);
    return 1.0;
}

qreal QVCocoaFunctions::displayHeadroomForRendering(const qreal currentHeadroom,
                                                     const qreal potentialHeadroom,
                                                     const qreal contentHeadroom)
{
    const qreal safeCurrent = std::max(1.0, currentHeadroom);
    const qreal safePotential = std::max(1.0, potentialHeadroom);
    if (safeCurrent > 1.001 || safePotential <= 1.001)
        return std::min(safeCurrent, safePotential);

    const qreal safeContent = contentHeadroom > 1.001
            ? contentHeadroom
            : safePotential;
    return std::min(safeContent, safePotential);
}

bool QVCocoaFunctions::isFinalHDRFrameReadyForReveal(
        const bool drawableGeometryMatches, const qreal transitionProgress)
{
    return drawableGeometryMatches && transitionProgress >= 0.999;
}

QTransform QVCocoaFunctions::persistentHDRLayerTransform(
        const QSizeF &sourceSize, const QPolygonF &imageCorners)
{
    if (sourceSize.width() <= 0.0 || sourceSize.height() <= 0.0
        || imageCorners.size() < 4)
        return {};

    const QPointF topLeft = imageCorners.at(0);
    const QPointF topRight = imageCorners.at(1);
    const QPointF bottomLeft = imageCorners.at(3);
    return QTransform(
            (topRight.x() - topLeft.x()) / sourceSize.width(),
            (topRight.y() - topLeft.y()) / sourceSize.width(),
            0.0,
            (bottomLeft.x() - topLeft.x()) / sourceSize.height(),
            (bottomLeft.y() - topLeft.y()) / sourceSize.height(),
            0.0,
            topLeft.x(), topLeft.y(), 1.0);
}

bool QVCocoaFunctions::persistentHDRLayerGeometryFlipped()
{
    return false;
}

QVCocoaFunctions::HDRPixelStatistics
QVCocoaFunctions::probeHDRPixelStatistics(const HDRImagePtr &opaqueImage)
{
    HDRPixelStatistics statistics;
    const auto nativeImage = std::dynamic_pointer_cast<const NativeHDRImage>(opaqueImage);
    if (!nativeImage)
        return statistics;

    @autoreleasepool {
        CGColorSpaceRef colorSpace = colorSyncDisplayP3ColorSpace(true);
        CIContext *probeContext = metalCIContext(colorSpace, colorSpace);
        if (!colorSpace || !probeContext) {
            if (colorSpace)
                CGColorSpaceRelease(colorSpace);
            return statistics;
        }

        const bool sdrValid = maximumCIImageRGBComponent(
                nativeImage->sdrCIImage(), probeContext, colorSpace,
                statistics.sdrMaximumComponent);
        const bool hdrValid = maximumCIImageRGBComponent(
                nativeImage->hdrCIImage(), probeContext, colorSpace,
                statistics.hdrMaximumComponent);
        statistics.valid = sdrValid && hdrValid;
        [probeContext clearCaches];
        CGColorSpaceRelease(colorSpace);
    }
    return statistics;
}

static void hideMenuShortcuts(QMenu *menu)
{
    for (QAction *action : menu->actions()) {
        action->setShortcutVisibleInContextMenu(false);
        if (action->menu())
            hideMenuShortcuts(action->menu());
    }
}

static void popUpNativeContextMenuAfterRelease(QMenu *menu,
                                               const NSPoint screenPoint)
{
    if (!menu)
        return;
    if (QGuiApplication::mouseButtons().testFlag(Qt::RightButton)) {
        // Qt < 6.8 has no configurable release trigger. Keep that build
        // compatible by waiting for its real release instead of manufacturing
        // one. On Qt 6.8+ the release trigger makes this branch unnecessary.
        QTimer::singleShot(8, menu, [menu, screenPoint]() {
            popUpNativeContextMenuAfterRelease(menu, screenPoint);
        });
        return;
    }
    [menu->toNSMenu() popUpMenuPositioningItem:nil
                                   atLocation:screenPoint
                                       inView:nil];
}

void QVCocoaFunctions::showMenu(QMenu *menu)
{
    if (!menu)
        return;
    hideMenuShortcuts(menu);
    const NSPoint screenPoint = NSEvent.mouseLocation;
    popUpNativeContextMenuAfterRelease(menu, screenPoint);
}

void QVCocoaFunctions::setUserDefaults()
{
    [[NSUserDefaults standardUserDefaults] setBool:NO forKey:@"NSFullScreenMenuItemEverywhere"];
}

void QVCocoaFunctions::registerWillPowerOffObserver()
{
    [[[NSWorkspace sharedWorkspace] notificationCenter]
        addObserverForName:NSWorkspaceWillPowerOffNotification
        object:nil
        queue:[NSOperationQueue mainQueue]
        usingBlock:^(__unused NSNotification *notification) {
            if (qvApp)
                qvApp->onSystemInitiatedQuit();
        }];
}

// This function should only be enabled once because it sets observers
void QVCocoaFunctions::setFullSizeContentView(QWidget *window, const bool enable)
{
    auto *view = reinterpret_cast<NSView*>(window->winId());

    // Make sure the requested state isn't already in effect
    if (enable == (view.window.styleMask & NSWindowStyleMaskFullSizeContentView))
        return;

    // Enable only if this Qt and macOS version combination is already using layer-backed view
    if (enable && !view.wantsLayer)
        return;

    // Changing the style mask causes the window to resize, so snapshot the original size
    NSRect originalFrame = view.window.frame;

#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    Qv::alterWindowFlags(window, [&](Qt::WindowFlags f) { return f.setFlag(Qt::ExpandedClientAreaHint, enable); });
#else
    if (enable)
        view.window.styleMask |= NSWindowStyleMaskFullSizeContentView;
    else
        view.window.styleMask &= ~NSWindowStyleMaskFullSizeContentView;
#endif

    // Restore original size after style mask change
    [view.window setFrame:originalFrame display:YES];
}

bool QVCocoaFunctions::getTitlebarHidden(const QWidget *window)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    return window->windowFlags().testFlags(Qt::NoTitleBarBackgroundHint);
#else
    auto *view = reinterpret_cast<NSView*>(window->winId());
    return view.window.titleVisibility == NSWindowTitleHidden;
#endif
}

void QVCocoaFunctions::setTitlebarHidden(QWidget *window, const bool hide)
{
    auto *view = reinterpret_cast<NSView*>(window->winId());
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    Qv::alterWindowFlags(window, [&](Qt::WindowFlags f) { return f.setFlag(Qt::NoTitleBarBackgroundHint, hide); });
#else
    view.window.titlebarAppearsTransparent = hide;
#endif
    view.window.titleVisibility = hide ? NSWindowTitleHidden : NSWindowTitleVisible;
}

void QVCocoaFunctions::setWindowCollectionBehaviorManaged(QWidget *window)
{
    auto *view = reinterpret_cast<NSView*>(window->winId());
    view.window.collectionBehavior =
        (view.window.collectionBehavior | NSWindowCollectionBehaviorManaged) &
        ~(NSWindowCollectionBehaviorTransient | NSWindowCollectionBehaviorStationary);
}

void QVCocoaFunctions::setWindowTheme(const Qv::Theme theme, QWindow *window)
{
    if (!window)
        return;

    auto *view = reinterpret_cast<NSView*>(window->winId());
    const NSAppearanceName appearanceName = theme == Qv::Theme::Dark ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua;
    [view.window setAppearance:[NSAppearance appearanceNamed:appearanceName]];
}

QString QVCocoaFunctions::getWindowAppearanceName(const QWindow *window)
{
    if (!window)
        return {};

    auto *view = reinterpret_cast<NSView*>(window->winId());
    NSString *name = view.window.effectiveAppearance.name;
    if ([name isEqualToString:NSAppearanceNameAqua])
        return QStringLiteral("Aqua");
    if ([name isEqualToString:NSAppearanceNameDarkAqua])
        return QStringLiteral("DarkAqua");
    return name ? QString::fromUtf8(name.UTF8String) : QString();
}

int QVCocoaFunctions::getObscuredHeight(QWindow *window)
{
    if (!window)
        return 0;

    auto *view = reinterpret_cast<NSView*>(window->winId());

    if (view.window.titlebarAppearsTransparent)
        return 0;

    int visibleHeight = view.window.contentLayoutRect.size.height;
    int totalHeight = view.window.contentView.frame.size.height;

    return totalHeight - visibleHeight;
}

bool QVCocoaFunctions::startWindowDrag(QWindow *window)
{
    if ((NSEvent.pressedMouseButtons & 1) == 0)
        return false;
    NSView *view = reinterpret_cast<NSView*>(window->winId());
    NSPoint startPoint = view.window.mouseLocationOutsideOfEventStream;
    NSEvent *event = [NSEvent mouseEventWithType:NSEventTypeLeftMouseDown location:startPoint modifierFlags:0
        timestamp:0 windowNumber:view.window.windowNumber context:nil eventNumber:0 clickCount:1 pressure:1];
    [view.window performWindowDragWithEvent:event];
    return true;
}

void QVCocoaFunctions::setWindowMenu(QMenu *menu)
{
    NSMenu *nativeMenu = menu->toNSMenu();
    [nativeMenu addItemWithTitle:@"Minimize" action:@selector(performMiniaturize:) keyEquivalent:@"m"];

    [nativeMenu addItemWithTitle:@"Zoom" action:@selector(performZoom:) keyEquivalent:@""];
    [[NSApplication sharedApplication] setWindowsMenu:nativeMenu];
}

void QVCocoaFunctions::setAlternate(QMenu *menu, int index)
{
    NSMenu *nativeMenu = menu->toNSMenu();
    [[nativeMenu.itemArray objectAtIndex:index] setAlternate:true];
}

void QVCocoaFunctions::setDockRecents(const QStringList &recentPathsList)
{
    NSDocumentController *documentController = [NSDocumentController sharedDocumentController];

    [documentController clearRecentDocuments:nil];

    if (recentPathsList.length() == 0)
        return;

    // The dispatch_async wrapper is a workaround for the first document not getting added after a call
    // to clear the documents. We just introduce a slight processing delay which is enough to fix it.
    const QStringList paths = recentPathsList;
    dispatch_async(dispatch_get_main_queue(), ^{
        for (int i = paths.size() - 1; i >= 0; i--)
        {
            const auto &path = paths[i];
            auto url = QUrl::fromLocalFile(path);
            [documentController noteNewRecentDocumentURL:url.toNSURL()];
        }
    });
}

QList<OpenWith::OpenWithItem> QVCocoaFunctions::getOpenWithItems(const QString &filePath, const bool loadIcons, const QString &defaultSuffix)
{
    auto fileUrl = QUrl(filePath);
    fileUrl.setScheme("file");

    NSString *utiType = nil;
    NSError *error = nil;
    BOOL success = [fileUrl.toNSURL() getResourceValue:&utiType forKey:NSURLTypeIdentifierKey error:&error];

    if (!success)
    {
        NSLog(@"getResourceValue:forKey:error: returned error == %@", error);
        return QList<OpenWith::OpenWithItem>();
    }

    NSArray *supportedApplications = [(NSArray *)LSCopyAllRoleHandlersForContentType((CFStringRef)utiType, kLSRolesAll) autorelease];
    NSString *defaultApplication = [(NSString *)LSCopyDefaultRoleHandlerForContentType((CFStringRef)utiType, kLSRolesAll) autorelease];

    QList<OpenWith::OpenWithItem> listOfOpenWithItems;
    for (NSString *appId in supportedApplications)
    {
        if ([appId isEqualToString:@"io.github.inostarlin-passion.Fovelle"] || [appId isEqualToString:@""])
            continue;

        OpenWith::OpenWithItem openWithItem;
        openWithItem.exec = "open";
        openWithItem.args.append({"-b", QString::fromNSString(appId)});

        NSURL *appURL = [[NSWorkspace sharedWorkspace] URLForApplicationWithBundleIdentifier:appId];
        if (!appURL)
            continue;
        NSString *absolutePath = appURL.path;

        NSString *appName = [[NSFileManager defaultManager] displayNameAtPath:absolutePath];
        openWithItem.name = QString::fromNSString(appName);

        if (loadIcons)
        {
            QFileIconProvider fiProvider;
            QIcon icon = fiProvider.icon(QFileInfo(QString::fromNSString(absolutePath)));
            openWithItem.icon = ActionManager::getCacheableIcon("application:" + QString::fromNSString(appId), icon);
        }

        // If the program is the default program, save it to add to the beginning after sorting
        if ([appId isEqualToString:defaultApplication])
        {
            openWithItem.isDefault = true;
            openWithItem.name += defaultSuffix;
        }

        listOfOpenWithItems.append(openWithItem);
    }

    return listOfOpenWithItems;
}

QByteArray QVCocoaFunctions::getIccProfileForWindow(const QWindow *window)
{
    NSView *view = reinterpret_cast<NSView*>(window->winId());
    NSColorSpace *nsColorSpace = view.window.colorSpace;
    if (nsColorSpace)
    {
        NSData *iccProfileData = nsColorSpace.ICCProfileData;
        if (iccProfileData)
        {
            return QByteArray::fromNSData(iccProfileData);
        }
    }
    return {};
}

QList<QByteArray> QVCocoaFunctions::getAdditionalImageFormats()
{
    QList<QByteArray> formats;
    for (const auto identifier : imageIOTypeIdentifiers())
    {
        for (const auto &format : typeTags(identifier, UTTagClassFilenameExtension)) {
            if (!formats.contains(format))
                formats.append(format);
        }
        CFRelease(identifier);
    }

    // EPS is a document UTI rather than an Image I/O image-source UTI on
    // current macOS releases. The native bridge renders its PostScript program
    // through Ghostscript, so expose the conventional filename aliases through
    // the same registry consumed by Settings and folder enumeration.
    for (const QByteArray &format : {QByteArrayLiteral("eps"),
                                     QByteArrayLiteral("epsf"),
                                     QByteArrayLiteral("epsi")})
    {
        if (!formats.contains(format))
            formats.append(format);
    }
    return formats;
}

QList<QString> QVCocoaFunctions::getAdditionalImageMimeTypes()
{
    QList<QString> mimeTypes;
    for (const auto identifier : imageIOTypeIdentifiers())
    {
        for (const auto &mimeType : typeTags(identifier, UTTagClassMIMEType)) {
            const QString value = QString::fromUtf8(mimeType);
            if (!value.isEmpty() && !mimeTypes.contains(value))
                mimeTypes.append(value);
        }
        CFRelease(identifier);
    }
    return mimeTypes;
}

bool QVCocoaFunctions::supportsAdditionalImageFormat(const QByteArray &format)
{
    QByteArray normalized = normalizedExtension(format);
    if (normalized == "avifs")
        normalized = "avif";
    if (normalized == "eps" || normalized == "epsf" || normalized == "epsi")
        return true;
    for (const auto identifier : imageIOTypeIdentifiers())
    {
        const bool supported =
                typeTags(identifier, UTTagClassFilenameExtension).contains(normalized);
        CFRelease(identifier);
        if (supported)
        {
            return true;
        }
    }

    return false;
}

QVCocoaFunctions::NativeImageReadResult
QVCocoaFunctions::readImageWithImageIO(const QString &filePath, const int fallbackLargestDimension)
{
    NativeImageReadResult result;

    @autoreleasepool
    {
        const EPSReadResult epsResult = readEPS(filePath, fallbackLargestDimension);
        if (epsResult.recognized)
        {
            result.image = epsResult.image;
            result.intrinsicSize = epsResult.intrinsicSize;
            result.typeIdentifier = QStringLiteral("com.adobe.encapsulated-postscript");
            // Keep the result on the native bridge path. This prevents the
            // asynchronous loader from interpreting a placement preview after
            // either a successful render or an actionable renderer failure.
            result.isImageIOType = true;
            result.allowsQtFallback = false;
            result.hdrMetadata.typeIdentifier = result.typeIdentifier;
            result.hdrMetadata.pixelSize = result.intrinsicSize;
            result.errorString = epsResult.errorString;
            return result;
        }

        const QUrl fileUrl = QUrl::fromLocalFile(filePath);
        CGImageSourceRef source = CGImageSourceCreateWithURL((CFURLRef)fileUrl.toNSURL(), nullptr);
        if (!source)
        {
            result.errorString = QStringLiteral("Image I/O could not create an image source");
            return result;
        }

        const CFStringRef sourceType = CGImageSourceGetType(source);
        result.typeIdentifier = QStringFromCFString(sourceType);
        result.isImageIOType = sourceType != nullptr;
        result.isRaw = isRawImageType(sourceType);
        const CGImagePropertyOrientation orientation = sourceOrientation(source);
        result.intrinsicSize = orientedPixelSize(sourcePixelSize(source), orientation);
        result.hdrMetadata.typeIdentifier = result.typeIdentifier;
        result.hdrMetadata.isRaw = result.isRaw;
        result.hdrMetadata.hasAppleGainMap =
                hasAuxiliaryImage(source, kCGImageAuxiliaryDataTypeHDRGainMap);
        if (@available(macOS 15.0, *))
            result.hdrMetadata.hasISOGainMap =
                    hasAuxiliaryImage(source, kCGImageAuxiliaryDataTypeISOGainMap);

        // Read the source properties through Image I/O before decoding. This
        // keeps orientation, color profile and camera metadata on the native
        // path instead of guessing from the filename.
        if (const CFDictionaryRef sourceProperties = CGImageSourceCopyProperties(source, nullptr))
            CFRelease(sourceProperties);

        if (result.isRaw)
        {
            CIRAWFilter *sdrRawFilter = nil;
            CIRAWFilter *hdrRawFilter = nil;
            if (@available(macOS 12.0, *)) {
                sdrRawFilter = [CIRAWFilter filterWithImageURL:fileUrl.toNSURL()];
                hdrRawFilter = [CIRAWFilter filterWithImageURL:fileUrl.toNSURL()];
            }
            if (sdrRawFilter && hdrRawFilter) {
                sdrRawFilter.orientation = orientation;
                hdrRawFilter.orientation = orientation;
                sdrRawFilter.draftModeEnabled = NO;
                hdrRawFilter.draftModeEnabled = NO;
                sdrRawFilter.scaleFactor = 1.0F;
                hdrRawFilter.scaleFactor = 1.0F;
            }

            // ProRAW can carry Apple's fully rendered, full-resolution
            // thumbnail together with the Adaptive HDR gain map. macOS 15
            // Quick Look and Preview consume this public representation. It
            // preserves the camera's Smart HDR/Deep Fusion/local tone recipe,
            // which cannot be reproduced by changing one CIRAWFilter knob.
            if (@available(macOS 15.0, *)) {
                const bool hasGainMap = result.hdrMetadata.hasAppleGainMap
                        || result.hdrMetadata.hasISOGainMap;
                if (hasGainMap) {
                    NSDictionary *previewOptions = @{
                        (id)kCIImageApplyOrientationProperty : @YES,
                        (id)kCIImageCacheImmediately : @YES
                    };
                    NSDictionary *gainMapOptions = @{
                        (id)kCIImageAuxiliaryHDRGainMap : @YES,
                        (id)kCIImageApplyOrientationProperty : @YES,
                        (id)kCIImageCacheImmediately : @YES
                    };
                    // CIRAWFilter.previewImage exposes the full-resolution,
                    // camera-processed thumbnail that the DNG gain map was
                    // authored against.  Prefer it over the generic RAW output
                    // graph: the latter is close, but omits parts of the
                    // camera/ProRAW rendering recipe and therefore loses fine
                    // highlight and local-tone detail relative to Quick Look.
                    CIImage *processedSDR = sdrRawFilter.previewImage;
                    if (!processedSDR) {
                        processedSDR = [CIImage imageWithContentsOfURL:fileUrl.toNSURL()
                                                                  options:previewOptions];
                    }
                    CIImage *gainMap = [CIImage imageWithContentsOfURL:fileUrl.toNSURL()
                                                                    options:gainMapOptions];
                    CIImage *processedHDR = processedSDR && gainMap
                            ? [processedSDR imageByApplyingGainMap:gainMap]
                            : nil;
                    const float processedHeadroom = ciImageContentHeadroom(processedHDR);
                    if (processedSDR && processedHDR && processedHeadroom > 1.001F
                        && CGRectEqualToRect(processedSDR.extent, processedHDR.extent)) {
                        const CGRect extent = processedHDR.extent;
                        result.intrinsicSize = QSize(
                            static_cast<int>(std::lround(extent.size.width)),
                            static_cast<int>(std::lround(extent.size.height)));
                        result.hdrMetadata.sourceKind =
                                QStringLiteral("camera-raw-processed-gain-map");
                        result.hdrMetadata.pixelSize = result.intrinsicSize;
                        result.hdrMetadata.bitsPerComponent = 16;
                        result.hdrMetadata.contentHeadroom = processedHeadroom;
                        result.hdrMetadata.decodedToHDR = true;
                        result.hdrMetadata.usesProcessedRawPreview = true;
                        result.hdrMetadata.usedRawPreview = true;
                        result.hdrMetadata.colorSpaceName =
                                colorSpaceName(processedHDR.colorSpace);
                        result.hdrMetadata.transferFunction =
                                transferFunctionName(processedHDR.colorSpace, true);
                        result.hdrImage = std::make_shared<NativeHDRImage>(
                                processedHDR, processedSDR, result.hdrMetadata, gainMap);
                        result.usedRawPreview = true;

                        CGColorSpaceRef workingColorSpace =
                                colorSyncDisplayP3ColorSpace(true);
                        CGColorSpaceRef fallbackColorSpace =
                                colorSyncDisplayP3ColorSpace(false);
                        CIContext *context = metalCIContext(
                                workingColorSpace, fallbackColorSpace);
                        result.image = imageFromCIImage(
                                processedSDR, context, fallbackColorSpace,
                                fallbackLargestDimension);
                        if (context)
                            [context clearCaches];
                        if (workingColorSpace)
                            CGColorSpaceRelease(workingColorSpace);
                        if (fallbackColorSpace)
                            CGColorSpaceRelease(fallbackColorSpace);
                    }
                }
            }

            if (!result.hdrImage && sdrRawFilter && hdrRawFilter)
            {
                // Other RAW formats retain independent, immutable Apple RAW
                // recipes. Preserve every camera default, including negative
                // BaselineExposure; changing that property alone flattens
                // highlight detail while leaving the rest of the tone recipe.
                sdrRawFilter.extendedDynamicRangeAmount = 0.0F;
                hdrRawFilter.extendedDynamicRangeAmount = 1.0F;
                CIImage *sdrImage = sdrRawFilter.outputImage;
                CIImage *hdrImage = hdrRawFilter.outputImage;
                if (hdrImage) {
                    const CGRect extent = hdrImage.extent;
                    if (!CGRectIsEmpty(extent)) {
                        result.intrinsicSize =
                                QSize(static_cast<int>(std::lround(extent.size.width)),
                                      static_cast<int>(std::lround(extent.size.height)));
                    }

                    result.hdrMetadata.sourceKind = QStringLiteral("camera-raw");
                    result.hdrMetadata.pixelSize = result.intrinsicSize;
                    result.hdrMetadata.bitsPerComponent = 16;
                    result.hdrMetadata.decodedToHDR = true;
                    result.hdrMetadata.usesRawExtendedDynamicRange = true;
                    result.hdrMetadata.colorSpaceName = colorSpaceName(hdrImage.colorSpace);
                    result.hdrMetadata.transferFunction =
                            transferFunctionName(hdrImage.colorSpace, false);

                    CGColorSpaceRef workingColorSpace = colorSyncDisplayP3ColorSpace(true);
                    CGColorSpaceRef fallbackColorSpace = colorSyncDisplayP3ColorSpace(false);
                    CIContext *context = metalCIContext(workingColorSpace, fallbackColorSpace);
                    result.image = imageFromCIImage(sdrImage, context, fallbackColorSpace,
                                                    fallbackLargestDimension);

                    // CIRAWFilter currently reports contentHeadroom == 0 for
                    // this class of camera RAW, meaning unknown. Measure the
                    // float endpoint once and attach the truthful content tag
                    // needed by CAMetalLayer/WindowServer tone mapping.
                    float measuredHeadroom = 0.0F;
                    maximumCIImageRGBComponent(hdrImage, context, workingColorSpace,
                                               measuredHeadroom);
                    result.hdrMetadata.contentHeadroom = static_cast<float>(
                            resolvedHDRContentHeadroom(
                                    ciImageContentHeadroom(hdrImage), measuredHeadroom));
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 160000
                    // CIImage content-headroom tagging was introduced after
                    // SDK15. Keep it when a newer compile SDK is selected,
                    // while the SDK15 Release build uses the same pixel graph
                    // and layer-level fallback contract.
                    if (@available(macOS 16.0, *))
                        hdrImage = [hdrImage imageBySettingContentHeadroom:
                                result.hdrMetadata.contentHeadroom];
#endif
                    result.hdrImage = std::make_shared<NativeHDRImage>(
                            hdrImage, sdrImage, result.hdrMetadata);

                    if (context)
                        [context clearCaches];
                    if (workingColorSpace)
                        CGColorSpaceRelease(workingColorSpace);
                    if (fallbackColorSpace)
                        CGColorSpaceRelease(fallbackColorSpace);
                }
            }

            if (result.image.isNull())
            {
                CIImage *rawPreview = sdrRawFilter.previewImage ?: hdrRawFilter.previewImage;
                if (rawPreview) {
                    CGColorSpaceRef workingColorSpace = colorSyncDisplayP3ColorSpace(true);
                    CGColorSpaceRef fallbackColorSpace = colorSyncDisplayP3ColorSpace(false);
                    CIContext *context = metalCIContext(workingColorSpace, fallbackColorSpace);
                    result.image = imageFromCIImage(rawPreview, context,
                                                    fallbackColorSpace, fallbackLargestDimension);
                    result.usedRawPreview = !result.image.isNull();
                    if (context)
                        [context clearCaches];
                    if (workingColorSpace)
                        CGColorSpaceRelease(workingColorSpace);
                    if (fallbackColorSpace)
                        CGColorSpaceRelease(fallbackColorSpace);
                }

                if (result.image.isNull()) {
                    if (CFDictionaryRef options =
                                thumbnailOptions(source, fallbackLargestDimension, false)) {
                        CGImageRef previewImage =
                                CGImageSourceCreateThumbnailAtIndex(source, 0, options);
                        CFRelease(options);
                        if (previewImage) {
                            result.image = imageFromCGImage(previewImage);
                            result.usedRawPreview = !result.image.isNull();
                            CGImageRelease(previewImage);
                        }
                    }
                }
            }

            result.hdrMetadata.usedRawPreview = result.usedRawPreview;

            if (result.image.isNull() && !result.hdrImage) {
                result.errorString = QStringLiteral(
                    "Core Image RAW decoder does not support this camera model and no embedded JPEG preview is available");
            }
        }
        else if (result.isImageIOType)
        {
            CGImageRef decodedImage = nullptr;
            if (CFDictionaryRef options = fullResolutionThumbnailOptions(source, true)) {
                decodedImage = CGImageSourceCreateThumbnailAtIndex(source, 0, options);
                CFRelease(options);
            }

            const bool hasGainMap =
                    result.hdrMetadata.hasAppleGainMap || result.hdrMetadata.hasISOGainMap;
            const float decodedHeadroom = cgImageContentHeadroom(decodedImage);
            CGColorSpaceRef decodedColorSpace =
                    decodedImage ? CGImageGetColorSpace(decodedImage) : nullptr;
            const bool hdrColorSpace = decodedColorSpace
                    && (CGColorSpaceIsHDR(decodedColorSpace)
                        || CGColorSpaceUsesExtendedRange(decodedColorSpace));
            const bool hdrCandidate = hasGainMap || decodedHeadroom > 1.0F || hdrColorSpace;

            if (hdrCandidate) {
                if (@available(macOS 14.0, *)) {
                    NSDictionary *hdrOptions = @{
                        (id)kCIImageExpandToHDR : @YES,
                        (id)kCIImageApplyOrientationProperty : @YES,
                        (id)kCIImageCacheImmediately : @YES
                    };
                    NSDictionary *sdrOptions = @{
                        (id)kCIImageApplyOrientationProperty : @YES,
                        (id)kCIImageCacheImmediately : @YES
                    };
                    CIImage *hdrImage = [CIImage imageWithContentsOfURL:fileUrl.toNSURL()
                                                                options:hdrOptions];
                    CIImage *sdrImage = [CIImage imageWithContentsOfURL:fileUrl.toNSURL()
                                                                options:sdrOptions];
                    if (hdrImage) {
                        const CGRect extent = hdrImage.extent;
                        if (!CGRectIsEmpty(extent)) {
                            result.intrinsicSize =
                                    QSize(static_cast<int>(std::lround(extent.size.width)),
                                          static_cast<int>(std::lround(extent.size.height)));
                        }

                        const float ciHeadroom = ciImageContentHeadroom(hdrImage);
                        result.hdrMetadata.sourceKind = hasGainMap ? QStringLiteral("adaptive-hdr")
                                                                   : QStringLiteral("iso-hdr");
                        result.hdrMetadata.pixelSize = result.intrinsicSize;
                        result.hdrMetadata.contentHeadroom =
                                ciHeadroom > 0.0F ? ciHeadroom : decodedHeadroom;
                        result.hdrMetadata.bitsPerComponent = decodedImage
                                ? static_cast<int>(CGImageGetBitsPerComponent(decodedImage))
                                : 16;
                        result.hdrMetadata.decodedToHDR = true;
                        result.hdrMetadata.colorSpaceName = colorSpaceName(hdrImage.colorSpace);
                        result.hdrMetadata.transferFunction =
                                transferFunctionName(hdrImage.colorSpace, hasGainMap);
                        result.hdrImage = std::make_shared<NativeHDRImage>(hdrImage, sdrImage,
                                                                           result.hdrMetadata);

                        CGColorSpaceRef workingColorSpace = colorSyncDisplayP3ColorSpace(true);
                        CGColorSpaceRef fallbackColorSpace = colorSyncDisplayP3ColorSpace(false);
                        CIContext *context = metalCIContext(workingColorSpace, fallbackColorSpace);
                        result.image = imageFromCIImage(sdrImage, context, fallbackColorSpace,
                                                        fallbackLargestDimension);
                        if (context)
                            [context clearCaches];
                        if (workingColorSpace)
                            CGColorSpaceRelease(workingColorSpace);
                        if (fallbackColorSpace)
                            CGColorSpaceRelease(fallbackColorSpace);
                    }
                }
            }

            if (!result.hdrImage && decodedImage)
                result.image = imageFromCGImage(decodedImage);
            if (decodedImage)
                CGImageRelease(decodedImage);

            if (result.hdrImage && result.image.isNull()) {
                if (CFDictionaryRef options =
                            thumbnailOptions(source, fallbackLargestDimension, false)) {
                    CGImageRef fallbackImage =
                            CGImageSourceCreateThumbnailAtIndex(source, 0, options);
                    CFRelease(options);
                    if (fallbackImage) {
                        result.image = imageFromCGImage(fallbackImage);
                        CGImageRelease(fallbackImage);
                    }
                }
            }

            if (result.image.isNull() && !result.hdrImage)
                result.errorString = QStringLiteral("Image I/O could not decode the image");
        }

        CFRelease(source);
    }

    return result;
}

QImage QVCocoaFunctions::readAdditionalImage(const QString &filePath, QString *errorString)
{
    const NativeImageReadResult result = readImageWithImageIO(filePath);
    if (errorString)
        *errorString = result.errorString;
    return result.image;
}

std::unique_ptr<QVCocoaFunctions::AnimatedImage> QVCocoaFunctions::createAnimatedImage(const QString &filePath)
{
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    if (suffix != QStringLiteral("png") && suffix != QStringLiteral("apng"))
        return nullptr;

    auto animatedImage = std::make_unique<NativeAnimatedImage>(filePath);
    if (!animatedImage->isValid())
        return nullptr;
    return animatedImage;
}

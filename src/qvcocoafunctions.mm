#include "actionmanager.h"
#include "qvapplication.h"
#include "qvcocoafunctions.h"

#include <QUrl>
#include <QDebug>
#include <QFile>
#include <QFileIconProvider>
#include <QCollator>
#include <QColorSpace>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <limits>

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
}

- (instancetype)initWithMetalLayer:(CAMetalLayer *)layer;
- (void)resetForImage;
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

- (void)invalidate
{
    [self resetForImage];
    [metalLayer release];
    metalLayer = nil;
}

- (void)dealloc
{
    [metalLayer release];
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

class NativeHDRImage final : public QVCocoaFunctions::HDRImage
{
public:
    NativeHDRImage(CIImage *hdrImage, CIImage *sdrImage, QVCocoaFunctions::HDRMetadata metadata)
        : hdr([hdrImage retain]), sdr([sdrImage retain]), imageMetadata(std::move(metadata))
    {
    }

    ~NativeHDRImage() override
    {
        [hdr release];
        [sdr release];
    }

    const QVCocoaFunctions::HDRMetadata &metadata() const override { return imageMetadata; }
    CIImage *hdrCIImage() const { return hdr; }
    CIImage *sdrCIImage() const { return sdr; }

private:
    CIImage *hdr{ nil };
    CIImage *sdr{ nil };
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
        outputColorSpace = colorSyncDisplayP3ColorSpace(true);
        backgroundColorSpace = colorSyncSrgbColorSpace();
        if (!commandQueue || !outputColorSpace || !backgroundColorSpace)
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
        if (!context)
            return;

        nativeView.wantsLayer = YES;
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
        if (@available(macOS 26.0, *))
            metalLayer.preferredDynamicRange = CADynamicRangeHigh;

        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        metalLayer.frame = nativeView.bounds;
        metalLayer.hidden = YES;
        metalLayer.opacity = 0.0F;
        [nativeView.layer addSublayer:metalLayer];
        [CATransaction commit];
        presentationState = [[QVHDRPresentationState alloc] initWithMetalLayer:metalLayer];

        state.rendererAvailable = presentationState != nil;
        state.usesRGBA16Float = true;
        state.usesExtendedLinearDisplayP3 = CGColorSpaceUsesExtendedRange(outputColorSpace);
        state.usesColorSync = true;
        state.wantsExtendedDynamicRangeContent = metalLayer.wantsExtendedDynamicRangeContent;
        state.clearsEntireDrawableOpaque = metalLayer.opaque;
        state.usesCoreImageManagedIntermediates = true;
        state.cachesIntermediates = true;
        setBackgroundColor(backgroundColor);
    }

    ~Impl()
    {
        [presentationState invalidate];
        clearPreparedImages();
        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        [metalLayer removeFromSuperlayer];
        [CATransaction commit];
        [presentationState release];
        [metalLayer release];
        [context release];
        [commandQueue release];
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
        [CATransaction commit];
        if (layerColor)
            CGColorRelease(layerColor);
    }

    bool setImage(const HDRImagePtr &newImage)
    {
        clearPreparedImages();
        // The context is intentionally long-lived for one interactive view,
        // but intermediates from the previous source are no longer reusable.
        // Release them only on image replacement, never on zoom or pan.
        if (context)
            [context clearCaches];
        const auto nativeImage = std::dynamic_pointer_cast<const NativeHDRImage>(newImage);
        image = nativeImage;
        [presentationState resetForImage];
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
        state.transitionProgress = 0.0F;
        state.targetHeadroom = 1.0F;
        state.renderCount = 0;
        state.lastRenderMilliseconds = 0.0;
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
        metalLayer.hidden = nativeImage == nullptr;
        metalLayer.opacity = 0.0F;
        [CATransaction commit];
        return nativeImage != nullptr && state.rendererAvailable;
    }

    void invalidateGeometry()
    {
        if (!image)
            return;

        clearPreparedImages();
        [presentationState resetForImage];
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
        metalLayer.opacity = 0.0F;
        [CATransaction commit];
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

        if (metadata.isRaw) {
            const float rawAmount = state.displayRenderingHeadroom > 1.001F ? progress : 0.0F;
            return mixImages(sdr, hdr, rawAmount);
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

        if (image->metadata().isRaw)
            return mixImages(preparedSDRImage, preparedHDRImage, progress);

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
        state.layerOpacity = metalLayer ? metalLayer.opacity : 0.0F;
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
                                 id<MTLCommandBuffer> commandBuffer)
    {
        if (presentationState->firstFrameSubmitted || !state.drawableGeometryMatches)
            return;

        presentationState->firstFrameSubmitted = YES;
        const NSUInteger frameGeneration = presentationState->generation;
        QVHDRPresentationState *gate = presentationState;
        void (^revealLayer)(void) = ^{
            if (gate->generation != frameGeneration || !gate->metalLayer)
                return;
            [CATransaction begin];
            [CATransaction setDisableActions:YES];
            gate->metalLayer.opacity = 1.0F;
            [CATransaction commit];
            gate->firstFramePresented = YES;
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
        id<MTLCommandBuffer> sdrPreparationBuffer = [commandQueue commandBuffer];
        id<MTLCommandBuffer> hdrPreparationBuffer = [commandQueue commandBuffer];
        id<MTLCommandBuffer> transitionPreparationBuffer = [commandQueue commandBuffer];
        if (!preparationTexture || !sdrPreparationBuffer || !hdrPreparationBuffer
            || !transitionPreparationBuffer) {
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
        preparedSDRImage = [[sdrSource imageByInsertingIntermediate:YES] retain];
        preparedHDRImage = [[hdrSource imageByInsertingIntermediate:YES] retain];
        if (!preparedSDRImage || !preparedHDRImage) {
            clearPreparedImages();
            return;
        }
        presentationState->hdrPreparationInFlight = YES;
        const NSUInteger preparationGeneration = presentationState->generation;
        QVHDRPresentationState *gate = presentationState;
        const CGRect bounds = CGRectMake(0, 0, textureSize.width, textureSize.height);
        CIImage *preparedSDRFrame = imageForTexture(
                preparedSDRImage, viewportSize, corners, textureSize);
        CIImage *preparedHDRFrame = imageForTexture(
                preparedHDRImage, viewportSize, corners, textureSize);
        if (!preparedSDRFrame || !preparedHDRFrame) {
            clearPreparedImages();
            presentationState->hdrPreparationInFlight = NO;
            return;
        }
        [context render:preparedSDRFrame
             toMTLTexture:preparationTexture
            commandBuffer:sdrPreparationBuffer
                   bounds:bounds
               colorSpace:outputColorSpace];
        [sdrPreparationBuffer commit];
        [context render:preparedHDRFrame
             toMTLTexture:preparationTexture
            commandBuffer:hdrPreparationBuffer
                   bounds:bounds
               colorSpace:outputColorSpace];
        [hdrPreparationBuffer commit];

        // Endpoint caching alone does not compile and schedule the dynamic
        // dissolve/tone-map portion of the graph. Warm representative ramp
        // states while the Qt proxy is still visible so shader setup and the
        // first full-texture reads cannot consume the visible transition.
        const qreal warmProgresses[]{ 0.1, 0.5, 1.0 };
        for (const qreal linearProgress : warmProgresses) {
            const float easedProgress = static_cast<float>(
                    QVCocoaFunctions::easedHDRTransition(linearProgress));
            const float targetHeadroom = static_cast<float>(
                    QVCocoaFunctions::effectiveHDRHeadroom(
                            state.contentHeadroom, state.displayRenderingHeadroom,
                            linearProgress));
            CIImage *warmImage = preparedDisplayImage(targetHeadroom, easedProgress);
            if (warmImage) {
                CIImage *warmFrame = imageForTexture(
                        warmImage, viewportSize, corners, textureSize);
                if (warmFrame) {
                    [context render:warmFrame
                         toMTLTexture:preparationTexture
                        commandBuffer:transitionPreparationBuffer
                               bounds:bounds
                           colorSpace:outputColorSpace];
                }
            }
        }
        [transitionPreparationBuffer addCompletedHandler:^(id<MTLCommandBuffer> completedBuffer) {
            const BOOL completed = completedBuffer.status == MTLCommandBufferStatusCompleted;
            dispatch_async(dispatch_get_main_queue(), ^{
                if (gate->generation != preparationGeneration)
                    return;
                gate->hdrPreparationInFlight = NO;
                gate->hdrPrepared = completed;
            });
        }];
        [transitionPreparationBuffer commit];
    }

    void render(const QSize &viewportSize, const QPolygonF &corners, const qreal linearProgress)
    {
        if (!state.rendererAvailable || !image || viewportSize.isEmpty() || corners.size() < 4)
            return;

        @autoreleasepool {
            syncPresentationDiagnostics();
            if (presentationState->hdrPreparationInFlight)
                return;

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

            const CGFloat backingScale = nativeView.window
                    ? nativeView.window.backingScaleFactor
                    : (screen ? screen.backingScaleFactor : 1.0);
            const CGSize requestedSize =
                    CGSizeMake(std::max<CGFloat>(1.0, viewportSize.width() * backingScale),
                               std::max<CGFloat>(1.0, viewportSize.height() * backingScale));
            state.requestedDrawableWidth = static_cast<int>(std::lround(requestedSize.width));
            state.requestedDrawableHeight = static_cast<int>(std::lround(requestedSize.height));

            [CATransaction begin];
            [CATransaction setDisableActions:YES];
            metalLayer.frame = nativeView.bounds;
            metalLayer.contentsScale = backingScale;
            metalLayer.drawableSize = requestedSize;
            metalLayer.hidden = NO;
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
                syncPresentationDiagnostics();
                return;
            }
            if (!needsManagedPreparation && !presentationState->hdrPrepared)
                scheduleHDRPreparation(viewportSize, corners, requestedSize);

            id<CAMetalDrawable> drawable = [metalLayer nextDrawable];
            id<MTLCommandBuffer> commandBuffer = [commandQueue commandBuffer];
            if (!drawable || !commandBuffer)
                return;
            const CGSize actualSize = CGSizeMake(drawable.texture.width, drawable.texture.height);
            state.actualTextureWidth = static_cast<int>(drawable.texture.width);
            state.actualTextureHeight = static_cast<int>(drawable.texture.height);
            state.drawableGeometryMatches =
                    state.actualTextureWidth == state.requestedDrawableWidth
                    && state.actualTextureHeight == state.requestedDrawableHeight;
            if (!state.drawableGeometryMatches)
                return;

            const bool preparedEndpointsActive = presentationState->hdrPrepared
                    && preparedSDRImage && preparedHDRImage;
            state.preparedGeometryActive = preparedEndpointsActive;
            if (needsManagedPreparation && !preparedEndpointsActive)
                return;
            CIImage *source = preparedEndpointsActive
                    ? preparedDisplayImage(state.targetHeadroom, state.transitionProgress)
                    : displayImage(*image, state.targetHeadroom, state.transitionProgress);
            source = imageForTexture(source, viewportSize, corners, actualSize);
            if (!source)
                return;

            const CGRect destinationBounds = CGRectMake(0, 0, actualSize.width, actualSize.height);
            QElapsedTimer timer;
            timer.start();
            [context render:source
                     toMTLTexture:drawable.texture
                    commandBuffer:commandBuffer
                           bounds:destinationBounds
                       colorSpace:outputColorSpace];
            revealAfterPresentation(drawable, commandBuffer);
            [commandBuffer presentDrawable:drawable];
            [commandBuffer commit];
            state.lastRenderMilliseconds = timer.nsecsElapsed() / 1000000.0;
            ++state.renderCount;
            syncPresentationDiagnostics();
        }
    }

    HDRRendererDiagnostics diagnostics()
    {
        syncPresentationDiagnostics();
        return state;
    }

    NSView *nativeView{ nil };
    CAMetalLayer *metalLayer{ nil };
    id<MTLDevice> device{ nil };
    id<MTLCommandQueue> commandQueue{ nil };
    CIContext *context{ nil };
    CGColorSpaceRef outputColorSpace{ nullptr };
    CGColorSpaceRef backgroundColorSpace{ nullptr };
    QColor backgroundColor{ Qv::viewportBackgroundColor(Qv::Theme::Light) };
    QVHDRPresentationState *presentationState{ nil };
    id<MTLTexture> preparationTexture{ nil };
    CIImage *preparedSDRImage{ nil };
    CIImage *preparedHDRImage{ nil };
    std::shared_ptr<const NativeHDRImage> image;
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
                                           const qreal transitionProgress)
{
    if (impl)
        impl->render(viewportSize, imageCorners, transitionProgress);
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

qreal QVCocoaFunctions::pacedHDRTransitionProgress(const qreal previousProgress,
                                                    const qreal desiredProgress,
                                                    const qreal maximumStep)
{
    const qreal previous = std::clamp(previousProgress, 0.0, 1.0);
    const qreal desired = std::clamp(desiredProgress, previous, 1.0);
    const qreal step = std::max(0.0, maximumStep);
    return std::min(desired, previous + step);
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

bool QVCocoaFunctions::shouldStartHDRTransition(const bool layoutReady,
                                                const bool firstFramePresented,
                                                const bool hdrPrepared)
{
    return layoutReady && firstFramePresented && hdrPrepared;
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

static void hideMenuShortcuts(NSMenu *nativeMenu)
{
    for (NSMenuItem *item in nativeMenu.itemArray)
    {
        [item setKeyEquivalent:@""];

        if (item.hasSubmenu)
            hideMenuShortcuts(item.submenu);
    }
}

void QVCocoaFunctions::showMenu(QMenu *menu, const QPoint &point, QWindow *window)
{
    NSView *view = reinterpret_cast<NSView*>(window->winId());
    NSMenu *nativeMenu = menu->toNSMenu();

    hideMenuShortcuts(nativeMenu);

    // Synthesize right mouse down event to open menu
    NSPoint downPoint = [view convertPoint:NSMakePoint(point.x(), point.y()) toView:nil];
    NSEvent *downEvent = [NSEvent mouseEventWithType:NSEventTypeRightMouseDown location:downPoint modifierFlags:0
        timestamp:0 windowNumber:view.window.windowNumber context:nil eventNumber:0 clickCount:1 pressure:1.0];
    [NSMenu popUpContextMenu:nativeMenu withEvent:downEvent forView:view];

    // Synthesize right mouse up event to avoid stuck button press
    NSPoint upPoint = view.window.mouseLocationOutsideOfEventStream;
    NSEvent *upEvent = [NSEvent mouseEventWithType:NSEventTypeRightMouseUp location:upPoint modifierFlags:0
        timestamp:0 windowNumber:view.window.windowNumber context:nil eventNumber:0 clickCount:1 pressure:0];
    [view rightMouseUp:upEvent];
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

            if (sdrRawFilter && hdrRawFilter)
            {
                // Keep the SDR and EDR recipes on independent immutable filter
                // graphs. Mutating one CIRAWFilter between lazy output reads
                // made later tile evaluation depend on timing.
                sdrRawFilter.orientation = orientation;
                hdrRawFilter.orientation = orientation;
                sdrRawFilter.extendedDynamicRangeAmount = 0.0F;
                // Apple defines zero baselineExposure as linear response. This
                // sample's negative camera default substantially lowers the
                // EDR rendition, so request linear response only for HDR while
                // the SDR companion preserves the camera default. The exact
                // Quick Look exposure policy is private; this is the measured
                // public-API approximation for the supplied fixture.
                if (hdrRawFilter.baselineExposure < 0.0F)
                    hdrRawFilter.baselineExposure = 0.0F;
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
                    if (@available(macOS 26.0, *))
                        hdrImage = [hdrImage imageBySettingContentHeadroom:
                                result.hdrMetadata.contentHeadroom];
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

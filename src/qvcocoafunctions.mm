#include "actionmanager.h"
#include "qvapplication.h"
#include "qvcocoafunctions.h"

#include <QUrl>
#include <QDebug>
#include <QFile>
#include <QFileIconProvider>
#include <QCollator>
#include <QColorSpace>
#include <QFileInfo>

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
    return typeIdentifier && UTTypeConformsTo(typeIdentifier, CFSTR("public.camera-raw-image"));
}

bool isImageType(CFStringRef typeIdentifier)
{
    return typeIdentifier && UTTypeConformsTo(typeIdentifier, kUTTypeImage);
}

QList<QByteArray> typeTags(CFStringRef typeIdentifier, CFStringRef tagClass)
{
    QList<QByteArray> tags;
    if (!typeIdentifier)
        return tags;

    CFArrayRef allTags = UTTypeCopyAllTagsWithClass(typeIdentifier, tagClass);
    if (!allTags)
        return tags;

    const CFIndex count = CFArrayGetCount(allTags);
    for (CFIndex index = 0; index < count; ++index)
    {
        const auto tag = static_cast<CFStringRef>(CFArrayGetValueAtIndex(allTags, index));
        const QByteArray normalized = normalizedExtension(QByteArray(QStringFromCFString(tag).toUtf8()));
        if (!normalized.isEmpty() && !tags.contains(normalized))
            tags.append(normalized);
    }
    CFRelease(allTags);
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

    CGColorSpaceRef colorSpace = colorSyncSrgbColorSpace();
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
    CGColorSpaceRelease(colorSpace);
    const QColorSpace qColorSpace = qColorSpaceFromCGColorSpace(CGImageGetColorSpace(cgImage));
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

CFDictionaryRef fullResolutionThumbnailOptions(CGImageSourceRef source)
{
    // Image I/O's thumbnail API is retained here because its transform option
    // applies orientation metadata. Its maximum is deliberately the source's
    // own largest dimension, never the screen-sized loader hint; otherwise a
    // later zoom would only interpolate pixels that were already discarded.
    const int maxDimension = sourceMaxPixelSize(source);
    CFNumberRef maxPixelSizeNumber = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &maxDimension);
    if (!maxPixelSizeNumber)
        return nullptr;

    const void *optionKeys[] = {
        kCGImageSourceCreateThumbnailFromImageAlways,
        kCGImageSourceCreateThumbnailWithTransform,
        kCGImageSourceThumbnailMaxPixelSize
    };
    const void *optionValues[] = {
        kCFBooleanTrue,
        kCFBooleanTrue,
        maxPixelSizeNumber
    };
    CFDictionaryRef options = CFDictionaryCreate(
        kCFAllocatorDefault,
        optionKeys,
        optionValues,
        sizeof(optionKeys) / sizeof(optionKeys[0]),
        &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);
    CFRelease(maxPixelSizeNumber);
    return options;
}

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
        for (const auto &format : typeTags(identifier, kUTTagClassFilenameExtension))
        {
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
        for (const auto &mimeType : typeTags(identifier, kUTTagClassMIMEType))
        {
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
        const bool supported = typeTags(identifier, kUTTagClassFilenameExtension).contains(normalized);
        CFRelease(identifier);
        if (supported)
        {
            return true;
        }
    }

    return false;
}

QVCocoaFunctions::NativeImageReadResult QVCocoaFunctions::readImageWithImageIO(const QString &filePath)
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
        result.intrinsicSize = sourcePixelSize(source);

        // Read the source properties through Image I/O before decoding. This
        // keeps orientation, color profile and camera metadata on the native
        // path instead of guessing from the filename.
        if (const CFDictionaryRef sourceProperties = CGImageSourceCopyProperties(source, nullptr))
            CFRelease(sourceProperties);

        const CGImagePropertyOrientation orientation = sourceOrientation(source);

        if (result.isRaw)
        {
            NSData *imageData = [NSData dataWithContentsOfURL:fileUrl.toNSURL()];
            CIRAWFilter *rawFilter = imageData ? [CIRAWFilter filterWithImageData:imageData identifierHint:(NSString *)sourceType] : nil;

            if (rawFilter)
            {
                rawFilter.orientation = orientation;

                CGColorSpaceRef outputColorSpace = colorSyncSrgbColorSpace();
                if (outputColorSpace)
                {
                    NSDictionary *contextOptions = @{
                        (id)kCIContextUseSoftwareRenderer: @NO,
                        (id)kCIContextWorkingColorSpace: (id)outputColorSpace,
                        (id)kCIContextOutputColorSpace: (id)outputColorSpace
                    };
                    id<MTLDevice> metalDevice = MTLCreateSystemDefaultDevice();
                    CIContext *context = metalDevice
                        ? [CIContext contextWithMTLDevice:metalDevice options:contextOptions]
                        : [CIContext contextWithOptions:contextOptions];

                    result.image = imageFromCIImage(rawFilter.outputImage, context, outputColorSpace, 0);
                    if (result.image.isNull())
                    {
                        // A supported container can still contain a camera
                        // model that the installed RAW decoder does not know.
                        // Prefer the embedded JPEG preview before reporting an
                        // error to the application.
                        result.image = imageFromCIImage(rawFilter.previewImage, context, outputColorSpace, 0);
                        result.usedRawPreview = !result.image.isNull();
                    }

                    if (context)
                        [context clearCaches];
                    CGColorSpaceRelease(outputColorSpace);
                }
            }

            if (result.image.isNull())
            {
                if (CFDictionaryRef options = fullResolutionThumbnailOptions(source))
                {
                    CGImageRef previewImage = CGImageSourceCreateThumbnailAtIndex(source, 0, options);
                    CFRelease(options);
                    if (previewImage)
                    {
                        result.image = imageFromCGImage(previewImage);
                        result.usedRawPreview = !result.image.isNull();
                        CGImageRelease(previewImage);
                    }
                }
            }

            if (result.image.isNull())
            {
                result.errorString = QStringLiteral(
                    "Core Image RAW decoder does not support this camera model and no embedded JPEG preview is available");
            }
        }
        else if (result.isImageIOType)
        {
            if (CFDictionaryRef options = fullResolutionThumbnailOptions(source))
            {
                CGImageRef cgImage = CGImageSourceCreateThumbnailAtIndex(source, 0, options);
                CFRelease(options);
                if (cgImage)
                {
                    result.image = imageFromCGImage(cgImage);
                    CGImageRelease(cgImage);
                }
            }

            if (result.image.isNull())
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

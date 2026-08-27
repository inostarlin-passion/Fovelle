#include "actionmanager.h"
#include "qvapplication.h"
#include "qvcocoafunctions.h"

#include <QUrl>
#include <QDebug>
#include <QFile>
#include <QFileIconProvider>
#include <QCollator>
#include <QColorSpace>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImageReader>
#include <QLocale>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPointer>
#include <QStandardPaths>
#include <QStyleHints>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QVector>
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
#import <objc/runtime.h>

static constexpr char SettingsToolbarAssociationKey = 0;
static constexpr char FullScreenTransitionCompleteAssociationKey = 0;
static constexpr char FullScreenExitPendingAssociationKey = 0;
static constexpr char FullScreenNormalFrameAssociationKey = 0;
static constexpr char FullScreenNormalTitlebarOverlapAssociationKey = 0;
static constexpr char FullScreenTargetContentSizeAssociationKey = 0;
static constexpr char FullScreenCustomAnimationAssociationKey = 0;
static constexpr char FullScreenAnimationHandlerAssociationKey = 0;
static constexpr char FullScreenAnimationAssociationKey = 0;
static constexpr char FullScreenImageRectProviderAssociationKey = 0;
static constexpr char FullScreenImageProviderAssociationKey = 0;
static constexpr char FullScreenBackgroundProviderAssociationKey = 0;
static constexpr char FullScreenTitlebarOverlapProviderAssociationKey = 0;
static constexpr char FullScreenNormalTitlebarSnapshotAssociationKey = 0;
static constexpr char FullScreenProxyWindowAssociationKey = 0;
static constexpr char FullScreenProxyWindowLayerAssociationKey = 0;
static constexpr char FullScreenProxyImageLayerAssociationKey = 0;
static constexpr char FullScreenProxyTitlebarLayerAssociationKey = 0;
static constexpr char FullScreenOriginalAlphaAssociationKey = 0;
static constexpr char FullScreenAnimationStartFrameAssociationKey = 0;

enum class FovelleFullScreenAnimationPhase : NSInteger
{
    Begin,
    Update,
    Cancel
};

typedef void (^FovelleFullScreenAnimationHandler)(
    FovelleFullScreenAnimationPhase phase, int titlebarOverlap,
    int targetTitlebarOverlap);
typedef QRect (^FovelleFullScreenImageRectProvider)(void);
typedef QImage (^FovelleFullScreenImageProvider)(void);
typedef QColor (^FovelleFullScreenBackgroundProvider)(void);
typedef int (^FovelleFullScreenTitlebarOverlapProvider)(void);

static void releaseFullScreenSnapshotProvider(
    void *info, __unused const void *data, __unused size_t size)
{
    delete static_cast<QImage *>(info);
}

static CGImageRef createFullScreenSnapshotCGImage(const QImage &source)
{
    if (source.isNull())
        return nullptr;

    auto *image = new QImage(
        source.convertToFormat(QImage::Format_RGBA8888_Premultiplied));
    if (image->isNull())
    {
        delete image;
        return nullptr;
    }

    const size_t dataSize = static_cast<size_t>(image->bytesPerLine())
        * static_cast<size_t>(image->height());
    CGDataProviderRef provider = CGDataProviderCreateWithData(
        image, image->constBits(), dataSize,
        releaseFullScreenSnapshotProvider);
    if (!provider)
    {
        delete image;
        return nullptr;
    }

    CGColorSpaceRef colorSpace = nullptr;
    const QByteArray iccProfile = image->colorSpace().iccProfile();
    if (!iccProfile.isEmpty())
    {
        CFDataRef profileData = CFDataCreate(
            kCFAllocatorDefault,
            reinterpret_cast<const UInt8 *>(iccProfile.constData()),
            iccProfile.size());
        if (profileData)
        {
            colorSpace = CGColorSpaceCreateWithICCData(profileData);
            CFRelease(profileData);
        }
    }
    // An untagged QImage is displayed by Qt as sRGB. Keep the proxy snapshot
    // in that same explicit space instead of letting the active display choose
    // the meaning of otherwise identical RGB component values.
    if (!colorSpace)
        colorSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    if (!colorSpace)
    {
        CGDataProviderRelease(provider);
        return nullptr;
    }
    CGImageRef result = CGImageCreate(
        static_cast<size_t>(image->width()),
        static_cast<size_t>(image->height()),
        8, 32, static_cast<size_t>(image->bytesPerLine()),
        colorSpace,
        kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big,
        provider, nullptr, true, kCGRenderingIntentDefault);
    CGColorSpaceRelease(colorSpace);
    CGDataProviderRelease(provider);
    return result;
}

static CGColorRef createFullScreenBackgroundCGColor(const QColor &source)
{
    const QColor background = source.toRgb();
    const CGFloat components[] = {
        background.redF(), background.greenF(), background.blueF(), 1.0
    };
    CGColorSpaceRef colorSpace =
        CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    if (!colorSpace)
        return nullptr;

    CGColorRef result = CGColorCreate(colorSpace, components);
    CGColorSpaceRelease(colorSpace);
    return result;
}

static NSRect nativeWindowRectForLocalQtRect(
    const NSRect windowFrame, const QRect &localRect)
{
    return NSMakeRect(
        localRect.x(),
        NSHeight(windowFrame) - localRect.y() - localRect.height(),
        localRect.width(), localRect.height());
}

static NSRect proxyLocalRect(const NSRect screenRect, NSWindow *proxyWindow)
{
    return NSOffsetRect(
        screenRect,
        -NSMinX(proxyWindow.frame),
        -NSMinY(proxyWindow.frame));
}

static NSBitmapImageRep *captureFullScreenTitlebar(
    NSWindow *window, const int titlebarOverlap)
{
    if (!window || titlebarOverlap <= 0)
        return nil;

    NSView *frameView = window.contentView.superview;
    if (!frameView)
        return nil;

    const NSRect bounds = frameView.bounds;
    const CGFloat height = qMin<CGFloat>(titlebarOverlap, NSHeight(bounds));
    const NSRect titlebarRect = NSMakeRect(
        NSMinX(bounds), frameView.isFlipped
            ? NSMinY(bounds) : NSMaxY(bounds) - height,
        NSWidth(bounds), height);
    NSBitmapImageRep *snapshot =
        [frameView bitmapImageRepForCachingDisplayInRect:titlebarRect];
    if (!snapshot)
        return nil;

    [frameView cacheDisplayInRect:titlebarRect toBitmapImageRep:snapshot];
    return snapshot;
}

@interface FovelleFullScreenAnimation : NSAnimation
{
@private
    NSWindow *_realWindow;
    NSWindow *_proxyWindow;
    CALayer *_windowLayer;
    CALayer *_imageLayer;
    CALayer *_titlebarLayer;
    NSRect _startWindowFrame;
    NSRect _endWindowFrame;
    NSRect _startImageFrame;
    NSRect _endImageFrame;
    NSRect _startTitlebarFrame;
    NSRect _endTitlebarFrame;
    int _endTitlebarOverlap;
    float _startTitlebarOpacity;
    float _endTitlebarOpacity;
    BOOL _enteringFullScreen;
    NSInteger _intermediateFrameCount;
    BOOL _realWindowPrepared;
    FovelleFullScreenAnimationHandler _handler;
}

- (instancetype)initWithRealWindow:(NSWindow *)realWindow
                       proxyWindow:(NSWindow *)proxyWindow
                        windowLayer:(CALayer *)windowLayer
                         imageLayer:(CALayer *)imageLayer
                      titlebarLayer:(CALayer *)titlebarLayer
                   startWindowFrame:(NSRect)startWindowFrame
                     endWindowFrame:(NSRect)endWindowFrame
                    startImageFrame:(NSRect)startImageFrame
                      endImageFrame:(NSRect)endImageFrame
                 startTitlebarFrame:(NSRect)startTitlebarFrame
                   endTitlebarFrame:(NSRect)endTitlebarFrame
             endTitlebarOverlap:(int)endTitlebarOverlap
            startTitlebarOpacity:(float)startTitlebarOpacity
              endTitlebarOpacity:(float)endTitlebarOpacity
               enteringFullScreen:(BOOL)enteringFullScreen
                       duration:(NSTimeInterval)duration
                        handler:(FovelleFullScreenAnimationHandler)handler;
@end

@implementation FovelleFullScreenAnimation

- (instancetype)initWithRealWindow:(NSWindow *)realWindow
                       proxyWindow:(NSWindow *)proxyWindow
                        windowLayer:(CALayer *)windowLayer
                         imageLayer:(CALayer *)imageLayer
                      titlebarLayer:(CALayer *)titlebarLayer
                   startWindowFrame:(NSRect)startWindowFrame
                     endWindowFrame:(NSRect)endWindowFrame
                    startImageFrame:(NSRect)startImageFrame
                      endImageFrame:(NSRect)endImageFrame
                 startTitlebarFrame:(NSRect)startTitlebarFrame
                   endTitlebarFrame:(NSRect)endTitlebarFrame
             endTitlebarOverlap:(int)endTitlebarOverlap
            startTitlebarOpacity:(float)startTitlebarOpacity
              endTitlebarOpacity:(float)endTitlebarOpacity
               enteringFullScreen:(BOOL)enteringFullScreen
                       duration:(NSTimeInterval)duration
                        handler:(FovelleFullScreenAnimationHandler)handler
{
    self = [super initWithDuration:duration animationCurve:NSAnimationEaseInOut];
    if (!self)
        return nil;

    _realWindow = realWindow;
    _proxyWindow = proxyWindow;
    _windowLayer = windowLayer;
    _imageLayer = imageLayer;
    _titlebarLayer = titlebarLayer;
    _startWindowFrame = startWindowFrame;
    _endWindowFrame = endWindowFrame;
    _startImageFrame = startImageFrame;
    _endImageFrame = endImageFrame;
    _startTitlebarFrame = startTitlebarFrame;
    _endTitlebarFrame = endTitlebarFrame;
    _endTitlebarOverlap = endTitlebarOverlap;
    _startTitlebarOpacity = startTitlebarOpacity;
    _endTitlebarOpacity = endTitlebarOpacity;
    _enteringFullScreen = enteringFullScreen;
    _intermediateFrameCount = 0;
    _realWindowPrepared = NO;
    _handler = [handler copy];
    self.animationBlockingMode = NSAnimationNonblocking;
    self.frameRate = 60.0f;
    return self;
}

- (void)dealloc
{
    [_handler release];
    [super dealloc];
}

- (void)setCurrentProgress:(NSAnimationProgress)progress
{
    [super setCurrentProgress:progress];
    if (!_realWindow || !_proxyWindow || !_windowLayer || !_imageLayer
        || !_handler)
        return;

    const CGFloat value = std::clamp<CGFloat>(self.currentValue, 0.0, 1.0);
    if (progress > 0.0f && progress < 1.0f)
        ++_intermediateFrameCount;
    const auto interpolate = [value](const CGFloat from, const CGFloat to) {
        return from + ((to - from) * value);
    };
    const auto interpolateRect = [&interpolate](
        const NSRect from, const NSRect to) {
        return NSMakeRect(
            interpolate(from.origin.x, to.origin.x),
            interpolate(from.origin.y, to.origin.y),
            interpolate(from.size.width, to.size.width),
            interpolate(from.size.height, to.size.height));
    };

    // The proxy NSWindow never changes size. Only its independent layers move,
    // so WindowServer cannot stretch an old window backing store between a
    // geometry commit and Qt's next paint.
    [CATransaction begin];
    [CATransaction setAnimationDuration:0.0];
    [CATransaction setDisableActions:YES];
    _windowLayer.frame = interpolateRect(
        _startWindowFrame, _endWindowFrame);
    _imageLayer.frame = interpolateRect(
        _startImageFrame, _endImageFrame);
    if (_titlebarLayer)
    {
        _titlebarLayer.frame = interpolateRect(
            _startTitlebarFrame, _endTitlebarFrame);
        _titlebarLayer.opacity = static_cast<float>(interpolate(
            _startTitlebarOpacity, _endTitlebarOpacity));
    }
    [CATransaction commit];
    [CATransaction flush];

    if (!_realWindowPrepared && progress >= 1.0f)
    {
        _realWindowPrepared = YES;
        const NSRect nativeEndFrame = NSOffsetRect(
            _endWindowFrame,
            NSMinX(_proxyWindow.frame),
            NSMinY(_proxyWindow.frame));
        [_realWindow setFrame:nativeEndFrame display:NO];
        _handler(
            FovelleFullScreenAnimationPhase::Update,
            _endTitlebarOverlap, _endTitlebarOverlap);
        [_realWindow displayIfNeeded];
        if (qEnvironmentVariableIsSet("FOVELLE_FULLSCREEN_TRANSITION_LOG"))
        {
            qInfo().noquote()
                << "FOVELLE_FULLSCREEN_TRANSITION"
                << (_enteringFullScreen ? "direction=enter" : "direction=exit")
                << "phase=handoff"
                << "intermediate_frames=" << _intermediateFrameCount
                << "duration_ms=" << self.duration * 1000.0
                << "start_window="
                << QString::fromNSString(NSStringFromRect(_startWindowFrame))
                << "end_window="
                << QString::fromNSString(NSStringFromRect(_endWindowFrame))
                << "start_image="
                << QString::fromNSString(NSStringFromRect(_startImageFrame))
                << "end_image="
                << QString::fromNSString(NSStringFromRect(_endImageFrame));
        }
    }
}

@end

static void revealFovelleFullScreenRealWindow(NSWindow *window)
{
    NSNumber *originalAlpha = objc_getAssociatedObject(
        window, &FullScreenOriginalAlphaAssociationKey);

    // AppKit still owns the custom windows until the matching DidEnter/DidExit
    // notification returns. Reveal the already-painted real endpoint behind
    // the pixel-identical proxy, then remove that proxy after the handoff.
    [CATransaction begin];
    [CATransaction setAnimationDuration:0.0];
    [CATransaction setDisableActions:YES];
    [window displayIfNeeded];
    if (originalAlpha)
        window.alphaValue = originalAlpha.doubleValue;
    [CATransaction commit];
    [CATransaction flush];
}

static void cleanupFovelleFullScreenProxy(NSWindow *window)
{
    revealFovelleFullScreenRealWindow(window);
    NSWindow *proxy = objc_getAssociatedObject(
        window, &FullScreenProxyWindowAssociationKey);
    [proxy orderOut:nil];

    objc_setAssociatedObject(
        window, &FullScreenProxyWindowAssociationKey,
        nil, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    objc_setAssociatedObject(
        window, &FullScreenProxyWindowLayerAssociationKey,
        nil, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    objc_setAssociatedObject(
        window, &FullScreenProxyImageLayerAssociationKey,
        nil, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    objc_setAssociatedObject(
        window, &FullScreenProxyTitlebarLayerAssociationKey,
        nil, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    objc_setAssociatedObject(
        window, &FullScreenOriginalAlphaAssociationKey,
        nil, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    objc_setAssociatedObject(
        window, &FullScreenAnimationStartFrameAssociationKey,
        nil, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

static void clearFovelleFullScreenNormalState(NSWindow *window)
{
    objc_setAssociatedObject(
        window, &FullScreenNormalFrameAssociationKey,
        nil, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    objc_setAssociatedObject(
        window, &FullScreenNormalTitlebarOverlapAssociationKey,
        nil, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    objc_setAssociatedObject(
        window, &FullScreenNormalTitlebarSnapshotAssociationKey,
        nil, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    objc_setAssociatedObject(
        window, &FullScreenTargetContentSizeAssociationKey,
        nil, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

static void restoreFovelleFullScreenAnimationStartFrame(NSWindow *window)
{
    NSValue *startFrame = objc_getAssociatedObject(
        window, &FullScreenAnimationStartFrameAssociationKey);
    if (startFrame)
        [window setFrame:startFrame.rectValue display:NO];

    FovelleFullScreenAnimationHandler handler = objc_getAssociatedObject(
        window, &FullScreenAnimationHandlerAssociationKey);
    if (handler)
        handler(FovelleFullScreenAnimationPhase::Cancel, 0, 0);
    [window displayIfNeeded];
}

static int fovelleNormalTitlebarOverlap(NSWindow *window)
{
    const int nativeTitlebarOverlap = qMax(
        qRound(window.contentView.frame.size.height
               - window.contentLayoutRect.size.height), 0);
    FovelleFullScreenTitlebarOverlapProvider overlapProvider =
        objc_getAssociatedObject(
            window, &FullScreenTitlebarOverlapProviderAssociationKey);
    return qMax(overlapProvider ? overlapProvider() : nativeTitlebarOverlap, 0);
}

static NSRect fovelleTitlebarFrame(
    const NSRect windowFrame, const int titlebarOverlap)
{
    const int overlap = qMax(titlebarOverlap, 0);
    return NSMakeRect(
        0, NSHeight(windowFrame) - overlap,
        NSWidth(windowFrame), overlap);
}

static NSSize fovelleWillUseFullScreenContentSize(
    __unused id delegate, __unused SEL selector, NSWindow *window,
    const NSSize proposedSize)
{
    if (qEnvironmentVariableIsSet("FOVELLE_FULLSCREEN_TRANSITION_LOG"))
        qInfo().noquote() << "FOVELLE_FULLSCREEN_TRANSITION direction=enter phase=proposed-content-size"
                          << proposedSize.width << proposedSize.height;
    objc_setAssociatedObject(
        window, &FullScreenTargetContentSizeAssociationKey,
        [NSValue valueWithSize:proposedSize],
        OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    return proposedSize;
}

static NSRect fovelleFullScreenTargetFrame(
    NSWindow *window, NSWindow *proxy)
{
    NSRect result = proxy.screen.frame;
    NSValue *targetContentSize = objc_getAssociatedObject(
        window, &FullScreenTargetContentSizeAssociationKey);
    if (targetContentSize
        && targetContentSize.sizeValue.width > 0
        && targetContentSize.sizeValue.height > 0)
    {
        // A full-screen NSWindow has no titlebar outside its content. AppKit's
        // proposed content size is therefore the authoritative target frame,
        // including the camera-housing safe area on notched displays.
        result.size = targetContentSize.sizeValue;
    }
    if (@available(macOS 12.0, *))
    {
        // AppKit's proposed *content* size may extend beneath the camera
        // housing by the small frame/content delta. This window has not opted
        // into the auxiliary areas, so its real frame is bounded by the
        // screen's safe top edge. Taking the stricter bound reproduces the
        // frame AppKit publishes after NSWindowDidEnterFullScreen.
        const CGFloat safeHeight = qMax<CGFloat>(
            proxy.screen.frame.size.height
                - proxy.screen.safeAreaInsets.top,
            1.0);
        result.size.height = qMin(result.size.height, safeHeight);

        const bool hasCameraHousingAreas =
            !NSIsEmptyRect(proxy.screen.auxiliaryTopLeftArea)
            || !NSIsEmptyRect(proxy.screen.auxiliaryTopRightArea);
        if (hasCameraHousingAreas)
        {
            // In display-safe-area compatibility mode AppKit proposes the
            // classic panel height below the entire menu-bar strip, not merely
            // below the camera aperture. This is observable only after the
            // custom animation callback, so derive the same public geometry
            // up front from the screen height and the current menu-bar height.
            const CGFloat compatibilityHeight = qMax<CGFloat>(
                proxy.screen.frame.size.height
                    - NSApp.mainMenu.menuBarHeight,
                1.0);
            result.size.height = qMin(
                result.size.height, compatibilityHeight);
        }
    }
    return result;
}

static NSWindow *createFovelleFullScreenProxy(
    NSWindow *window, NSScreen *screen, const QRect &imageRect,
    const QImage &snapshot, const QColor &background,
    NSBitmapImageRep *titlebarSnapshot, const int normalTitlebarOverlap,
    const bool enteringFullScreen)
{
    if (!window || !screen || imageRect.isEmpty() || snapshot.isNull())
        return nil;

    CGImageRef snapshotImage = createFullScreenSnapshotCGImage(snapshot);
    CGColorRef backgroundColor = createFullScreenBackgroundCGColor(background);
    if (!snapshotImage || !backgroundColor)
    {
        if (snapshotImage)
            CGImageRelease(snapshotImage);
        if (backgroundColor)
            CGColorRelease(backgroundColor);
        return nil;
    }

    NSWindow *oldProxy = objc_getAssociatedObject(
        window, &FullScreenProxyWindowAssociationKey);
    [oldProxy orderOut:nil];

    auto *proxy = [[NSWindow alloc]
        initWithContentRect:screen.frame
        styleMask:NSWindowStyleMaskBorderless
        backing:NSBackingStoreBuffered
        defer:NO
        screen:screen];
    proxy.releasedWhenClosed = NO;
    proxy.opaque = NO;
    proxy.backgroundColor = NSColor.clearColor;
    proxy.hasShadow = NO;
    proxy.ignoresMouseEvents = YES;
    proxy.level = window.level + 1;
    proxy.collectionBehavior = NSWindowCollectionBehaviorFullScreenAuxiliary;
    proxy.contentView.wantsLayer = YES;
    proxy.contentView.layer.backgroundColor = NSColor.clearColor.CGColor;

    CALayer *windowLayer = [CALayer layer];
    windowLayer.backgroundColor = backgroundColor;
    windowLayer.opaque = YES;
    windowLayer.frame = proxyLocalRect(window.frame, proxy);
    windowLayer.masksToBounds = YES;
    if (qEnvironmentVariableIsSet("FOVELLE_FULLSCREEN_TRANSITION_LOG"))
    {
        CFStringRef colorSpaceName = CGColorSpaceGetName(
            CGColorGetColorSpace(backgroundColor));
        qInfo().noquote()
            << "FOVELLE_FULLSCREEN_TRANSITION"
            << (enteringFullScreen ? "direction=enter" : "direction=exit")
            << "phase=proxy"
            << "background_space="
            << (colorSpaceName
                ? QString::fromNSString((NSString *)colorSpaceName)
                : QStringLiteral("unknown"));
    }
    CGColorRelease(backgroundColor);

    CALayer *imageLayer = [CALayer layer];
    imageLayer.contents = reinterpret_cast<id>(snapshotImage);
    imageLayer.contentsGravity = kCAGravityResize;
    imageLayer.minificationFilter = kCAFilterLinear;
    imageLayer.magnificationFilter = kCAFilterLinear;
    imageLayer.contentsScale = qMax(snapshot.devicePixelRatio(), 1.0);
    imageLayer.frame = nativeWindowRectForLocalQtRect(
        window.frame, imageRect);
    CGImageRelease(snapshotImage);

    CALayer *titlebarLayer = nil;
    if (normalTitlebarOverlap > 0 && titlebarSnapshot.CGImage)
    {
        titlebarLayer = [CALayer layer];
        titlebarLayer.contents = reinterpret_cast<id>(titlebarSnapshot.CGImage);
        titlebarLayer.contentsGravity = kCAGravityResize;
        titlebarLayer.minificationFilter = kCAFilterLinear;
        titlebarLayer.magnificationFilter = kCAFilterLinear;
        titlebarLayer.contentsScale = qMax(window.backingScaleFactor, 1.0);
        titlebarLayer.frame = enteringFullScreen
            ? fovelleTitlebarFrame(window.frame, normalTitlebarOverlap)
            : fovelleTitlebarFrame(window.frame, 0);
        titlebarLayer.opacity = enteringFullScreen ? 1.0f : 0.0f;
    }

    [proxy.contentView.layer addSublayer:windowLayer];
    [windowLayer addSublayer:imageLayer];
    if (titlebarLayer)
        [windowLayer addSublayer:titlebarLayer];
    objc_setAssociatedObject(
        window, &FullScreenProxyWindowAssociationKey,
        proxy, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    objc_setAssociatedObject(
        window, &FullScreenProxyWindowLayerAssociationKey,
        windowLayer, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    objc_setAssociatedObject(
        window, &FullScreenProxyImageLayerAssociationKey,
        imageLayer, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    objc_setAssociatedObject(
        window, &FullScreenProxyTitlebarLayerAssociationKey,
        titlebarLayer, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    [proxy release];
    return proxy;
}

static NSArray<NSWindow *> *fovelleCustomWindowsToEnterFullScreen(
    __unused id delegate, __unused SEL selector, NSWindow *window)
{
    NSNumber *enabled = objc_getAssociatedObject(
        window, &FullScreenCustomAnimationAssociationKey);
    FovelleFullScreenAnimationHandler handler = objc_getAssociatedObject(
        window, &FullScreenAnimationHandlerAssociationKey);
    FovelleFullScreenImageRectProvider rectProvider = objc_getAssociatedObject(
        window, &FullScreenImageRectProviderAssociationKey);
    FovelleFullScreenImageProvider imageProvider = objc_getAssociatedObject(
        window, &FullScreenImageProviderAssociationKey);
    FovelleFullScreenBackgroundProvider backgroundProvider =
        objc_getAssociatedObject(
            window, &FullScreenBackgroundProviderAssociationKey);
    if (!enabled.boolValue || !handler
        || !rectProvider || !imageProvider || !backgroundProvider)
        return nil;

    const QRect imageRect = rectProvider();
    const QImage snapshot = imageProvider();
    if (imageRect.isEmpty() || snapshot.isNull() || !window.screen)
        return nil;

    const int normalTitlebarOverlap = fovelleNormalTitlebarOverlap(window);
    if (qEnvironmentVariableIsSet("FOVELLE_FULLSCREEN_TRANSITION_LOG"))
        qInfo().noquote() << "FOVELLE_FULLSCREEN_TRANSITION direction=enter phase=prepare"
                          << "window=" << QString::fromNSString(NSStringFromRect(window.frame))
                          << "image=" << imageRect;
    NSBitmapImageRep *titlebarSnapshot =
        captureFullScreenTitlebar(window, normalTitlebarOverlap);
    objc_setAssociatedObject(
        window, &FullScreenNormalFrameAssociationKey,
        [NSValue valueWithRect:window.frame],
        OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    objc_setAssociatedObject(
        window, &FullScreenNormalTitlebarOverlapAssociationKey,
        @(normalTitlebarOverlap), OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    objc_setAssociatedObject(
        window, &FullScreenNormalTitlebarSnapshotAssociationKey,
        titlebarSnapshot, OBJC_ASSOCIATION_RETAIN_NONATOMIC);

    NSWindow *proxy = createFovelleFullScreenProxy(
        window, window.screen, imageRect, snapshot, backgroundProvider(),
        titlebarSnapshot, normalTitlebarOverlap, true);
    return proxy ? @[window, proxy] : nil;
}

static NSArray<NSWindow *> *fovelleCustomWindowsToExitFullScreen(
    __unused id delegate, __unused SEL selector, NSWindow *window)
{
    NSNumber *enabled = objc_getAssociatedObject(
        window, &FullScreenCustomAnimationAssociationKey);
    NSValue *normalFrame = objc_getAssociatedObject(
        window, &FullScreenNormalFrameAssociationKey);
    NSNumber *normalTitlebarOverlap = objc_getAssociatedObject(
        window, &FullScreenNormalTitlebarOverlapAssociationKey);
    NSBitmapImageRep *titlebarSnapshot = objc_getAssociatedObject(
        window, &FullScreenNormalTitlebarSnapshotAssociationKey);
    FovelleFullScreenAnimationHandler handler = objc_getAssociatedObject(
        window, &FullScreenAnimationHandlerAssociationKey);
    FovelleFullScreenImageRectProvider rectProvider = objc_getAssociatedObject(
        window, &FullScreenImageRectProviderAssociationKey);
    FovelleFullScreenImageProvider imageProvider = objc_getAssociatedObject(
        window, &FullScreenImageProviderAssociationKey);
    FovelleFullScreenBackgroundProvider backgroundProvider =
        objc_getAssociatedObject(
            window, &FullScreenBackgroundProviderAssociationKey);
    if (!enabled.boolValue || !normalFrame || !handler
        || !rectProvider || !imageProvider || !backgroundProvider)
        return nil;

    const QRect imageRect = rectProvider();
    const QImage snapshot = imageProvider();
    if (qEnvironmentVariableIsSet("FOVELLE_FULLSCREEN_TRANSITION_LOG"))
        qInfo().noquote() << "FOVELLE_FULLSCREEN_TRANSITION direction=exit phase=prepare"
                          << "window=" << QString::fromNSString(NSStringFromRect(window.frame))
                          << "image=" << imageRect;
    NSWindow *proxy = createFovelleFullScreenProxy(
        window, window.screen, imageRect, snapshot, backgroundProvider(),
        titlebarSnapshot, normalTitlebarOverlap.intValue, false);
    return proxy ? @[window, proxy] : nil;
}

static void startFovelleFullScreenAnimation(
    NSWindow *window, const NSTimeInterval duration,
    const bool enteringFullScreen)
{
    const NSRect appKitStartFrame = window.frame;
    NSValue *storedFrame = objc_getAssociatedObject(
        window, &FullScreenNormalFrameAssociationKey);
    NSNumber *storedTitlebarOverlap = objc_getAssociatedObject(
        window, &FullScreenNormalTitlebarOverlapAssociationKey);
    NSWindow *proxy = objc_getAssociatedObject(
        window, &FullScreenProxyWindowAssociationKey);
    CALayer *windowLayer = objc_getAssociatedObject(
        window, &FullScreenProxyWindowLayerAssociationKey);
    CALayer *imageLayer = objc_getAssociatedObject(
        window, &FullScreenProxyImageLayerAssociationKey);
    CALayer *titlebarLayer = objc_getAssociatedObject(
        window, &FullScreenProxyTitlebarLayerAssociationKey);
    FovelleFullScreenImageRectProvider rectProvider = objc_getAssociatedObject(
        window, &FullScreenImageRectProviderAssociationKey);
    FovelleFullScreenAnimationHandler handler = objc_getAssociatedObject(
        window, &FullScreenAnimationHandlerAssociationKey);
    if (!storedFrame || !proxy || !windowLayer
        || !imageLayer || !rectProvider || !handler)
    {
        cleanupFovelleFullScreenProxy(window);
        return;
    }

    const NSRect normalFrame = storedFrame.rectValue;
    const int normalTitlebarOverlap = qMax(storedTitlebarOverlap.intValue, 0);
    const NSRect fullScreenFrame = enteringFullScreen
        ? fovelleFullScreenTargetFrame(window, proxy) : window.frame;
    const NSRect sourceFrame = enteringFullScreen
        ? normalFrame : fullScreenFrame;
    const NSRect endFrame = enteringFullScreen
        ? fullScreenFrame : normalFrame;
    const int sourceTitlebarOverlap = enteringFullScreen
        ? normalTitlebarOverlap : 0;
    const int endTitlebarOverlap = enteringFullScreen
        ? 0 : normalTitlebarOverlap;

    if (qEnvironmentVariableIsSet("FOVELLE_FULLSCREEN_TRANSITION_LOG"))
    {
        qInfo().noquote()
            << "FOVELLE_FULLSCREEN_TRANSITION"
            << (enteringFullScreen ? "direction=enter" : "direction=exit")
            << "phase=start"
            << "appkit_window="
            << QString::fromNSString(NSStringFromRect(appKitStartFrame))
            << "calculated_target="
            << QString::fromNSString(NSStringFromRect(fullScreenFrame));
    }

    objc_setAssociatedObject(
        window, &FullScreenOriginalAlphaAssociationKey,
        @(window.alphaValue), OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    objc_setAssociatedObject(
        window, &FullScreenAnimationStartFrameAssociationKey,
        [NSValue valueWithRect:sourceFrame],
        OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    [proxy orderWindow:NSWindowAbove relativeTo:window.windowNumber];
    window.alphaValue = 0.0;

    // Measure both layouts from the same live zoom/pan state while the real
    // window is hidden. The proxy then owns every visible intermediate frame;
    // the real window is shown only after it has painted the exact endpoint.
    handler(
        FovelleFullScreenAnimationPhase::Begin,
        sourceTitlebarOverlap, endTitlebarOverlap);
    [window setFrame:sourceFrame display:NO];
    handler(
        FovelleFullScreenAnimationPhase::Update,
        sourceTitlebarOverlap, endTitlebarOverlap);
    [window displayIfNeeded];
    const QRect sourceImageRect = rectProvider();

    [window setFrame:endFrame display:NO];
    handler(
        FovelleFullScreenAnimationPhase::Update,
        endTitlebarOverlap, endTitlebarOverlap);
    [window displayIfNeeded];
    const QRect endImageRect = rectProvider();
    if (sourceImageRect.isEmpty() || endImageRect.isEmpty())
    {
        restoreFovelleFullScreenAnimationStartFrame(window);
        cleanupFovelleFullScreenProxy(window);
        return;
    }

    // AppKit owns the Space transition. Restore the semantic source until the
    // animation's final progress callback commits the pre-painted endpoint.
    [window setFrame:sourceFrame display:NO];
    handler(
        FovelleFullScreenAnimationPhase::Update,
        sourceTitlebarOverlap, endTitlebarOverlap);
    [window displayIfNeeded];

    auto *animation = [[FovelleFullScreenAnimation alloc]
        initWithRealWindow:window
        proxyWindow:proxy
        windowLayer:windowLayer
        imageLayer:imageLayer
        titlebarLayer:titlebarLayer
        startWindowFrame:proxyLocalRect(sourceFrame, proxy)
        endWindowFrame:proxyLocalRect(endFrame, proxy)
        startImageFrame:nativeWindowRectForLocalQtRect(
            sourceFrame, sourceImageRect)
        endImageFrame:nativeWindowRectForLocalQtRect(
            endFrame, endImageRect)
        startTitlebarFrame:fovelleTitlebarFrame(
            sourceFrame, sourceTitlebarOverlap)
        endTitlebarFrame:fovelleTitlebarFrame(
            endFrame, endTitlebarOverlap)
        endTitlebarOverlap:endTitlebarOverlap
        startTitlebarOpacity:sourceTitlebarOverlap > 0 ? 1.0f : 0.0f
        endTitlebarOpacity:endTitlebarOverlap > 0 ? 1.0f : 0.0f
        enteringFullScreen:enteringFullScreen
        duration:duration
        handler:handler];
    if (!animation)
    {
        restoreFovelleFullScreenAnimationStartFrame(window);
        cleanupFovelleFullScreenProxy(window);
        return;
    }
    objc_setAssociatedObject(
        window, &FullScreenAnimationAssociationKey,
        animation, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    [animation startAnimation];
    [animation release];
}

static void fovelleStartCustomAnimationToEnterFullScreen(
    __unused id delegate, __unused SEL selector, NSWindow *window,
    const NSTimeInterval duration)
{
    startFovelleFullScreenAnimation(window, duration, true);
}

static void fovelleStartCustomAnimationToExitFullScreen(
    __unused id delegate, __unused SEL selector, NSWindow *window,
    const NSTimeInterval duration)
{
    startFovelleFullScreenAnimation(window, duration, false);
}

static void fovelleDidFailFullScreenTransition(
    __unused id delegate, __unused SEL selector, NSWindow *window)
{
    auto *animation = static_cast<FovelleFullScreenAnimation *>(
        objc_getAssociatedObject(window, &FullScreenAnimationAssociationKey));
    [animation stopAnimation];
    objc_setAssociatedObject(
        window, &FullScreenAnimationAssociationKey,
        nil, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    restoreFovelleFullScreenAnimationStartFrame(window);
    cleanupFovelleFullScreenProxy(window);
    objc_setAssociatedObject(
        window, &FullScreenTargetContentSizeAssociationKey,
        nil, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

static bool installFovelleFullScreenAnimationMethods(NSWindow *window)
{
    if (!window.delegate)
        return false;

    Class delegateClass = object_getClass(window.delegate);
    const auto installMethod = [delegateClass](
        SEL selector, IMP implementation, const char *types) {
        Method method = class_getInstanceMethod(delegateClass, selector);
        if (!method)
        {
            if (!class_addMethod(delegateClass, selector, implementation, types))
                return false;
            method = class_getInstanceMethod(delegateClass, selector);
        }
        return method && method_getImplementation(method) == implementation;
    };

    const auto contentSizeMethod = protocol_getMethodDescription(
        @protocol(NSWindowDelegate),
        @selector(window:willUseFullScreenContentSize:), NO, YES);

    return contentSizeMethod.types
        && installMethod(
               @selector(window:willUseFullScreenContentSize:),
               reinterpret_cast<IMP>(fovelleWillUseFullScreenContentSize),
               contentSizeMethod.types)
        && installMethod(
               @selector(customWindowsToEnterFullScreenForWindow:),
               reinterpret_cast<IMP>(fovelleCustomWindowsToEnterFullScreen),
               "@@:@")
        && installMethod(
               @selector(window:startCustomAnimationToEnterFullScreenWithDuration:),
               reinterpret_cast<IMP>(fovelleStartCustomAnimationToEnterFullScreen),
               "v@:@d")
        && installMethod(
               @selector(windowDidFailToEnterFullScreen:),
               reinterpret_cast<IMP>(fovelleDidFailFullScreenTransition),
               "v@:@")
        && installMethod(
               @selector(customWindowsToExitFullScreenForWindow:),
               reinterpret_cast<IMP>(fovelleCustomWindowsToExitFullScreen),
               "@@:@")
        && installMethod(
               @selector(window:startCustomAnimationToExitFullScreenWithDuration:),
               reinterpret_cast<IMP>(fovelleStartCustomAnimationToExitFullScreen),
               "v@:@d")
        && installMethod(
               @selector(windowDidFailToExitFullScreen:),
               reinterpret_cast<IMP>(fovelleDidFailFullScreenTransition),
               "v@:@");
}

// Qt's Cocoa platform plugin looks these strings up in this exact context
// when it refreshes the native Fovelle application menu. Keeping the markers
// in application source makes lupdate retain the self-contained catalogs.
[[maybe_unused]] static const char *const MacApplicationMenuTranslationSources[] = {
    QT_TRANSLATE_NOOP("MAC_APPLICATION_MENU", "About %1"),
    QT_TRANSLATE_NOOP("MAC_APPLICATION_MENU", "Preferences..."),
    QT_TRANSLATE_NOOP("MAC_APPLICATION_MENU", "Services"),
    QT_TRANSLATE_NOOP("MAC_APPLICATION_MENU", "Hide %1"),
    QT_TRANSLATE_NOOP("MAC_APPLICATION_MENU", "Hide Others"),
    QT_TRANSLATE_NOOP("MAC_APPLICATION_MENU", "Show All"),
    QT_TRANSLATE_NOOP("MAC_APPLICATION_MENU", "Quit %1")
};

[[maybe_unused]] static const char *const MacWindowMenuTranslationSources[] = {
    QT_TRANSLATE_NOOP("MAC_WINDOW_MENU", "Minimize"),
    QT_TRANSLATE_NOOP("MAC_WINDOW_MENU", "Minimize All"),
    QT_TRANSLATE_NOOP("MAC_WINDOW_MENU", "Zoom"),
    QT_TRANSLATE_NOOP("MAC_WINDOW_MENU", "Zoom All"),
    QT_TRANSLATE_NOOP("MAC_WINDOW_MENU", "Fill"),
    QT_TRANSLATE_NOOP("MAC_WINDOW_MENU", "Center"),
    QT_TRANSLATE_NOOP("MAC_WINDOW_MENU", "Move & Resize"),
    QT_TRANSLATE_NOOP("MAC_WINDOW_MENU", "Full Screen Tile"),
    QT_TRANSLATE_NOOP("MAC_WINDOW_MENU", "Remove Window from Set"),
    QT_TRANSLATE_NOOP("MAC_WINDOW_MENU", "Halves"),
    QT_TRANSLATE_NOOP("MAC_WINDOW_MENU", "Left"),
    QT_TRANSLATE_NOOP("MAC_WINDOW_MENU", "Right"),
    QT_TRANSLATE_NOOP("MAC_WINDOW_MENU", "Top"),
    QT_TRANSLATE_NOOP("MAC_WINDOW_MENU", "Bottom"),
    QT_TRANSLATE_NOOP("MAC_WINDOW_MENU", "Quarters"),
    QT_TRANSLATE_NOOP("MAC_WINDOW_MENU", "Top Left"),
    QT_TRANSLATE_NOOP("MAC_WINDOW_MENU", "Top Right"),
    QT_TRANSLATE_NOOP("MAC_WINDOW_MENU", "Bottom Left"),
    QT_TRANSLATE_NOOP("MAC_WINDOW_MENU", "Bottom Right"),
    QT_TRANSLATE_NOOP("MAC_WINDOW_MENU", "Arrange"),
    QT_TRANSLATE_NOOP("MAC_WINDOW_MENU", "Left & Right"),
    QT_TRANSLATE_NOOP("MAC_WINDOW_MENU", "Left & Quarters"),
    QT_TRANSLATE_NOOP("MAC_WINDOW_MENU", "Right & Left"),
    QT_TRANSLATE_NOOP("MAC_WINDOW_MENU", "Right & Quarters"),
    QT_TRANSLATE_NOOP("MAC_WINDOW_MENU", "Top & Bottom"),
    QT_TRANSLATE_NOOP("MAC_WINDOW_MENU", "Top & Quarters"),
    QT_TRANSLATE_NOOP("MAC_WINDOW_MENU", "Bottom & Top"),
    QT_TRANSLATE_NOOP("MAC_WINDOW_MENU", "Bottom & Quarters"),
    QT_TRANSLATE_NOOP("MAC_WINDOW_MENU", "Return to Previous Size"),
    QT_TRANSLATE_NOOP("MAC_WINDOW_MENU", "Left of Screen"),
    QT_TRANSLATE_NOOP("MAC_WINDOW_MENU", "Right of Screen"),
    QT_TRANSLATE_NOOP("MAC_WINDOW_MENU", "Bring All to Front"),
    QT_TRANSLATE_NOOP("MAC_WINDOW_MENU", "Arrange in Front"),
    QT_TRANSLATE_NOOP("MAC_WINDOW_MENU", "Enter Full Screen"),
    QT_TRANSLATE_NOOP("MAC_WINDOW_MENU", "Exit Full Screen"),
    QT_TRANSLATE_NOOP("MAC_WINDOW_MENU", "Make Window Full Screen"),
    QT_TRANSLATE_NOOP("MAC_WINDOW_MENU", "Tile Window to Left of Screen"),
    QT_TRANSLATE_NOOP("MAC_WINDOW_MENU", "Tile Window to Right of Screen"),
    QT_TRANSLATE_NOOP("MAC_WINDOW_MENU", "Move Window to Left Side of Screen"),
    QT_TRANSLATE_NOOP("MAC_WINDOW_MENU", "Move Window to Right Side of Screen"),
    QT_TRANSLATE_NOOP("MAC_WINDOW_MENU", "Cycle Through Windows")
};

@interface FovelleSettingsToolbarController : NSObject<NSToolbarDelegate>
{
@private
    QPointer<QTabBar> *_categoryTabs;
    NSArray<NSToolbarItemIdentifier> *_itemIdentifiers;
    NSArray<NSString *> *_symbolNames;
    NSToolbar *_toolbar;
}

- (instancetype)initWithCategoryTabs:(QTabBar *)categoryTabs;
- (void)selectPane:(NSToolbarItem *)sender;
- (void)syncSelection:(NSInteger)index;
- (void)setToolbar:(NSToolbar *)toolbar;
@end

@implementation FovelleSettingsToolbarController

- (instancetype)initWithCategoryTabs:(QTabBar *)categoryTabs
{
    self = [super init];
    if (!self)
        return nil;

    _categoryTabs = new QPointer<QTabBar>(categoryTabs);
    _itemIdentifiers = [@[
        @"io.github.inostarlin-passion.Fovelle.settings.general",
        @"io.github.inostarlin-passion.Fovelle.settings.shortcuts",
        @"io.github.inostarlin-passion.Fovelle.settings.mouse"
    ] retain];
    _symbolNames = [@[
        @"gearshape",
        @"keyboard",
        @"computermouse"
    ] retain];
    return self;
}

- (void)dealloc
{
    delete _categoryTabs;
    [_itemIdentifiers release];
    [_symbolNames release];
    [super dealloc];
}

- (NSArray<NSToolbarItemIdentifier> *)toolbarDefaultItemIdentifiers:(NSToolbar *)toolbar
{
    Q_UNUSED(toolbar);
    return _itemIdentifiers;
}

- (NSArray<NSToolbarItemIdentifier> *)toolbarAllowedItemIdentifiers:(NSToolbar *)toolbar
{
    Q_UNUSED(toolbar);
    return _itemIdentifiers;
}

- (NSArray<NSToolbarItemIdentifier> *)toolbarSelectableItemIdentifiers:(NSToolbar *)toolbar
{
    Q_UNUSED(toolbar);
    return _itemIdentifiers;
}

- (NSToolbarItem *)toolbar:(NSToolbar *)toolbar
      itemForItemIdentifier:(NSToolbarItemIdentifier)itemIdentifier
  willBeInsertedIntoToolbar:(BOOL)flag
{
    Q_UNUSED(toolbar);
    Q_UNUSED(flag);
    if (!_categoryTabs || _categoryTabs->isNull())
        return nil;

    const NSUInteger index = [_itemIdentifiers indexOfObject:itemIdentifier];
    if (index == NSNotFound || index >= static_cast<NSUInteger>((*_categoryTabs)->count()))
        return nil;

    auto *item = [[[NSToolbarItem alloc] initWithItemIdentifier:itemIdentifier] autorelease];
    const QString qtLabel = (*_categoryTabs)->tabText(static_cast<int>(index));
    NSString *label = qtLabel.toNSString();
    item.label = label;
    item.paletteLabel = label;
    item.toolTip = label;
    item.image = [NSImage imageWithSystemSymbolName:_symbolNames[index]
                          accessibilityDescription:label];
    item.target = self;
    item.action = @selector(selectPane:);
    return item;
}

- (void)selectPane:(NSToolbarItem *)sender
{
    const NSUInteger index = [_itemIdentifiers indexOfObject:sender.itemIdentifier];
    if (index == NSNotFound || !_categoryTabs || _categoryTabs->isNull())
        return;

    (*_categoryTabs)->setCurrentIndex(static_cast<int>(index));
    [self syncSelection:static_cast<NSInteger>(index)];
}

- (void)syncSelection:(NSInteger)index
{
    if (!_toolbar || !_categoryTabs || _categoryTabs->isNull()
        || index < 0 || index >= (*_categoryTabs)->count())
        return;

    _toolbar.selectedItemIdentifier = _itemIdentifiers[static_cast<NSUInteger>(index)];
    QWidget *settingsWindow = (*_categoryTabs)->window();
    if (settingsWindow)
        settingsWindow->setWindowTitle((*_categoryTabs)->tabText(static_cast<int>(index)));
}

- (void)setToolbar:(NSToolbar *)toolbar
{
    _toolbar = toolbar;
}

@end

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

QString selectedMacMenuLocalization()
{
    QString language = qvApp->getSettingsManager().getString(QStringLiteral("language"));
    if (language == QStringLiteral("system"))
        language = SettingsManager::languageCodeForLocale(QLocale::system());

    if (language == QStringLiteral("zh_Hans"))
        return QStringLiteral("zh-CN");
    if (language == QStringLiteral("zh_Hant"))
        return QStringLiteral("zh-TW");
    if (language == QStringLiteral("es") || language == QStringLiteral("ja"))
        return language;
    return {};
}

QString appKitMenuCommandTranslation(const QString &sourceTitle,
                                     const QString &tableKey)
{
    const QString localization = selectedMacMenuLocalization();
    if (localization.isEmpty())
        return sourceTitle;

    // macOS 15.4 introduced an explicit-localization lookup. It is important
    // here because Fovelle's chosen UI language can differ from AppleLanguages.
    if (@available(macOS 15.4, *))
    {
        CFBundleRef appKitBundle = CFBundleGetBundleWithIdentifier(CFSTR("com.apple.AppKit"));
        if (appKitBundle)
        {
            const auto key = static_cast<CFStringRef>(tableKey.toNSString());
            const auto fallback = static_cast<CFStringRef>(sourceTitle.toNSString());
            const auto locale = static_cast<CFStringRef>(localization.toNSString());
            const void *localeValues[] = { locale };
            CFArrayRef locales = CFArrayCreate(kCFAllocatorDefault, localeValues, 1,
                                               &kCFTypeArrayCallBacks);
            CFStringRef translated = CFBundleCopyLocalizedStringForLocalizations(
                        appKitBundle, key, fallback, CFSTR("MenuCommands"), locales);
            CFRelease(locales);
            const QString result = QStringFromCFString(translated);
            if (translated)
                CFRelease(translated);
            if (!result.isEmpty() && result != sourceTitle)
                return result;
        }
    }

    // 15.0-15.3, or a future AppKit table change, uses the catalogs shipped
    // with Fovelle. Unknown/dynamic window titles intentionally pass through.
    return QCoreApplication::translate("MAC_WINDOW_MENU",
                                       sourceTitle.toUtf8().constData());
}

void localizeNativeWindowMenu(NSMenu *menu)
{
    if (!menu)
        return;

    for (NSMenuItem *item in menu.itemArray)
    {
        if (item.separatorItem)
            continue;

        const QString sourceTitle = QString::fromNSString(item.title);
        const bool isSubmenuTitle = item.submenu != nil;
        const QString translated = QVCocoaFunctions::localizedWindowMenuTitle(
                    sourceTitle, isSubmenuTitle);
        if (!translated.isEmpty() && translated != sourceTitle)
            item.title = translated.toNSString();
        if (item.submenu)
            localizeNativeWindowMenu(item.submenu);
    }
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

bool sourceHasAlpha(CGImageSourceRef source)
{
    bool hasAlpha = false;
    CFDictionaryRef properties =
            CGImageSourceCopyPropertiesAtIndex(source, 0, nullptr);
    if (!properties)
        return false;

    const CFTypeRef value =
            CFDictionaryGetValue(properties, kCGImagePropertyHasAlpha);
    if (value && CFGetTypeID(value) == CFBooleanGetTypeID())
        hasAlpha = CFBooleanGetValue(static_cast<CFBooleanRef>(value));
    else if (value && CFGetTypeID(value) == CFNumberGetTypeID())
    {
        int numericValue = 0;
        if (CFNumberGetValue(static_cast<CFNumberRef>(value),
                             kCFNumberIntType, &numericValue))
            hasAlpha = numericValue != 0;
    }
    CFRelease(properties);
    return hasAlpha;
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

QImage imageFromCGImage(CGImageRef cgImage, const int largestDimension = 0)
{
    if (!cgImage)
        return {};

    const size_t sourceWidth = CGImageGetWidth(cgImage);
    const size_t sourceHeight = CGImageGetHeight(cgImage);
    if (sourceWidth == 0 || sourceHeight == 0)
        return {};
    const double boundedScale = largestDimension > 0
            ? std::min(1.0, static_cast<double>(largestDimension)
                              / std::max(sourceWidth, sourceHeight))
            : 1.0;
    const size_t width = std::max<size_t>(
            1, static_cast<size_t>(std::lround(sourceWidth * boundedScale)));
    const size_t height = std::max<size_t>(
            1, static_cast<size_t>(std::lround(sourceHeight * boundedScale)));
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

    CGContextSetInterpolationQuality(context, kCGInterpolationHigh);
    CGContextDrawImage(context, CGRectMake(
            0, 0, static_cast<CGFloat>(width), static_cast<CGFloat>(height)), cgImage);
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

CFMutableDictionaryRef thumbnailOptions(CGImageSourceRef source,
                                        const int largestDimension,
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

bool hasAuxiliaryImage(CGImageSourceRef source, CFStringRef auxiliaryType)
{
    CFDictionaryRef auxiliary = CGImageSourceCopyAuxiliaryDataInfoAtIndex(source, 0, auxiliaryType);
    if (!auxiliary)
        return false;
    CFRelease(auxiliary);
    return true;
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
// bounded Ghostscript child process into a high-level PDF document.  A small
// preview is retained for UI sampling, while the PDF drawing commands remain
// the authoritative source used by the viewport at every zoom level.
constexpr quint32 DosEPSMagic = 0xC6D3D0C5;
constexpr int EPSRendererStartTimeoutMs = 5000;
constexpr int EPSRendererTimeoutMs = 30000;
constexpr qsizetype MaxEPSRendererDiagnosticBytes = 64 * 1024;
constexpr quint64 MaxEPSRenderedPixels = 64ULL * 1024ULL * 1024ULL;
constexpr quint64 MaxEPSIntermediatePDFBytes = 256ULL * 1024ULL * 1024ULL;
constexpr int EPSPreviewLargestDimension = 512;

struct EPSReadResult
{
    bool recognized {false};
    QImage image;
    Qv::VectorImageData vectorImage;
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

    const QString applicationDirectory = QCoreApplication::applicationDirPath();
    const QStringList bundledPaths {
        QDir(applicationDirectory).filePath("../Resources/ghostscript/bin/gs"),
        QDir(applicationDirectory).filePath("../Resources/Ghostscript/bin/gs"),
        QDir(applicationDirectory).filePath("../Resources/ghostscript/gs")
    };
    for (const QString &path : bundledPaths)
    {
        if (QFileInfo(path).isExecutable())
            return QFileInfo(path).absoluteFilePath();
    }

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
            "EPS rendering requires Ghostscript; the bundled runtime is unavailable or FOVELLE_GHOSTSCRIPT is invalid");
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
    const QFileInfo executableInfo(executable);
    const QString executablePath = executableInfo.absoluteFilePath();
    if (executablePath.contains(QStringLiteral("/Contents/Resources/ghostscript/")))
    {
        QDir runtimeRoot(executableInfo.absolutePath());
        runtimeRoot.cdUp();
        const QDir shareRoot(runtimeRoot.filePath("share/ghostscript"));
        QDir versionRoot(shareRoot);
        if (!versionRoot.exists(QStringLiteral("Resource/Init")))
        {
            const QStringList versions = shareRoot.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
            for (const QString &version : versions)
            {
                QDir candidate(shareRoot.filePath(version));
                if (candidate.exists(QStringLiteral("Resource/Init")))
                {
                    versionRoot = candidate;
                    break;
                }
            }
        }
        if (versionRoot.exists(QStringLiteral("Resource/Init")))
        {
            QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
            environment.insert("GS_LIB", QStringList {
                versionRoot.filePath("Resource/Init"),
                versionRoot.filePath("lib"),
                versionRoot.filePath("Resource"),
                versionRoot.filePath("iccprofiles")
            }.join(QDir::listSeparator()));
            environment.insert("GS_FONTPATH", versionRoot.filePath("fonts"));
            process.setProcessEnvironment(environment);
        }
    }
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

    const int previewLargestDimension = requestedLargestDimension > 0
            ? qMin(requestedLargestDimension, EPSPreviewLargestDimension)
            : EPSPreviewLargestDimension;
    result.image = imageFromPDFPage(pdfPath, previewLargestDimension,
                                    result.intrinsicSize, result.errorString);
    if (result.image.isNull())
        return result;

    QFile pdfFile(pdfPath);
    if (!pdfFile.open(QIODevice::ReadOnly))
    {
        result.image = {};
        result.errorString = QStringLiteral("The Ghostscript EPS rendering could not be retained");
        return result;
    }
    const QByteArray pdfData = pdfFile.readAll();
    if (pdfData.isEmpty()
        || static_cast<quint64>(pdfData.size()) > MaxEPSIntermediatePDFBytes)
    {
        result.image = {};
        result.errorString = QStringLiteral("The Ghostscript EPS rendering exceeded the safety limit");
        return result;
    }
    result.vectorImage.format = Qv::VectorImageFormat::Pdf;
    result.vectorImage.encodedData = pdfData;
    result.vectorImage.logicalSize = QSizeF(result.intrinsicSize);
    return result;
}

class NativeMetalImageGraph
{
public:
    virtual ~NativeMetalImageGraph() = default;
    virtual CIImage *hdrCIImage() const = 0;
    virtual CIImage *sdrCIImage() const = 0;
    virtual CIImage *gainMapCIImage() const = 0;
    virtual const QVCocoaFunctions::HDRMetadata &rendererMetadata() const = 0;
    virtual bool isHDR() const = 0;
};

class NativeHDRImage final : public QVCocoaFunctions::HDRImage,
                             public NativeMetalImageGraph
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
    CIImage *hdrCIImage() const override { return hdr; }
    CIImage *sdrCIImage() const override { return sdr; }
    CIImage *gainMapCIImage() const override { return gainMap; }
    const QVCocoaFunctions::HDRMetadata &rendererMetadata() const override
        { return imageMetadata; }
    bool isHDR() const override { return true; }

private:
    CIImage *hdr{ nil };
    CIImage *sdr{ nil };
    CIImage *gainMap{ nil };
    QVCocoaFunctions::HDRMetadata imageMetadata;
};

class NativeSDRImage final : public QVCocoaFunctions::SDRImage,
                             public NativeMetalImageGraph
{
public:
    struct Tile
    {
        CIImage *image{ nil };
        CGImageRef layerImage{ nullptr };
        CGRect dataRect{ CGRectZero };
        CGRect coreRect{ CGRectZero };
        CGRect layerFrame{ CGRectZero };
        CGRect layerContentsRect{ CGRectZero };
    };

    NativeSDRImage(CGImageRef image, const bool alpha)
        : materializedImage(image ? CGImageRetain(image) : nullptr),
          sourceSize(image
                  ? QSize(static_cast<int>(CGImageGetWidth(image)),
                          static_cast<int>(CGImageGetHeight(image)))
                  : QSize()),
          sourceHasAlpha(alpha)
    {
        imageMetadata.sourceKind = QStringLiteral("sdr");
        imageMetadata.pixelSize = sourceSize;
        imageMetadata.contentHeadroom = 1.0F;
        imageMetadata.bitsPerComponent = image
                ? static_cast<int>(CGImageGetBitsPerComponent(image)) : 0;
        materializedByteCount = image
                ? static_cast<quint64>(CGImageGetBytesPerRow(image))
                        * static_cast<quint64>(CGImageGetHeight(image))
                : 0;
        buildFullSingleImage();
        buildTiles();
        // `fullSingleImage` and every tile retain exactly the providers they
        // need. Drop this extra whole-image ownership before interaction; the
        // loader still owns its decode long enough to build the bounded Qt
        // cold-open proxy immediately after this constructor returns.
        if (materializedImage) {
            CGImageRelease(materializedImage);
            materializedImage = nullptr;
        }
    }

    NativeSDRImage(QImage image, const bool alpha)
        : backingImage(std::move(image)),
          sourceSize(backingImage.size()),
          sourceHasAlpha(alpha)
    {
        if (!supportsDirectQImageFormat(backingImage.format()))
        {
            backingImage = backingImage.convertToFormat(
                    sourceHasAlpha
                            ? QImage::Format_ARGB32_Premultiplied
                            : QImage::Format_RGB32);
            sourceSize = backingImage.size();
        }
        imageMetadata.sourceKind = QStringLiteral("sdr-bounded-provider");
        imageMetadata.pixelSize = sourceSize;
        imageMetadata.contentHeadroom = 1.0F;
        imageMetadata.bitsPerComponent = backingImage.isNull() ? 0 : 8;
        materializedByteCount = static_cast<quint64>(
                qMax<qsizetype>(0, backingImage.sizeInBytes()));
        buildQImageTiles();
    }

    ~NativeSDRImage() override
    {
        for (const Tile &tile : std::as_const(tiles)) {
            [tile.image release];
            if (tile.layerImage)
                CGImageRelease(tile.layerImage);
        }
        [fullSingleImage release];
        if (materializedImage)
            CGImageRelease(materializedImage);
    }

    QSize pixelSize() const override { return sourceSize; }
    bool hasAlpha() const override { return sourceHasAlpha; }
    CIImage *hdrCIImage() const override { return nil; }
    CIImage *sdrCIImage() const override { return nil; }
    CIImage *gainMapCIImage() const override { return nil; }
    const QVCocoaFunctions::HDRMetadata &rendererMetadata() const override
        { return imageMetadata; }
    bool isHDR() const override { return false; }
    const QVector<Tile> &sourceTiles() const { return tiles; }
    CIImage *fullSingleCIImage() const { return fullSingleImage; }
    quint64 materializedBytes() const
        { return materializedByteCount; }

private:
    static bool supportsDirectQImageFormat(const QImage::Format format)
    {
        return format == QImage::Format_RGB32
                || format == QImage::Format_ARGB32
                || format == QImage::Format_ARGB32_Premultiplied
                || format == QImage::Format_RGB888
                || format == QImage::Format_RGBX8888
                || format == QImage::Format_RGBA8888
                || format == QImage::Format_RGBA8888_Premultiplied;
    }

    static CGBitmapInfo bitmapInfoForQImage(const QImage::Format format)
    {
        switch (format)
        {
        case QImage::Format_RGB32:
            return static_cast<CGBitmapInfo>(
                    kCGBitmapByteOrder32Host | kCGImageAlphaNoneSkipFirst);
        case QImage::Format_ARGB32:
            return static_cast<CGBitmapInfo>(
                    kCGBitmapByteOrder32Host | kCGImageAlphaFirst);
        case QImage::Format_ARGB32_Premultiplied:
            return static_cast<CGBitmapInfo>(
                    kCGBitmapByteOrder32Host | kCGImageAlphaPremultipliedFirst);
        case QImage::Format_RGBX8888:
            return static_cast<CGBitmapInfo>(
                    kCGBitmapByteOrder32Big | kCGImageAlphaNoneSkipLast);
        case QImage::Format_RGBA8888:
            return static_cast<CGBitmapInfo>(
                    kCGBitmapByteOrder32Big | kCGImageAlphaLast);
        case QImage::Format_RGBA8888_Premultiplied:
            return static_cast<CGBitmapInfo>(
                    kCGBitmapByteOrder32Big | kCGImageAlphaPremultipliedLast);
        case QImage::Format_RGB888:
        default:
            return static_cast<CGBitmapInfo>(
                    kCGBitmapByteOrderDefault | kCGImageAlphaNone);
        }
    }

    static CGColorSpaceRef colorSpaceForQImage(const QImage &image)
    {
        const QByteArray profile = image.colorSpace().iccProfile();
        if (!profile.isEmpty())
        {
            CFDataRef data = CFDataCreate(
                    kCFAllocatorDefault,
                    reinterpret_cast<const UInt8 *>(profile.constData()),
                    static_cast<CFIndex>(profile.size()));
            CGColorSpaceRef colorSpace = data
                    ? CGColorSpaceCreateWithICCData(data) : nullptr;
            if (data)
                CFRelease(data);
            if (colorSpace)
                return colorSpace;
        }
        return colorSyncSrgbColorSpace();
    }

    static void releaseQImageProvider(void *info, const void *, size_t)
    {
        delete static_cast<QImage *>(info);
    }

    static CGImageRef createQImageBand(const QImage &image,
                                       const int top, const int height)
    {
        if (image.isNull() || top < 0 || height <= 0
            || top + height > image.height()
            || !supportsDirectQImageFormat(image.format()))
            return nullptr;

        auto *retainedImage = new QImage(image);
        const uchar *bytes = retainedImage->constScanLine(top);
        const size_t bytesPerRow = static_cast<size_t>(
                retainedImage->bytesPerLine());
        const size_t providerSize = bytesPerRow
                * static_cast<size_t>(height);
        CGDataProviderRef provider = CGDataProviderCreateWithData(
                retainedImage, bytes, providerSize, releaseQImageProvider);
        if (!provider)
        {
            delete retainedImage;
            return nullptr;
        }

        CGColorSpaceRef colorSpace = colorSpaceForQImage(*retainedImage);
        const size_t bitsPerPixel = static_cast<size_t>(
                retainedImage->depth());
        CGImageRef band = colorSpace ? CGImageCreate(
                static_cast<size_t>(retainedImage->width()),
                static_cast<size_t>(height),
                8,
                bitsPerPixel,
                bytesPerRow,
                colorSpace,
                bitmapInfoForQImage(retainedImage->format()),
                provider,
                nullptr,
                false,
                kCGRenderingIntentDefault) : nullptr;
        if (colorSpace)
            CGColorSpaceRelease(colorSpace);
        CGDataProviderRelease(provider);
        return band;
    }

    void appendTile(CGImageRef tileCGImage, const QRect &data,
                    const QRect &core)
    {
        CIImage *tileImage = tileCGImage
                ? [CIImage imageWithCGImage:tileCGImage] : nil;
        CGImageRef layerImage = tileCGImage
                ? CGImageRetain(tileCGImage) : nullptr;
        if (!tileImage)
        {
            if (layerImage)
                CGImageRelease(layerImage);
            return;
        }

        Tile tile;
        tile.image = [tileImage retain];
        tile.layerImage = layerImage;
        const QRect coreImageData = QVCocoaFunctions::coreImageTileRect(
                data, sourceSize.height());
        const QRect coreImageCore = QVCocoaFunctions::coreImageTileRect(
                core, sourceSize.height());
        tile.dataRect = CGRectMake(
                coreImageData.x(), coreImageData.y(),
                coreImageData.width(), coreImageData.height());
        tile.coreRect = CGRectMake(
                coreImageCore.x(), coreImageCore.y(),
                coreImageCore.width(), coreImageCore.height());
        const CGFloat dataWidth = data.width();
        const CGFloat dataHeight = data.height();
        const CGFloat coreOffsetX = core.x() - data.x();
        const CGFloat coreOffsetFromTop = core.y() - data.y();
        tile.layerFrame = CGRectMake(
                coreImageCore.x(), coreImageCore.y(),
                coreImageCore.width(), coreImageCore.height());
        tile.layerContentsRect = CGRectMake(
                coreOffsetX / dataWidth,
                (dataHeight - coreOffsetFromTop - core.height())
                        / dataHeight,
                core.width() / dataWidth,
                core.height() / dataHeight);
        tiles.append(tile);
    }

    void buildFullSingleImage()
    {
        if (!materializedImage || sourceSize.isEmpty())
            return;
        // A single full-resolution CIImage is the fastest path at or below the
        // conservative 8192-pixel Apple-family 2D texture-limit floor. Larger
        // sources always use the bounded authoritative tiles below.
        constexpr int SafeFullImageDimension = 8192;
        const int largest = std::max(sourceSize.width(), sourceSize.height());
        if (largest <= SafeFullImageDimension)
            fullSingleImage = [[CIImage imageWithCGImage:materializedImage] retain];
    }

    void buildTiles()
    {
        if (!materializedImage || sourceSize.isEmpty())
            return;

        constexpr int TileSize = 2048;
        constexpr int SamplingBorder = 2;
        const int width = sourceSize.width();
        const int height = sourceSize.height();
        for (int coreY = 0; coreY < height; coreY += TileSize)
        {
            for (int coreX = 0; coreX < width; coreX += TileSize)
            {
                const QRect core(coreX, coreY,
                                 std::min(TileSize, width - coreX),
                                 std::min(TileSize, height - coreY));
                const QRect data = core.adjusted(
                        -SamplingBorder, -SamplingBorder,
                        SamplingBorder, SamplingBorder)
                        .intersected(QRect(0, 0, width, height));
                // CGImageCreateWithImageInRect exposes only this bounded source
                // region to Core Image. Every leaf is therefore below Metal's
                // texture limit, and the implementation never materializes a
                // second full-size QImage for the multi-gigabyte bitmap.
                CGImageRef tileCGImage = CGImageCreateWithImageInRect(
                        materializedImage,
                        CGRectMake(data.x(), data.y(),
                                   data.width(), data.height()));
                appendTile(tileCGImage, data, core);
                if (tileCGImage)
                    CGImageRelease(tileCGImage);
            }
        }
    }

    void buildQImageTiles()
    {
        if (backingImage.isNull() || sourceSize.isEmpty())
            return;

        constexpr int TileSize = 2048;
        constexpr int SamplingBorder = 2;
        const int width = sourceSize.width();
        const int height = sourceSize.height();
        for (int coreY = 0; coreY < height; coreY += TileSize)
        {
            const QRect rowCore(0, coreY, width,
                                std::min(TileSize, height - coreY));
            const QRect rowData = rowCore.adjusted(
                    0, -SamplingBorder, 0, SamplingBorder)
                    .intersected(QRect(0, 0, width, height));
            // Every provider is a full-width horizontal band below 2 GiB.
            // Tile crops retain that bounded provider, while the implicitly
            // shared QImage keeps one authoritative full-resolution decode.
            CGImageRef bandImage = createQImageBand(
                    backingImage, rowData.y(), rowData.height());
            if (!bandImage)
                continue;

            for (int coreX = 0; coreX < width; coreX += TileSize)
            {
                const QRect core(coreX, coreY,
                                 std::min(TileSize, width - coreX),
                                 rowCore.height());
                const QRect data = core.adjusted(
                        -SamplingBorder, -SamplingBorder,
                        SamplingBorder, SamplingBorder)
                        .intersected(QRect(0, 0, width, height));
                CGImageRef tileCGImage = CGImageCreateWithImageInRect(
                        bandImage,
                        CGRectMake(data.x(), 0,
                                   data.width(), data.height()));
                appendTile(tileCGImage, data, core);
                if (tileCGImage)
                    CGImageRelease(tileCGImage);
            }
            CGImageRelease(bandImage);
        }
    }

    CGImageRef materializedImage{ nullptr };
    QImage backingImage;
    quint64 materializedByteCount{ 0 };
    CIImage *fullSingleImage{ nil };
    QVector<Tile> tiles;
    QSize sourceSize;
    bool sourceHasAlpha{ false };
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
    std::atomic<quint64> presentedRenderGeneration{ 0 };
    std::atomic<quint64> missedTargetDeadlineCount{ 0 };
    std::atomic<double> lastPresentedTime{ 0.0 };
    std::atomic<double> lastGPUExecutionMilliseconds{ 0.0 };
    std::atomic<double> lastPresentedIntervalMilliseconds{ 0.0 };
    std::atomic<double> lastRequestToPresentationMilliseconds{ 0.0 };
    std::atomic<double> maximumInteractiveRenderMilliseconds{ 0.0 };
    std::atomic<double> maximumInteractiveGPUExecutionMilliseconds{ 0.0 };
    std::atomic<bool> firstVisibleFrameUsesFinalHeadroom{ false };
    std::atomic<quint64> sdrAuthoritativePresentedFrameCount{ 0 };
};

void updateAtomicMaximum(std::atomic<double> &destination, const double value)
{
    double previous = destination.load();
    while (value > previous
           && !destination.compare_exchange_weak(previous, value)) {
    }
}

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

        viewport = viewportWidget;
        // A QWidget::winId() call on the QGraphicsView viewport promotes the
        // complete QAbstractScrollArea ancestry to native child windows. That
        // splits SDR dragging across several independent Cocoa backing stores.
        // The top-level widget is native already, so host the HDR-only CALayer
        // subtree there and clip it to the alien viewport's mapped rectangle.
        // The platform window can release its QNSView as soon as a QWidget is
        // closed even when the QWidget itself deliberately remains alive.
        // Diagnostics and presentation timers belong to that QWidget, so keep
        // their native host valid until this renderer is destroyed.
        nativeView = [reinterpret_cast<NSView *>(
            viewportWidget->window()->winId()) retain];
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
        presentationContainerLayer.frame = CGRectZero;
        presentationContainerLayer.autoresizingMask = kCALayerNotSizable;
        // Do not snapshot QNSView's AppKit-managed backing-layer flag here.
        // During attachment it can still be YES and later settle to NO, which
        // leaves this standalone subtree in the opposite coordinate system.
        presentationContainerLayer.geometryFlipped =
                QVCocoaFunctions::persistentHDRLayerGeometryFlipped();
        presentationContainerLayer.masksToBounds = YES;
        presentationContainerLayer.opacity = 0.0F;
        presentationContainerLayer.hidden = YES;
        viewportBackgroundLayer = [[CALayer layer] retain];
        viewportBackgroundLayer.opaque = YES;
        viewportBackgroundLayer.frame = CGRectZero;
        viewportBackgroundLayer.autoresizingMask =
                kCALayerWidthSizable | kCALayerHeightSizable;
        viewportBackgroundLayer.hidden = YES;

        // SDR pixels are decoded once into bounded CGImage tiles, then owned
        // by a persistent Core Animation subtree. Pan and zoom subsequently
        // mutate one affine transform instead of re-importing every visible
        // tile into a new Core Image command buffer on every display tick.
        persistentSDRTileLayer = [[CALayer layer] retain];
        persistentSDRTileLayer.geometryFlipped = YES;
        persistentSDRTileLayer.masksToBounds = NO;
        persistentSDRTileLayer.hidden = YES;
        persistentSDRTileLayer.opacity = 0.0F;

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
        navigationOverlayLayer.frame = CGRectZero;
        navigationOverlayLayer.autoresizingMask = kCALayerNotSizable;
        navigationOverlayLayer.zPosition = 1000.0;
        for (int index = 0; index < 2; ++index) {
            navigationButtonLayers[index] = [CALayer layer];
            // Flatten the translucent bottom and opaque chevron before the
            // container opacity is applied. Without group opacity, Core
            // Animation applies opacity to overlapping children separately.
            navigationButtonLayers[index].allowsGroupOpacity = YES;
            navigationButtonLayers[index].hidden = YES;
            navigationBackgroundLayers[index] = [CAShapeLayer layer];
            navigationBackgroundLayers[index].hidden = YES;
            navigationChevronLayers[index] = [CAShapeLayer layer];
            navigationChevronLayers[index].hidden = YES;
            navigationChevronLayers[index].fillColor = nil;
            navigationChevronLayers[index].lineWidth = 4.0;
            navigationChevronLayers[index].lineCap = kCALineCapRound;
            navigationChevronLayers[index].lineJoin = kCALineJoinRound;
            [navigationOverlayLayer addSublayer:navigationButtonLayers[index]];
            [navigationButtonLayers[index] addSublayer:navigationBackgroundLayers[index]];
            [navigationButtonLayers[index] addSublayer:navigationChevronLayers[index]];
        }
        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        metalLayer.frame = CGRectZero;
        metalLayer.hidden = YES;
        metalLayer.opacity = 0.0F;
        [nativeView.layer addSublayer:presentationContainerLayer];
        [presentationContainerLayer addSublayer:viewportBackgroundLayer];
        [presentationContainerLayer addSublayer:persistentSDRTileLayer];
        [presentationContainerLayer addSublayer:persistentImageLayer];
        [presentationContainerLayer addSublayer:metalLayer];
        // Keep controls fixed in viewport coordinates while the persistent
        // HDR image layer moves underneath them.
        [nativeView.layer addSublayer:navigationOverlayLayer];
        [CATransaction commit];
        syncViewportLayerGeometry();
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
            configureDisplayLinkFrameRate(displayLink);
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
        clearPersistentSDRTileSurface();
        persistentImageLayer.contents = nil;
        if (persistentImage)
            CGImageRelease(persistentImage);
        if (persistentCheckerboardImage)
            CGImageRelease(persistentCheckerboardImage);
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
        [persistentSDRTileLayer release];
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
        [nativeView release];
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

    void setCheckerboardBackground(const bool enabled)
    {
        if (checkerboardBackground == enabled)
            return;
        checkerboardBackground = enabled;
        if (image && !imageIsHDR) {
            updatePersistentSDRBackground(latestViewportSize);
            renderPending = true;
        }
    }

    CGRect viewportFrameInNativeView() const
    {
        if (!viewport || !nativeView)
            return CGRectZero;

        QWidget *hostWidget = viewport->window();
        const QPoint origin = viewport->mapTo(hostWidget, QPoint(0, 0));
        const QSize size = viewport->size();
        const CGFloat width = std::max(0, size.width());
        const CGFloat height = std::max(0, size.height());
        const CGFloat y = nativeView.layer.geometryFlipped
                ? origin.y()
                : CGRectGetHeight(nativeView.bounds) - origin.y() - height;
        return CGRectMake(origin.x(), y, width, height);
    }

    void syncViewportLayerGeometry()
    {
        if (!presentationContainerLayer || !navigationOverlayLayer
            || !viewportBackgroundLayer || !metalLayer)
            return;

        const CGRect viewportFrame = viewportFrameInNativeView();
        const CGRect viewportBounds = CGRectMake(
                0.0, 0.0, viewportFrame.size.width, viewportFrame.size.height);
        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        presentationContainerLayer.frame = viewportFrame;
        presentationContainerLayer.bounds = viewportBounds;
        viewportBackgroundLayer.frame = viewportBounds;
        metalLayer.frame = viewportBounds;
        navigationOverlayLayer.frame = viewportFrame;
        navigationOverlayLayer.bounds = viewportBounds;
        [CATransaction commit];
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
        if (!presentationActiveRequested || !imageIsHDR)
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
            setExtendedDynamicRangeEnabled(imageIsHDR);
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

        if (!animated || !imageIsHDR || distance <= 0.001F) {
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

        syncViewportLayerGeometry();

        CALayer *buttonLayer = navigationButtonLayers[index];
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
        // Light artwork is the transparent-chevron variant: it has no hover
        // tile, matching the reference Photos control.
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
        // The old implementation drove two sibling shape layers with the
        // same numeric opacity. Their independent compositor surfaces still
        // produced different perceived fade curves because the rounded fill
        // has translucent color while the chevron is opaque. A single button
        // container is now the only animated surface; its children stay at
        // full opacity and therefore enter/leave in one compositor step.
        buttonLayer.frame = frame;
        buttonLayer.opacity = boundedOpacity;
        buttonLayer.hidden = !artworkVisible;
        backgroundLayer.frame = CGRectMake(0.0, 0.0, frameWidth, frameHeight);
        backgroundLayer.path = backgroundPath;
        backgroundLayer.fillColor = backgroundCGColor;
        backgroundLayer.opacity = 1.0F;
        backgroundLayer.hidden = !artworkVisible || !background.isValid();
        chevronLayer.frame = CGRectMake(0.0, 0.0, frameWidth, frameHeight);
        chevronLayer.path = chevronPath;
        chevronLayer.strokeColor = foregroundCGColor;
        chevronLayer.opacity = 1.0F;
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
            if (!navigationButtonLayers[layerIndex].hidden
                && navigationButtonLayers[layerIndex].opacity > 0.001F)
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
            navigationButtonLayers[index].hidden = YES;
        }
        [CATransaction commit];
        state.nativeNavigationVisibleCount = 0;
        ++state.navigationOverlayUpdateCount;
    }

    void clearPersistentSDRTileSurface()
    {
        persistentSDRTileSurfaceReady = false;
        if (!persistentSDRTileLayer)
            return;

        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        persistentSDRTileLayer.hidden = YES;
        persistentSDRTileLayer.opacity = 0.0F;
        NSArray<CALayer *> *oldTileLayers = [persistentSDRTileLayer.sublayers copy];
        for (CALayer *tileLayer in oldTileLayers) {
            tileLayer.contents = nil;
            [tileLayer removeFromSuperlayer];
        }
        [oldTileLayers release];
        [CATransaction commit];
    }

    void updatePersistentSDRBackground(const QSize &viewportSize)
    {
        if (!viewportBackgroundLayer)
            return;
        if (!checkerboardBackground || viewportSize.isEmpty()) {
            [CATransaction begin];
            [CATransaction setDisableActions:YES];
            viewportBackgroundLayer.contents = nil;
            [CATransaction commit];
            if (persistentCheckerboardImage) {
                CGImageRelease(persistentCheckerboardImage);
                persistentCheckerboardImage = nullptr;
            }
            persistentCheckerboardPixelSize = {};
            return;
        }

        NSScreen *screen = nativeView.window.screen ?: NSScreen.mainScreen;
        const CGFloat scale = nativeView.window
                ? nativeView.window.backingScaleFactor
                : (screen ? screen.backingScaleFactor : 1.0);
        const QSize pixelSize(
                std::max(1, qRound(viewportSize.width() * scale)),
                std::max(1, qRound(viewportSize.height() * scale)));
        if (!persistentCheckerboardImage
            || persistentCheckerboardPixelSize != pixelSize) {
            const size_t width = static_cast<size_t>(pixelSize.width());
            const size_t height = static_cast<size_t>(pixelSize.height());
            CGContextRef bitmap = CGBitmapContextCreate(
                    nullptr, width, height, 8, width * 4,
                    backgroundColorSpace,
                    static_cast<CGBitmapInfo>(
                            kCGImageAlphaNoneSkipLast | kCGBitmapByteOrder32Big));
            if (!bitmap)
                return;
            CGContextSetRGBFillColor(bitmap, 1.0, 1.0, 1.0, 1.0);
            CGContextFillRect(bitmap, CGRectMake(0.0, 0.0, width, height));
            CGContextSetRGBFillColor(bitmap, 0.8, 0.8, 0.8, 1.0);
            const int tile = std::max(1, qRound(16.0 * scale));
            for (int y = 0; y < pixelSize.height(); y += tile) {
                for (int x = 0; x < pixelSize.width(); x += tile) {
                    if (((x / tile) + (y / tile)) % 2 == 0)
                        continue;
                    CGContextFillRect(bitmap, CGRectMake(
                            x, y,
                            std::min(tile, pixelSize.width() - x),
                            std::min(tile, pixelSize.height() - y)));
                }
            }
            CGImageRef checkerboard = CGBitmapContextCreateImage(bitmap);
            CGContextRelease(bitmap);
            if (!checkerboard)
                return;
            if (persistentCheckerboardImage)
                CGImageRelease(persistentCheckerboardImage);
            persistentCheckerboardImage = checkerboard;
            persistentCheckerboardPixelSize = pixelSize;
        }

        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        viewportBackgroundLayer.contents =
                reinterpret_cast<id>(persistentCheckerboardImage);
        viewportBackgroundLayer.contentsScale = scale;
        viewportBackgroundLayer.contentsGravity = kCAGravityResize;
        [CATransaction commit];
    }

    bool installPersistentSDRTileSurface(const NativeSDRImage &nativeImage)
    {
        clearPersistentSDRTileSurface();
        if (!persistentSDRTileLayer || nativeImage.pixelSize().isEmpty()
            || nativeImage.sourceTiles().isEmpty())
            return false;

        const QSize sourceSize = nativeImage.pixelSize();
        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        persistentSDRTileLayer.bounds = CGRectMake(
                0.0, 0.0, sourceSize.width(), sourceSize.height());
        persistentSDRTileLayer.anchorPoint = CGPointZero;
        persistentSDRTileLayer.position = CGPointZero;
        persistentSDRTileLayer.affineTransform = CGAffineTransformIdentity;
        for (const NativeSDRImage::Tile &tile : nativeImage.sourceTiles()) {
            if (!tile.layerImage || CGRectIsEmpty(tile.layerFrame)
                || CGRectIsEmpty(tile.layerContentsRect))
                continue;
            CALayer *tileLayer = [CALayer layer];
            tileLayer.frame = tile.layerFrame;
            tileLayer.contents = reinterpret_cast<id>(tile.layerImage);
            tileLayer.contentsRect = tile.layerContentsRect;
            tileLayer.contentsGravity = kCAGravityResize;
            tileLayer.contentsScale = 1.0;
            tileLayer.opaque = !nativeImage.hasAlpha();
            tileLayer.minificationFilter = kCAFilterTrilinear;
            tileLayer.magnificationFilter = kCAFilterLinear;
            tileLayer.allowsEdgeAntialiasing = NO;
            tileLayer.drawsAsynchronously = YES;
            [persistentSDRTileLayer addSublayer:tileLayer];
        }
        [CATransaction commit];

        persistentSDRTileSurfaceReady =
                persistentSDRTileLayer.sublayers.count
                == static_cast<NSUInteger>(nativeImage.sourceTiles().size());
        if (!persistentSDRTileSurfaceReady)
            clearPersistentSDRTileSurface();
        return persistentSDRTileSurfaceReady;
    }

    void updatePersistentSDRTileGeometry(const NativeSDRImage &nativeImage,
                                         const QSize &viewportSize,
                                         const QPolygonF &corners)
    {
        if (!persistentSDRTileSurfaceReady || viewportSize.isEmpty()
            || corners.size() < 4)
            return;

        updatePersistentSDRBackground(viewportSize);
        const QSize sourceSize = nativeImage.pixelSize();
        const QTransform qtTransform = QVCocoaFunctions::persistentHDRLayerTransform(
                sourceSize, corners);
        const CGAffineTransform transform = CGAffineTransformMake(
                qtTransform.m11(), qtTransform.m12(),
                qtTransform.m21(), qtTransform.m22(),
                qtTransform.dx(), qtTransform.dy());
        const CGRect viewportBounds = CGRectMake(
                0.0, 0.0, viewportSize.width(), viewportSize.height());
        int visibleTileCount = 0;
        for (const NativeSDRImage::Tile &tile : nativeImage.sourceTiles()) {
            const CGRect output = CGRectIntersection(
                    CGRectApplyAffineTransform(tile.layerFrame, transform),
                    viewportBounds);
            if (!CGRectIsNull(output) && !CGRectIsEmpty(output))
                ++visibleTileCount;
        }

        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        viewportBackgroundLayer.frame = presentationContainerLayer.bounds;
        persistentSDRTileLayer.bounds = CGRectMake(
                0.0, 0.0, sourceSize.width(), sourceSize.height());
        persistentSDRTileLayer.anchorPoint = CGPointZero;
        persistentSDRTileLayer.position = CGPointZero;
        persistentSDRTileLayer.affineTransform = transform;
        [CATransaction commit];
        state.sdrVisibleTileCount = visibleTileCount;
        ++state.compositorGeometryUpdateCount;
    }

    void revealPersistentSDRTileSurface()
    {
        if (!persistentSDRTileSurfaceReady)
            return;
        const bool firstReveal = !presentationState->firstFramePresented;

        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        persistentSDRTileLayer.hidden = NO;
        persistentSDRTileLayer.opacity = 1.0F;
        persistentImageLayer.hidden = YES;
        persistentImageLayer.opacity = 0.0F;
        viewportBackgroundLayer.hidden = NO;
        metalLayer.opacity = 0.0F;
        metalLayer.hidden = YES;
        [CATransaction commit];
        if (firstReveal) {
            presentationState->firstFrameSubmitted = YES;
            presentationState->firstFramePresented = YES;
            presentationState->hdrPrepared = YES;
            if (frameFlow)
                frameFlow->firstVisibleFrameUsesFinalHeadroom.store(true);
        }
        state.drawableGeometryMatches = true;
        if (firstReveal)
            applyPresentationTarget(false);
        if (displayLink)
            displayLink.paused = YES;
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
        syncViewportLayerGeometry();
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
        viewportBackgroundLayer.frame = presentationContainerLayer.bounds;
        persistentImageLayer.bounds = CGRectMake(
                0.0, 0.0, sourceWidth, sourceHeight);
        persistentImageLayer.anchorPoint = CGPointZero;
        persistentImageLayer.position = CGPointZero;
        persistentImageLayer.affineTransform = transform;
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
        if (!imageIsHDR || persistentSurfaceReady || persistentSurfacePreparationInFlight
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

    int maximumFramesPerSecondForCurrentDisplay() const
    {
        NSScreen *screen = nativeView.window.screen ?: NSScreen.mainScreen;
        if (!screen)
            return 60;
        return std::max<NSInteger>(1, screen.maximumFramesPerSecond);
    }

    void configureDisplayLinkFrameRate(CAMetalDisplayLink *link)
    {
        if (!link)
            return;

        const int displayMaximum = maximumFramesPerSecondForCurrentDisplay();
        state.displayMaximumFramesPerSecond = displayMaximum;
        if (imageIsHDR) {
            const float maximum = std::min<float>(120.0F, displayMaximum);
            const float minimum = std::min<float>(80.0F, maximum);
            link.preferredFrameRateRange =
                    CAFrameRateRangeMake(minimum, maximum, maximum);
            state.requestedFrameRateMinimum = minimum;
            state.requestedFrameRateMaximum = maximum;
            state.requestedFrameRatePreferred = maximum;
            state.displayCanPresent180FPS = false;
            return;
        }

        const auto policy = QVCocoaFunctions::sdrFrameRatePolicy(displayMaximum);
        link.preferredFrameRateRange = CAFrameRateRangeMake(
                static_cast<float>(policy.minimum),
                static_cast<float>(policy.maximum),
                static_cast<float>(policy.preferred));
        state.requestedFrameRateMinimum = static_cast<float>(policy.minimum);
        state.requestedFrameRateMaximum = static_cast<float>(policy.maximum);
        state.requestedFrameRatePreferred = static_cast<float>(policy.preferred);
        state.displayCanPresent180FPS = policy.displayCanPresent180FPS;
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
            configureDisplayLinkFrameRate(replacement);
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
        return setImageGraph(
                std::dynamic_pointer_cast<const NativeHDRImage>(newImage));
    }

    bool setSDRImage(const SDRImagePtr &newImage)
    {
        return setImageGraph(
                std::dynamic_pointer_cast<const NativeSDRImage>(newImage));
    }

    bool setImageGraph(
            const std::shared_ptr<const NativeMetalImageGraph> &nativeImage)
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
        clearPersistentSDRTileSurface();
        // The context is intentionally long-lived for one interactive view,
        // but intermediates from the previous source are no longer reusable.
        // Release them only on image replacement, never on zoom or pan.
        if (context)
            [context clearCaches];
        image = nativeImage;
        imageIsHDR = nativeImage && nativeImage->isHDR();
        // SDR requests 180...240 Hz only when the window's display can
        // physically present it. Lower-refresh displays use their exact maximum
        // instead of advertising an impossible cadence. HDR keeps its separate
        // sustainable 80...120 Hz graph budget.
        configureDisplayLinkFrameRate(displayLink);
        [presentationState resetForImage];
        ++presentationTransitionGeneration;
        presentationAnimationInFlight = false;
        discardPersistentSurface(
                nativeImage && previousPersistentPresentationVisible);
        latestViewportSize = {};
        latestCorners.clear();
        state.imageActive = nativeImage != nullptr;
        state.sdrImageActive = nativeImage != nullptr && !imageIsHDR;
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
        state.presentedRenderGeneration = 0;
        state.presentedFrameCount = 0;
        state.missedTargetDeadlineCount = 0;
        state.framesInFlight = 0;
        state.displayLinkInteractiveSubmissionCount = 0;
        state.compositorGeometryUpdateCount = 0;
        state.compositorInteractiveSubmissionCount = 0;
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
        state.maximumInteractiveRenderMilliseconds = 0.0;
        state.maximumInteractiveGPUExecutionMilliseconds = 0.0;
        state.usesMaterializedSDRTiles = false;
        state.usesSDRFullSingleImage = false;
        state.usesPersistentSDRTileSurface = false;
        state.sdrAuthoritativePresentedFrameCount = 0;
        state.sdrMaterializedBytes = 0;
        state.sdrTileCount = 0;
        state.sdrVisibleTileCount = 0;
        pendingRenderGeneration = 0;
        if (nativeImage) {
            const HDRMetadata &metadata = nativeImage->rendererMetadata();
            state.isRaw = metadata.isRaw;
            state.hasGainMap = metadata.hasAppleGainMap || metadata.hasISOGainMap;
            state.contentHeadroom = metadata.contentHeadroom;
            if (const auto nativeSDR =
                        std::dynamic_pointer_cast<const NativeSDRImage>(nativeImage)) {
                state.usesMaterializedSDRTiles = true;
                state.sdrMaterializedBytes = nativeSDR->materializedBytes();
                state.sdrTileCount = nativeSDR->sourceTiles().size();
                state.usesSDRFullSingleImage =
                        nativeSDR->fullSingleCIImage() != nil;
                state.usesPersistentSDRTileSurface =
                        installPersistentSDRTileSurface(*nativeSDR);
            }
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
            persistentSDRTileLayer.hidden = YES;
            persistentSDRTileLayer.opacity = 0.0F;
            persistentImageLayer.hidden = YES;
            persistentImageLayer.opacity = 0.0F;
        }
        [CATransaction commit];
        state.layerOpacity = nativeImage && retainPreviousPresentation ? 1.0F : 0.0F;
        setExtendedDynamicRangeEnabled(
                nativeImage && imageIsHDR && presentationActiveRequested);
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

    CIImage *displayImage(const NativeMetalImageGraph &nativeImage,
                          const float targetHeadroom,
                          const float progress)
    {
        CIImage *hdr = nativeImage.hdrCIImage();
        CIImage *sdr = nativeImage.sdrCIImage();
        const HDRMetadata &metadata = nativeImage.rendererMetadata();

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
            && targetHeadroom + 0.001F >= image->rendererMetadata().contentHeadroom)
            return preparedHDRImage;

        if (image->rendererMetadata().isRaw
            && !image->rendererMetadata().usesProcessedRawPreview)
            return mixImages(preparedSDRImage, preparedHDRImage, progress);

        if (image->rendererMetadata().usesProcessedRawPreview
            && image->gainMapCIImage()) {
            if (@available(macOS 15.0, *)) {
                CIImage *adapted = [preparedSDRImage
                        imageByApplyingGainMap:image->gainMapCIImage()
                                      headroom:std::max(1.0F, targetHeadroom)];
                if (adapted)
                    return adapted;
            }
        }

        if (@available(macOS 15.0, *)) {
            if (image->rendererMetadata().contentHeadroom > 1.0F) {
                CIFilter *toneMap = [CIFilter filterWithName:@"CIToneMapHeadroom"];
                [toneMap setValue:preparedHDRImage forKey:kCIInputImageKey];
                [toneMap setValue:@(image->rendererMetadata().contentHeadroom)
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
        state.presentedRenderGeneration = frameFlow
                ? frameFlow->presentedRenderGeneration.load() : 0;
        state.missedTargetDeadlineCount = frameFlow
                ? frameFlow->missedTargetDeadlineCount.load() : 0;
        state.lastPresentedIntervalMilliseconds = frameFlow
                ? frameFlow->lastPresentedIntervalMilliseconds.load() : 0.0;
        state.lastGPUExecutionMilliseconds = frameFlow
                ? frameFlow->lastGPUExecutionMilliseconds.load() : 0.0;
        state.maximumInteractiveRenderMilliseconds = frameFlow
                ? frameFlow->maximumInteractiveRenderMilliseconds.load() : 0.0;
        state.maximumInteractiveGPUExecutionMilliseconds = frameFlow
                ? frameFlow->maximumInteractiveGPUExecutionMilliseconds.load() : 0.0;
        state.lastRequestToPresentationMilliseconds = frameFlow
                ? frameFlow->lastRequestToPresentationMilliseconds.load() : 0.0;
        state.firstVisibleFrameUsesFinalHeadroom = frameFlow
                && frameFlow->firstVisibleFrameUsesFinalHeadroom.load();
        state.sdrAuthoritativePresentedFrameCount = frameFlow
                ? frameFlow->sdrAuthoritativePresentedFrameCount.load() : 0;
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

    std::optional<CGAffineTransform> sourceToTextureTransform(
            const CGRect sourceExtent, const QSize &viewportSize,
            const QPolygonF &corners, const CGSize textureSize) const
    {
        if (viewportSize.isEmpty() || corners.size() < 4
            || textureSize.width <= 0 || textureSize.height <= 0
            || CGRectIsEmpty(sourceExtent) || sourceExtent.size.width <= 0
            || sourceExtent.size.height <= 0)
            return std::nullopt;

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
        return CGAffineTransformMake(a, b, c, d, tx, ty);
    }

    CIImage *backgroundImageForTexture(const CGRect destinationBounds,
                                       const CGFloat scaleX,
                                       const CGFloat scaleY) const
    {
        // QColor stores these constants in sRGB. Keep that source tag so
        // ColorSync converts the exact Qt theme color into extended-linear P3
        // instead of interpreting gamma-encoded components as linear values.
        CIImage *clearImage = nil;
        if (checkerboardBackground && !imageIsHDR) {
            CIFilter *checkerboard = [CIFilter filterWithName:@"CICheckerboardGenerator"];
            CIColor *white = [CIColor colorWithRed:1.0 green:1.0 blue:1.0 alpha:1.0
                                         colorSpace:backgroundColorSpace];
            CIColor *gray = [CIColor colorWithRed:0.8 green:0.8 blue:0.8 alpha:1.0
                                        colorSpace:backgroundColorSpace];
            [checkerboard setValue:[CIVector vectorWithX:0.0 Y:0.0]
                            forKey:kCIInputCenterKey];
            [checkerboard setValue:white forKey:@"inputColor0"];
            [checkerboard setValue:gray forKey:@"inputColor1"];
            [checkerboard setValue:@(16.0 * std::max(scaleX, scaleY))
                            forKey:kCIInputWidthKey];
            [checkerboard setValue:@1.0 forKey:kCIInputSharpnessKey];
            clearImage = [checkerboard.outputImage imageByCroppingToRect:destinationBounds];
        }
        if (!clearImage) {
            CIColor *clearColor = [CIColor colorWithRed:backgroundColor.redF()
                                                  green:backgroundColor.greenF()
                                                   blue:backgroundColor.blueF()
                                                  alpha:1
                                             colorSpace:backgroundColorSpace];
            clearImage = [[CIImage imageWithColor:clearColor]
                    imageByCroppingToRect:destinationBounds];
        }
        return clearImage;
    }

    CIImage *imageForTexture(CIImage *source, const QSize &viewportSize,
                             const QPolygonF &corners, const CGSize textureSize)
    {
        if (!source)
            return nil;
        const auto transform = sourceToTextureTransform(
                source.extent, viewportSize, corners, textureSize);
        if (!transform)
            return nil;
        source = [source imageByApplyingTransform:*transform];
        const CGRect destinationBounds = CGRectMake(
                0, 0, textureSize.width, textureSize.height);
        const CGFloat scaleX = textureSize.width / viewportSize.width();
        const CGFloat scaleY = textureSize.height / viewportSize.height();
        CIImage *clearImage = backgroundImageForTexture(
                destinationBounds, scaleX, scaleY);
        return [[source imageByCroppingToRect:destinationBounds]
                imageByCompositingOverImage:clearImage];
    }

    CIImage *tiledSDRImageForTexture(const NativeSDRImage &nativeImage,
                                     const QSize &viewportSize,
                                     const QPolygonF &corners,
                                     const CGSize textureSize)
    {
        const QSize sourceSize = nativeImage.pixelSize();
        const CGRect sourceExtent = CGRectMake(
                0, 0, sourceSize.width(), sourceSize.height());
        const auto transform = sourceToTextureTransform(
                sourceExtent, viewportSize, corners, textureSize);
        if (!transform)
            return nil;

        if (nativeImage.fullSingleCIImage()) {
            state.usesSDRFullSingleImage = true;
            state.sdrVisibleTileCount = 0;
            return imageForTexture(
                    nativeImage.fullSingleCIImage(),
                    viewportSize, corners, textureSize);
        }
        state.usesSDRFullSingleImage = false;

        const CGRect destinationBounds = CGRectMake(
                0, 0, textureSize.width, textureSize.height);
        const CGFloat scaleX = textureSize.width / viewportSize.width();
        const CGFloat scaleY = textureSize.height / viewportSize.height();
        CIImage *composite = backgroundImageForTexture(
                destinationBounds, scaleX, scaleY);
        int visibleTileCount = 0;
        for (const NativeSDRImage::Tile &tile : nativeImage.sourceTiles())
        {
            const CGRect transformedCore = CGRectApplyAffineTransform(
                    tile.coreRect, *transform);
            const CGRect outputRect = CGRectIntersection(
                    transformedCore, destinationBounds);
            if (CGRectIsNull(outputRect) || CGRectIsEmpty(outputRect))
                continue;

            // SamplingBorder pixels live outside coreRect. They remain
            // available to Core Image's affine sampler, then output is clipped
            // back to the nonoverlapping core so adjacent tiles cannot create
            // translucent seams or double-composite alpha.
            CIImage *placed = [tile.image imageByApplyingTransform:
                    CGAffineTransformMakeTranslation(
                            tile.dataRect.origin.x, tile.dataRect.origin.y)];
            CIImage *transformed = [placed imageByApplyingTransform:*transform];
            transformed = [transformed imageByCroppingToRect:outputRect];
            composite = [transformed imageByCompositingOverImage:composite];
            ++visibleTileCount;
        }
        state.sdrVisibleTileCount = visibleTileCount;
        return composite;
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
            if (owner) {
                // A retained prior persistent HDR surface is now completely
                // covered by the newly presented opaque Metal drawable.
                if (!owner->persistentSurfaceReady && owner->persistentImage)
                    owner->discardPersistentSurface(false);
                owner->applyPresentationTarget(true);
            }
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

        if (!imageIsHDR) {
            presentationState->hdrPrepared = YES;
            return;
        }

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
        if (image->rendererMetadata().usesProcessedRawPreview) {
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

        if (maximumFramesPerSecondForCurrentDisplay()
            != state.displayMaximumFramesPerSecond)
            configureDisplayLinkFrameRate(displayLink);

        syncViewportLayerGeometry();
        latestViewportSize = viewportSize;
        latestCorners = corners;
        if (persistentSDRTileSurfaceReady && !imageIsHDR) {
            const auto nativeSDR =
                    std::dynamic_pointer_cast<const NativeSDRImage>(image);
            if (!nativeSDR)
                return;

            QElapsedTimer geometryTimer;
            geometryTimer.start();
            ++state.renderRequestCount;
            const quint64 geometryGeneration = ++state.requestedRenderGeneration;
            updatePersistentSDRTileGeometry(*nativeSDR, viewportSize, corners);
            revealPersistentSDRTileSurface();
            const double elapsedMilliseconds =
                    geometryTimer.nsecsElapsed() / 1000000.0;
            const double now = CACurrentMediaTime();
            state.submittedRenderGeneration = geometryGeneration;
            state.lastRenderMilliseconds = elapsedMilliseconds;
            state.lastGPUExecutionMilliseconds = 0.0;
            state.lastRequestToPresentationMilliseconds = elapsedMilliseconds;
            ++state.renderCount;
            if (interactive)
                ++state.compositorInteractiveSubmissionCount;
            if (frameFlow) {
                const double previous = frameFlow->lastPresentedTime.exchange(now);
                frameFlow->lastPresentedIntervalMilliseconds.store(
                        previous > 0.0 ? (now - previous) * 1000.0 : 0.0);
                frameFlow->lastRequestToPresentationMilliseconds.store(
                        elapsedMilliseconds);
                frameFlow->lastGPUExecutionMilliseconds.store(0.0);
                frameFlow->presentedRenderGeneration.store(geometryGeneration);
                frameFlow->presentedFrameCount.fetch_add(1);
                frameFlow->sdrAuthoritativePresentedFrameCount.fetch_add(1);
                if (interactive)
                    updateAtomicMaximum(
                            frameFlow->maximumInteractiveRenderMilliseconds,
                            elapsedMilliseconds);
            }
            renderPending = false;
            pendingInteractive = interactive;
            interactiveKeepAliveUntil = 0.0;
            if (displayLink)
                displayLink.paused = YES;
            syncPresentationDiagnostics();
            return;
        }
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
        metalLayer.frame = presentationContainerLayer.bounds;
        metalLayer.contentsScale = backingScale;
        if (drawableSizeChanged)
            metalLayer.drawableSize = requestedSize;
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
        // Match CAMetalLayer's three-drawable pool so CPU encoding, GPU work
        // and WindowServer scanout can overlap without starving a 120 Hz
        // display link. Pending geometry remains latest-only, so this does not
        // turn input events into an unbounded frame queue.
        const int maximumFramesInFlight = imageIsHDR ? 2 : 3;
        if (frameFlow
            && frameFlow->framesInFlight.load() >= maximumFramesInFlight) {
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
            if (!imageIsHDR) {
                // SDR uses the same color-managed float drawable, but never
                // opts the layer into EDR or evaluates HDR transition graphs.
                state.displayCurrentHeadroom = 1.0F;
                state.displayPotentialHeadroom = 1.0F;
                state.displayRenderingHeadroom = 1.0F;
                state.displayHeadroomOverridden = false;
                state.displayCurrentHeadroomOverridden = false;
                state.bootstrappingEDR = false;
                state.transitionProgress = 1.0F;
                state.targetHeadroom = 1.0F;
            }

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
            CIImage *source = nil;
            const auto nativeSDR = !imageIsHDR
                    ? dynamic_cast<const NativeSDRImage *>(image.get()) : nullptr;
            if (nativeSDR) {
                source = tiledSDRImageForTexture(
                        *nativeSDR, viewportSize, corners, actualSize);
            } else {
                source = preparedEndpointsActive
                        ? preparedDisplayImage(
                                state.targetHeadroom, state.transitionProgress)
                        : displayImage(
                                *image, state.targetHeadroom,
                                state.transitionProgress);
                source = imageForTexture(
                        source, viewportSize, corners, actualSize);
            }
            const bool usesAuthoritativeSDRFrame = nativeSDR != nullptr;
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
                        const double gpuMilliseconds =
                                (gpuEnd - gpuStart) * 1000.0;
                        submittedFlow->lastGPUExecutionMilliseconds.store(
                                gpuMilliseconds);
                        if (interactive)
                            updateAtomicMaximum(
                                    submittedFlow
                                            ->maximumInteractiveGPUExecutionMilliseconds,
                                    gpuMilliseconds);
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
                    submittedFlow->presentedRenderGeneration.store(renderGeneration);
                    if (usesAuthoritativeSDRFrame)
                        submittedFlow->sdrAuthoritativePresentedFrameCount.fetch_add(1);
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
                if (interactive && submittedFlow)
                    updateAtomicMaximum(
                            submittedFlow->maximumInteractiveRenderMilliseconds,
                            elapsedMilliseconds);
                CGColorSpaceRelease(renderColorSpace);
            });
            syncPresentationDiagnostics();
            if (imageIsHDR && finalHeadroom && presentationState->hdrPrepared)
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

    QWidget *viewport{ nullptr };
    NSView *nativeView{ nil };
    CALayer *presentationContainerLayer{ nil };
    CALayer *viewportBackgroundLayer{ nil };
    CALayer *persistentSDRTileLayer{ nil };
    CALayer *persistentImageLayer{ nil };
    CAMetalLayer *metalLayer{ nil };
    CALayer *navigationOverlayLayer{ nil };
    CALayer *navigationButtonLayers[2]{ nil, nil };
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
    bool checkerboardBackground{ false };
    QVHDRPresentationState *presentationState{ nil };
    std::shared_ptr<HDRFrameFlowState> frameFlow{ std::make_shared<HDRFrameFlowState>() };
    std::shared_ptr<HDRPersistentSurfaceGate> persistentSurfaceGate{
        std::make_shared<HDRPersistentSurfaceGate>()
    };
    CGImageRef persistentImage{ nullptr };
    CGImageRef persistentCheckerboardImage{ nullptr };
    QSize persistentCheckerboardPixelSize;
    bool persistentSDRTileSurfaceReady{ false };
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
    std::shared_ptr<const NativeMetalImageGraph> image;
    bool imageIsHDR{ false };
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

bool QVCocoaFunctions::HDRRenderer::setSDRImage(const SDRImagePtr &image)
{
    return impl && impl->setSDRImage(image);
}

void QVCocoaFunctions::HDRRenderer::setBackgroundColor(const QColor &color)
{
    if (impl)
        impl->setBackgroundColor(color);
}

void QVCocoaFunctions::HDRRenderer::setCheckerboardBackground(const bool enabled)
{
    if (impl)
        impl->setCheckerboardBackground(enabled);
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

QRect QVCocoaFunctions::coreImageTileRect(const QRect &topLeftPixelRect,
                                          const int sourceHeight)
{
    if (!topLeftPixelRect.isValid() || sourceHeight <= 0
        || topLeftPixelRect.top() < 0
        || topLeftPixelRect.bottom() >= sourceHeight)
        return {};

    return QRect(topLeftPixelRect.x(),
                 sourceHeight - topLeftPixelRect.y()
                         - topLeftPixelRect.height(),
                 topLeftPixelRect.width(), topLeftPixelRect.height());
}

QVCocoaFunctions::SDRFrameRatePolicy
QVCocoaFunctions::sdrFrameRatePolicy(const qreal displayMaximumFramesPerSecond)
{
    constexpr qreal FallbackDisplayFramesPerSecond = 60.0;
    constexpr qreal RequiredFramesPerSecond = 180.0;
    constexpr qreal MaximumRequestedFramesPerSecond = 240.0;
    const qreal displayMaximum =
            std::isfinite(displayMaximumFramesPerSecond)
                    && displayMaximumFramesPerSecond > 0.0
            ? displayMaximumFramesPerSecond
            : FallbackDisplayFramesPerSecond;

    SDRFrameRatePolicy policy;
    policy.displayCanPresent180FPS = displayMaximum >= RequiredFramesPerSecond;
    if (!policy.displayCanPresent180FPS) {
        policy.minimum = displayMaximum;
        policy.maximum = displayMaximum;
        policy.preferred = displayMaximum;
        return policy;
    }

    policy.minimum = RequiredFramesPerSecond;
    policy.maximum = std::min(displayMaximum,
                              MaximumRequestedFramesPerSecond);
    policy.preferred = policy.maximum;
    return policy;
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

    // QWindow publishes its requested FullScreen state before AppKit finishes
    // the Space transition. Track the native completion boundary so an Escape
    // or View command issued during entry can be delivered exactly once after
    // the NSWindow becomes actionable.
    static dispatch_once_t fullScreenObserversOnce;
    dispatch_once(&fullScreenObserversOnce, ^{
        NSNotificationCenter *center = [NSNotificationCenter defaultCenter];
        [center addObserverForName:NSWindowWillEnterFullScreenNotification
                            object:nil
                             queue:[NSOperationQueue mainQueue]
                        usingBlock:^(NSNotification *notification) {
            NSWindow *window = notification.object;
            NSNumber *customAnimation = objc_getAssociatedObject(
                window, &FullScreenCustomAnimationAssociationKey);
            NSWindow *preparedProxy = objc_getAssociatedObject(
                window, &FullScreenProxyWindowAssociationKey);
            if (customAnimation.boolValue && !preparedProxy)
            {
                objc_setAssociatedObject(
                    window, &FullScreenNormalFrameAssociationKey,
                    [NSValue valueWithRect:window.frame],
                    OBJC_ASSOCIATION_RETAIN_NONATOMIC);
                const int titlebarOverlap =
                    fovelleNormalTitlebarOverlap(window);
                objc_setAssociatedObject(
                    window, &FullScreenNormalTitlebarOverlapAssociationKey,
                    @(titlebarOverlap), OBJC_ASSOCIATION_RETAIN_NONATOMIC);
                objc_setAssociatedObject(
                    window, &FullScreenNormalTitlebarSnapshotAssociationKey,
                    captureFullScreenTitlebar(window, titlebarOverlap),
                    OBJC_ASSOCIATION_RETAIN_NONATOMIC);
            }
            objc_setAssociatedObject(
                window, &FullScreenTransitionCompleteAssociationKey,
                @NO, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        }];
        [center addObserverForName:NSWindowDidEnterFullScreenNotification
                            object:nil
                             queue:[NSOperationQueue mainQueue]
                        usingBlock:^(NSNotification *notification) {
            NSWindow *window = notification.object;
            auto *animation = static_cast<FovelleFullScreenAnimation *>(
                objc_getAssociatedObject(
                    window, &FullScreenAnimationAssociationKey));
            if (animation.animating)
                animation.currentProgress = 1.0f;
            [animation stopAnimation];
            FovelleFullScreenAnimationHandler handler =
                objc_getAssociatedObject(
                    window, &FullScreenAnimationHandlerAssociationKey);
            if (handler)
                handler(FovelleFullScreenAnimationPhase::Cancel, 0, 0);
            objc_setAssociatedObject(
                window, &FullScreenAnimationAssociationKey,
                nil, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
            revealFovelleFullScreenRealWindow(window);
            dispatch_async(dispatch_get_main_queue(), ^{
                // Preserve the measured normal endpoint: the symmetric exit
                // animation consumes it when this full-screen Space closes.
                cleanupFovelleFullScreenProxy(window);
            });
            objc_setAssociatedObject(
                window, &FullScreenTransitionCompleteAssociationKey,
                @YES, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
            NSNumber *pending = objc_getAssociatedObject(
                window, &FullScreenExitPendingAssociationKey);
            if (!pending.boolValue)
                return;

            objc_setAssociatedObject(
                window, &FullScreenExitPendingAssociationKey,
                @NO, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
            dispatch_async(dispatch_get_main_queue(), ^{
                if (window.styleMask & NSWindowStyleMaskFullScreen)
                    [window toggleFullScreen:nil];
            });
        }];
        [center addObserverForName:NSWindowWillExitFullScreenNotification
                            object:nil
                             queue:[NSOperationQueue mainQueue]
                        usingBlock:^(NSNotification *notification) {
            NSWindow *window = notification.object;
            objc_setAssociatedObject(
                window, &FullScreenTransitionCompleteAssociationKey,
                @NO, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        }];
        [center addObserverForName:NSWindowDidExitFullScreenNotification
                            object:nil
                             queue:[NSOperationQueue mainQueue]
                        usingBlock:^(NSNotification *notification) {
            NSWindow *window = notification.object;
            objc_setAssociatedObject(
                window, &FullScreenTransitionCompleteAssociationKey,
                @NO, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
            objc_setAssociatedObject(
                window, &FullScreenExitPendingAssociationKey,
                @NO, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
            auto *animation = static_cast<FovelleFullScreenAnimation *>(
                objc_getAssociatedObject(
                    window, &FullScreenAnimationAssociationKey));
            if (animation.animating)
                animation.currentProgress = 1.0f;
            [animation stopAnimation];
            objc_setAssociatedObject(
                window, &FullScreenAnimationAssociationKey,
                nil, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
            revealFovelleFullScreenRealWindow(window);
            dispatch_async(dispatch_get_main_queue(), ^{
                cleanupFovelleFullScreenProxy(window);
                clearFovelleFullScreenNormalState(window);
            });
        }];
    });
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

bool QVCocoaFunctions::requestFullScreenExit(QWindow *window)
{
    if (!window)
        return false;

    auto *view = reinterpret_cast<NSView *>(window->winId());
    NSWindow *nativeWindow = view.window;
    if (!nativeWindow)
        return false;

    const bool nativeFullScreen =
        nativeWindow.styleMask & NSWindowStyleMaskFullScreen;
    if (!nativeFullScreen && window->windowState() != Qt::WindowFullScreen)
        return false;

    NSNumber *transitionComplete = objc_getAssociatedObject(
        nativeWindow, &FullScreenTransitionCompleteAssociationKey);
    if (!transitionComplete && nativeFullScreen)
    {
        // Compatibility fallback for a native full-screen window created
        // before observer installation.
        [nativeWindow toggleFullScreen:nil];
        return true;
    }
    if (!transitionComplete.boolValue)
    {
        objc_setAssociatedObject(
            nativeWindow, &FullScreenExitPendingAssociationKey,
            @YES, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        return true;
    }

    // This is the same AppKit action used by Escape and by a native View →
    // Exit Full Screen menu item. QCocoaWindow observes the asynchronous
    // completion notification and publishes the final Qt state itself.
    [nativeWindow toggleFullScreen:nil];
    return true;
}

// This function should only be enabled once because it sets observers
void QVCocoaFunctions::setFullSizeContentView(QWidget *window, const bool enable)
{
    auto *view = reinterpret_cast<NSView*>(window->winId());
    NSWindow *nativeWindow = view.window;
    const bool customAnimationEnabled = enable && view.wantsLayer
        && installFovelleFullScreenAnimationMethods(nativeWindow);
    objc_setAssociatedObject(
        nativeWindow, &FullScreenCustomAnimationAssociationKey,
        customAnimationEnabled ? @YES : nil, OBJC_ASSOCIATION_RETAIN_NONATOMIC);

    if (customAnimationEnabled)
    {
        const QPointer<QWidget> guardedWindow(window);
        FovelleFullScreenAnimationHandler handler = ^(
            const FovelleFullScreenAnimationPhase phase,
            const int titlebarOverlap,
            const int targetTitlebarOverlap) {
            if (!guardedWindow)
                return;
            if (phase == FovelleFullScreenAnimationPhase::Cancel)
            {
                QMetaObject::invokeMethod(
                    guardedWindow.data(),
                    "cancelFullScreenLayoutTransition",
                    Qt::DirectConnection);
                return;
            }
            if (phase == FovelleFullScreenAnimationPhase::Update)
            {
                QMetaObject::invokeMethod(
                    guardedWindow.data(),
                    "updateFullScreenLayoutTransition",
                    Qt::DirectConnection,
                    Q_ARG(int, titlebarOverlap));
                return;
            }
            QMetaObject::invokeMethod(
                guardedWindow.data(),
                "beginFullScreenLayoutTransition",
                Qt::DirectConnection,
                Q_ARG(int, titlebarOverlap),
                Q_ARG(int, targetTitlebarOverlap));
        };
        objc_setAssociatedObject(
            nativeWindow, &FullScreenAnimationHandlerAssociationKey,
            handler, OBJC_ASSOCIATION_COPY_NONATOMIC);

        FovelleFullScreenImageRectProvider rectProvider = ^QRect {
            if (!guardedWindow)
                return {};
            QRect result;
            QMetaObject::invokeMethod(
                guardedWindow.data(),
                "fullScreenTransitionImageRect",
                Qt::DirectConnection,
                Q_RETURN_ARG(QRect, result));
            return result;
        };
        FovelleFullScreenImageProvider imageProvider = ^QImage {
            if (!guardedWindow)
                return {};
            QImage result;
            QMetaObject::invokeMethod(
                guardedWindow.data(),
                "fullScreenTransitionImage",
                Qt::DirectConnection,
                Q_RETURN_ARG(QImage, result));
            return result;
        };
        FovelleFullScreenBackgroundProvider backgroundProvider = ^QColor {
            if (!guardedWindow)
                return {};
            QColor result;
            QMetaObject::invokeMethod(
                guardedWindow.data(),
                "fullScreenTransitionBackgroundColor",
                Qt::DirectConnection,
                Q_RETURN_ARG(QColor, result));
            return result;
        };
        FovelleFullScreenTitlebarOverlapProvider overlapProvider = ^int {
            if (!guardedWindow)
                return 0;
            int result = 0;
            QMetaObject::invokeMethod(
                guardedWindow.data(),
                "fullScreenTransitionTitlebarOverlap",
                Qt::DirectConnection,
                Q_RETURN_ARG(int, result));
            return result;
        };
        objc_setAssociatedObject(
            nativeWindow, &FullScreenImageRectProviderAssociationKey,
            rectProvider, OBJC_ASSOCIATION_COPY_NONATOMIC);
        objc_setAssociatedObject(
            nativeWindow, &FullScreenImageProviderAssociationKey,
            imageProvider, OBJC_ASSOCIATION_COPY_NONATOMIC);
        objc_setAssociatedObject(
            nativeWindow, &FullScreenBackgroundProviderAssociationKey,
            backgroundProvider, OBJC_ASSOCIATION_COPY_NONATOMIC);
        objc_setAssociatedObject(
            nativeWindow, &FullScreenTitlebarOverlapProviderAssociationKey,
            overlapProvider, OBJC_ASSOCIATION_COPY_NONATOMIC);
    }
    else
    {
        objc_setAssociatedObject(
            nativeWindow, &FullScreenAnimationHandlerAssociationKey,
            nil, OBJC_ASSOCIATION_COPY_NONATOMIC);
        objc_setAssociatedObject(
            nativeWindow, &FullScreenNormalFrameAssociationKey,
            nil, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        objc_setAssociatedObject(
            nativeWindow, &FullScreenNormalTitlebarOverlapAssociationKey,
            nil, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        objc_setAssociatedObject(
            nativeWindow, &FullScreenNormalTitlebarSnapshotAssociationKey,
            nil, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        objc_setAssociatedObject(
            nativeWindow, &FullScreenImageRectProviderAssociationKey,
            nil, OBJC_ASSOCIATION_COPY_NONATOMIC);
        objc_setAssociatedObject(
            nativeWindow, &FullScreenImageProviderAssociationKey,
            nil, OBJC_ASSOCIATION_COPY_NONATOMIC);
        objc_setAssociatedObject(
            nativeWindow, &FullScreenBackgroundProviderAssociationKey,
            nil, OBJC_ASSOCIATION_COPY_NONATOMIC);
        objc_setAssociatedObject(
            nativeWindow, &FullScreenTitlebarOverlapProviderAssociationKey,
            nil, OBJC_ASSOCIATION_COPY_NONATOMIC);
        auto *animation = static_cast<FovelleFullScreenAnimation *>(
            objc_getAssociatedObject(
                nativeWindow, &FullScreenAnimationAssociationKey));
        [animation stopAnimation];
        objc_setAssociatedObject(
            nativeWindow, &FullScreenAnimationAssociationKey,
            nil, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        cleanupFovelleFullScreenProxy(nativeWindow);
        clearFovelleFullScreenNormalState(nativeWindow);
    }

    // Make sure the requested state isn't already in effect
    if (enable == (nativeWindow.styleMask & NSWindowStyleMaskFullSizeContentView))
        return;

    // Enable only if this Qt and macOS version combination is already using layer-backed view
    if (enable && !view.wantsLayer)
        return;

    // Changing the style mask causes the window to resize, so snapshot the original size
    NSRect originalFrame = nativeWindow.frame;

#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    Qv::alterWindowFlags(window, [&](Qt::WindowFlags f) { return f.setFlag(Qt::ExpandedClientAreaHint, enable); });
#else
    if (enable)
        nativeWindow.styleMask |= NSWindowStyleMaskFullSizeContentView;
    else
        nativeWindow.styleMask &= ~NSWindowStyleMaskFullSizeContentView;
#endif

    // Restore original size after style mask change
    [nativeWindow setFrame:originalFrame display:YES];
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

bool QVCocoaFunctions::attachWindowAbove(QWindow *child, QWindow *parent)
{
    if (!child || !parent || child == parent)
        return false;

    auto *childView = reinterpret_cast<NSView *>(child->winId());
    auto *parentView = reinterpret_cast<NSView *>(parent->winId());
    NSWindow *childWindow = childView.window;
    NSWindow *parentWindow = parentView.window;
    if (!childWindow || !parentWindow || childWindow == parentWindow)
        return false;

    // Removing a previous relationship is important when the user opens
    // Preferences from a different Fovelle window while it is already open.
    // AppKit then maintains the new child-above-parent ordering on subsequent
    // ordering operations involving either window.
    NSWindow *existingParent = childWindow.parentWindow;
    if (existingParent != parentWindow)
    {
        if (existingParent)
            [existingParent removeChildWindow:childWindow];
        [parentWindow addChildWindow:childWindow ordered:NSWindowAbove];
    }

    return childWindow.parentWindow == parentWindow;
}

bool QVCocoaFunctions::isWindowChildOf(const QWindow *child, const QWindow *parent)
{
    if (!child || !parent || child == parent)
        return false;

    auto *childView = reinterpret_cast<NSView *>(child->winId());
    auto *parentView = reinterpret_cast<NSView *>(parent->winId());
    NSWindow *childWindow = childView.window;
    NSWindow *parentWindow = parentView.window;
    return childWindow && parentWindow && childWindow != parentWindow
        && childWindow.parentWindow == parentWindow;
}

Qv::Theme QVCocoaFunctions::resolvedTheme(const Qv::Theme theme)
{
    if (theme != Qv::Theme::System)
        return theme;

    const QByteArray override = qgetenv("FOVELLE_SYSTEM_THEME").toLower();
    if (override == "dark")
        return Qv::Theme::Dark;
    if (override == "light")
        return Qv::Theme::Light;

    NSAppearance *appearance = NSApp ? NSApp.effectiveAppearance : [NSAppearance currentDrawingAppearance];
    if (!appearance)
        return Qv::Theme::Light;

    NSArray *candidates = @[ NSAppearanceNameAqua, NSAppearanceNameDarkAqua ];
    const NSAppearanceName match = [appearance bestMatchFromAppearancesWithNames:candidates];
    return [match isEqualToString:NSAppearanceNameDarkAqua] ? Qv::Theme::Dark : Qv::Theme::Light;
}

void QVCocoaFunctions::setApplicationTheme(const Qv::Theme theme)
{
    const QByteArray systemOverride = qgetenv("FOVELLE_SYSTEM_THEME").toLower();
    const bool hasControlledSystemTheme = theme == Qv::Theme::System
        && (systemOverride == "light" || systemOverride == "dark");
    const bool followsSystem = theme == Qv::Theme::System && !hasControlledSystemTheme;
    const Qv::Theme effectiveTheme = followsSystem ? Qv::Theme::System : resolvedTheme(theme);

    NSAppearanceName requestedAppearanceName = nil;
    if (effectiveTheme == Qv::Theme::Light)
        requestedAppearanceName = NSAppearanceNameAqua;
    else if (effectiveTheme == Qv::Theme::Dark)
        requestedAppearanceName = NSAppearanceNameDarkAqua;

    // settingsUpdated() runs for every preference, not just Theme.  Avoid a
    // needless application-wide palette rebuild when the scheme is already
    // the requested one.
    NSAppearance *explicitAppearance = NSApp.appearance;
    if ((!requestedAppearanceName && !explicitAppearance)
        || (requestedAppearanceName &&
            [explicitAppearance.name isEqualToString:requestedAppearanceName]))
        return;

#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    Qt::ColorScheme colorScheme = Qt::ColorScheme::Unknown;
    if (effectiveTheme == Qv::Theme::Light)
        colorScheme = Qt::ColorScheme::Light;
    else if (effectiveTheme == Qv::Theme::Dark)
        colorScheme = Qt::ColorScheme::Dark;

    // On Cocoa, Qt maps this public API to NSApp.appearance.  Its platform
    // theme observer then invalidates the cached system/role palettes and
    // sends ApplicationPaletteChange to every QWidget.
    QGuiApplication::styleHints()->setColorScheme(colorScheme);
#else
    NSApp.appearance = requestedAppearanceName
        ? [NSAppearance appearanceNamed:requestedAppearanceName]
        : nil;
#endif
}

void QVCocoaFunctions::setWindowTheme(const Qv::Theme theme, QWindow *window)
{
    if (!window)
        return;

    // Theme is application-wide.  Keep this compatibility entry point for
    // existing callers, but make every window inherit NSApp rather than
    // creating a second, potentially stale per-window appearance override.
    setApplicationTheme(theme);
    auto *view = reinterpret_cast<NSView*>(window->winId());
    [view.window setAppearance:nil];
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

void QVCocoaFunctions::configureSettingsToolbar(QWindow *window, QTabBar *categoryTabs)
{
    if (!window || !categoryTabs || categoryTabs->count() == 0)
        return;

    auto *view = reinterpret_cast<NSView *>(window->winId());
    NSWindow *nativeWindow = view.window;
    if (!nativeWindow)
        return;

    auto *controller = static_cast<FovelleSettingsToolbarController *>(
        objc_getAssociatedObject(nativeWindow, &SettingsToolbarAssociationKey));
    if (controller)
    {
        [controller syncSelection:categoryTabs->currentIndex()];
        return;
    }

    controller = [[FovelleSettingsToolbarController alloc]
        initWithCategoryTabs:categoryTabs];
    auto *toolbar = [[NSToolbar alloc]
        initWithIdentifier:@"io.github.inostarlin-passion.Fovelle.settings.toolbar"];
    toolbar.delegate = controller;
    toolbar.displayMode = NSToolbarDisplayModeIconAndLabel;
    toolbar.sizeMode = NSToolbarSizeModeRegular;
    toolbar.allowsUserCustomization = NO;
    toolbar.autosavesConfiguration = NO;
    toolbar.allowsExtensionItems = NO;
    toolbar.visible = YES;
    [controller setToolbar:toolbar];

    nativeWindow.toolbarStyle = NSWindowToolbarStylePreference;
    nativeWindow.titleVisibility = NSWindowTitleVisible;
    nativeWindow.titlebarSeparatorStyle = NSTitlebarSeparatorStyleAutomatic;
    nativeWindow.tabbingMode = NSWindowTabbingModeDisallowed;
    nativeWindow.toolbar = toolbar;

    // Settings windows don't need Dock minimization or zooming; this also
    // produces the standard dimmed yellow/green controls used by Preview.
    [[nativeWindow standardWindowButton:NSWindowMiniaturizeButton] setEnabled:NO];
    [[nativeWindow standardWindowButton:NSWindowZoomButton] setEnabled:NO];

    objc_setAssociatedObject(nativeWindow, &SettingsToolbarAssociationKey,
                             controller, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    [toolbar release];
    [controller release];

    QPointer<QWindow> guardedWindow(window);
    QObject::connect(categoryTabs, &QTabBar::currentChanged, categoryTabs,
                     [guardedWindow](const int index) {
        if (!guardedWindow)
            return;
        auto *nativeView = reinterpret_cast<NSView *>(guardedWindow->winId());
        auto *toolbarController = static_cast<FovelleSettingsToolbarController *>(
            objc_getAssociatedObject(nativeView.window, &SettingsToolbarAssociationKey));
        [toolbarController syncSelection:index];
    });
    [static_cast<FovelleSettingsToolbarController *>(
        objc_getAssociatedObject(nativeWindow, &SettingsToolbarAssociationKey))
        syncSelection:categoryTabs->currentIndex()];
}

bool QVCocoaFunctions::hasNativeSettingsToolbar(const QWindow *window)
{
    if (!window)
        return false;

    auto *view = reinterpret_cast<NSView *>(window->winId());
    NSToolbar *toolbar = view.window.toolbar;
    return toolbar
        && [toolbar.identifier isEqualToString:
            @"io.github.inostarlin-passion.Fovelle.settings.toolbar"]
        && view.window.toolbarStyle == NSWindowToolbarStylePreference;
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

    // AppKit augments this menu after windows are registered and again while
    // opening its nested Move & Resize / Full Screen Tile menus. Translate at
    // both lifecycle points, always starting from the native menu's titles.
    const auto localizeMenu = [menu]() {
        localizeNativeWindowMenu(menu->toNSMenu());
    };
    QObject::connect(menu, &QMenu::aboutToShow, menu, localizeMenu);
    QTimer::singleShot(0, menu, localizeMenu);
}

QString QVCocoaFunctions::localizedWindowMenuTitle(const QString &sourceTitle,
                                                   const bool submenuTitle)
{
    QString tableKey = sourceTitle;
    if (sourceTitle == QStringLiteral("Quarters"))
        tableKey = submenuTitle ? QStringLiteral("Quarters_Header")
                                : QStringLiteral("Quarters_MenuItem");
    return appKitMenuCommandTranslation(sourceTitle, tableKey);
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

QVCocoaFunctions::FileAssociationResult QVCocoaFunctions::associateAllSupportedFormats(
        const QStringList &extensions, const bool dryRun)
{
    FileAssociationResult result;
    QSet<QByteArray> seen;
    NSString *bundleIdentifier = [[NSBundle mainBundle] bundleIdentifier];

    for (const QString &extension : extensions)
    {
        const QByteArray normalized = normalizedExtension(extension.toUtf8());
        if (normalized.isEmpty() || seen.contains(normalized))
            continue;
        seen.insert(normalized);
        ++result.requestedCount;

        if (dryRun)
        {
            ++result.associatedCount;
            continue;
        }

        NSString *tag = [NSString stringWithUTF8String:normalized.constData()];
        UTType *type = [UTType typeWithTag:tag
                                  tagClass:UTTagClassFilenameExtension
                         conformingToType:nil];
        NSString *identifier = type.identifier;
        if (!bundleIdentifier || !identifier)
        {
            result.failedExtensions.append(QString::fromUtf8(normalized));
            continue;
        }

        const OSStatus status = LSSetDefaultRoleHandlerForContentType(
                (CFStringRef)identifier, kLSRolesViewer, (CFStringRef)bundleIdentifier);
        if (status == noErr)
            ++result.associatedCount;
        else
            result.failedExtensions.append(QString::fromUtf8(normalized));
    }

    return result;
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

struct QVCocoaFunctions::PDFVectorDocument::Impl
{
    explicit Impl(const QByteArray &bytes) : sourceData(bytes)
    {
        data = CFDataCreateWithBytesNoCopy(
            kCFAllocatorDefault,
            reinterpret_cast<const UInt8 *>(sourceData.constData()),
            static_cast<CFIndex>(sourceData.size()),
            kCFAllocatorNull);
        if (!data)
            return;
        CGDataProviderRef provider = CGDataProviderCreateWithCFData(data);
        if (!provider)
            return;
        document = CGPDFDocumentCreateWithProvider(provider);
        CGDataProviderRelease(provider);
        if (!document || !CGPDFDocumentGetPage(document, 1))
            return;
        drawingBox = kCGPDFCropBox;
        if (CGRectIsEmpty(CGPDFPageGetBoxRect(
                CGPDFDocumentGetPage(document, 1), drawingBox)))
        {
            drawingBox = kCGPDFMediaBox;
        }
        valid = !CGRectIsEmpty(CGPDFPageGetBoxRect(
            CGPDFDocumentGetPage(document, 1), drawingBox));
    }

    ~Impl()
    {
        if (document)
            CGPDFDocumentRelease(document);
        if (data)
            CFRelease(data);
    }

    QByteArray sourceData;
    CFDataRef data {nullptr};
    CGPDFDocumentRef document {nullptr};
    CGPDFBox drawingBox {kCGPDFCropBox};
    bool valid {false};
};

QVCocoaFunctions::PDFVectorDocument::PDFVectorDocument(const QByteArray &pdfData)
    : impl(std::make_unique<Impl>(pdfData))
{
}

QVCocoaFunctions::PDFVectorDocument::~PDFVectorDocument() = default;

bool QVCocoaFunctions::PDFVectorDocument::isValid() const
{
    return impl && impl->valid;
}

QImage QVCocoaFunctions::PDFVectorDocument::renderTile(
        const QSizeF &logicalPageSize, const QRectF &sourceRect,
        const QSize &pixelSize, QString *errorString) const
{
    const auto fail = [errorString](const QString &message) {
        if (errorString)
            *errorString = message;
        return QImage();
    };
    if (errorString)
        errorString->clear();

    const QRectF pageRect(QPointF(), logicalPageSize);
    const QRectF clippedSource = sourceRect.intersected(pageRect);
    if (!isValid() || !logicalPageSize.isValid()
        || logicalPageSize.isEmpty() || clippedSource.isEmpty()
        || pixelSize.isEmpty())
    {
        return fail(QStringLiteral("The vector PDF tile request is invalid"));
    }
    if (static_cast<quint64>(pixelSize.width())
            * static_cast<quint64>(pixelSize.height()) > MaxEPSRenderedPixels)
    {
        return fail(QStringLiteral("The vector PDF tile exceeds the safety limit"));
    }

    CGPDFPageRef page = CGPDFDocumentGetPage(impl->document, 1);

    QImage tile(pixelSize, QImage::Format_RGBA8888_Premultiplied);
    if (tile.isNull())
        return fail(QStringLiteral("The vector PDF tile could not be allocated"));
    tile.fill(Qt::transparent);

    CGColorSpaceRef colorSpace = colorSyncSrgbColorSpace();
    CGContextRef context = CGBitmapContextCreate(
        tile.bits(),
        static_cast<size_t>(pixelSize.width()),
        static_cast<size_t>(pixelSize.height()),
        8,
        static_cast<size_t>(tile.bytesPerLine()),
        colorSpace,
        kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    CGColorSpaceRelease(colorSpace);
    if (!context)
        return fail(QStringLiteral("The vector PDF tile context could not be created"));

    // First establish the full logical page exactly as imageFromPDFPage does,
    // then translate the requested local rectangle to the tile origin.  The
    // final bitmap dimensions come from QPainter's device transform, so this
    // is final-device rasterization rather than a persistent zoomed image.
    CGContextSetInterpolationQuality(context, kCGInterpolationHigh);
    CGContextScaleCTM(
        context,
        static_cast<CGFloat>(pixelSize.width()) / clippedSource.width(),
        static_cast<CGFloat>(pixelSize.height()) / clippedSource.height());
    // QGraphicsItem uses a top-left origin while a PDF destination uses a
    // bottom-left origin.  Convert the exposed item's vertical interval to
    // its matching Quartz interval before translating it to the tile origin.
    const qreal quartzSourceBottom = logicalPageSize.height()
            - clippedSource.bottom();
    CGContextTranslateCTM(context, -clippedSource.left(), -quartzSourceBottom);
    const CGRect logicalDestination = CGRectMake(
        0, 0, logicalPageSize.width(), logicalPageSize.height());
    CGContextConcatCTM(context, CGPDFPageGetDrawingTransform(
        page, impl->drawingBox, logicalDestination, 0, true));
    CGContextDrawPDFPage(context, page);
    CGContextRelease(context);
    tile.setColorSpace(QColorSpace::SRgb);
    return tile;
}

QVCocoaFunctions::PDFVectorDocumentPtr
QVCocoaFunctions::createPDFVectorDocument(const QByteArray &pdfData,
                                           QString *errorString)
{
    if (errorString)
        errorString->clear();
    if (pdfData.isEmpty())
    {
        if (errorString)
            *errorString = QStringLiteral("The vector PDF data is empty");
        return {};
    }
    const auto document = std::shared_ptr<PDFVectorDocument>(
        new PDFVectorDocument(pdfData));
    if (!document->isValid())
    {
        if (errorString)
            *errorString = QStringLiteral("The vector PDF document is invalid");
        return {};
    }
    return document;
}

QImage QVCocoaFunctions::renderPDFVectorTile(const QByteArray &pdfData,
                                              const QSizeF &logicalPageSize,
                                              const QRectF &sourceRect,
                                              const QSize &pixelSize,
                                              QString *errorString)
{
    const PDFVectorDocumentPtr document = createPDFVectorDocument(pdfData,
                                                                   errorString);
    return document ? document->renderTile(logicalPageSize, sourceRect,
                                            pixelSize, errorString)
                    : QImage();
}

QSize QVCocoaFunctions::imagePixelSize(const QString &filePath)
{
    @autoreleasepool
    {
        const QUrl fileUrl = QUrl::fromLocalFile(filePath);
        CGImageSourceRef source = CGImageSourceCreateWithURL(
                reinterpret_cast<CFURLRef>(fileUrl.toNSURL()), nullptr);
        if (!source)
            return {};
        const QSize size = orientedPixelSize(
                sourcePixelSize(source), sourceOrientation(source));
        CFRelease(source);
        return size;
    }
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
            result.vectorImage = epsResult.vectorImage;
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
            const bool hasGainMap =
                    result.hdrMetadata.hasAppleGainMap || result.hdrMetadata.hasISOGainMap;
            CIImage *probeImage = nil;
            if (@available(macOS 14.0, *)) {
                NSDictionary *probeOptions = @{
                    (id)kCIImageExpandToHDR : @YES,
                    (id)kCIImageApplyOrientationProperty : @YES,
                    (id)kCIImageCacheImmediately : @NO
                };
                // CIImage is an immutable recipe. Inspecting its range and
                // extent does not require a full RGBA allocation, unlike the
                // old full-resolution Image I/O thumbnail used as a probe.
                probeImage = [CIImage imageWithContentsOfURL:fileUrl.toNSURL()
                                                    options:probeOptions];
            }
            const float decodedHeadroom = ciImageContentHeadroom(probeImage);
            CGColorSpaceRef decodedColorSpace = probeImage.colorSpace;
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
                        result.hdrMetadata.bitsPerComponent = 16;
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

            std::shared_ptr<NativeSDRImage> nativeSDRImage;
            if (!result.hdrImage && CGImageSourceGetCount(source) == 1) {
                const quint64 pixelCount = result.intrinsicSize.isValid()
                        ? static_cast<quint64>(result.intrinsicSize.width())
                                * static_cast<quint64>(result.intrinsicSize.height())
                        : 0;
                const bool exceedsSignedProviderRange = pixelCount > 0
                        && pixelCount
                                > static_cast<quint64>(
                                          std::numeric_limits<qint32>::max()) / 4ULL;

                if (exceedsSignedProviderRange) {
                    // A monolithic 32-bit provider above 2 GiB is observably
                    // accepted by Image I/O but imported as solid black by
                    // Quartz/Core Animation on the supplied 23094x31390 PNG.
                    // Decode once into QImage's qsizetype-addressed storage,
                    // then publish CGImages backed by independent horizontal
                    // providers whose byte ranges are each safely bounded.
                    QImageReader oversizedReader(filePath);
                    oversizedReader.setAutoTransform(true);
                    QImage decodedImage = oversizedReader.read();
                    if (!decodedImage.isNull()) {
                        result.intrinsicSize = decodedImage.size();
                        const int proxyLimit = fallbackLargestDimension > 0
                                ? fallbackLargestDimension : 2048;
                        result.image = decodedImage.scaled(
                                proxyLimit, proxyLimit, Qt::KeepAspectRatio,
                                Qt::SmoothTransformation);
                        nativeSDRImage = std::make_shared<NativeSDRImage>(
                                std::move(decodedImage), sourceHasAlpha(source));
                        if (!nativeSDRImage->sourceTiles().isEmpty()) {
                            result.sdrImage = nativeSDRImage;
                        } else {
                            result.image = {};
                            result.errorString = QStringLiteral(
                                    "The oversized image could not be split into bounded providers");
                        }
                        if (qEnvironmentVariableIsSet("FOVELLE_SDR_PERF_LOG"))
                            qInfo().noquote()
                                    << "FOVELLE_SDR_BOUNDED_PROVIDER"
                                    << "pixels=" << result.intrinsicSize
                                    << "tiles="
                                    << nativeSDRImage->sourceTiles().size()
                                    << "bytes="
                                    << nativeSDRImage->materializedBytes();
                    } else {
                        result.errorString = QStringLiteral(
                                "Qt image decoder could not create bounded providers for an image above 2 GiB: %1")
                                .arg(oversizedReader.errorString());
                    }
                } else {
                    // Interaction is never allowed to fall back to an enlarged
                    // proxy. Decode the authoritative SDR pixels once on the
                    // loader worker, then expose them to Core Image as bounded
                    // authoritative tiles. This moves the unavoidable sequential PNG
                    // decode to open time and removes it from first zoom/pan.
                    CGImageRef decodedImage = nullptr;
                    if (CFMutableDictionaryRef options =
                                thumbnailOptions(source, 0, false)) {
                        CFDictionarySetValue(
                                options,
                                kCGImageSourceShouldCacheImmediately,
                                kCFBooleanTrue);
                        decodedImage = CGImageSourceCreateThumbnailAtIndex(
                                source, 0, options);
                        CFRelease(options);
                    }
                    if (decodedImage) {
                        result.intrinsicSize = QSize(
                                static_cast<int>(CGImageGetWidth(decodedImage)),
                                static_cast<int>(CGImageGetHeight(decodedImage)));
                        nativeSDRImage = std::make_shared<NativeSDRImage>(
                                decodedImage, sourceHasAlpha(source));
                        if (!nativeSDRImage->sourceTiles().isEmpty()) {
                            result.sdrImage = nativeSDRImage;
                            // Reuse the already decoded provider for the bounded
                            // Qt placeholder. A second ImageIO thumbnail request
                            // would parse and inflate a huge PNG again, doubling
                            // cold-open latency without improving visible pixels.
                            result.image = imageFromCGImage(
                                    decodedImage, fallbackLargestDimension);
                        }
                        CGImageRelease(decodedImage);
                    }
                }
            }

            // Qt remains a bounded, immediately paintable cold-open placeholder
            // only until the independent Metal layer presents its first frame.
            // It is never a source for Metal zoom/pan; the native SDR/HDR graph
            // above remains authoritative at every interaction zoom level.
            if ((result.sdrImage || result.hdrImage) && result.image.isNull()) {
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

            if (result.image.isNull() && !result.sdrImage && !result.hdrImage
                && result.errorString.isEmpty())
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

#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>
#import <objc/runtime.h>
#include <QWindow>
#include "native_titlebar_probe.h"

@interface FovelleTestFullScreenObserver : NSObject {
@public
    int entries;
    int exits;
}
- (void)didEnter:(NSNotification *)notification;
- (void)didExit:(NSNotification *)notification;
@end

@implementation FovelleTestFullScreenObserver
- (void)didEnter:(NSNotification *)notification { Q_UNUSED(notification); ++entries; }
- (void)didExit:(NSNotification *)notification { Q_UNUSED(notification); ++exits; }
- (void)dealloc
{
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    [super dealloc];
}
@end

NativeTitlebarSnapshot nativeTitlebarSnapshot(QWindow *window)
{
    NSView *view = reinterpret_cast<NSView *>(window->winId());
    NSWindow *nativeWindow = view.window;
    if (!nativeWindow)
        return {false, 0, 0};
    static char observerKey;
    FovelleTestFullScreenObserver *observer = objc_getAssociatedObject(nativeWindow, &observerKey);
    if (!observer) {
        observer = [[FovelleTestFullScreenObserver alloc] init];
        NSNotificationCenter *center = [NSNotificationCenter defaultCenter];
        [center addObserver:observer selector:@selector(didEnter:)
            name:NSWindowDidEnterFullScreenNotification object:nativeWindow];
        [center addObserver:observer selector:@selector(didExit:)
            name:NSWindowDidExitFullScreenNotification object:nativeWindow];
        objc_setAssociatedObject(nativeWindow, &observerKey, observer, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        [observer release];
    }
    // Read AppKit independently of the production Qt flag getter. The
    // notifications prevent Qt's requested state from ending sampling early.
    const bool hidden = nativeWindow.titleVisibility == NSWindowTitleHidden
        && nativeWindow.titlebarAppearsTransparent
        && [nativeWindow standardWindowButton:NSWindowCloseButton].hidden;
    return {hidden, observer->entries, observer->exits};
}

NativeFullScreenPresentation nativeFullScreenPresentation(QWindow *window, bool refreshTransaction)
{
    NSView *view = reinterpret_cast<NSView *>(window->winId());
    NSWindow *real = view.window;
    if (!real)
        return {};
    // Discover the visible auxiliary proxy by public AppKit properties. Do
    // not consult production association keys or its NSAnimation progress.
    for (NSWindow *candidate in NSApp.windows) {
        if (candidate == real || !candidate.visible || !candidate.ignoresMouseEvents
            || candidate.level != real.level + 1
            || !(candidate.collectionBehavior & NSWindowCollectionBehaviorFullScreenAuxiliary))
            continue;
        CALayer *layer = candidate.contentView.layer.sublayers.firstObject;
        // Presentation values are cached for the current CA transaction.
        // End the sampling transaction so reads during a deliberately paused
        // AppKit run loop use a fresh media time; this dispatches no UI events.
        // Paint-budget observers must not commit transactions from inside
        // paint delivery. Motion sampling explicitly requests a fresh clock.
        if (refreshTransaction)
            [CATransaction flush];
        CALayer *presentation = layer.presentationLayer;
        // Once the explicit trajectory is removed, a presentation tree may
        // disappear even though the proxy still covers the real window.
        // Sample its committed endpoint then; never use the model tree as
        // a substitute while animations are running.
        if (layer.animationKeys.count == 0)
            presentation = layer;
        CALayer *image = presentation.sublayers.firstObject;
        CALayer *modelImage = layer.sublayers.firstObject;
        if (!presentation || !image || !modelImage.contents)
            continue;
        const auto rect = [](CGRect r) {
            return QRectF(r.origin.x, r.origin.y, r.size.width, r.size.height);
        };
        return {true, rect(presentation.frame), rect(image.frame),
            layer.animationKeys.count != 0};
    }
    return {};
}

NativeFullScreenImageSnapshot nativeFullScreenImageSnapshot(QWindow *window)
{
    NSWindow *real = reinterpret_cast<NSView *>(window->winId()).window;
    for (NSWindow *candidate in NSApp.windows) {
        if (candidate == real || !candidate.visible || !candidate.ignoresMouseEvents
            || candidate.level != real.level + 1
            || !(candidate.collectionBehavior & NSWindowCollectionBehaviorFullScreenAuxiliary))
            continue;
        CALayer *image = candidate.contentView.layer.sublayers.firstObject.sublayers.firstObject;
        CGImageRef contents = reinterpret_cast<CGImageRef>(image.contents);
        if (!contents || CGRectIsEmpty(image.frame))
            continue;
        NativeFullScreenImageSnapshot result;
        result.active = true;
        result.sourceSize = QSize(CGImageGetWidth(contents), CGImageGetHeight(contents));
        result.orientedSize = QSizeF(image.frame.size.width, image.frame.size.height);
        result.sourcePreview = QImage(32, 32, QImage::Format_RGBA8888_Premultiplied);
        result.orientedPreview = QImage(32, 32, QImage::Format_RGBA8888_Premultiplied);
        const auto context = [contents](QImage &target) {
            target.fill(Qt::transparent);
            return CGBitmapContextCreate(target.bits(), target.width(), target.height(),
                8, target.bytesPerLine(), CGImageGetColorSpace(contents),
                kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
        };
        CGContextRef raw = context(result.sourcePreview);
        if (!raw)
            return {};
        CGContextSetInterpolationQuality(raw, kCGInterpolationNone);
        CGContextDrawImage(raw, CGRectMake(0, 0, 32, 32), contents);
        CGContextRelease(raw);

        // Preserve the actual model bounds, contents gravity and transform;
        // translate only the frame origin so the entire image is in the oracle.
        CALayer *root = [CALayer layer];
        root.bounds = CGRectMake(0, 0, image.frame.size.width, image.frame.size.height);
        CALayer *copy = [CALayer layer];
        copy.contents = image.contents;
        copy.contentsGravity = image.contentsGravity;
        copy.bounds = image.bounds;
        copy.anchorPoint = image.anchorPoint;
        copy.transform = image.transform;
        copy.position = CGPointMake(image.position.x - image.frame.origin.x,
            image.position.y - image.frame.origin.y);
        [root addSublayer:copy];
        CGContextRef oriented = context(result.orientedPreview);
        if (!oriented)
            return {};
        CGContextScaleCTM(oriented, 32.0 / image.frame.size.width,
            32.0 / image.frame.size.height);
        [root renderInContext:oriented];
        CGContextRelease(oriented);
        return result;
    }
    return {};
}

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

NativeFullScreenPresentation nativeFullScreenPresentation(QWindow *window)
{
    NSView *view = reinterpret_cast<NSView *>(window->winId());
    NSWindow *real = view.window;
    if (!real || real.alphaValue != 0.0)
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
        [CATransaction flush];
        CALayer *presentation = layer.presentationLayer;
        CALayer *image = presentation.sublayers.firstObject;
        if (!presentation || !image || !image.contents)
            continue;
        const auto rect = [](CGRect r) {
            return QRectF(r.origin.x, r.origin.y, r.size.width, r.size.height);
        };
        return {true, rect(presentation.frame), rect(image.frame)};
    }
    return {};
}

#import <AppKit/AppKit.h>
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

#import <AppKit/AppKit.h>
#import <Sparkle/Sparkle.h>
#include <cstdio>

static void event(NSString *name) {
    printf("%s\n", name.UTF8String); fflush(stdout);
    NSString *path = NSProcessInfo.processInfo.environment[@"FOVELLE_UPDATE_TEST_EVENTS"] ?: [NSBundle.mainBundle objectForInfoDictionaryKey:@"FovelleTestEvents"];
    if (path) {
        NSFileHandle *file = [NSFileHandle fileHandleForWritingAtPath:path];
        [file seekToEndOfFile];
        [file writeData:[[name stringByAppendingString:@"\n"] dataUsingEncoding:NSUTF8StringEncoding]];
        [file closeFile];
    }
}
static NSArray<NSView *> *views(NSView *root) {
    NSMutableArray *result = [NSMutableArray arrayWithObject:root];
    for (NSView *view in root.subviews) [result addObjectsFromArray:views(view)];
    return result;
}
@interface Probe : NSObject <NSApplicationDelegate, SPUUpdaterDelegate, SPUStandardUserDriverDelegate>
@property SPUStandardUpdaterController *controller;
@property NSTimer *timer;
@property NSWindow *mainWindow;
@property BOOL progress;
@property BOOL downloaded;
@property BOOL extracted;
@property BOOL canceled;
@property BOOL offered;
@property NSString *mode;
@end
@implementation Probe
- (void)applicationDidFinishLaunching:(NSNotification *)note {
    if ([[NSBundle.mainBundle objectForInfoDictionaryKey:@"CFBundleVersion"] isEqualToString:@"2.0.0"]) {
        event(@"RELAUNCHED_NEW_VERSION");
        [NSApp terminate:nil]; return;
    }
    self.mainWindow = [[NSWindow alloc] initWithContentRect:NSMakeRect(200, 200, 320, 120)
        styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
    self.mainWindow.title = @"Disposable Fovelle Update Test";
    [self.mainWindow makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    self.mode = NSProcessInfo.processInfo.environment[@"FOVELLE_UPDATE_TEST_MODE"] ?: @"install";
    self.controller = [[SPUStandardUpdaterController alloc] initWithStartingUpdater:NO updaterDelegate:self userDriverDelegate:self];
    NSError *error = nil;
    if (![self.controller.updater startUpdater:&error]) { event(error.description); exit(2); }
    self.controller.updater.automaticallyChecksForUpdates = NO;
    dispatch_async(dispatch_get_main_queue(), ^{ [self.controller checkForUpdates:nil]; });
    if ([self.mode isEqualToString:@"install"]) {
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC / 5), dispatch_get_main_queue(), ^{
            event(@"REPEATED_MANUAL_CHECK"); [self.controller checkForUpdates:nil];
        });
    }
    self.timer = [NSTimer timerWithTimeInterval:0.1 target:self selector:@selector(tick:) userInfo:nil repeats:YES];
    [NSRunLoop.mainRunLoop addTimer:self.timer forMode:NSRunLoopCommonModes];
    [NSRunLoop.mainRunLoop addTimer:self.timer forMode:NSModalPanelRunLoopMode];
}
- (void)standardUserDriverWillShowModalAlert { event(@"WILL_SHOW_ALERT"); }
- (void)standardUserDriverDidShowModalAlert {
    event(@"RESULT_ALERT_VISIBLE");
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC / 5), dispatch_get_main_queue(), ^{
        for (NSWindow *window in NSApp.windows) {
            if (!window.visible) continue;
            for (NSView *view in views(window.contentView)) {
                if ([view isKindOfClass:NSButton.class] && ((NSButton *)view).enabled) {
                    NSButton *button = (NSButton *)view;
                    if (([button.title isEqualToString:@"OK"] || [button.title isEqualToString:@"Cancel Update"])) { [button performClick:nil]; return; }
                }
            }
        }
    });
}
- (void)updater:(SPUUpdater *)updater didAbortWithError:(NSError *)error { event([NSString stringWithFormat:@"ABORT_%ld", (long)error.code]); }
- (void)tick:(NSTimer *)timer {
    for (NSWindow *window in NSApp.windows) {
        if (!window.visible) continue;
        NSArray *all = views(window.contentView);
        for (NSView *view in all) {
            if ([view isKindOfClass:NSProgressIndicator.class] && !self.progress) {
                self.progress = YES; event(@"CHECK_PROGRESS_VISIBLE");
            }
            if (![view isKindOfClass:NSButton.class]) continue;
            NSButton *button = (NSButton *)view;
            if (!button.enabled || button.hidden) continue;
            if ([button.title isEqualToString:@"Cancel"] && [self.mode isEqualToString:@"cancel-check"] && !self.canceled) {
                self.canceled = YES; event(@"CANCEL_CHECK"); [button performClick:nil]; return;
            }
            if ([button.title isEqualToString:@"Install Update"] && !self.offered) {
                self.offered = YES; event(@"UPDATE_OFFER_VISIBLE"); [button performClick:nil]; return;
            }
            if ([button.title isEqualToString:@"Cancel"] && [self.mode isEqualToString:@"cancel-download"] && self.offered && !self.canceled) {
                self.canceled = YES; event(@"CANCEL_DOWNLOAD"); [button performClick:nil]; return;
            }
            if ([button.title isEqualToString:@"Install and Relaunch"] && self.extracted) {
                event(@"INSTALL_AND_RELAUNCH"); [button performClick:nil]; return;
            }
            if (([button.title isEqualToString:@"OK"] || [button.title isEqualToString:@"Cancel Update"]) && ![self.mode isEqualToString:@"install"]) {
                event(@"RESULT_ALERT_VISIBLE"); [button performClick:nil];
            }
        }
    }
    if (self.canceled && self.controller.updater.canCheckForUpdates) {
        event(@"READY_AFTER_CANCEL"); [NSApp terminate:nil];
    }
}
- (void)updater:(SPUUpdater *)updater didDownloadUpdate:(SUAppcastItem *)item {
    self.downloaded = YES; event(@"DOWNLOAD_FINISHED");
}
- (void)updater:(SPUUpdater *)updater didExtractUpdate:(SUAppcastItem *)item {
    self.extracted = YES; event(@"EXTRACTION_FINISHED");
}
- (void)updater:(SPUUpdater *)updater willInstallUpdate:(SUAppcastItem *)item { event(@"INSTALL_STARTED"); }
- (void)updater:(SPUUpdater *)updater didFinishUpdateCycleForUpdateCheck:(SPUUpdateCheck)check error:(NSError *)error {
    if (error) event([NSString stringWithFormat:@"ERROR_%ld %@", (long)error.code, error]);
    else event(@"CYCLE_FINISHED");
    if (![self.mode isEqualToString:@"install"]) {
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC), dispatch_get_main_queue(), ^{
            event(@"CYCLE_READY"); [NSApp terminate:nil];
        });
    }
}
@end
int main() {
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        Probe *probe = [Probe new]; NSApp.delegate = probe;
        [NSApp run];
    }
    return 0;
}

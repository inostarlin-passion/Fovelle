#include "updatechecker.h"
#include "qvaboutdialog.h"
#include <QApplication>
#include <QMessageBox>
#include "nativedialogs.h"
#import <AppKit/AppKit.h>
#import <Sparkle/Sparkle.h>

namespace {
NSWindow *nativeAboutWindow(QVAboutDialog *about)
{
    NSView *view = (__bridge NSView *)reinterpret_cast<void *>(about->winId());
    return view.window;
}

bool isSparkleWindow(NSWindow *window)
{
    NSWindowController *controller = window.windowController;
    return controller &&
            [NSBundle bundleForClass:controller.class] ==
            [NSBundle bundleForClass:SPUStandardUserDriver.class];
}
} // namespace

@interface FovelleUpdateUserDriver : SPUStandardUserDriver
@end

@implementation FovelleUpdateUserDriver
- (void)attachWindowsToAbout
{
    auto *about = qobject_cast<QVAboutDialog *>(QApplication::activeModalWidget());
    if (!about || !about->isVisible())
        return;

    NSWindow *parent = nativeAboutWindow(about);
    // AppKit's modal session accepts input in child windows. An independent
    // Sparkle window can be raised but still cannot receive mouse events.
    for (NSWindow *window in NSApp.windows) {
        if (window.visible && !window.parentWindow && isSparkleWindow(window))
            [parent addChildWindow:window ordered:NSWindowAbove];
    }
}

- (void)showUserInitiatedUpdateCheckWithCancellation:(void (^)(void))cancellation
{
    [super showUserInitiatedUpdateCheckWithCancellation:cancellation];
    [self attachWindowsToAbout];
}

- (void)showUpdatePermissionRequest:(SPUUpdatePermissionRequest *)request
                              reply:(void (^)(SUUpdatePermissionResponse *))reply
{
    [super showUpdatePermissionRequest:request reply:reply];
    [self attachWindowsToAbout];
}

- (void)showUpdateFoundWithAppcastItem:(SUAppcastItem *)item
                                 state:(SPUUserUpdateState *)state
                                 reply:(void (^)(SPUUserUpdateChoice))reply
{
    [super showUpdateFoundWithAppcastItem:item state:state reply:reply];
    [self attachWindowsToAbout];
}

- (void)showDownloadInitiatedWithCancellation:(void (^)(void))cancellation
{
    [super showDownloadInitiatedWithCancellation:cancellation];
    [self attachWindowsToAbout];
}

- (void)showDownloadDidStartExtractingUpdate
{
    [super showDownloadDidStartExtractingUpdate];
    [self attachWindowsToAbout];
}

- (void)showReadyToInstallAndRelaunch:(void (^)(SPUUserUpdateChoice))installUpdateHandler
{
    [super showReadyToInstallAndRelaunch:installUpdateHandler];
    [self attachWindowsToAbout];
}

- (void)showUpdateInFocus
{
    [super showUpdateInFocus];
    [self attachWindowsToAbout];
}
@end

@interface FovelleUpdaterObserver : NSObject
@property (nonatomic, assign) UpdateChecker *owner;
@end
@implementation FovelleUpdaterObserver
- (void)observeValueForKeyPath:(NSString *)keyPath
                      ofObject:(id)object
                        change:(NSDictionary *)change
                       context:(void *)context
{
    if ([keyPath isEqualToString:@"canCheckForUpdates"])
        emit self.owner->stateChanged();
    else
        [super observeValueForKeyPath:keyPath ofObject:object change:change context:context];
}
@end

struct UpdateChecker::Impl
{
    SPUUpdater *updater = nil;
    FovelleUpdaterObserver *observer = nil;
    Qv::UpdateCheckFrequency frequency = Qv::UpdateCheckFrequency::Weekly;
    QString error;
    bool initialized = false;
    bool started = false;
};

UpdateChecker::UpdateChecker(QObject *parent) : QObject(parent), impl(std::make_unique<Impl>()) { }
UpdateChecker::~UpdateChecker()
{
    if (impl->observer)
        [impl->updater removeObserver:impl->observer forKeyPath:@"canCheckForUpdates"];
}

void UpdateChecker::initialize(Qv::UpdateCheckFrequency frequency)
{
    if (impl->initialized)
        return;
    impl->initialized = true;
    impl->frequency = frequency;
    @autoreleasepool {
        NSBundle *bundle = NSBundle.mainBundle;
        NSString *feed = [bundle objectForInfoDictionaryKey:@"SUFeedURL"];
        NSString *key = [bundle objectForInfoDictionaryKey:@"SUPublicEDKey"];
        if (!isConfigurationValid(QString::fromUtf8(feed.UTF8String),
                                  QString::fromUtf8(key.UTF8String))) {
            impl->error = tr("Updates are not configured. A valid HTTPS update feed and Ed25519 "
                             "public key are required.");
            return;
        }
        auto *driver = [[FovelleUpdateUserDriver alloc] initWithHostBundle:bundle delegate:nil];
        impl->updater = [[SPUUpdater alloc] initWithHostBundle:bundle
                                             applicationBundle:bundle
                                                    userDriver:driver
                                                      delegate:nil];
        NSError *error = nil;
        if (![impl->updater startUpdater:&error]) {
            impl->error = QString::fromUtf8(error.localizedDescription.UTF8String);
            return;
        }
        impl->started = true;
        impl->observer = [[FovelleUpdaterObserver alloc] init];
        impl->observer.owner = this;
        [impl->updater addObserver:impl->observer
                        forKeyPath:@"canCheckForUpdates"
                           options:NSKeyValueObservingOptionInitial | NSKeyValueObservingOptionNew
                           context:nullptr];
        setFrequency(frequency);
    }
}

void UpdateChecker::setFrequency(Qv::UpdateCheckFrequency frequency)
{
    const bool changed = impl->frequency != frequency;
    impl->frequency = frequency;
    if (!impl->started)
        return;
    bool automatic = frequency != Qv::UpdateCheckFrequency::Never;
#ifdef QV_DISABLE_ONLINE_VERSION_CHECK
    automatic = false;
#endif
    if (qEnvironmentVariableIsSet("FOVELLE_DISABLE_AUTO_UPDATE_CHECK"))
        automatic = false;
    auto *updater = impl->updater;
    if (updater.automaticallyChecksForUpdates != automatic)
        updater.automaticallyChecksForUpdates = automatic;
    const int interval = checkIntervalSeconds(frequency);
    if (interval > 0 && updater.updateCheckInterval != interval)
        updater.updateCheckInterval = interval;
    if (changed)
        [updater resetUpdateCycleAfterShortDelay];
}

void UpdateChecker::check(bool isManualCheck)
{
    if (!impl->initialized)
        initialize(impl->frequency);
    if (!isManualCheck)
        return; // Sparkle owns the automatic scheduler, skipped versions, and last-check time.
    if (!impl->started) {
        NativeDialogs::showMessage(QMessageBox::Warning, tr("Unable to check for updates"),
                                   impl->error, QMessageBox::Ok, QApplication::activeWindow());
        return;
    }
    // This standard entry point shows checking progress and manages download,
    // signature validation, installation and relaunch, including cancellation.
    [impl->updater checkForUpdates];
}

void UpdateChecker::detachWindowsFromAbout(QVAboutDialog *about)
{
    if (!about->windowHandle())
        return;
    NSWindow *parent = nativeAboutWindow(about);
    for (NSWindow *window in parent.childWindows) {
        if (isSparkleWindow(window))
            [parent removeChildWindow:window];
    }
}

bool UpdateChecker::getIsChecking() const
{
    return impl->started && !impl->updater.canCheckForUpdates;
}
QString UpdateChecker::configurationError() const
{
    return impl->error;
}

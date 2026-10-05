#include "updatechecker.h"
#include <QApplication>
#include <QMessageBox>
#include "nativedialogs.h"
#import <Sparkle/Sparkle.h>

@interface FovelleUpdaterObserver : NSObject
@property(nonatomic, assign) UpdateChecker *owner;
@end
@implementation FovelleUpdaterObserver
- (void)observeValueForKeyPath:(NSString *)keyPath ofObject:(id)object
                       change:(NSDictionary *)change context:(void *)context
{
    if ([keyPath isEqualToString:@"canCheckForUpdates"])
        emit self.owner->stateChanged();
    else
        [super observeValueForKeyPath:keyPath ofObject:object change:change context:context];
}
@end

struct UpdateChecker::Impl
{
    SPUStandardUpdaterController *controller = nil;
    FovelleUpdaterObserver *observer = nil;
    Qv::UpdateCheckFrequency frequency = Qv::UpdateCheckFrequency::Weekly;
    QString error;
    bool initialized = false;
    bool started = false;
};

UpdateChecker::UpdateChecker(QObject *parent) : QObject(parent), impl(std::make_unique<Impl>()) {}
UpdateChecker::~UpdateChecker()
{
    if (impl->observer)
        [impl->controller.updater removeObserver:impl->observer forKeyPath:@"canCheckForUpdates"];
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
        if (!isConfigurationValid(QString::fromUtf8(feed.UTF8String), QString::fromUtf8(key.UTF8String))) {
            impl->error = tr("Updates are not configured. A valid HTTPS update feed and Ed25519 public key are required.");
            return;
        }
        impl->controller = [[SPUStandardUpdaterController alloc]
            initWithStartingUpdater:NO updaterDelegate:nil userDriverDelegate:nil];
        NSError *error = nil;
        if (![impl->controller.updater startUpdater:&error]) {
            impl->error = QString::fromUtf8(error.localizedDescription.UTF8String);
            return;
        }
        impl->started = true;
        impl->observer = [[FovelleUpdaterObserver alloc] init];
        impl->observer.owner = this;
        [impl->controller.updater addObserver:impl->observer forKeyPath:@"canCheckForUpdates"
            options:NSKeyValueObservingOptionInitial | NSKeyValueObservingOptionNew context:nullptr];
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
    auto *updater = impl->controller.updater;
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
    [impl->controller checkForUpdates:nil];
}

bool UpdateChecker::getIsChecking() const
{
    return impl->started && !impl->controller.updater.canCheckForUpdates;
}
QString UpdateChecker::configurationError() const { return impl->error; }

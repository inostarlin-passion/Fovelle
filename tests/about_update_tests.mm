#import <AppKit/AppKit.h>

#include "qvapplication.h"

#include <QFileInfo>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>

namespace {
NSButton *findCancelButton(NSView *view)
{
    if ([view isKindOfClass:NSButton.class] &&
        [view.accessibilityIdentifier isEqualToString:@"SUStatusCancel"])
        return static_cast<NSButton *>(view);

    for (NSView *child in view.subviews) {
        if (NSButton *button = findCancelButton(child))
            return button;
    }
    return nil;
}

NSButton *visibleProgressCancelButton()
{
    for (NSWindow *window in NSApp.windows) {
        if (window.visible && [window.identifier isEqualToString:@"SUStatus"]) {
            if (NSButton *button = findCancelButton(window.contentView))
                return button;
        }
    }
    return nil;
}

bool isOrderedAbove(NSWindow *front, NSWindow *back)
{
    NSArray<NSDictionary *> *windows = (NSArray<NSDictionary *> *)CGWindowListCopyWindowInfo(
            kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements, kCGNullWindowID);
    bool frontSeen = false;
    bool inOrder = false;
    for (NSDictionary *window in windows) {
        NSInteger number = [window[(NSString *)kCGWindowNumber] integerValue];
        if (number == front.windowNumber)
            frontSeen = true;
        if (number == back.windowNumber) {
            inOrder = frontSeen;
            break;
        }
    }
    if (windows)
        CFRelease((CFArrayRef)windows);
    return back ? inOrder : frontSeen;
}

qint64 startedConnections()
{
    return QFileInfo(qEnvironmentVariable("FOVELLE_ABOUT_UPDATE_CONNECTIONS")).size();
}

bool checkingProgressReady(const qint64 previousConnections, NSWindow *parent)
{
    NSButton *cancel = visibleProgressCancelButton();
    return NSApp.active && cancel.enabled && startedConnections() > previousConnections
            && cancel.window.parentWindow == parent && isOrderedAbove(cancel.window, parent);
}

void postMouseClick(NSButton *button)
{
    const NSPoint point =
            [button convertPoint:NSMakePoint(NSMidX(button.bounds), NSMidY(button.bounds))
                          toView:nil];
    for (NSEventType type : { NSEventTypeLeftMouseDown, NSEventTypeLeftMouseUp }) {
        NSEvent *event = [NSEvent mouseEventWithType:type
                                            location:point
                                       modifierFlags:0
                                           timestamp:NSProcessInfo.processInfo.systemUptime
                                        windowNumber:button.window.windowNumber
                                             context:nil
                                         eventNumber:0
                                          clickCount:1
                                            pressure:1];
        [NSApp postEvent:event atStart:NO];
    }
}

void triggerPreferencesShortcut()
{
    NSEvent *event = [NSEvent keyEventWithType:NSEventTypeKeyDown
                                      location:NSZeroPoint
                                 modifierFlags:NSEventModifierFlagCommand
                                     timestamp:NSProcessInfo.processInfo.systemUptime
                                  windowNumber:NSApp.keyWindow.windowNumber
                                       context:nil
                                    characters:@","
                   charactersIgnoringModifiers:@","
                                     isARepeat:NO
                                       keyCode:43];
    [NSApp.mainMenu performKeyEquivalent:event];
}

QVOptionsDialog *visiblePreferences()
{
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        if (auto *options = qobject_cast<QVOptionsDialog *>(widget);
            options && options->isVisible())
            return options;
    }
    return nullptr;
}
} // namespace

class AboutUpdateTests : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void opensApplicationModal();
    void mainWindowRegistrationsAreReleased_data();
    void mainWindowRegistrationsAreReleased();
    void blocksPreferencesWhileOpen();
    void showsProgressInFront();
    void canCancelAndRetry();
    void closingAboutKeepsProgressUsable();

private:
    QPointer<QVAboutDialog> about;
    QPushButton *checkButton = nullptr;
    NSWindow *aboutWindow = nil;
};

void AboutUpdateTests::init()
{
    qvApp->openAboutDialog();
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        if (auto *dialog = qobject_cast<QVAboutDialog *>(widget)) {
            about = dialog;
            break;
        }
    }

    QVERIFY(about);
    QVERIFY(QTest::qWaitForWindowExposed(about->windowHandle()));
    checkButton = about->findChild<QPushButton *>("checkForUpdatesButton");
    QVERIFY(checkButton);
    QVERIFY(checkButton->isEnabled());
    aboutWindow = reinterpret_cast<NSView *>(about->winId()).window;
    QVERIFY(aboutWindow.visible);
    [aboutWindow center];
    [NSApp activate];
}

void AboutUpdateTests::cleanup()
{
    QSignalSpy stateChanges(&qvApp->getUpdateChecker(), &UpdateChecker::stateChanged);
    NSButton *cancel = visibleProgressCancelButton();
    if (cancel)
        [cancel performClick:nil];
    if (about)
        about->close();
    if (auto *options = visiblePreferences())
        options->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    if (cancel)
        QTRY_VERIFY(!stateChanges.isEmpty());
    QTRY_VERIFY(!visibleProgressCancelButton() && !qvApp->getUpdateChecker().getIsChecking());
}

void AboutUpdateTests::opensApplicationModal()
{
    QCOMPARE(about->windowModality(), Qt::ApplicationModal);
    QCOMPARE(QApplication::activeModalWidget(), about.data());
    QTRY_COMPARE(NSApp.modalWindow, aboutWindow);
}

void AboutUpdateTests::mainWindowRegistrationsAreReleased_data()
{
    QTest::addColumn<bool>("showWindow");
    QTest::addColumn<bool>("closeWindow");
    QTest::newRow("shown-direct-destruction") << true << false;
    QTest::newRow("unshown-direct-destruction") << false << false;
    QTest::newRow("close-then-destroy") << true << true;
}

void AboutUpdateTests::mainWindowRegistrationsAreReleased()
{
    QFETCH(bool, showWindow);
    QFETCH(bool, closeWindow);
    qvApp->getMenuBar();
    const auto &manager = qvApp->getActionManager();
    const auto menusBefore = manager.getAllClonesOfMenu("recents");
    const auto actionsBefore = manager.getAllClonesOfAction("recent0");
    QVERIFY(!menusBefore.isEmpty());
    QVERIFY(!actionsBefore.isEmpty());
    {
        MainWindow window;
        window.setAttribute(Qt::WA_DeleteOnClose, false);
        if (showWindow) {
            window.show();
            QVERIFY(window.isVisible());
        }
        QCOMPARE(manager.getAllClonesOfMenu("recents").size(), menusBefore.size() + 1);
        QCOMPARE(manager.getAllClonesOfAction("recent0").size(), actionsBefore.size() + 1);
        if (closeWindow) {
            QVERIFY(window.close());
            QCOMPARE(manager.getAllClonesOfMenu("recents"), menusBefore);
            QCOMPARE(manager.getAllClonesOfAction("recent0"), actionsBefore);
        }
    }
    QCOMPARE(manager.getAllClonesOfMenu("recents"), menusBefore);
    QCOMPARE(manager.getAllClonesOfAction("recent0"), actionsBefore);
}

void AboutUpdateTests::blocksPreferencesWhileOpen()
{
    MainWindow mainWindow;
    mainWindow.setAttribute(Qt::WA_DeleteOnClose, false);
    mainWindow.show();
    qvApp->getMenuBar();
    QVERIFY(QTest::qWaitForWindowExposed(mainWindow.windowHandle()));
    QVERIFY(NSApp.mainMenu);

    triggerPreferencesShortcut();
    qvApp->openOptionsDialog(&mainWindow);
    QCoreApplication::processEvents();
    QVERIFY(!visiblePreferences());
    QVERIFY(about && about->isVisible());

    about->close();
    QTRY_VERIFY(!about);
    triggerPreferencesShortcut();
    QTRY_VERIFY(visiblePreferences());
    visiblePreferences()->close();
}

void AboutUpdateTests::showsProgressInFront()
{
    QVERIFY2(qvApp->getUpdateChecker().configurationError().isEmpty(),
             qPrintable(qvApp->getUpdateChecker().configurationError()));
    const auto previousConnections = startedConnections();
    QTest::mouseClick(checkButton, Qt::LeftButton);
    QTRY_VERIFY(checkingProgressReady(previousConnections, aboutWindow));
    NSWindow *progressWindow = visibleProgressCancelButton().window;
    QVERIFY(aboutWindow.visible);
    QVERIFY(NSIntersectsRect(progressWindow.frame, aboutWindow.frame));
    QTRY_VERIFY2(isOrderedAbove(progressWindow, aboutWindow),
                 qPrintable(QStringLiteral("Sparkle layer %1, About layer %2, About modal %3")
                                    .arg(progressWindow.level)
                                    .arg(aboutWindow.level)
                                    .arg(NSApp.modalWindow == aboutWindow)));
    QCOMPARE(progressWindow.parentWindow, aboutWindow);
    QCOMPARE(NSApp.modalWindow, aboutWindow);
    QCOMPARE(QApplication::activeModalWidget(), about.data());
}

void AboutUpdateTests::canCancelAndRetry()
{
    QVERIFY2(qvApp->getUpdateChecker().configurationError().isEmpty(),
             qPrintable(qvApp->getUpdateChecker().configurationError()));
    for (int attempt = 0; attempt < 2; ++attempt) {
        const auto previousConnections = startedConnections();
        QTest::mouseClick(checkButton, Qt::LeftButton);
        QVERIFY(!checkButton->isEnabled());
        QTRY_VERIFY(checkingProgressReady(previousConnections, aboutWindow));
        NSButton *cancel = visibleProgressCancelButton();
        // Sparkle can accept another click to refocus visible progress. Wait
        // for the cancellation's completion notification before starting again.
        QSignalSpy stateChanges(&qvApp->getUpdateChecker(), &UpdateChecker::stateChanged);
        postMouseClick(cancel);
        QTRY_VERIFY(!visibleProgressCancelButton());
        QTRY_VERIFY(!stateChanges.isEmpty());
        QTRY_VERIFY(about && about->isVisible() && checkButton->isEnabled());
        QVERIFY(!qvApp->getUpdateChecker().getIsChecking());
        QCOMPARE(QApplication::activeModalWidget(), about.data());
        QCOMPARE(NSApp.modalWindow, aboutWindow);
    }
}

void AboutUpdateTests::closingAboutKeepsProgressUsable()
{
    QVERIFY2(qvApp->getUpdateChecker().configurationError().isEmpty(),
             qPrintable(qvApp->getUpdateChecker().configurationError()));
    const auto previousConnections = startedConnections();
    QTest::mouseClick(checkButton, Qt::LeftButton);
    QTRY_VERIFY(checkingProgressReady(previousConnections, aboutWindow));
    NSWindow *progressWindow = visibleProgressCancelButton().window;

    about->close();
    QTRY_VERIFY(!about);
    QTRY_VERIFY(checkingProgressReady(previousConnections, nil));
    NSButton *cancel = visibleProgressCancelButton();
    QCOMPARE(cancel.window, progressWindow);
    QSignalSpy stateChanges(&qvApp->getUpdateChecker(), &UpdateChecker::stateChanged);
    postMouseClick(cancel);
    QTRY_VERIFY(!visibleProgressCancelButton() && !stateChanges.isEmpty());
    QTRY_VERIFY(!qvApp->getUpdateChecker().getIsChecking());
}

int runAboutUpdateTests(int argc, char *argv[])
{
    qvApp->setQuitOnLastWindowClosed(false);
    AboutUpdateTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "about_update_tests.moc"

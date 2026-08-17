#include "qvapplication.h"
#include "qvoptionsdialog.h"
#include "qvcocoafunctions.h"
#include "updatechecker.h"

#include <QFileOpenEvent>
#include <QSettings>
#include <QTimer>
#include <QFileDialog>
#include <QFontDatabase>
#include <QUrl>

QVApplication::QVApplication(int &argc, char **argv) : QApplication(argc, argv)
{
    // Connections
    connect(&actionManager, &ActionManager::recentsMenuUpdated, this,
            &QVApplication::recentsMenuUpdated);

#ifndef QV_DISABLE_ONLINE_VERSION_CHECK
    connect(&updateChecker, &UpdateChecker::checkedUpdates, this, &QVApplication::checkedUpdates);
#endif // QV_DISABLE_ONLINE_VERSION_CHECK

    defineFilterLists();

    // Check for updates
    // TODO: move this to after first window show event
    if (getSettingsManager().getBool("updatenotifications")) {
        checkUpdates(true);
    }

    // Block any erroneous icons from showing up in macOS menus.
    setAttribute(Qt::AA_DontShowIconsInMenus);
    // Adwaita Qt styles should hide icons for a more consistent look
    if (style()->objectName() == "adwaita-dark" || style()->objectName() == "adwaita")
        setAttribute(Qt::AA_DontShowIconsInMenus);

    // Setup macOS dock menu
    dockMenu = new QMenu();
    connect(dockMenu, &QMenu::triggered, this,
            [](QAction *triggeredAction) { ActionManager::actionTriggered(triggeredAction); });

    actionManager.loadRecentsList();

    actionManager.addCloneOfAction(dockMenu, "newwindow");
    actionManager.addCloneOfAction(dockMenu, "open");
    dockMenu->setAsDockMenu();

    // Build menu bar
    menuBar = actionManager.buildMenuBar();
    connect(menuBar, &QMenuBar::triggered, this,
            [](QAction *triggeredAction) { ActionManager::actionTriggered(triggeredAction); });

    // Set macOS-specific application settings.
    QVCocoaFunctions::setUserDefaults();
    setQuitOnLastWindowClosed(getSettingsManager().getBool("quitonlastwindow"));

}

QVApplication::~QVApplication()
{
    dockMenu->deleteLater();
    menuBar->deleteLater();
}

bool QVApplication::event(QEvent *event)
{
    if (event->type() == QEvent::FileOpen) {
        const auto *openEvent = static_cast<QFileOpenEvent *>(event);
        const QUrl url = openEvent->url();
        const QString file = url.isLocalFile() ? url.toLocalFile() : openEvent->file();

        // Launch Services expects the open-document event to be acknowledged promptly. Queue the
        // image load so Finder is not held up by window creation or image decoding, and dismiss
        // the first-launch modal before showing the requested image.
        if (!file.isEmpty()) {
            queueFileOpen(file);
            if (welcomeDialog)
                welcomeDialog->close();
        }

        return true;
    } else if (event->type() == QEvent::ApplicationStateChange) {
        auto *stateEvent = static_cast<QApplicationStateChangeEvent *>(event);
        if (stateEvent->applicationState() == Qt::ApplicationActive)
            settingsManager.loadSettings();
    }
    return QApplication::event(event);
}

void QVApplication::queueFileOpen(const QString &file)
{
    pendingFileOpenPaths.append(file);

    if (fileOpenDispatchScheduled)
        return;

    fileOpenDispatchScheduled = true;
    QTimer::singleShot(0, this, &QVApplication::processPendingFileOpenEvents);
}

void QVApplication::processPendingFileOpenEvents()
{
    fileOpenDispatchScheduled = false;

    QStringList files;
    files.swap(pendingFileOpenPaths);
    for (const auto &file : files)
        openFile(file);
}

void QVApplication::openFile(MainWindow *window, const QString &file, bool resize)
{
    window->setJustLaunchedWithImage(resize);
    window->openFile(file);
}

void QVApplication::openFile(const QString &file, bool resize)
{
    auto *window = qvApp->getMainWindow(true);

    QVApplication::openFile(window, file, resize);
}

void QVApplication::pickFile(MainWindow *parent)
{
    QSettings settings;
    settings.beginGroup("recents");

    auto *fileDialog = new QFileDialog(parent, tr("Open..."));
    fileDialog->setDirectory(settings.value("lastFileDialogDir", QDir::homePath()).toString());
    fileDialog->setFileMode(QFileDialog::ExistingFiles);
    fileDialog->setNameFilters(qvApp->getNameFilterList());
    if (parent)
        fileDialog->setWindowModality(Qt::WindowModal);

    connect(fileDialog, &QFileDialog::filesSelected, fileDialog,
            [parent](const QStringList &selected) {
                bool isFirstLoop = true;
                for (const auto &file : selected) {
                    if (isFirstLoop && parent)
                        parent->openFile(file);
                    else
                        QVApplication::openFile(file);

                    isFirstLoop = false;
                }

                // Set lastFileDialogDir
                QSettings settings;
                settings.beginGroup("recents");
                settings.setValue("lastFileDialogDir", QFileInfo(selected.constFirst()).path());
            });
    fileDialog->show();
}

MainWindow *QVApplication::newWindow()
{
    auto *w = new MainWindow();
    w->show();
    w->raise();

    return w;
}

MainWindow *QVApplication::getMainWindow(bool shouldBeEmpty)
{
    // Attempt to use from list of last active windows
    for (const auto &window : qAsConst(lastActiveWindows)) {
        if (!window)
            continue;

        if (shouldBeEmpty) {
            // File info is set if an image load is requested, but not loaded
            if (!window->getCurrentFileDetails().isLoadRequested) {
                return window;
            }
        } else {
            return window;
        }
    }

    // If none of those are valid, scan the list for any existing MainWindow
    const auto topLevelWidgets = QApplication::topLevelWidgets();
    for (const auto &widget : topLevelWidgets) {
        if (auto *window = qobject_cast<MainWindow *>(widget)) {
            if (shouldBeEmpty) {
                if (!window->getCurrentFileDetails().isLoadRequested) {
                    return window;
                }
            } else {
                return window;
            }
        }
    }

    // If there are no valid ones, make a new one.
    auto *window = newWindow();
    return window;
}

void QVApplication::checkUpdates(bool isStartupCheck)
{
#ifndef QV_DISABLE_ONLINE_VERSION_CHECK
    updateChecker.check(isStartupCheck);
#endif // QV_DISABLE_ONLINE_VERSION_CHECK
}

void QVApplication::checkedUpdates()
{
#ifndef QV_DISABLE_ONLINE_VERSION_CHECK
    if (aboutDialog) {
        aboutDialog->setLatestVersionNum(updateChecker.getLatestVersionNum());
    } else if (updateChecker.getLatestVersionNum() > VERSION
               && getSettingsManager().getBool("updatenotifications")) {
        updateChecker.openDialog();
    }
#endif // QV_DISABLE_ONLINE_VERSION_CHECK
}

void QVApplication::recentsMenuUpdated()
{
    QStringList recentsPathList;
    for (const auto &recent : actionManager.getRecentsList()) {
        recentsPathList << recent.filePath;
    }
    QVCocoaFunctions::setDockRecents(recentsPathList);
}

void QVApplication::addToLastActiveWindows(MainWindow *window)
{
    if (!window)
        return;

    if (!lastActiveWindows.isEmpty() && window == lastActiveWindows.first())
        return;

    lastActiveWindows.prepend(window);

    if (lastActiveWindows.length() > 5)
        lastActiveWindows.removeLast();
}

void QVApplication::deleteFromLastActiveWindows(MainWindow *window)
{
    if (!window)
        return;

    lastActiveWindows.removeAll(window);
}

void QVApplication::openOptionsDialog(QWidget *parent)
{
    // On macOS, the dialog should not be dependent on any window
    parent = nullptr;

    if (optionsDialog) {
        optionsDialog->raise();
        optionsDialog->activateWindow();
        return;
    }

    optionsDialog = new QVOptionsDialog(parent);
    optionsDialog->show();
}

void QVApplication::openWelcomeDialog(QWidget *parent)
{
    // On macOS, the dialog should not be dependent on any window
    parent = nullptr;

    if (welcomeDialog) {
        welcomeDialog->raise();
        welcomeDialog->activateWindow();
        return;
    }

    welcomeDialog = new QVWelcomeDialog(parent);
    welcomeDialog->show();
}

void QVApplication::openAboutDialog(QWidget *parent)
{
    // On macOS, the dialog should not be dependent on any window
    parent = nullptr;

    if (aboutDialog) {
        aboutDialog->raise();
        aboutDialog->activateWindow();
        return;
    }

#ifndef QV_DISABLE_ONLINE_VERSION_CHECK
    aboutDialog = new QVAboutDialog(updateChecker.getLatestVersionNum(), parent);
#else
    aboutDialog = new QVAboutDialog(-1, parent);
#endif // QV_DISABLE_ONLINE_VERSION_CHECK
    aboutDialog->show();
}

void QVApplication::defineFilterLists()
{
    const auto &byteArrayFormats = QImageReader::supportedImageFormats();

    auto filterString = tr("Supported Images") + " (";
    fileExtensionList.reserve(byteArrayFormats.size() - 1);

    const auto addExtension = [&](const QString &extension) {
        filterString += "*" + extension + " ";
        fileExtensionList << extension;
    };

    // Build the filterlist, filterstring, and filterregexplist in one loop
    for (const auto &byteArray : byteArrayFormats) {
        const auto fileExtension = "." + QString::fromUtf8(byteArray);
        // Qt 5.15 seems to have added pdf support for QImageReader but it is super broken in Fovelle
        if (fileExtension == ".pdf")
            continue;

        addExtension(fileExtension);

        // Register additional file extensions that decoders support but don't advertise
        if (fileExtension == ".jpg") {
            addExtension(".jpe");
            addExtension(".jfi");
#if QT_VERSION < QT_VERSION_CHECK(6, 8, 0)
            addExtension(".jfif");
#endif
        } else if (fileExtension == ".heic") {
            addExtension(".heics");
        } else if (fileExtension == ".heif") {
            addExtension(".heifs");
            addExtension(".hif");
        }
    }
    filterString.chop(1);
    filterString += ")";

    // Build mime type list
    const auto &byteArrayMimeTypes = QImageReader::supportedMimeTypes();
    mimeTypeNameList.reserve(byteArrayMimeTypes.size() - 1);
    for (const auto &byteArray : byteArrayMimeTypes) {
        // Qt 5.15 seems to have added pdf support for QImageReader but it is super broken in Fovelle
        const QString mime = QString::fromUtf8(byteArray);
        if (mime == "application/pdf")
            continue;

        mimeTypeNameList << mime;
    }

    // Build name filter list for file dialogs
    nameFilterList << filterString;
    nameFilterList << tr("All Files") + " (*)";
}

void QVApplication::ensureFontLoaded(const QString &path)
{
    if (loadedFontPaths.contains(path))
        return;

    QFontDatabase::addApplicationFont(path);
    loadedFontPaths.insert(path);
}

QIcon QVApplication::iconFromFont(const QString &fontFamily, const QChar &codePoint, const int pixelSize, const qreal pixelRatio)
{
    const int scaledPixelSize = qRound(pixelSize * pixelRatio);

    QFont font(fontFamily);
    font.setPixelSize(pixelSize);

    QPixmap pixmap(scaledPixelSize, scaledPixelSize);
    pixmap.fill(Qt::transparent);
    pixmap.setDevicePixelRatio(pixelRatio);

    QPainter painter(&pixmap);
    painter.setFont(font);
    painter.setPen(QApplication::palette().color(QPalette::WindowText));
    painter.drawText(pixmap.rect(), codePoint);

    return QIcon(pixmap);
}

qreal QVApplication::getPerceivedBrightness(const QColor &color)
{
    return (color.red() * 0.299 + color.green() * 0.587 + color.blue() * 0.114) / 255.0;
}

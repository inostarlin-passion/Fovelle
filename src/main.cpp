#include "mainwindow.h"
#include "qvapplication.h"
#include <QCheckBox>
#include <QCommandLineParser>
#include <QDebug>
#include <QElapsedTimer>
#include <QHeaderView>
#include <QTabBar>
#include <QTableWidget>
#include <QTimer>

namespace
{
class StartupTrace
{
public:
    StartupTrace() : enabled(qEnvironmentVariableIsSet("FOVELLE_STARTUP_PERF"))
    {
        if (enabled)
            timer.start();
    }

    void mark(const char *phase) const
    {
        if (!enabled)
            return;

        qInfo().noquote() << QStringLiteral("FOVELLE_STARTUP phase=%1 elapsed_ms=%2")
                                 .arg(QString::fromLatin1(phase))
                                 .arg(timer.elapsed());
    }

private:
    bool enabled {false};
    QElapsedTimer timer;
};
}

int main(int argc, char *argv[])
{
    const StartupTrace startupTrace;

    QCoreApplication::setOrganizationName("Fovelle");
    QCoreApplication::setOrganizationDomain("io.github.inostarlin-passion");
    QCoreApplication::setApplicationName("Fovelle");
    QGuiApplication::setApplicationDisplayName("Fovelle");
    QCoreApplication::setApplicationVersion(QStringLiteral(VERSION_STRING));
    startupTrace.mark("application-identity");

    SettingsManager::migrateOldSettings();
    startupTrace.mark("settings-migrated");

    QVApplication app(argc, argv);
    startupTrace.mark("application-constructed");
    const bool systemProbe = qEnvironmentVariableIsSet("FOVELLE_SYSTEM_PROBE");
    const bool settingsSystemProbe =
        qEnvironmentVariableIsSet("FOVELLE_SETTINGS_SYSTEM_PROBE");

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QObject::tr("file"), QObject::tr("The file to open."));
    parser.process(app);
    startupTrace.mark("command-line-parsed");

    if (!parser.positionalArguments().isEmpty())
    {
        QVApplication::openFile(QVApplication::newWindow(), parser.positionalArguments().constFirst(), true);
    }
    else if (systemProbe || settingsSystemProbe
             || !QVApplication::tryRestoreLastSession())
    {
        QVApplication::newWindow();
    }
    startupTrace.mark("window-created");

    QTimer::singleShot(0, &app, [&startupTrace]() {
        startupTrace.mark("first-event-loop-turn");
    });

    if (settingsSystemProbe)
    {
        bool probeDelayIsValid = false;
        const int requestedProbeDelay = qEnvironmentVariableIntValue(
            "FOVELLE_SYSTEM_PROBE_DELAY_MS", &probeDelayIsValid);
        const int probeDelay = probeDelayIsValid
            ? qMax(0, requestedProbeDelay) : 300;
        QTimer::singleShot(probeDelay, &app, [&app, &startupTrace]() {
            startupTrace.mark("settings-system-probe-open");
            app.openOptionsDialog();

            QTimer::singleShot(300, &app, [&app, &startupTrace]() {
                auto *dialog = [&app]() -> QVOptionsDialog * {
                    for (QWidget *widget : app.topLevelWidgets())
                    {
                        if (auto *options = qobject_cast<QVOptionsDialog *>(widget))
                            return options;
                    }
                    return nullptr;
                }();
                if (dialog)
                {
                    if (auto *tabs = dialog->findChild<QTabBar *>(
                            QStringLiteral("categoryTabs")))
                        tabs->setCurrentIndex(1);
                }

                QTimer::singleShot(300, &app, [&app, &startupTrace]() {
                    startupTrace.mark("settings-system-probe");
                    QVOptionsDialog *dialog = nullptr;
                    for (QWidget *widget : app.topLevelWidgets())
                    {
                        if (auto *options = qobject_cast<QVOptionsDialog *>(widget))
                        {
                            dialog = options;
                            break;
                        }
                    }

                    int tabCount = 0;
                    bool adaptive = false;
                    bool tabWidthsValid = false;
                    bool columnsEqual = false;
                    bool checkerboardRenamed = false;
                    int currentTabWidth = 0;
                    if (dialog)
                    {
                        auto *tabs = dialog->findChild<QTabBar *>(
                            QStringLiteral("categoryTabs"));
                        auto *table = dialog->findChild<QTableWidget *>(
                            QStringLiteral("shortcutsTable"));
                        auto *checkerboard = dialog->findChild<QCheckBox *>(
                            QStringLiteral("checkerboardBackgroundCheckbox"));
                        tabCount = tabs ? tabs->count() : 0;
                        adaptive = dialog->property(
                            "settingsAdaptiveTabWidths").toBool();
                        const QVariantList widths = dialog->property(
                            "settingsTabWidths").toList();
                        tabWidthsValid = widths.size() == tabCount;
                        for (const QVariant &width : widths)
                            tabWidthsValid = tabWidthsValid && width.toInt() > 0;
                        currentTabWidth = dialog->property(
                            "settingsTabWidth").toInt();
                        if (table)
                        {
                            auto *header = table->horizontalHeader();
                            // Stretch sections are integer pixels; an odd
                            // viewport can therefore differ by one pixel.
                            columnsEqual = qAbs(header->sectionSize(0)
                                                - header->sectionSize(1)) <= 1
                                && header->sectionSize(0) > 0;
                        }
                        checkerboardRenamed = checkerboard
                            && !checkerboard->text().contains(
                                QStringLiteral("after opening image"),
                                Qt::CaseInsensitive);
                    }

                    qInfo().noquote() << QStringLiteral(
                        "FOVELLE_SETTINGS_SYSTEM_PROBE tabs=%1 adaptive=%2 "
                        "tab_widths_valid=%3 current_tab_width=%4 "
                        "columns_equal=%5 checkerboard_renamed=%6")
                             .arg(tabCount)
                             .arg(adaptive ? QStringLiteral("true")
                                           : QStringLiteral("false"))
                             .arg(tabWidthsValid ? QStringLiteral("true")
                                                  : QStringLiteral("false"))
                             .arg(currentTabWidth)
                             .arg(columnsEqual ? QStringLiteral("true")
                                               : QStringLiteral("false"))
                             .arg(checkerboardRenamed
                                      ? QStringLiteral("true")
                                      : QStringLiteral("false"));
                    if (dialog)
                        dialog->close();
                    app.onSystemInitiatedQuit();
                    app.quit();
                });
            });
        });
    }
    else if (systemProbe)
    {
        bool probeDelayIsValid = false;
        const int requestedProbeDelay = qEnvironmentVariableIntValue(
            "FOVELLE_SYSTEM_PROBE_DELAY_MS", &probeDelayIsValid);
        const int probeDelay = probeDelayIsValid
            ? qMax(0, requestedProbeDelay) : 300;
        QTimer::singleShot(probeDelay, &app, [&app, &startupTrace]() {
            startupTrace.mark("system-probe");
            int mainWindowCount = 0;
            bool allWindowsMaximized = true;
            for (QWidget *widget : app.topLevelWidgets())
            {
                auto *window = qobject_cast<MainWindow *>(widget);
                if (!window)
                    continue;
                ++mainWindowCount;
                allWindowsMaximized = allWindowsMaximized
                    && window->windowState().testFlag(Qt::WindowMaximized);
            }
            qInfo().noquote() << QStringLiteral("FOVELLE_SYSTEM_PROBE windows=%1 maximized=%2")
                                     .arg(mainWindowCount)
                                     .arg(allWindowsMaximized ? QStringLiteral("true") : QStringLiteral("false"));
            app.onSystemInitiatedQuit();
            app.quit();
            startupTrace.mark("quit-requested");
        });
    }

    const int exitCode = QApplication::exec();
    startupTrace.mark("event-loop-return");
    return exitCode;
}

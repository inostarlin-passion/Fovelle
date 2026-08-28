#include "mainwindow.h"
#include "qvapplication.h"
#include <QCommandLineParser>
#include <QDebug>
#include <QElapsedTimer>
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
    else if (systemProbe || !QVApplication::tryRestoreLastSession())
    {
        QVApplication::newWindow();
    }
    startupTrace.mark("window-created");

    QTimer::singleShot(0, &app, [&startupTrace]() {
        startupTrace.mark("first-event-loop-turn");
    });

    if (systemProbe)
    {
        QTimer::singleShot(300, &app, [&app, &startupTrace]() {
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
            app.quit();
        });
    }

    return QApplication::exec();
}

#include "mainwindow.h"
#include "qvapplication.h"
#include <QCommandLineParser>
#include <QTimer>

int main(int argc, char *argv[])
{
    QCoreApplication::setOrganizationName("Fovelle");
    QCoreApplication::setOrganizationDomain("io.github.inostarlin-passion");
    QCoreApplication::setApplicationName("Fovelle");
    QGuiApplication::setApplicationDisplayName("Fovelle");
    QCoreApplication::setApplicationVersion(QStringLiteral(VERSION_STRING));

    SettingsManager::migrateOldSettings();

    QVApplication app(argc, argv);
    const bool systemProbe = qEnvironmentVariableIsSet("FOVELLE_SYSTEM_PROBE");

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QObject::tr("file"), QObject::tr("The file to open."));
    parser.process(app);

    if (!parser.positionalArguments().isEmpty())
    {
        QVApplication::openFile(QVApplication::newWindow(), parser.positionalArguments().constFirst(), true);
    }
    else if (systemProbe || !QVApplication::tryRestoreLastSession())
    {
        QVApplication::newWindow();
    }

    if (systemProbe)
    {
        QTimer::singleShot(300, &app, [&app]() {
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

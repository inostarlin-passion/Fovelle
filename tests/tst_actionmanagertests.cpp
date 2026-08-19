#include <QtTest>
#include <QFileInfo>
#include <QFileOpenEvent>
#include <QImage>
#include <QLabel>
#include <QTextDocumentFragment>
#include <QTemporaryDir>

#include "mainwindow.h"
#include "qvaboutdialog.h"
#include "qvapplication.h"

class ActionManagerTests : public QObject
{
    Q_OBJECT

private slots:
    void testClonedActionsUntracked();
    void testApplicationIdentity();
    void testAboutDialogIdentity();
    void testWindowTitleIdentity();
    void testFinderFileOpenEvent();
};

void ActionManagerTests::testClonedActionsUntracked()
{
    const bool originalQuitOnLastWindowClosed = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);

    const int fullscreenCount = qvApp->getActionManager().getAllInstancesOfAction("fullscreen").length();
    const int openCount = qvApp->getActionManager().getAllInstancesOfAction("open").length();
    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.show();
    QVERIFY(qvApp->getActionManager().getAllInstancesOfAction("fullscreen").length() != fullscreenCount);
    QVERIFY(qvApp->getActionManager().getAllInstancesOfAction("open").length() != openCount);
    window.close();
    QCOMPARE(qvApp->getActionManager().getAllInstancesOfAction("fullscreen").length(), fullscreenCount);
    QCOMPARE(qvApp->getActionManager().getAllInstancesOfAction("open").length(), openCount);

    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

void ActionManagerTests::testApplicationIdentity()
{
    QCOMPARE(QCoreApplication::organizationName(), QString("Fovelle"));
    QCOMPARE(QCoreApplication::organizationDomain(), QString("io.github.inostarlin-passion"));
    QCOMPARE(QCoreApplication::applicationName(), QString("Fovelle"));
    QCOMPARE(QGuiApplication::applicationDisplayName(), QString("Fovelle"));
    QCOMPARE(QCoreApplication::applicationVersion(), QString("0.1.1"));
}

void ActionManagerTests::testAboutDialogIdentity()
{
    QVAboutDialog dialog;
    const auto *logoLabel = dialog.findChild<QLabel *>("logoLabel");
    const auto *subtitleLabel = dialog.findChild<QLabel *>("subtitleLabel");
    const auto *infoLabel = dialog.findChild<QLabel *>("infoLabel2");
    QVERIFY(logoLabel);
    QVERIFY(subtitleLabel);
    QVERIFY(infoLabel);
    QCOMPARE(dialog.windowTitle(), QString("About Fovelle"));
    QCOMPARE(logoLabel->text(), QString("Fovelle"));
    QCOMPARE(subtitleLabel->text(), QString("version 0.1.1"));

    const QString visibleText = QTextDocumentFragment::fromHtml(infoLabel->text()).toPlainText();
    QCOMPARE(visibleText,
             QString("Based on qView\n"
                     "Copyright © 2018–2025 jurplel and qView contributors\n"
                     "Fovelle modifications © 2026 Fovelle contributors\n"
                     "Includes portions of commits from jdpurcell/qView by jdpurcell\n\n"
                     "Licensed under GPLv3"));
    QVERIFY(infoLabel->text().contains("https://github.com/inostarlin-passion/Fovelle"));
    QVERIFY(infoLabel->text().contains("https://github.com/jdpurcell/qView"));
    QVERIFY(!infoLabel->text().contains("interversehq.com"));
}

void ActionManagerTests::testWindowTitleIdentity()
{
    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.buildWindowTitle();
    QCOMPARE(window.windowTitle(), QString("Fovelle"));
    window.close();
}

void ActionManagerTests::testFinderFileOpenEvent()
{
    const bool originalQuitOnLastWindowClosed = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);

    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString imagePath = temporaryDirectory.filePath("finder-open.png");
    QImage image(8, 8, QImage::Format_RGB32);
    image.fill(Qt::green);
    QVERIFY(image.save(imagePath));

    auto *window = QVApplication::newWindow();
    QVERIFY(window);
    QFileOpenEvent openEvent(imagePath);
    QVERIFY(QCoreApplication::sendEvent(qvApp, &openEvent));
    QVERIFY(!window->getCurrentFileDetails().isLoadRequested);
    QTRY_VERIFY_WITH_TIMEOUT(window->getCurrentFileDetails().isPixmapLoaded, 2000);
    QCOMPARE(window->getCurrentFileDetails().fileInfo.absoluteFilePath(), QFileInfo(imagePath).absoluteFilePath());

    window->close();
    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

int main(int argc, char *argv[])
{
    QCoreApplication::setOrganizationName("Fovelle");
    QCoreApplication::setOrganizationDomain("io.github.inostarlin-passion");
    QCoreApplication::setApplicationName("Fovelle");
    QGuiApplication::setApplicationDisplayName("Fovelle");
    QCoreApplication::setApplicationVersion("0.1.1");
    QVApplication app(argc, argv);
    ActionManagerTests actionManagerTests;
    return QTest::qExec(&actionManagerTests, argc, argv);
}

#include "tst_actionmanagertests.moc"

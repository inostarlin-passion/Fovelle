#include <QtTest>
#include <QLabel>

#include "qvapplication.h"

class ActionManagerTests : public QObject
{
    Q_OBJECT

public:
    ActionManagerTests();
    ~ActionManagerTests();

private slots:
    void testClonedActionsUntracked();
    void testApplicationIdentity();
    void testAboutDialogIdentity();
    void testWindowTitleIdentity();
};

ActionManagerTests::ActionManagerTests() { }

ActionManagerTests::~ActionManagerTests() { }

void ActionManagerTests::testClonedActionsUntracked()
{
    // Get initial counts of certain actions
    int fullscreenCount = qvApp->getActionManager().getAllInstancesOfAction("fullscreen").length();
    int openCount = qvApp->getActionManager().getAllInstancesOfAction("open").length();
    qDebug() << fullscreenCount;

    // Have window clone actions
    MainWindow window;
    window.show();
    // Make sure they were cloned
    QVERIFY(qvApp->getActionManager().getAllInstancesOfAction("fullscreen").length()
            != fullscreenCount);
    QVERIFY(qvApp->getActionManager().getAllInstancesOfAction("open").length() != openCount);
    // Untrack them
    window.close();

    // Make sure the count has not changed from the initial
    QCOMPARE(qvApp->getActionManager().getAllInstancesOfAction("fullscreen").length(),
             fullscreenCount);
    QCOMPARE(qvApp->getActionManager().getAllInstancesOfAction("open").length(), openCount);
}

void ActionManagerTests::testApplicationIdentity()
{
    QCOMPARE(QCoreApplication::organizationName(), QString("Fovelle"));
    QCOMPARE(QCoreApplication::organizationDomain(), QString("io.github.inostarlin-passion"));
    QCOMPARE(QCoreApplication::applicationName(), QString("Fovelle"));
    QCOMPARE(QCoreApplication::applicationVersion(), QString("0.1.0"));
}

void ActionManagerTests::testAboutDialogIdentity()
{
    QVAboutDialog dialog(-1);

    const auto *logoLabel = dialog.findChild<QLabel *>("logoLabel");
    const auto *subtitleLabel = dialog.findChild<QLabel *>("subtitleLabel");
    const auto *infoLabel = dialog.findChild<QLabel *>("infoLabel2");
    const auto *updateLabel = dialog.findChild<QLabel *>("updateLabel");

    QCOMPARE(dialog.windowTitle(), QString("About Fovelle"));
    QVERIFY(logoLabel);
    QVERIFY(subtitleLabel);
    QVERIFY(infoLabel);
    QVERIFY(updateLabel);
    QCOMPARE(logoLabel->text(), QString("Fovelle"));
    QCOMPARE(subtitleLabel->text(), QString("version 0.1.0"));

    const QString expectedAboutText =
            "Based on qView<br>"
            "Copyright © 2018–2025 jurplel and qView contributors<br>"
            "Fovelle modifications © 2026 Fovelle contributors<br><br>"
            "Licensed under GPLv3<br>"
            R"(Source code: <a style="color: #03A9F4; text-decoration:none;" href="https://github.com/inostarlin-passion/Fovelle">GitHub</a>)";
    QCOMPARE(infoLabel->text(), expectedAboutText);
    QVERIFY(!infoLabel->text().contains("interversehq.com"));
    QCOMPARE(updateLabel->text(), QString());
}

void ActionManagerTests::testWindowTitleIdentity()
{
    MainWindow window;
    window.updateWindowTitle();
    QCOMPARE(window.windowTitle(), QString("Fovelle"));
}

int main(int argc, char *argv[])
{
    QCoreApplication::setOrganizationName("Fovelle");
    QCoreApplication::setOrganizationDomain("io.github.inostarlin-passion");
    QCoreApplication::setApplicationName("Fovelle");
    QCoreApplication::setApplicationVersion("0.1.0");
    QVApplication app(argc, argv);
    ActionManagerTests actionManagerTests;
    return QTest::qExec(&actionManagerTests, argc, argv);
}

#include "tst_actionmanagertests.moc"

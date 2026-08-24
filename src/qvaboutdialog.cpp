#include "qvaboutdialog.h"
#include "ui_qvaboutdialog.h"

#include "qvapplication.h"
#include "nativedialogs.h"

#include <QJsonDocument>
#include <QShowEvent>

QVAboutDialog::QVAboutDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::QVAboutDialog)
{
    ui->setupUi(this);

    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);

    connect(ui->checkForUpdatesButton, &QPushButton::clicked, this, &QVAboutDialog::checkForUpdatesButtonClicked);

    setWindowModality(Qt::ApplicationModal);

    const QString subtitleText = tr("version %1").arg(QCoreApplication::applicationVersion());
    ui->subtitleLabel->setText(subtitleText);

    const QString labelText2 = tr("Based on qView<br>"
                                  "Copyright © 2018–2025 jurplel and qView contributors<br>"
                                  R"(Fovelle modifications © 2026 <a href="https://github.com/inostarlin-passion/Fovelle">Fovelle</a> contributors<br>)"
                                  R"(Includes portions of commits from <a href="https://github.com/jdpurcell/qView">jdpurcell/qView</a> by jdpurcell<br><br>)"
                                  "Licensed under GPLv3");

    ui->infoLabel2->setText(labelText2);

    ui->infoLabel2->setTextInteractionFlags(Qt::TextBrowserInteraction);
    ui->infoLabel2->setOpenExternalLinks(true);

    updateCheckForUpdatesButtonState();
}

QVAboutDialog::~QVAboutDialog()
{
    delete ui;
}

void QVAboutDialog::showEvent(QShowEvent *event)
{
    NativeDialogs::applyTheme(this);
    QDialog::showEvent(event);
}

void QVAboutDialog::updateCheckForUpdatesButtonState()
{
    ui->checkForUpdatesButton->setEnabled(!qvApp->getUpdateChecker().getIsChecking());
}

void QVAboutDialog::checkForUpdatesButtonClicked()
{
    qvApp->getUpdateChecker().check(true);
    updateCheckForUpdatesButtonState();
}

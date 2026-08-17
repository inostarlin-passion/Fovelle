#include "qvaboutdialog.h"
#include "ui_qvaboutdialog.h"

#include "qvapplication.h"

#include <QJsonDocument>

QVAboutDialog::QVAboutDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::QVAboutDialog)
{
    ui->setupUi(this);

    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlags(windowFlags() & (~Qt::WindowContextHelpButtonHint | Qt::CustomizeWindowHint));

    connect(ui->checkForUpdatesButton, &QPushButton::clicked, this, &QVAboutDialog::checkForUpdatesButtonClicked);

    setWindowModality(Qt::ApplicationModal);

    // add fonts
    qvApp->ensureFontLoaded(":/fonts/Lato-Light.ttf");
    qvApp->ensureFontLoaded(":/fonts/Lato-Regular.ttf");

    //set main title font
    const QFont font1 = QFont("Lato", 96, QFont::Light);
    ui->logoLabel->setFont(font1);

    //set subtitle font & text
    QFont font2 = QFont("Lato", 22);
    font2.setStyleName("Regular");
    const QString subtitleText = tr("version %1").arg(QCoreApplication::applicationVersion());
    ui->subtitleLabel->setFont(font2);
    ui->subtitleLabel->setText(subtitleText);

    //set infolabel2 font, text, and properties
    QFont font4 = QFont("Lato", 12);
    font4.setStyleName("Regular");
    const QString labelText2 = tr("Based on qView<br>"
                                  "Copyright © 2018–2025 jurplel and qView contributors<br>"
                                  R"(Fovelle modifications © 2026 <a style="color: #03A9F4; text-decoration:none;" href="https://github.com/inostarlin-passion/Fovelle">Fovelle</a> contributors<br><br>)"
                                  "Licensed under GPLv3");

    ui->infoLabel2->setFont(font4);
    ui->infoLabel2->setText(labelText2);

    ui->infoLabel2->setTextInteractionFlags(Qt::TextBrowserInteraction);
    ui->infoLabel2->setOpenExternalLinks(true);

    updateCheckForUpdatesButtonState();
}

QVAboutDialog::~QVAboutDialog()
{
    delete ui;
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

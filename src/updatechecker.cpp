#include "updatechecker.h"

#include "qvapplication.h"

#include <QMessageBox>
#include <QPushButton>
#include <QDateTime>
#include <QRegularExpression>
#include <QDesktopServices>

UpdateChecker::UpdateChecker(QObject *parent) : QObject(parent)
{
    connect(&netAccessManager, &QNetworkAccessManager::finished, this, &UpdateChecker::readReply);
}

void UpdateChecker::check(bool isManualCheck)
{
    if (isChecking)
        return;

#ifndef NIGHTLY
    // This fork uses only the nightly build number for update versioning
    onError(tr("This build is not configured for update checking."));
    return;
#endif

    if (!isManualCheck)
    {
        QDateTime lastCheckTime = getLastCheckTime();
        if (lastCheckTime.isValid() && QDateTime::currentDateTimeUtc() < lastCheckTime.addSecs(AUTO_CHECK_INTERVAL_HOURS * 3600))
            return;
    }

    isChecking = true;
    QNetworkRequest request(API_BASE_URL + "/latest");
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    netAccessManager.get(request);
}

void UpdateChecker::readReply(QNetworkReply *reply)
{
    isChecking = false;
    hasChecked = true;

    if (reply->error() != QNetworkReply::NoError)
    {
        onError(reply->errorString());
        return;
    }

    QJsonDocument json = QJsonDocument::fromJson(reply->readAll());

    if (json.isNull())
    {
        onError(tr("Received null JSON."));
        return;
    }

    QJsonObject object = json.object();

    checkResult = {
        true,
        {},
        object.value("tag_name").toString(),
        object.value("name").toString(),
        object.value("body").toString()
    };

    setLastCheckTime(QDateTime::currentDateTimeUtc());

    emit checkedUpdates();
}

void UpdateChecker::onError(QString msg)
{
    checkResult = {
        false,
        msg,
        {},
        {},
        {}
    };

    emit checkedUpdates();
}

QDateTime UpdateChecker::getLastCheckTime()
{
    qint64 secsSinceEpoch = QSettings().value("lastupdatecheck").toLongLong();
    return secsSinceEpoch == 0 ? QDateTime() : QDateTime::fromSecsSinceEpoch(secsSinceEpoch, QTimeZone::utc());
}

void UpdateChecker::setLastCheckTime(QDateTime value)
{
    QSettings().setValue("lastupdatecheck", value.toSecsSinceEpoch());
}

QString UpdateChecker::getSkippedTagName()
{
    return QSettings().value("skippedupdatetagname").toString();
}

void UpdateChecker::setSkippedTagName(QString value)
{
    QSettings().setValue("skippedupdatetagname", value);
}

double UpdateChecker::parseVersion(QString str)
{
    return str.remove(QRegularExpression("[^0-9]")).left(8).toDouble();
}

bool UpdateChecker::isVersionConsideredUpdate(QString tagName)
{
#ifndef NIGHTLY
    // This fork uses only the nightly build number for update versioning
    return false;
#endif

    QString skippedTagName = getSkippedTagName();
    if (!skippedTagName.isEmpty() && tagName == skippedTagName)
        return false;

    double tagVersion = parseVersion(tagName);
    return tagVersion > 0 && tagVersion > parseVersion(QT_STRINGIFY(NIGHTLY));
}

void UpdateChecker::openDialog(QWidget *parent, bool isAutoCheck)
{
    if (!(hasChecked && checkResult.wasSuccessful && checkResult.isConsideredUpdate()))
        return;

    auto *msgBox = new QMessageBox(parent);
    msgBox->setWindowTitle(tr("qView Update Available"));
    msgBox->setText(tr("A newer version is available to download.")
                    + "\n\n" + checkResult.releaseName + ":\n" + checkResult.changelog);
    msgBox->setWindowModality(Qt::ApplicationModal);
    msgBox->setStandardButtons(QMessageBox::Close | (isAutoCheck ? QMessageBox::Reset : QMessageBox::NoButton));
    if (isAutoCheck)
    {
        auto *skipButton = new QPushButton(tr("Skip Version"), msgBox);
        msgBox->addButton(skipButton, QMessageBox::ActionRole);
        connect(skipButton, &QAbstractButton::clicked, this, [this]{
            setSkippedTagName(checkResult.tagName);
        });
    }
    auto *downloadButton = new QPushButton(tr("Download"), msgBox);
    msgBox->addButton(downloadButton, QMessageBox::ActionRole);
    connect(downloadButton, &QAbstractButton::clicked, this, [this]{
        QDesktopServices::openUrl(DOWNLOAD_URL);
    });
    if (isAutoCheck)
    {
        msgBox->button(QMessageBox::Reset)->setText(tr("&Disable Checking"));
        connect(msgBox->button(QMessageBox::Reset), &QAbstractButton::clicked, qvApp, []{
            QSettings settings;
            settings.beginGroup("options");
            settings.setValue("updatenotifications", false);
            qvApp->getSettingsManager().loadSettings();
            QMessageBox::information(nullptr, tr("qView Update Checking Disabled"), tr("Update notifications on startup have been disabled.\nYou can reenable them in the options dialog."), QMessageBox::Ok);
        });
    }
    msgBox->open();
    msgBox->setDefaultButton(QMessageBox::Close);
}

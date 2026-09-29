#include "updatechecker.h"

#include "qvapplication.h"
#include "nativedialogs.h"

#include <QMessageBox>
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

    if (!isManualCheck)
    {
        const QDateTime now = QDateTime::currentDateTimeUtc();
        const auto frequency = qvApp->getSettingsManager().getEnum<Qv::UpdateCheckFrequency>("updatecheckfrequency");
        if (!shouldCheckAutomatically(now, getLastCheckTime(), frequency))
            return;
    }

    isChecking = true;
    lastCheckWasManual = isManualCheck;
    QNetworkRequest request(API_BASE_URL + "/latest");
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    netAccessManager.get(request);
}

bool UpdateChecker::shouldCheckAutomatically(const QDateTime &now,
                                             const QDateTime &lastCheck,
                                             const Qv::UpdateCheckFrequency frequency)
{
    if (frequency == Qv::UpdateCheckFrequency::Never || !now.isValid())
        return false;
    if (!lastCheck.isValid())
        return true;

    const QDateTime nextCheck = [&]() {
        switch (frequency)
        {
        case Qv::UpdateCheckFrequency::Daily:
            return lastCheck.addDays(1);
        case Qv::UpdateCheckFrequency::Weekly:
            return lastCheck.addDays(7);
        case Qv::UpdateCheckFrequency::Monthly:
            return lastCheck.addMonths(1);
        case Qv::UpdateCheckFrequency::Never:
            break;
        }
        return lastCheck;
    }();
    return now >= nextCheck;
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
    QString skippedTagName = getSkippedTagName();
    if (!skippedTagName.isEmpty() && tagName == skippedTagName)
        return false;

    double tagVersion = parseVersion(tagName);
    return tagVersion > 0 && tagVersion > parseVersion(QCoreApplication::applicationVersion());
}

void UpdateChecker::openDialog(QWidget *parent, bool isAutoCheck)
{
    if (!(hasChecked && checkResult.wasSuccessful && checkResult.isConsideredUpdate()))
        return;

    auto *msgBox = NativeDialogs::createMessageBox(
        QMessageBox::Information,
        tr("Fovelle Update Available"),
        tr("A newer version is available to download.")
            + "\n\n" + checkResult.releaseName + ":\n" + checkResult.changelog,
        QMessageBox::Open | QMessageBox::Close
            | (isAutoCheck ? QMessageBox::Ignore : QMessageBox::NoButton),
        parent);
    msgBox->setDefaultButton(QMessageBox::Open);
    msgBox->setEscapeButton(QMessageBox::Close);
    connect(msgBox, &QMessageBox::buttonClicked, this,
            [this, msgBox, isAutoCheck, tagName = checkResult.tagName](QAbstractButton *button) {
        switch (msgBox->standardButton(button)) {
        case QMessageBox::Open:
            QDesktopServices::openUrl(DOWNLOAD_URL);
            break;
        case QMessageBox::Ignore:
            if (isAutoCheck)
                setSkippedTagName(tagName);
            break;
        default:
            break;
        }
    });
    msgBox->show();
}

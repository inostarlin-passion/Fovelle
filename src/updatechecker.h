#ifndef UPDATECHECKER_H
#define UPDATECHECKER_H

#include "qvnamespace.h"

#include <QtNetwork>

class UpdateChecker : public QObject
{
    Q_OBJECT
public:
    explicit UpdateChecker(QObject *parent = nullptr);

    struct CheckResult
    {
        bool wasSuccessful;
        QString errorMessage;
        QString tagName;
        QString releaseName;
        QString changelog;

        bool isConsideredUpdate() const { return isVersionConsideredUpdate(tagName); }
    };

    void check(bool isManualCheck = false);

    static bool shouldCheckAutomatically(const QDateTime &now,
                                         const QDateTime &lastCheck,
                                         Qv::UpdateCheckFrequency frequency);

    void openDialog(QWidget *parent, bool isAutoCheck);

    bool getIsChecking() const { return isChecking; }

    bool getHasChecked() const { return hasChecked; }

    bool getLastCheckWasManual() const { return lastCheckWasManual; }

    CheckResult getCheckResult() const { return checkResult; }

signals:
    void checkedUpdates();

protected:
    void readReply(QNetworkReply *reply);

    void onError(QString msg);

    static QDateTime getLastCheckTime();

    static void setLastCheckTime(QDateTime value);

    static QString getSkippedTagName();

    static void setSkippedTagName(QString value);

    static double parseVersion(QString str);

    static bool isVersionConsideredUpdate(QString tagName);

private:
    const QString API_BASE_URL = "https://api.github.com/repos/inostarlin-passion/Fovelle/releases";
    const QString DOWNLOAD_URL = "https://github.com/inostarlin-passion/Fovelle/releases";
    bool isChecking {false};
    bool hasChecked {false};
    bool lastCheckWasManual {false};
    CheckResult checkResult;

    QNetworkAccessManager netAccessManager;
};

#endif // UPDATECHECKER_H

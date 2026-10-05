#ifndef UPDATECHECKER_H
#define UPDATECHECKER_H

#include "qvnamespace.h"
#include <QObject>
#include <memory>

// Owns Sparkle's standard UI and installer for the lifetime of the app.
class UpdateChecker : public QObject
{
    Q_OBJECT
public:
    explicit UpdateChecker(QObject *parent = nullptr);
    ~UpdateChecker() override;
    void initialize(Qv::UpdateCheckFrequency frequency);
    void setFrequency(Qv::UpdateCheckFrequency frequency);
    void check(bool isManualCheck = false);
    bool getIsChecking() const;
    QString configurationError() const;
    static bool isConfigurationValid(const QString &feed, const QString &publicKey);
    static int checkIntervalSeconds(Qv::UpdateCheckFrequency frequency);
signals:
    void stateChanged();
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
#endif

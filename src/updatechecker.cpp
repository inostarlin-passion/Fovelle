#include "updatechecker.h"
#include <QUrl>

int UpdateChecker::checkIntervalSeconds(Qv::UpdateCheckFrequency frequency)
{
    switch (frequency) {
    case Qv::UpdateCheckFrequency::Never: return 0;
    case Qv::UpdateCheckFrequency::Daily: return 24 * 60 * 60;
    case Qv::UpdateCheckFrequency::Weekly: return 7 * 24 * 60 * 60;
    case Qv::UpdateCheckFrequency::Monthly: return 30 * 24 * 60 * 60;
    }
    return 0;
}

bool UpdateChecker::isConfigurationValid(const QString &feed, const QString &publicKey)
{
    const QUrl url(feed, QUrl::StrictMode);
    const QByteArray encoded = publicKey.toLatin1();
    const QByteArray decoded = QByteArray::fromBase64(encoded);
    return url.isValid() && url.scheme() == QStringLiteral("https") && !url.host().isEmpty()
        && url.userInfo().isEmpty() && decoded.size() == 32 && decoded.toBase64() == encoded;
}

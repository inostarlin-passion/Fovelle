#ifndef URLDOWNLOAD_H
#define URLDOWNLOAD_H

#include <QObject>
#include <QFile>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QTemporaryDir>
#include <QTimer>
#include <memory>

class QNetworkReply;

// One owner-thread transaction. The artifact keeps the original container alive
// until its last consumer releases it (including asynchronous image readers).
class UrlDownload final : public QObject
{
    Q_OBJECT
public:
    struct Policy
    {
        qint64 maxBytes = 128LL * 1024 * 1024;
        int idleTimeoutMs = 15000;
        int totalTimeoutMs = 120000;
    };
    struct Artifact
    {
        std::shared_ptr<QTemporaryDir> directory;
        QString path;
    };
    explicit UrlDownload(QObject *parent = nullptr);
    explicit UrlDownload(Policy policy, QObject *parent = nullptr);
    ~UrlDownload() override;
    void start(const QUrl &url);
    void cancel();
    qint64 receivedBytes() const { return bytes; }
signals:
    void succeeded(const UrlDownload::Artifact &artifact);
    void failed(const QString &message);
    void canceled();
    void progress(qint64 received, qint64 total);

private:
    enum class State { Idle, Active, Succeeded, Failed, Canceled };
    void consume();
    void finish(State terminal, const QString &message = { });
    Policy policy;
    State state = State::Idle;
    QNetworkAccessManager manager;
    QPointer<QNetworkReply> reply;
    QTimer idleTimer;
    QTimer totalTimer;
    Artifact artifact;
    QFile output;
    qint64 bytes = 0;
};
Q_DECLARE_METATYPE(UrlDownload::Artifact)
#endif

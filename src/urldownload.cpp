#include "urldownload.h"
#include <QNetworkReply>
#include <QDir>
#include <QRegularExpression>
#include <QThread>
#include <QUrl>

namespace {
constexpr qint64 ChunkBytes = 64 * 1024;
}

UrlDownload::UrlDownload(QObject *parent) : UrlDownload(Policy{ }, parent) { }
UrlDownload::UrlDownload(Policy value, QObject *parent)
    : QObject(parent), policy(value), manager(this)
{
    qRegisterMetaType<Artifact>();
    idleTimer.setParent(this);
    totalTimer.setParent(this);
    output.setParent(this);
    idleTimer.setSingleShot(true);
    totalTimer.setSingleShot(true);
    connect(&idleTimer, &QTimer::timeout, this,
            [this] { finish(State::Failed, tr("Download idle timeout")); });
    connect(&totalTimer, &QTimer::timeout, this,
            [this] { finish(State::Failed, tr("Download total timeout")); });
}

UrlDownload::~UrlDownload()
{
    // Destruction emits no business signal and cannot reenter a dying window.
    if (reply) {
        reply->disconnect(this);
        reply->abort();
    }
}

void UrlDownload::start(const QUrl &url)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (state != State::Idle)
        return;
    state = State::Active;
    if (!url.isValid() || (url.scheme() != "http" && url.scheme() != "https")
        || policy.maxBytes <= 0 || policy.idleTimeoutMs <= 0 || policy.totalTimeoutMs <= 0) {
        finish(State::Failed, tr("Invalid download URL or policy"));
        return;
    }
    artifact.directory =
            std::make_shared<QTemporaryDir>(QDir::tempPath() + "/FovelleDownload-XXXXXX");
    QString suffix = url.path().section('.', -1).toLower();
    if (!QRegularExpression("^[a-z0-9]{1,10}$").match(suffix).hasMatch())
        suffix = "image";
    artifact.path = artifact.directory->filePath("download." + suffix);
    output.setFileName(artifact.path);
    if (!artifact.directory->isValid() || !output.open(QIODevice::WriteOnly)) {
        finish(State::Failed, tr("Cannot create download file"));
        return;
    }
    QNetworkRequest request(url);
    request.setTransferTimeout(policy.idleTimeoutMs);
    reply = manager.get(request);
    reply->setReadBufferSize(ChunkBytes);
    connect(reply, &QNetworkReply::metaDataChanged, this, [this] {
        if (state != State::Active || !reply)
            return;
        const auto length = reply->header(QNetworkRequest::ContentLengthHeader);
        if (length.isValid() && length.toLongLong() > policy.maxBytes)
            finish(State::Failed, tr("Download exceeds byte limit"));
    });
    connect(reply, &QNetworkReply::readyRead, this, &UrlDownload::consume);
    connect(reply, &QNetworkReply::finished, this, [this] {
        if (state != State::Active || !reply)
            return;
        if (reply->error() != QNetworkReply::NoError) {
            finish(State::Failed, reply->errorString());
            return;
        }
        consume();
        if (state == State::Active) {
            if (!output.flush())
                finish(State::Failed, output.errorString());
            else
                finish(State::Succeeded);
        }
    });
    idleTimer.start(policy.idleTimeoutMs);
    totalTimer.start(policy.totalTimeoutMs);
}

void UrlDownload::consume()
{
    Q_ASSERT(QThread::currentThread() == thread());
    while (state == State::Active && reply && reply->bytesAvailable() > 0) {
        // Read at most one extra byte to detect an unknown-length overflow;
        // never write that extra byte to disk.
        const auto chunk = reply->read(qMin(ChunkBytes - 1, policy.maxBytes - bytes) + 1);
        if (chunk.isEmpty())
            break;
        if (chunk.size() > policy.maxBytes - bytes) {
            finish(State::Failed, tr("Download exceeds byte limit"));
            return;
        }
        if (output.write(chunk) != chunk.size()) {
            finish(State::Failed, output.errorString());
            return;
        }
        bytes += chunk.size();
        idleTimer.start(policy.idleTimeoutMs);
        const auto length = reply->header(QNetworkRequest::ContentLengthHeader);
        emit progress(bytes, length.isValid() ? length.toLongLong() : -1);
    }
}

void UrlDownload::cancel()
{
    finish(State::Canceled);
}

void UrlDownload::finish(State terminal, const QString &message)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (state != State::Active)
        return;
    state = terminal; // Before abort(): finished can be emitted synchronously.
    idleTimer.stop();
    totalTimer.stop();
    output.close();
    if (reply) {
        auto *finishedReply = reply.data();
        reply.clear();
        finishedReply->disconnect(this);
        if (!finishedReply->isFinished())
            finishedReply->abort();
        finishedReply->deleteLater();
    }
    if (terminal == State::Succeeded) {
        const Artifact delivered = artifact;
        artifact = { };
        emit succeeded(delivered);
    } else {
        artifact = { };
        if (terminal == State::Canceled)
            emit canceled();
        else
            emit failed(message);
    }
}

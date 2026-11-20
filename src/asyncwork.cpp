#include "asyncwork.h"
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QSettings>
#include <QThreadPool>
#include <QRunnable>
#include <QElapsedTimer>
#include <cstdlib>
#include <csignal>

namespace {
QThreadPool &pool()
{
    static QThreadPool instance;
    static const bool configured = [] {
        instance.setMaxThreadCount(4);
        return true;
    }();
    Q_UNUSED(configured);
    return instance;
}
// Native background decoders cannot always be interrupted. Keep their execution
// slots separate from foreground image requests, with a fixed global bound.
QThreadPool &foregroundPool()
{
    static QThreadPool instance;
    static const bool configured = [] {
        instance.setMaxThreadCount(2);
        return true;
    }();
    Q_UNUSED(configured);
    return instance;
}
QThreadPool &thumbnailPool(QvAsync::Lane lane)
{
    struct Pool : QThreadPool
    {
        Pool() { setMaxThreadCount(2); }
    };
    static Pool decode;
    static Pool metadata;
    return lane == QvAsync::Lane::ThumbnailDecode ? decode : metadata;
}

QThreadPool &threadPool(QvAsync::Lane lane)
{
    switch (lane) {
    case QvAsync::Lane::Foreground:
        return foregroundPool();
    case QvAsync::Lane::ThumbnailDecode:
    case QvAsync::Lane::ThumbnailMetadata:
        return thumbnailPool(lane);
    case QvAsync::Lane::Shared:
        return pool();
    }
    Q_UNREACHABLE();
}

bool waitForPool(QThreadPool &instance, int timeoutMs)
{
    return instance.waitForDone(timeoutMs);
}
std::atomic<bool> stopping{ false };
thread_local QvAsync::Ticket currentTicket;
QMutex childrenMutex;
QSet<qint64> children;
} // namespace
void QvAsync::cancel(const Ticket &ticket)
{
    if (ticket)
        ticket->canceled.store(true);
}
void QvAsync::schedule(std::function<void()> work, int priority, Lane lane)
{
    threadPool(lane).start(std::move(work), priority);
}
bool QvAsync::trySchedule(std::function<void()> work, Lane lane)
{
    if (stopping.load())
        return false;
    auto *runnable = QRunnable::create(std::move(work));
    if (threadPool(lane).tryStart(runnable))
        return true;
    delete runnable;
    return false;
}
void QvAsync::setCurrentTicket(const Ticket &ticket)
{
    currentTicket = ticket;
}
bool QvAsync::isCanceled()
{
    return stopping.load() || (currentTicket && currentTicket->canceled.load());
}
void QvAsync::registerChild(qint64 pid)
{
    QMutexLocker lock(&childrenMutex);
    children.insert(pid);
}
void QvAsync::unregisterChild(qint64 pid)
{
    QMutexLocker lock(&childrenMutex);
    children.remove(pid);
}
bool QvAsync::drain(int timeoutMs)
{
    stopping.store(true);
    pool().clear();
    foregroundPool().clear();
    auto &thumbnailDecode = thumbnailPool(Lane::ThumbnailDecode);
    auto &thumbnailMetadata = thumbnailPool(Lane::ThumbnailMetadata);
    thumbnailDecode.clear();
    thumbnailMetadata.clear();
    QElapsedTimer elapsed;
    elapsed.start();
    const auto remaining = [&] {
        return timeoutMs < 0 ? -1 : qMax(0, timeoutMs - static_cast<int>(elapsed.elapsed()));
    };
    const bool sharedComplete = waitForPool(pool(), remaining());
    const bool foregroundComplete = waitForPool(foregroundPool(), remaining());
    const bool thumbnailDecodeComplete = waitForPool(thumbnailDecode, remaining());
    const bool thumbnailMetadataComplete = waitForPool(thumbnailMetadata, remaining());
    const bool complete = sharedComplete && foregroundComplete && thumbnailDecodeComplete
            && thumbnailMetadataComplete;
    if (complete)
        stopping.store(false);
    return complete;
}
void QvAsync::finishOrExit(int exitCode, int timeoutMs)
{
    if (drain(timeoutMs))
        return;
    // No Qt destruction while an uninterruptible native decoder is using Qt.
    // The only writes here are temporary downloads and saved preferences;
    // windows cancel/delete downloads before this boundary.
    QSettings().sync();
    {
        QMutexLocker lock(&childrenMutex);
        for (qint64 pid : std::as_const(children))
            ::kill(static_cast<pid_t>(pid), SIGKILL);
    }
    std::_Exit(exitCode);
}

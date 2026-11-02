#include "asyncwork.h"
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QSettings>
#include <QThreadPool>
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
    auto &target = lane == Lane::Foreground ? foregroundPool() : pool();
    target.start(std::move(work), priority);
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
    QElapsedTimer elapsed;
    elapsed.start();
    const auto remaining = [&] {
        return timeoutMs < 0 ? -1 : qMax(0, timeoutMs - static_cast<int>(elapsed.elapsed()));
    };
    const bool sharedComplete = waitForPool(pool(), remaining());
    const bool foregroundComplete = waitForPool(foregroundPool(), remaining());
    const bool complete = sharedComplete && foregroundComplete;
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

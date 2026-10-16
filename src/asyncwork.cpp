#include "asyncwork.h"
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QSettings>
#include <QThreadPool>
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
void QvAsync::schedule(std::function<void()> work, int priority)
{
    pool().start(std::move(work), priority);
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
    const bool complete = pool().waitForDone(timeoutMs);
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

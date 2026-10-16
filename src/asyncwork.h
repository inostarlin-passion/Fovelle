#ifndef ASYNCWORK_H
#define ASYNCWORK_H
#include <QCoreApplication>
#include <QMetaObject>
#include <QScopeGuard>
#include <atomic>
#include <functional>
#include <memory>

// Only read-only, owner-independent work belongs here. Cancellation invalidates
// delivery; native synchronous decoders may run until the process drain boundary.
namespace QvAsync {
struct State
{
    std::atomic<bool> canceled{ false };
};
using Ticket = std::shared_ptr<State>;
void cancel(const Ticket &ticket);
void setCurrentTicket(const Ticket &ticket);
void schedule(std::function<void()> work, int priority = 0);
bool drain(int timeoutMs);
void finishOrExit(int exitCode = 75, int timeoutMs = 1500);
bool isCanceled();
void registerChild(qint64 pid);
void unregisterChild(qint64 pid);

template <class Work, class Completion>
Ticket run(Work work, Completion completion, int priority = 0)
{
    auto ticket = std::make_shared<State>();
    auto *application = QCoreApplication::instance();
    schedule(
            [ticket, application, work = std::move(work),
             completion = std::move(completion)]() mutable {
                if (ticket->canceled.load())
                    return;
                setCurrentTicket(ticket);
                const auto clearTicket = qScopeGuard([] { setCurrentTicket({ }); });
                auto result = work();
                if (ticket->canceled.load())
                    return;
                // Application destruction drains this pool before destroying Qt state.
                QMetaObject::invokeMethod(
                        application,
                        [ticket, completion = std::move(completion),
                         result = std::move(result)]() mutable {
                            if (!ticket->canceled.load())
                                completion(std::move(result));
                        },
                        Qt::QueuedConnection);
            },
            priority);
    return ticket;
}
} // namespace QvAsync
#endif

#include "ResidentGateway.h"
#include <QMetaObject>
#include <QUuid>
#include <QSet>
#include <QPointer>
#include <deque>
#include <mutex>
#include <utility>

struct ResidentGateway::Bridge {
    std::mutex mutex;
    ResidentGateway *owner = nullptr;
    std::deque<std::pair<QString,QJsonObject>> events;
    QSet<QString> overflow;
    qsizetype bytes = 0;
    bool scheduled = false;
};

ResidentGateway::ResidentGateway(QObject *parent) : QObject(parent), m_bridge(std::make_shared<Bridge>())
{ m_bridge->owner = this; }

ResidentGateway::~ResidentGateway()
{
    { std::lock_guard lock(m_bridge->mutex); m_bridge->owner = nullptr; }
    for (const auto &entry : std::as_const(m_pending)) if (entry.cancel) entry.cancel();
}

void ResidentGateway::invalidate(const QString &reason)
{
    QPointer<ResidentGateway> guard(this);
    m_executor.reset();
    const auto tickets = m_pending.keys();
    for (const auto &ticket : tickets) {
        auto it = m_pending.find(ticket);
        if (it == m_pending.end() || it->invalidated) continue;
        it->invalidated = true;
        const auto cancel = it->cancel;
        if (cancel) cancel();
        if (!guard) return;
        emit event(ticket, {{"kind", "finished"}, {"error", reason}});
        if (!guard) return;
    }
}

void ResidentGateway::setLoading(const QString &model)
{
    QPointer<ResidentGateway> guard(this);
    invalidate(QStringLiteral("The resident model changed."));
    if (!guard) return;
    m_status = {{"state", "loading"}, {"model", model}};
    emit changed(m_status);
}
void ResidentGateway::setReady(const QString &model, Executor executor)
{
    if (!executor) { setError(model, QStringLiteral("The inference executor is unavailable.")); return; }
    QPointer<ResidentGateway> guard(this);
    if (m_executor) invalidate(QStringLiteral("The resident model changed."));
    if (!guard) return;
    m_executor = std::make_shared<Executor>(std::move(executor));
    m_status = {{"state", "ready"}, {"model", model}};
    emit changed(m_status);
}
void ResidentGateway::setError(const QString &model, const QString &error)
{
    QPointer<ResidentGateway> guard(this);
    invalidate(error);
    if (!guard) return;
    m_status = {{"state", "error"}, {"model", model}, {"error", error}};
    emit changed(m_status);
}
void ResidentGateway::clear(const QString &reason)
{
    QPointer<ResidentGateway> guard(this);
    invalidate(reason);
    if (!guard) return;
    m_status = {{"state", "unavailable"}, {"error", reason}};
    emit changed(m_status);
}

QString ResidentGateway::start(const QJsonArray &messages, const QJsonObject &options, QString *error)
{
    const auto fail = [error](const QString &message) { if (error) *error = message; return QString(); };
    if (!m_executor) return fail(QStringLiteral("The resident model is not ready."));
    if (m_pending.size() >= 32) return fail(QStringLiteral("Too many outstanding model requests."));
    if (messages.isEmpty() || messages.size() > 128) return fail(QStringLiteral("A bounded message list is required."));
    qsizetype length = 0;
    for (const auto &item : messages) {
        if (!item.isObject()) return fail(QStringLiteral("Invalid message."));
        const auto message = item.toObject();
        const auto role = message.value("role").toString();
        if ((role != "system" && role != "user" && role != "assistant") || !message.value("content").isString())
            return fail(QStringLiteral("Each message requires a supported role and text content."));
        length += message.value("content").toString().size();
        if (length > 1024 * 1024) return fail(QStringLiteral("Messages exceed the input limit."));
    }
    const auto ticket = QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto executor = m_executor;
    m_pending.insert(ticket, {{}, executor, false});
    const auto bridge = m_bridge;
    try {
        auto cancel = (*executor)(messages, options, [bridge, ticket](QJsonObject value) {
            std::lock_guard lock(bridge->mutex);
            if (!bridge->owner) return;
            if (bridge->overflow.contains(ticket)) return;
            const auto bytes=(value.value("text").toString().size()+value.value("error").toString().size())*2+256;
            if (bridge->events.size()>=1024 || bytes>1024*1024-bridge->bytes) bridge->overflow.insert(ticket);
            else { bridge->bytes+=bytes; bridge->events.emplace_back(ticket,std::move(value)); }
            if (bridge->scheduled) return;
            bridge->scheduled=true;
            QMetaObject::invokeMethod(bridge->owner, [bridge] {
                ResidentGateway *owner;
                { std::lock_guard guard(bridge->mutex); owner = bridge->owner; }
                if (owner) owner->drain();
            }, Qt::QueuedConnection);
        });
        m_pending[ticket].cancel = std::move(cancel);
    } catch (const std::exception &failure) {
        m_pending.remove(ticket);
        return fail(QString::fromUtf8(failure.what()));
    }
    return ticket;
}

void ResidentGateway::drain()
{
    std::deque<std::pair<QString,QJsonObject>> events;
    QSet<QString> overflow;
    {
        std::lock_guard lock(m_bridge->mutex);
        events.swap(m_bridge->events); overflow.swap(m_bridge->overflow);
        m_bridge->bytes=0; m_bridge->scheduled=false;
    }
    QPointer<ResidentGateway> guard(this);
    for (const auto &[ticket,value]:events) { deliver(ticket,value); if (!guard) return; }
    for (const auto &ticket:overflow) {
        cancel(ticket);
        if (!guard) return;
        deliver(ticket,{{"kind","finished"},{"error","The client could not keep up with the model stream."}});
        if (!guard) return;
    }
}
bool ResidentGateway::cancel(const QString &ticket)
{
    const auto it = m_pending.find(ticket);
    if (it == m_pending.end() || it->invalidated) return false;
    if (it->cancel) it->cancel();
    return true;
}
void ResidentGateway::deliver(const QString &ticket, const QJsonObject &value)
{
    auto it = m_pending.find(ticket);
    if (it == m_pending.end()) return;
    const bool invalidated = it->invalidated;
    const bool finished = value.value("kind").toString() == "finished";
    // Keep the executor/service alive until its terminal event is delivered.
    auto hold = it->executor;
    if (finished) m_pending.erase(it);
    if (!invalidated) emit event(ticket, value);
}

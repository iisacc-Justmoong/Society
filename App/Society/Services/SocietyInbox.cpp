#include "SocietyInbox.h"
#include <DeliveryStore.h>
#include <QLoggingCategory>
#include <QPointer>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>

Q_LOGGING_CATEGORY(inboxLog, "iisacc.society.inbox")

namespace {
class DeliveryInboxStore final : public SocietyInbox::Store {
public:
    bool open(const QString &directory, QString *error) override { return store_.open(directory, error); }
    void close() override { store_.close(); }
    qint64 acknowledged(const QString &consumer) override { return store_.acknowledged(consumer); }
    QVariantList readAfter(qint64 sequence, int limit) override { return store_.readAfter(sequence, limit); }
    QVariantMap daemonSnapshot() override { return store_.daemonSnapshot(); }
    bool acknowledge(const QString &consumer, qint64 sequence) override { return store_.acknowledge(consumer, sequence); }
    QString errorString() const override { return store_.errorString(); }
private:
    iiSocietyHelper::DeliveryStore store_;
};
}

// A DeliveryStore owns a thread-affine SQL connection. Its complete lifetime,
// including open/close and explicit commands, belongs to this one worker.
class SocietyInbox::Worker {
public:
    struct State {
        std::mutex mutex;
        std::condition_variable condition;
        std::deque<std::function<void(Store &, const std::shared_ptr<State> &)>> tasks;
        SocietyInbox *receiver = nullptr;
        bool stopping = false;
    };
    Worker(SocietyInbox *receiver, StoreFactory factory) : m_state(std::make_shared<State>()) {
        m_state->receiver = receiver;
        // The thread owns the SQL connection until its current operation ends.
        // It is independent of the application and global thread-pool teardown.
        std::thread([state = m_state, factory = std::move(factory)] {
        auto store = factory();
        for (;;) {
            std::function<void(Store &, const std::shared_ptr<State> &)> task;
            {
                std::unique_lock lock(state->mutex);
                state->condition.wait(lock, [&] { return state->stopping || !state->tasks.empty(); });
                if (state->stopping) break;
                task = std::move(state->tasks.front());
                state->tasks.pop_front();
            }
            task(*store, state);
        }
        }).detach();
    }
    ~Worker() {
        {
            std::lock_guard lock(m_state->mutex);
            m_state->receiver = nullptr;
            m_state->stopping = true;
            m_state->tasks.clear();
        }
        m_state->condition.notify_one();
    }
    void post(std::function<void(Store &, const std::shared_ptr<State> &)> task) {
        { std::lock_guard lock(m_state->mutex); m_state->tasks.push_back(std::move(task)); }
        m_state->condition.notify_one();
    }
    static void deliver(const std::shared_ptr<State> &state, std::function<void(SocietyInbox &)> completion) {
        // Synchronize posting with destruction. Qt removes queued calls for a
        // destroyed receiver, and no worker retains the receiver afterwards.
        std::lock_guard lock(state->mutex);
        if (auto *receiver = state->receiver)
            QMetaObject::invokeMethod(receiver, [receiver, completion = std::move(completion)] {
                completion(*receiver);
            }, Qt::QueuedConnection);
    }
    template<class Function> auto call(Function function) {
        using Result = std::invoke_result_t<Function, Store &>;
        auto task = std::make_shared<std::packaged_task<Result(Store &)>>(std::move(function));
        auto result = task->get_future();
        post([task](auto &store, const auto &) { (*task)(store); });
        return result.get();
    }
private:
    std::shared_ptr<State> m_state;
};

SocietyInbox::SocietyInbox(QObject *parent)
    : SocietyInbox([] { return std::make_unique<DeliveryInboxStore>(); }, parent) {}
SocietyInbox::SocietyInbox(StoreFactory factory, QObject *parent)
    : QObject(parent), m_worker(std::make_unique<Worker>(this, std::move(factory)))
{
    m_timer.setInterval(250);
    connect(&m_timer, &QTimer::timeout, this, &SocietyInbox::refresh);
}
SocietyInbox::~SocietyInbox() { stop(); }
void SocietyInbox::setError(const QString &message)
{
    if (m_error == message) return;
    m_error = message;
    if (!message.isEmpty()) qCWarning(inboxLog).noquote() << message;
    emit statusChanged();
}
bool SocietyInbox::start(const QString &directory)
{
    if (directory.isEmpty()) return false;
    const QPointer<SocietyInbox> guard(this);
    stop();
    if (!guard) return false;
    m_starting = true;
    setError({});
    if (!guard) return false;
    emit statusChanged();
    if (!guard || !m_starting) return false;
    const auto epoch = m_epoch;
    m_worker->post([directory, epoch](auto &store, const auto &state) {
        QString error;
        qint64 cursor = -1;
        if (store.open(directory, &error)) {
            cursor = store.acknowledged("com.iisacc.society");
            if (cursor < 0) { error = store.errorString(); store.close(); }
        }
        Worker::deliver(state, [epoch, cursor, error](SocietyInbox &inbox) {
            if (epoch != inbox.m_epoch || !inbox.m_starting) return;
            const QPointer<SocietyInbox> guard(&inbox);
            inbox.m_starting = false;
            if (cursor < 0) {
                inbox.setError(error);
                if (guard) emit guard->statusChanged();
                return;
            }
            inbox.m_cursor = cursor;
            inbox.m_connected = true;
            inbox.m_messages.clear();
            inbox.m_snapshot.clear();
            inbox.setError({});
            if (!guard || epoch != guard->m_epoch) return;
            emit inbox.statusChanged();
            if (!guard || epoch != guard->m_epoch) return;
            emit inbox.messagesChanged();
            if (!guard || epoch != guard->m_epoch) return;
            inbox.m_timer.start();
            inbox.refresh();
        });
    });
    return true;
}
void SocietyInbox::stop()
{
    m_timer.stop();
    ++m_epoch;
    m_polling = false;
    const bool hadStore = m_starting || m_connected;
    m_starting = false;
    m_connected = false;
    const bool hadMessages = !m_messages.isEmpty() || m_cursor != 0;
    const bool hadSnapshot = !m_snapshot.isEmpty();
    m_messages.clear();
    m_snapshot.clear();
    m_cursor = 0;
    if (hadStore) m_worker->post([](auto &store, const auto &) { store.close(); });
    const QPointer<SocietyInbox> guard(this);
    emit statusChanged();
    if (!guard) return;
    if (hadMessages) emit messagesChanged();
    if (guard && hadSnapshot) emit snapshotChanged();
}
void SocietyInbox::refresh()
{
    if (!m_connected || m_polling) return;
    m_polling = true;
    const auto epoch = m_epoch;
    const auto cursor = m_cursor;
    m_worker->post([epoch, cursor](auto &store, const auto &state) {
        const auto messages = store.readAfter(cursor);
        auto error = store.errorString();
        const auto snapshot = error.isEmpty() ? store.daemonSnapshot() : QVariantMap{};
        if (error.isEmpty()) error = store.errorString();
        Worker::deliver(state, [epoch, messages, snapshot, error](SocietyInbox &inbox) {
            if (epoch != inbox.m_epoch || !inbox.m_connected || !inbox.m_timer.isActive()) return;
            const QPointer<SocietyInbox> guard(&inbox);
            inbox.m_polling = false;
            inbox.setError(error);
            if (!guard) return;
            if (!error.isEmpty() || epoch != inbox.m_epoch) return;
            for (const auto &entry : messages) {
                if (epoch != inbox.m_epoch || !inbox.m_timer.isActive()) return;
                const auto message = entry.toMap();
                inbox.m_cursor = message.value("sequence").toLongLong();
                inbox.m_messages.append(message);
                if (inbox.m_messages.size() > 100) inbox.m_messages.removeFirst();
                qCInfo(inboxLog).noquote() << "Society inbox received" << message.value("id").toString()
                    << "from" << message.value("sender").toMap().value("applicationId").toString()
                    << message.value("topic").toString();
                emit inbox.dataReceived(message);
                if (!guard) return;
            }
            if (epoch != inbox.m_epoch) return;
            if (!messages.isEmpty()) emit inbox.messagesChanged();
            if (!guard) return;
            if (epoch != inbox.m_epoch) return;
            if (snapshot != inbox.m_snapshot) { inbox.m_snapshot = snapshot; emit inbox.snapshotChanged(); }
        });
    });
}
bool SocietyInbox::acknowledge(qint64 sequence)
{
    if (!m_connected) { setError(QStringLiteral("Society inbox is not connected.")); return false; }
    if (sequence < 0 || sequence > m_cursor) { setError(QStringLiteral("Cannot acknowledge unseen Society inbox data.")); return false; }
    const auto result = m_worker->call([sequence](auto &store) {
        const bool ok = store.acknowledge("com.iisacc.society", sequence);
        return std::pair{ok, store.errorString()};
    });
    setError(result.second);
    return result.first;
}
QVariantList SocietyInbox::readAfter(qint64 sequence, int limit)
{
    if (!m_connected) { setError(QStringLiteral("Society inbox is not connected.")); return {}; }
    const auto result = m_worker->call([sequence, limit](auto &store) {
        const auto rows = store.readAfter(sequence, limit);
        return std::pair{rows, store.errorString()};
    });
    setError(result.second);
    return result.first;
}

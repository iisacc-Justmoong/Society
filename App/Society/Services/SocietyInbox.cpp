#include "SocietyInbox.h"
#include <DeliveryStore.h>
#include <QLoggingCategory>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>

Q_LOGGING_CATEGORY(inboxLog, "iisacc.society.inbox")

// A DeliveryStore owns a thread-affine SQL connection. Its complete lifetime,
// including open/close and explicit commands, belongs to this one worker.
class SocietyInbox::Worker {
public:
    Worker() : m_thread([this] {
        iiSocietyHelper::DeliveryStore store;
        for (;;) {
            std::function<void(iiSocietyHelper::DeliveryStore &)> task;
            {
                std::unique_lock lock(m_mutex);
                m_condition.wait(lock, [this] { return m_stopping || !m_tasks.empty(); });
                if (m_tasks.empty() && m_stopping) break;
                task = std::move(m_tasks.front());
                m_tasks.pop_front();
            }
            task(store);
        }
    }) {}
    ~Worker() {
        { std::lock_guard lock(m_mutex); m_stopping = true; }
        m_condition.notify_one();
        m_thread.join();
    }
    void post(std::function<void(iiSocietyHelper::DeliveryStore &)> task) {
        { std::lock_guard lock(m_mutex); m_tasks.push_back(std::move(task)); }
        m_condition.notify_one();
    }
    template<class Function> auto call(Function function) {
        using Result = std::invoke_result_t<Function, iiSocietyHelper::DeliveryStore &>;
        auto task = std::make_shared<std::packaged_task<Result(iiSocietyHelper::DeliveryStore &)>>(std::move(function));
        auto result = task->get_future();
        post([task](auto &store) { (*task)(store); });
        return result.get();
    }
private:
    std::mutex m_mutex;
    std::condition_variable m_condition;
    std::deque<std::function<void(iiSocietyHelper::DeliveryStore &)>> m_tasks;
    bool m_stopping = false;
    std::thread m_thread;
};

SocietyInbox::SocietyInbox(QObject *parent) : QObject(parent), m_worker(std::make_unique<Worker>())
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
    stop();
    const auto opened = m_worker->call([directory](auto &store) {
        QString error;
        if (!store.open(directory, &error)) return std::pair<qint64, QString>{-1, error};
        const auto cursor = store.acknowledged("com.iisacc.society");
        if (cursor < 0) { error = store.errorString(); store.close(); }
        return std::pair<qint64, QString>{cursor, error};
    });
    if (opened.first < 0) { setError(opened.second); return false; }
    m_cursor = opened.first;
    m_connected = true;
    m_messages.clear();
    m_snapshot.clear();
    setError({});
    emit statusChanged();
    emit messagesChanged();
    m_timer.start();
    refresh();
    return true;
}
void SocietyInbox::stop()
{
    m_timer.stop();
    ++m_epoch;
    m_polling = false;
    m_connected = false;
    m_worker->call([](auto &store) { store.close(); });
    emit statusChanged();
}
void SocietyInbox::refresh()
{
    if (!m_connected || m_polling) return;
    m_polling = true;
    const auto epoch = m_epoch;
    const auto cursor = m_cursor;
    m_worker->post([this, epoch, cursor](auto &store) {
        const auto messages = store.readAfter(cursor);
        auto error = store.errorString();
        const auto snapshot = error.isEmpty() ? store.daemonSnapshot() : QVariantMap{};
        if (error.isEmpty()) error = store.errorString();
        QMetaObject::invokeMethod(this, [this, epoch, messages, snapshot, error] {
            if (epoch != m_epoch || !m_connected || !m_timer.isActive()) return;
            m_polling = false;
            setError(error);
            if (!error.isEmpty() || epoch != m_epoch) return;
            for (const auto &entry : messages) {
                if (epoch != m_epoch || !m_timer.isActive()) return;
                const auto message = entry.toMap();
                m_cursor = message.value("sequence").toLongLong();
                m_messages.append(message);
                if (m_messages.size() > 100) m_messages.removeFirst();
                qCInfo(inboxLog).noquote() << "Society inbox received" << message.value("id").toString()
                    << "from" << message.value("sender").toMap().value("applicationId").toString()
                    << message.value("topic").toString();
                emit dataReceived(message);
            }
            if (epoch != m_epoch) return;
            if (!messages.isEmpty()) emit messagesChanged();
            if (epoch != m_epoch) return;
            if (snapshot != m_snapshot) { m_snapshot = snapshot; emit snapshotChanged(); }
        }, Qt::QueuedConnection);
    });
}
bool SocietyInbox::acknowledge(qint64 sequence)
{
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
    const auto result = m_worker->call([sequence, limit](auto &store) {
        const auto rows = store.readAfter(sequence, limit);
        return std::pair{rows, store.errorString()};
    });
    setError(result.second);
    return result.first;
}

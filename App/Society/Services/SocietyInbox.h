#pragma once

#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <memory>
#include <functional>

class SocietyInbox final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY statusChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY statusChanged)
    Q_PROPERTY(QVariantList messages READ messages NOTIFY messagesChanged)
    Q_PROPERTY(QVariantMap lastDaemonSnapshot READ lastDaemonSnapshot NOTIFY snapshotChanged)
    Q_PROPERTY(qint64 lastSequence READ lastSequence NOTIFY messagesChanged)
public:
    // The delivery backend lives entirely on the inbox worker thread. The
    // factory is injectable so slow and unavailable storage can be tested.
    class Store {
    public:
        virtual ~Store() = default;
        virtual bool open(const QString &directory, QString *error) = 0;
        virtual void close() = 0;
        virtual qint64 acknowledged(const QString &consumer) = 0;
        virtual QVariantList readAfter(qint64 sequence, int limit = 128) = 0;
        virtual QVariantMap daemonSnapshot() = 0;
        virtual bool acknowledge(const QString &consumer, qint64 sequence) = 0;
        virtual QString errorString() const = 0;
    };
    using StoreFactory = std::function<std::unique_ptr<Store>()>;
    explicit SocietyInbox(QObject *parent = nullptr);
    explicit SocietyInbox(StoreFactory factory, QObject *parent = nullptr);
    ~SocietyInbox() override;
    // Returns whether opening was queued; observe statusChanged/connected for
    // completion. Requests during an earlier open are serialized on the worker.
    bool start(const QString &directory);
    Q_INVOKABLE void stop();
    Q_INVOKABLE bool acknowledge(qint64 sequence);
    Q_INVOKABLE QVariantList readAfter(qint64 sequence, int limit = 128);
    bool connected() const { return m_connected && m_error.isEmpty(); }
    bool starting() const { return m_starting; }
    QString errorString() const { return m_error; }
    QVariantList messages() const { return m_messages; }
    QVariantMap lastDaemonSnapshot() const { return m_snapshot; }
    qint64 lastSequence() const { return m_cursor; }
signals:
    void dataReceived(const QVariantMap &message);
    void messagesChanged();
    void snapshotChanged();
    void statusChanged();
private:
    void refresh();
    void setError(const QString &message);
    class Worker;
    std::unique_ptr<Worker> m_worker;
    bool m_connected = false;
    bool m_starting = false;
    bool m_polling = false;
    quint64 m_epoch = 0;
    QTimer m_timer;
    QString m_error;
    QVariantList m_messages;
    QVariantMap m_snapshot;
    qint64 m_cursor = 0;
};

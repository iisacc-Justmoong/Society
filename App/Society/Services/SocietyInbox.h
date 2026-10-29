#pragma once

#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <memory>

class SocietyInbox final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY statusChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY statusChanged)
    Q_PROPERTY(QVariantList messages READ messages NOTIFY messagesChanged)
    Q_PROPERTY(QVariantMap lastDaemonSnapshot READ lastDaemonSnapshot NOTIFY snapshotChanged)
    Q_PROPERTY(qint64 lastSequence READ lastSequence NOTIFY messagesChanged)
public:
    explicit SocietyInbox(QObject *parent = nullptr);
    ~SocietyInbox() override;
    bool start(const QString &directory);
    Q_INVOKABLE void stop();
    Q_INVOKABLE bool acknowledge(qint64 sequence);
    Q_INVOKABLE QVariantList readAfter(qint64 sequence, int limit = 128);
    bool connected() const { return m_connected && m_error.isEmpty(); }
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
    bool m_polling = false;
    quint64 m_epoch = 0;
    QTimer m_timer;
    QString m_error;
    QVariantList m_messages;
    QVariantMap m_snapshot;
    qint64 m_cursor = 0;
};

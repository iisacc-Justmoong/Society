#pragma once

#include <QJsonObject>
#include <QLocalSocket>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <functional>

// Thin asynchronous transport. A disconnected window never owns or cancels a
// daemon job, and uncertain requests are not automatically replayed.
class ModelBackendClient final : public QObject {
    Q_OBJECT
public:
    explicit ModelBackendClient(QObject *parent = nullptr, int requestTimeoutMs = 15000,
                                std::function<void()> startService = {});
    ~ModelBackendClient() override;
    void start();
    void stop();
    void setContainerPath(const QString &path);
    bool connected() const;
    bool pendingMutation() const { return !m_mutations.isEmpty(); }
    bool request(const QString &operation, QJsonObject arguments = {}, bool mutation = false);
signals:
    void connectedChanged();
    void pendingChanged();
    void stateReceived(const QJsonObject &state);
    void systemDriveStateReceived(const QJsonObject &state);
    void eventReceived(const QJsonObject &event);
    void requestError(const QString &error);
private:
    void connectIfNeeded();
    void readMessages();
    void handle(const QJsonObject &message);
    QLocalSocket m_socket;
    QTimer m_retry;
    QString m_container;
    QByteArray m_input;
    QSet<QString> m_pending;
    QSet<QString> m_mutations;
    bool m_enabled = false;
    int m_requestTimeoutMs;
    qint64 m_lastActivationMs = 0;
    std::function<void()> m_startService;
};

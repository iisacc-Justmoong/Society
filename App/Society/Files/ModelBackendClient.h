#pragma once

#include <QJsonObject>
#include <QHash>
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
    bool request(const QString &operation, QJsonObject arguments = {}, bool mutation = false, QString *requestId = nullptr);
signals:
    void connectedChanged();
    void pendingChanged();
    void stateReceived(const QJsonObject &state);
    void systemDriveStateReceived(const QJsonObject &state);
    void eventReceived(const QJsonObject &event);
    void agentStateReceived(const QJsonObject &state);
    void agentResultReceived(const QString &operation, bool accepted, const QString &error);
    void suggestedStateReceived(const QJsonObject &state);
    void suggestedResultReceived(const QString &operation, bool accepted, const QString &error);
    void recentStateReceived(const QJsonObject &state);
    void recentResultReceived(const QString &operation, bool accepted, const QString &error);
    void directoryResultReceived(const QString &operation, const QString &id, const QJsonObject &result);
    void directoryStateReceived(const QJsonObject &state);
    void directoryActivated(const QJsonObject &event);
    void fileResultReceived(const QString &operation, const QString &id, const QJsonObject &result);
    void resourceResultReceived(const QString &id, const QJsonObject &result);
    void workspaceResultReceived(const QString &operation, const QString &id, const QJsonObject &result);
    void fileJobChanged(const QJsonObject &event);
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
    QHash<QString, QString> m_pendingOperations;
    QHash<QString, QString> m_pendingAgentContainers;
    QHash<QString, quint64> m_pendingAgentGenerations;
    QString m_agentSubscription, m_suggestedSubscription, m_recentSubscription;
    quint64 m_selectionGeneration = 0;
    QSet<QString> m_mutations;
    bool m_enabled = false;
    int m_requestTimeoutMs;
    qint64 m_lastActivationMs = 0;
    std::function<void()> m_startService;
};

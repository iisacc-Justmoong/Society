#pragma once

#include <iiSocietyContainer>
#include <QHash>
#include <QJsonObject>
#include <QLocalServer>
#include <QQueue>
#include <QSet>
#include <QPointer>
#include <QLocalSocket>
#include <functional>
#include <memory>

class QLockFile;
class RecentBackend;
#ifdef SOCIETY_AGENT_BACKEND
class WorkspaceAgentBackend;
class SuggestedBackend;
class ResidentGateway;
#endif

// Local, same-user interface to the SDK-owned model operations. The service
// and its workers outlive every individual window or socket connection.
class ModelBackendServer final : public QObject {
    Q_OBJECT
public:
    using UiPeerVerifier = std::function<bool(QLocalSocket *)>;
    explicit ModelBackendServer(QObject *parent = nullptr, UiPeerVerifier uiPeerVerifier = {});
    ~ModelBackendServer() override;
    bool listen(const QString &directory);
    void selectContainer(const QString &path);
    void setSystemDriveState(const QJsonObject &state);
#ifdef SOCIETY_AGENT_BACKEND
    ResidentGateway *residentGateway() const;
#endif
signals:
    void systemDriveConnectRequested();
private:
    void reconcileContainer();
    void accept();
    void receive(QLocalSocket *socket, const QJsonObject &request);
    void send(QLocalSocket *socket, const QJsonObject &message);
    void broadcast(const QJsonObject &message);
    void broadcastState();
    QJsonObject state() const;
    QLocalServer m_server;
    iiSocietyContainer::ModelImportService m_models;
    iiSocietyContainer::FileJobService m_fileJobs;
    std::unique_ptr<RecentBackend> m_recent;
    QHash<QLocalSocket *, QString> m_recentClients;
    QSet<QLocalSocket *> m_clients;
    QString m_selected, m_selectedId;
    struct FileQuery { QPointer<QLocalSocket> socket; QString requestId, container; };
    QHash<QString, FileQuery> m_fileQueries;
    QHash<QString, QString> m_fileJobContainers;
    quint64 m_resourceGeneration = 0;
    int m_resourceQueries = 0;
    QJsonObject m_systemDrive;
    UiPeerVerifier m_uiPeerVerifier;
    QPointer<QLocalSocket> m_uiSocket;
    struct UiCall { QPointer<QLocalSocket> caller; QString requestId; };
    QHash<QString,UiCall> m_uiCalls;
    int m_lock = -1;
#ifdef Q_OS_WIN
    std::unique_ptr<QLockFile> m_windowsLock;
#endif
    QHash<QString, QPair<QByteArray, QJsonObject>> m_replies;
    QHash<QString, QByteArray> m_addPending;
    QQueue<QString> m_replyOrder;
#ifdef SOCIETY_AGENT_BACKEND
    std::unique_ptr<WorkspaceAgentBackend> m_agent;
    std::unique_ptr<SuggestedBackend> m_suggested;
    QHash<QLocalSocket *, QString> m_agentClients;
    QHash<QLocalSocket *, QString> m_suggestedClients;
    QHash<QLocalSocket *, QHash<QString, QPair<QByteArray, QJsonObject>>> m_agentReplies;
    struct ResidentRequest { QPointer<QLocalSocket> socket; QString id; };
    QHash<QString, ResidentRequest> m_residentOwners;
    QHash<QLocalSocket *, QHash<QString, QString>> m_residentRequests;
    QHash<QLocalSocket *, QString> m_residentClients;
#endif
};

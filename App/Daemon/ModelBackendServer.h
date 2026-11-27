#pragma once

#include "Society/Content/ContentIndex.h"
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
class ContentIndexScheduler;
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
    using ContentEmbedderFactory = std::function<society::content::EmbedderAccess()>;
    explicit ModelBackendServer(QObject *parent = nullptr, UiPeerVerifier uiPeerVerifier = {},
                                ContentEmbedderFactory contentEmbedderFactory = {},
                                int contentPollIntervalMs = 0);
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
    [[nodiscard]] society::content::EmbedderAccess contentEmbedder() const;
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
    // Bound temporary disk use by admitting at most one byte comparison.
    int m_resourceComparisons = 0;
    // Keep local content inference bounded and off the daemon event loop.
    int m_contentQueries = 0;
    int m_contentPolicyQueries = 0;
    QJsonObject m_systemDrive;
    UiPeerVerifier m_uiPeerVerifier;
    ContentEmbedderFactory m_contentEmbedderFactory;
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
    /// A connection subscribes to exactly one Space session at a time.
    struct SpaceSubscription { QString session, token; };
    QHash<QLocalSocket *, SpaceSubscription> m_spaceClients;
    QHash<QLocalSocket *, QString> m_suggestedClients;
    QHash<QLocalSocket *, QHash<QString, QPair<QByteArray, QJsonObject>>> m_agentReplies;
    struct ResidentRequest { QPointer<QLocalSocket> socket; QString id; };
    QHash<QString, ResidentRequest> m_residentOwners;
    QHash<QLocalSocket *, QHash<QString, QString>> m_residentRequests;
    QHash<QLocalSocket *, QString> m_residentClients;
#endif
    std::unique_ptr<ContentIndexScheduler> m_contentScheduler;
};

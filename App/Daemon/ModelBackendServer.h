#pragma once

#include <ModelImportService.h>
#include <FileJobService.h>
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
    QSet<QLocalSocket *> m_clients;
    QString m_selected, m_selectedId;
    struct FileQuery { QPointer<QLocalSocket> socket; QString requestId, container; };
    QHash<QString, FileQuery> m_fileQueries;
    QHash<QString, QString> m_fileJobContainers;
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
    QQueue<QString> m_replyOrder;
};

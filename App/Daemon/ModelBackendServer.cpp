#include "ModelBackendServer.h"
#include "NativeDirectorySession.h"
#include "RecentBackend.h"
#include "Society/Resources/ResourceInspection.h"
#include "Shared/LocalBackendAddress.h"
#include "Shared/LocalBackendPeer.h"
#ifdef SOCIETY_AGENT_BACKEND
#include "WorkspaceAgentBackend.h"
#include "SuggestedBackend.h"
#endif
#include <SocietyDrive.h>
#include <QDir>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QLockFile>
#include <QTimer>
#include <QUuid>
#include <QtEndian>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <utility>
#include <cerrno>
#if defined(Q_OS_UNIX)
#include <fcntl.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {
constexpr qsizetype maxFrame = 1024 * 1024;
bool sameUser(QLocalSocket *socket)
{
#if defined(Q_OS_DARWIN) || defined(Q_OS_FREEBSD)
    uid_t uid{}; gid_t gid{};
    return ::getpeereid(int(socket->socketDescriptor()), &uid, &gid) == 0 && uid == ::geteuid();
#elif defined(Q_OS_LINUX)
    struct ucred peer{}; socklen_t size = sizeof(peer);
    return ::getsockopt(int(socket->socketDescriptor()), SOL_SOCKET, SO_PEERCRED, &peer, &size) == 0 && peer.uid == ::geteuid();
#elif defined(Q_OS_WIN)
    return societyBackendSameWindowsUser(socket, true);
#else
    Q_UNUSED(socket); return false;
#endif
}
}

ModelBackendServer::ModelBackendServer(QObject *parent, UiPeerVerifier uiPeerVerifier)
    : QObject(parent), m_uiPeerVerifier(std::move(uiPeerVerifier))
{
    if (!m_uiPeerVerifier) m_uiPeerVerifier=[](QLocalSocket *socket) {
        return societyBackendPeerMatchesExecutable(socket,QDir(QCoreApplication::applicationDirPath()).filePath("Society"));
    };
    m_recent=std::make_unique<RecentBackend>();
    connect(m_recent.get(),&RecentBackend::changed,this,[this](const QJsonObject &recent) {
        for (auto it=m_recentClients.cbegin();it!=m_recentClients.cend();++it)
            send(it.key(),{{"type","recentState"},{"container",m_selected},{"subscription",it.value()},{"recent",recent}});
    });
#ifdef SOCIETY_AGENT_BACKEND
    m_agent = std::make_unique<WorkspaceAgentBackend>();
    m_suggested = std::make_unique<SuggestedBackend>(m_agent.get());
    connect(m_agent.get(), &WorkspaceAgentBackend::changed, this, [this](const QJsonObject &agent) {
        for (auto it=m_agentClients.cbegin();it!=m_agentClients.cend();++it)
            send(it.key(), {{"type","agentState"},{"container",m_selected},{"subscription",it.value()},{"agent",agent}});
    });
    connect(m_suggested.get(), &SuggestedBackend::changed, this, [this](const QJsonObject &suggested) {
        for (auto it=m_suggestedClients.cbegin();it!=m_suggestedClients.cend();++it)
            send(it.key(), {{"type","suggestedState"},{"container",m_selected},{"subscription",it.value()},{"suggested",suggested}});
    });
#endif
    connect(&m_server, &QLocalServer::newConnection, this, &ModelBackendServer::accept);
    connect(&m_models, &iiSocietyContainer::ModelImportService::stateChanged, this, &ModelBackendServer::broadcastState);
    connect(&m_models, &iiSocietyContainer::ModelImportService::jobsChanged, this, &ModelBackendServer::broadcastState);
    connect(&m_models, &iiSocietyContainer::ModelImportService::finished, this, [this](const QString &root, const QStringList &paths) {
        broadcast({{"type", "finished"}, {"container", root}, {"paths", QJsonArray::fromStringList(paths)}});
        reconcileContainer();
    });
    connect(&m_models, &iiSocietyContainer::ModelImportService::organized, this, [this](const QString &root, const QStringList &paths) {
        broadcast({{"type", "organized"}, {"container", root}, {"paths", QJsonArray::fromStringList(paths)}});
        reconcileContainer();
    });
    connect(&m_models, &iiSocietyContainer::ModelImportService::recategorized, this, [this](const QString &root, const QString &oldPath, const QString &path) {
        broadcast({{"type", "recategorized"}, {"container", root}, {"previousPath", oldPath}, {"path", path}});
        reconcileContainer();
    });
    connect(&m_fileJobs,&iiSocietyContainer::FileJobService::recorded,this,[this](const QString &id) {
        broadcast({{"type","fileJobChanged"},{"jobId",id},{"container",m_fileJobContainers.value(id)},{"state","recorded"}});
    });
    connect(&m_fileJobs,&iiSocietyContainer::FileJobService::finished,this,[this](const QString &id,const QString &path,const QString &error) {
        const auto container=m_fileJobContainers.take(id);
        broadcast({{"type","fileJobChanged"},{"jobId",id},{"container",container},
                   {"state","finished"},{"path",path},{"error",error},{"success",error.isEmpty()}});
        if (!container.isEmpty()) {
            if (error.isEmpty() && !path.isEmpty()) m_recent->record(container,QDir(container).absoluteFilePath(path));
            // A partially completed job can still move or remove a file.
            if (societySameBackendPath(container,m_selected)) m_recent->refresh();
        }
#ifdef SOCIETY_AGENT_BACKEND
        if (error.isEmpty() && m_suggested) m_suggested->refresh();
#endif
    });
    connect(&m_fileJobs,&iiSocietyContainer::FileJobService::inspected,this,[this](const QString &ticket,const QVariantMap &job,const QString &error) {
        const auto pending=m_fileQueries.take(ticket);
        if (!pending.socket) return;
        QJsonObject reply{{"type","result"},{"id",pending.requestId},{"accepted",false}};
        if (!societySameBackendPath(pending.container,m_selected)) reply.insert("error","The selected Society drive changed.");
        else if (!error.isEmpty()) reply.insert("error",error);
        else if (m_selectedId.isEmpty() || job.value("containerId").toString()!=m_selectedId)
            reply.insert("error","This job belongs to a different Society drive.");
        else { reply.insert("accepted",true); reply.insert("job",QJsonObject::fromVariantMap(job)); }
        send(pending.socket,reply);
    });
}

ModelBackendServer::~ModelBackendServer()
{
    disconnect(&m_models, nullptr, this, nullptr);
    const auto clients = m_clients;
    for (auto *socket : clients) {
        disconnect(socket, nullptr, this, nullptr);
        socket->abort();
    }
    m_server.close();
#if defined(Q_OS_UNIX)
    if (m_lock >= 0) ::close(m_lock);
#endif
}

bool ModelBackendServer::listen(const QString &directory)
{
#if defined(Q_OS_UNIX)
    if (m_server.isListening()) return true;
    if (!QDir::isAbsolutePath(directory)) return false;
    struct stat info{};
    const auto root = QFile::encodeName(directory);
    if (::lstat(root.constData(), &info) || !S_ISDIR(info.st_mode) || info.st_uid != ::geteuid() || (info.st_mode & 0077)) return false;
    const auto lockName = QFile::encodeName(directory + "/backend.lock");
    const int fd = ::open(lockName.constData(), O_RDWR | O_CREAT | O_NOFOLLOW, 0600);
    if (fd < 0) return false;
    if (::fstat(fd, &info) || !S_ISREG(info.st_mode) || info.st_uid != ::geteuid() || (info.st_mode & 0077)
        || ::flock(fd, LOCK_EX | LOCK_NB)) { ::close(fd); return false; }
    const auto path = directory + "/backend.sock";
    const auto encoded = QFile::encodeName(path);
    if (::lstat(encoded.constData(), &info) == 0) {
        if (!S_ISSOCK(info.st_mode) || info.st_uid != ::geteuid() || (info.st_mode & 0077)) { ::close(fd); return false; }
        QLocalSocket probe; probe.connectToServer(path);
        if (probe.waitForConnected(100) || ::unlink(encoded.constData())) { ::close(fd); return false; }
    } else if (errno != ENOENT) { ::close(fd); return false; }
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server.listen(path) || ::lstat(encoded.constData(), &info) || !S_ISSOCK(info.st_mode)
        || info.st_uid != ::geteuid() || ::chmod(encoded.constData(), 0600)) {
        m_server.close(); ::close(fd); return false;
    }
    m_lock = fd;
#ifdef SOCIETY_AGENT_BACKEND
    if (m_agent) m_agent->setBrokerDirectory(directory);
#endif
    return true;
#elif defined(Q_OS_WIN)
    if (m_server.isListening()) return true;
    if (!QDir::isAbsolutePath(directory) || QFileInfo(directory).isSymLink()
        || !QDir().mkpath(directory) || !QFileInfo(directory).isDir()) return false;
    const auto lockPath = QDir(directory).filePath("backend.lock");
    if (QFileInfo(lockPath).isSymLink()) return false;
    auto lock = std::make_unique<QLockFile>(lockPath);
    lock->setStaleLockTime(0);
    if (!lock->tryLock()) return false;
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server.listen(societyBackendAddress(directory))) return false;
    m_windowsLock = std::move(lock);
#ifdef SOCIETY_AGENT_BACKEND
    if (m_agent) m_agent->setBrokerDirectory(directory);
#endif
    return true;
#else
    Q_UNUSED(directory); return false;
#endif
}

void ModelBackendServer::selectContainer(const QString &path)
{
    const auto selected = path.isEmpty() ? QString() : QDir::cleanPath(path);
    const bool changed = m_selected != selected;
    m_selected = selected;
    if (changed) {
        ++m_resourceGeneration;
        m_selectedId.clear();
        if (!selected.isEmpty()) if (const auto drive=iiSocietyContainer::SocietyDrive::open(selected))
            m_selectedId=drive->storageKind()==iiSocietyContainer::SocietyDrive::StorageKind::LogicalDisk
                ? drive->storageIdentifier() : drive->identifier();
    }
    if (changed) for (auto *client : m_clients)
        if (auto *directory = client->findChild<NativeDirectorySession *>()) directory->invalidate();
#ifdef SOCIETY_AGENT_BACKEND
    if (changed && m_agent) m_agent->selectContainer(m_selected);
    if (changed && m_suggested) m_suggested->selectContainer(m_selected);
#endif
    if (changed) m_recent->selectContainer(m_selected);
    reconcileContainer();
    if (changed) broadcastState();
}

void ModelBackendServer::setSystemDriveState(const QJsonObject &state)
{
    if (m_systemDrive == state) return;
    m_systemDrive = state;
    broadcastState();
}

void ModelBackendServer::reconcileContainer()
{
    if (m_models.containerPath() != m_selected && !m_models.busy() && !m_models.organizing())
        m_models.setContainerPath(m_selected);
}

QJsonObject ModelBackendServer::state() const
{
    return {{"containerPath", m_models.containerPath()}, {"selectedContainer", m_selected},
        {"busy", m_models.busy()}, {"organizing", m_models.organizing()}, {"progress", m_models.progress()},
        {"status", m_models.status()}, {"errorString", m_models.errorString()},
        {"currentJobId", m_models.currentJobId()}, {"selectedJobId", m_models.selectedJobId()},
        {"jobs", QJsonArray::fromVariantList(m_models.jobs())}, {"jobItems", QJsonArray::fromVariantList(m_models.jobItems())},
        {"hasMoreJobItems", m_models.hasMoreJobItems()}, {"jobsError", m_models.jobsError()},
        {"systemDrive", m_systemDrive}};
}

void ModelBackendServer::send(QLocalSocket *socket, const QJsonObject &message)
{
    if (!socket || socket->state() != QLocalSocket::ConnectedState) return;
    const auto body = QJsonDocument(message).toJson(QJsonDocument::Compact);
    if (body.isEmpty() || body.size() > maxFrame || socket->bytesToWrite() + body.size() > 2 * maxFrame) { socket->abort(); return; }
    QByteArray frame(4, '\0'); qToBigEndian(quint32(body.size()), frame.data());
    if (socket->write(frame + body) != frame.size() + body.size()) socket->abort();
}
void ModelBackendServer::broadcast(const QJsonObject &message)
{
    const auto clients = m_clients;
    for (auto *socket : clients) send(socket, message);
}
void ModelBackendServer::broadcastState() { broadcast({{"type", "state"}, {"state", state()}}); }

void ModelBackendServer::accept()
{
    while (auto *socket = m_server.nextPendingConnection()) {
        if (m_clients.size() >= 16 || !sameUser(socket)) { socket->abort(); socket->deleteLater(); continue; }
        m_clients.insert(socket);
        auto *directory = new NativeDirectorySession(socket);
        connect(directory, &NativeDirectorySession::changed, socket, [this, socket](const QJsonObject &state) {
            send(socket, {{"type", "directoryChanged"}, {"directory", state}});
        });
        connect(directory, &NativeDirectorySession::openResult, socket, [this, socket](QJsonObject result) {
            if (result.value("error").toString().isEmpty() && !result.value("path").toString().isEmpty())
                m_recent->record(m_selected,result.value("path").toString());
            result.insert("type","directoryActivated"); send(socket,result);
        });
        socket->setReadBufferSize(maxFrame + 4);
        auto *deadline = new QTimer(socket); deadline->setSingleShot(true); deadline->setInterval(10000);
        connect(deadline, &QTimer::timeout, socket, &QLocalSocket::abort);
        connect(socket, &QLocalSocket::disconnected, this, [this, socket] {
            m_recentClients.remove(socket);
#ifdef SOCIETY_AGENT_BACKEND
            m_suggestedClients.remove(socket);
#endif
            if (m_uiSocket == socket) {
                m_uiSocket.clear();
                const auto pending=std::exchange(m_uiCalls,{});
                for (auto it=pending.cbegin();it!=pending.cend();++it)
                    send(it->caller,{{"type","result"},{"id",it->requestId},{"accepted",false},{"error","The Society window disconnected."}});
            } else {
                QStringList cancelled;
                for (auto it=m_uiCalls.begin();it!=m_uiCalls.end();) {
                    if (it->caller==socket) { cancelled.append(it.key()); it=m_uiCalls.erase(it); }
                    else ++it;
                }
                for (const auto &id:cancelled) send(m_uiSocket,{{"type","uiCancel"},{"id",id}});
            }
            m_clients.remove(socket); socket->deleteLater();
        });
        connect(socket, &QLocalSocket::readyRead, this, [this, socket, deadline] {
            auto input = socket->property("input").toByteArray() + socket->readAll();
            if (input.size() > maxFrame + 4) { socket->abort(); return; }
            while (input.size() >= 4) {
                const auto size = qFromBigEndian<quint32>(input.constData());
                if (!size || size > maxFrame) { socket->abort(); return; }
                if (input.size() < qsizetype(size + 4)) break;
                QJsonParseError error;
                const auto parsed = QJsonDocument::fromJson(input.mid(4, size), &error);
                input.remove(0, size + 4);
                if (error.error != QJsonParseError::NoError || !parsed.isObject()) { socket->abort(); return; }
                receive(socket, parsed.object());
                if (socket->state() != QLocalSocket::ConnectedState) return;
            }
            socket->setProperty("input", input);
            if (input.isEmpty()) deadline->stop();
            else if (!deadline->isActive()) deadline->start();
        });
        send(socket, {{"type", "state"}, {"state", state()}});
    }
}

void ModelBackendServer::receive(QLocalSocket *socket, const QJsonObject &request)
{
    const auto id = request.value("id").toString();
    const auto operation = request.value("op").toString();
    if (id.isEmpty() || id.size() > 128 || operation.isEmpty() || operation.size() > 64) { socket->abort(); return; }
    if (operation=="resourceInspect") {
        QJsonObject reply{{"type","result"},{"id",id},{"accepted",false},{"operation",operation}};
        const auto directory=QDir(QCoreApplication::applicationDirPath());
        if (!m_uiPeerVerifier(socket) && societyBackendPeerPid(socket)!=QCoreApplication::applicationPid()
            && !societyBackendPeerMatchesExecutable(socket,directory.filePath("society-cli"))
            && !societyBackendPeerMatchesExecutable(socket,directory.filePath("bin/society-cli"))) {
            reply.insert("error","The resource client could not be verified."); send(socket,reply); return;
        }
        if (m_selected.isEmpty() || !societySameBackendPath(QDir::cleanPath(request.value("container").toString()),m_selected)) {
            reply.insert("error","The selected Society drive is unavailable or has changed."); send(socket,reply); return;
        }
        const auto path=request.value("path");
        if (!path.isString() || path.toString().isEmpty() || path.toString().size()>4096 || m_resourceQueries>=16) {
            reply.insert("error","Invalid or excessive resource inspection requests."); send(socket,reply); return;
        }
        const auto root=m_selected, logical=path.toString(); const auto generation=m_resourceGeneration;
        QPointer<QLocalSocket> client(socket); ++m_resourceQueries;
        auto *watcher=new QFutureWatcher<society::resources::Inspection>(this);
        connect(watcher,&QFutureWatcher<society::resources::Inspection>::finished,this,[this,watcher,client,id,generation] {
            const auto result=watcher->result(); watcher->deleteLater(); --m_resourceQueries;
            if (!client) return;
            QJsonObject response{{"type","result"},{"id",id},{"accepted",false},{"operation","resourceInspect"}};
            if (generation!=m_resourceGeneration) response.insert("error","The selected Society drive changed.");
            else if (!result.error.isEmpty()) response.insert("error",result.error);
            else { response.insert("accepted",true); response.insert("resource",result.snapshot); }
            send(client,response);
        });
        watcher->setFuture(QtConcurrent::run([root,logical] {
            try { return society::resources::inspect(root,logical); }
            catch (const std::exception &error) { return society::resources::Inspection{{},QString::fromUtf8(error.what())}; }
            catch (...) { return society::resources::Inspection{{},QStringLiteral("Could not inspect the resource.")}; }
        }));
        return;
    }
    if (operation=="recentSubscribe" || operation=="recentRefresh" || operation=="recentOpened") {
        QJsonObject reply{{"type","result"},{"id",id},{"accepted",false},{"operation",operation}};
        const auto directory=QDir(QCoreApplication::applicationDirPath());
        if (!m_uiPeerVerifier(socket) && societyBackendPeerPid(socket)!=QCoreApplication::applicationPid()
            && !societyBackendPeerMatchesExecutable(socket,directory.filePath("society-cli"))
            && !societyBackendPeerMatchesExecutable(socket,directory.filePath("bin/society-cli"))) {
            reply.insert("error","The Recent client could not be verified."); send(socket,reply); return;
        }
        if (m_selected.isEmpty() || !societySameBackendPath(QDir::cleanPath(request.value("container").toString()),m_selected)) {
            reply.insert("error","The selected Society drive is unavailable or has changed."); send(socket,reply); return;
        }
        if (operation=="recentSubscribe") {
            const auto subscription=request.value("subscription").toString(id);
            if (subscription.isEmpty() || subscription.size()>128) { reply.insert("error","Invalid Recent subscription."); send(socket,reply); return; }
            m_recentClients.insert(socket,subscription); m_recent->activate();
        } else if (operation=="recentRefresh") m_recent->refresh();
        else {
            const auto path=request.value("path").toString();
            if (path.isEmpty() || path.size()>4096 || !QDir::isAbsolutePath(path) || QDir::cleanPath(path)!=path) {
                reply.insert("error","Invalid Recent file path."); send(socket,reply); return;
            }
            m_recent->record(m_selected,path);
        }
        reply.insert("accepted",true); reply.insert("recent",m_recent->snapshot()); send(socket,reply); return;
    }
#ifdef SOCIETY_AGENT_BACKEND
    if (operation=="suggestedSubscribe" || operation=="suggestedRefresh") {
        QJsonObject reply{{"type","result"},{"id",id},{"accepted",false},{"operation",operation}};
        const auto directory=QDir(QCoreApplication::applicationDirPath());
        if (!m_uiPeerVerifier(socket) && societyBackendPeerPid(socket)!=QCoreApplication::applicationPid()
            && !societyBackendPeerMatchesExecutable(socket,directory.filePath("society-cli"))
            && !societyBackendPeerMatchesExecutable(socket,directory.filePath("bin/society-cli"))) {
            reply.insert("error","The Suggested client could not be verified."); send(socket,reply); return;
        }
        if (m_selected.isEmpty() || !societySameBackendPath(QDir::cleanPath(request.value("container").toString()),m_selected)) {
            reply.insert("error","The selected Society drive is unavailable or has changed."); send(socket,reply); return;
        }
        if (operation=="suggestedSubscribe") {
            const auto subscription=request.value("subscription").toString(id);
            if (subscription.isEmpty() || subscription.size()>128) { reply.insert("error","Invalid Suggested subscription."); send(socket,reply); return; }
            m_suggestedClients.insert(socket,subscription);
            m_suggested->activate();
        } else m_suggested->refresh();
        reply.insert("accepted",true); reply.insert("suggested",m_suggested->snapshot()); send(socket,reply); return;
    }
    if (operation.startsWith("agent")) {
        QJsonObject reply{{"type","result"},{"id",id},{"accepted",false},{"operation",operation}};
        const auto directory=QDir(QCoreApplication::applicationDirPath());
        const auto cli=directory.filePath(QStringLiteral("society-cli"));
        const auto buildCli=directory.filePath(QStringLiteral("bin/society-cli"));
        if (!m_uiPeerVerifier(socket) && societyBackendPeerPid(socket) != QCoreApplication::applicationPid()
            && !societyBackendPeerMatchesExecutable(socket,cli) && !societyBackendPeerMatchesExecutable(socket,buildCli)) {
            reply.insert("error","The agent client could not be verified."); send(socket,reply); return;
        }
        if (!societySameBackendPath(QDir::cleanPath(request.value("container").toString()),m_selected) || m_selected.isEmpty()) {
            reply.insert("error","The selected Society drive is unavailable or has changed."); send(socket,reply); return;
        }
        const auto bytes=QJsonDocument(request).toJson(QJsonDocument::Compact);
        if (!m_agentReplies.contains(socket))
            connect(socket,&QObject::destroyed,this,[this,socket] { m_agentClients.remove(socket); m_agentReplies.remove(socket); });
        auto &cache=m_agentReplies[socket];
        if (cache.contains(id)) {
            const auto previous=cache.value(id);
            if (previous.first!=bytes) { socket->abort(); return; }
            send(socket,previous.second); return;
        }
        QString error; bool accepted=false;
        if (operation=="agentSubscribe") {
            const auto subscription=request.value("subscription").toString(id);
            if (subscription.isEmpty() || subscription.size()>128) error="Invalid agent subscription.";
            else { m_agentClients.insert(socket,subscription); accepted=true; }
        } else if (operation=="agentSelect") accepted=m_agent->selectModel(request.value("model").toString(),&error);
        else if (operation=="agentSend") accepted=m_agent->submit(request.value("prompt").toString(),&error);
        else if (operation=="agentNew") accepted=m_agent->newConversation(&error);
        else if (operation=="agentCancel") accepted=m_agent->cancel(&error);
        else error="Unknown agent operation.";
        reply.insert("accepted",accepted);
        if (!error.isEmpty()) reply.insert("error",error);
        reply.insert("agent",m_agent->snapshot());
        if (cache.size()>=32) cache.clear();
        cache.insert(id,{bytes,reply}); send(socket,reply); return;
    }
#endif
    // UI traffic stays on authenticated per-peer channels and never enters the
    // shared reply cache or broadcasts. Only this daemon may initiate a call.
    if (operation == "uiRegister") {
        QJsonObject reply{{"type","result"},{"id",id},{"accepted",false}};
        if ((!m_uiSocket || m_uiSocket==socket) && m_uiPeerVerifier(socket)) {m_uiSocket=socket;reply.insert("accepted",true);}
        else reply.insert("error","The Society window connection could not be verified.");
        send(socket,reply); return;
    }
    if (operation=="uiInvoke") {
        const auto name=request.value("tool").toString();
        static const QSet<QString> allowed{"status","open_section","navigate","refresh","AskUserQuestion",
            "agent_status","agent_open_section","agent_navigate","agent_refresh"};
        if (societyBackendPeerPid(socket)!=QCoreApplication::applicationPid() || !m_uiSocket
            || m_uiSocket->state()!=QLocalSocket::ConnectedState || !allowed.contains(name)
            || !request.value("arguments").isObject() || m_uiCalls.size()>=64) {
            send(socket,{{"type","result"},{"id",id},{"accepted",false},{"error","A verified Society window is not connected."}}); return;
        }
        if (name.startsWith("agent_") && !societySameBackendPath(request.value("arguments").toObject().value("container").toString(),m_selected)) {
            send(socket,{{"type","result"},{"id",id},{"accepted",false},{"error","The selected Society drive has changed."}}); return;
        }
        const auto call=QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_uiCalls.insert(call,{socket,id});
        send(m_uiSocket,{{"type","uiInvoke"},{"id",call},{"tool",name},{"arguments",request.value("arguments")}}); return;
    }
    if (operation=="uiResult") {
        const auto it=m_uiCalls.find(id);
        if (socket!=m_uiSocket || it==m_uiCalls.end() || !request.value("result").isObject()) return;
        const auto call=it.value(); m_uiCalls.erase(it);
        send(call.caller,{{"type","result"},{"id",call.requestId},{"accepted",true},{"result",request.value("result")}}); return;
    }
    const auto bytes = QJsonDocument(request).toJson(QJsonDocument::Compact);
    const bool directoryOperation = operation == "directoryOpen" || operation == "directoryState" || operation == "directoryPage" || operation == "directoryActivate";
    auto *directory = directoryOperation ? socket->findChild<NativeDirectorySession *>() : nullptr;
    if (directory) if (const auto previous = directory->cachedReply(id)) {
        if (previous->first != bytes) { socket->abort(); return; }
        send(socket, previous->second); return;
    }
    if (!directoryOperation && operation != "fileJobStatus" && m_replies.contains(id)) {
        const auto previous = m_replies.value(id);
        if (previous.first != bytes) { socket->abort(); return; }
        send(socket, previous.second); return;
    }
    QJsonObject reply{{"type", "result"}, {"id", id}, {"accepted", false}};
    const auto requested = QDir::cleanPath(request.value("container").toString());
    if (operation=="fileJobStatus" && societySameBackendPath(requested,m_selected)) {
        const auto jobId=request.value("jobId");
        if (!jobId.isString() || QUuid(jobId.toString()).isNull() || m_fileQueries.size()>=128) {
            reply.insert("error","Invalid or excessive file job requests."); send(socket,reply); return;
        }
        const auto ticket=QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_fileQueries.insert(ticket,{socket,id,requested}); m_fileJobs.inspect(ticket,jobId.toString()); return;
    }
    if (!societySameBackendPath(requested, m_selected)) {
        reply.insert("error", "The selected Society drive is unavailable or has changed.");
    } else if (operation == "systemDriveReconnect") {
        const auto driveId = QUuid(m_systemDrive.value("identifier").toString());
        const bool valid = m_systemDrive.value("ready").toBool()
            && societySameBackendPath(m_systemDrive.value("sourcePath").toString(), m_selected)
            && !driveId.isNull() && driveId == QUuid(request.value("identifier").toString());
        reply.insert("accepted", valid);
        if (valid) emit systemDriveConnectRequested();
        else reply.insert("error", "The selected Society system drive is unavailable or has changed.");
    } else if (directoryOperation) {
        QString error;
        bool accepted = false;
        if (!directory) error = QStringLiteral("The directory session is unavailable.");
        else if (operation == "directoryOpen") accepted = directory->open(m_selected, request, &error);
        else if (operation == "directoryActivate") accepted = directory->activate(request, &error);
        else if (operation == "directoryState") {
            accepted = directory->valid();
            if (!accepted) error = QStringLiteral("No directory is open.");
        } else {
            const auto page = directory->page(request, &error);
            accepted = error.isEmpty();
            if (accepted) for (auto it = page.constBegin(); it != page.constEnd(); ++it) reply.insert(it.key(), it.value());
        }
        reply.insert("accepted", accepted);
        if (directory && directory->valid() && operation != "directoryPage") reply.insert("directory", directory->state());
        if (!accepted) reply.insert("error", error);
    } else if (operation=="fileOperation") {
        using Kind=iiSocietyContainer::FileJobService::Kind;
        const QHash<QString,Kind> actions{{"duplicate",Kind::Duplicate},{"rename",Kind::Rename},
            {"trash",Kind::Trash},{"remove",Kind::Remove},{"restore",Kind::Restore},
            {"move",Kind::Move},{"create-directory",Kind::CreateDirectory},{"copy",Kind::Copy}};
        const auto action=request.value("action").toString(); QString error;
        QString jobId;
        if (!actions.contains(action) || !request.value("path").isString()
            || (request.contains("argument") && !request.value("argument").isString()))
            error=QStringLiteral("Invalid file operation.");
        else jobId=m_fileJobs.submit(actions.value(action),m_selected,request.value("path").toString(),request.value("argument").toString(),&error);
        reply.insert("accepted",!jobId.isEmpty());
        if (jobId.isEmpty()) reply.insert("error",error);
        else { m_fileJobContainers.insert(jobId,m_selected); reply.insert("jobId",jobId); reply.insert("state","queued"); }
    } else if (!societySameBackendPath(m_models.containerPath(), m_selected)) {
        reply.insert("error", "The selected Society drive is unavailable or has changed.");
    } else if (operation == "snapshot") {
        reply.insert("accepted", true);
    } else if (operation == "import") {
        const auto items = request.value("sources").toArray();
        QList<iiSocietyContainer::ModelImportSource> sources;
        bool valid = !items.isEmpty() && items.size() <= 512;
        for (const auto &item : items) {
            const auto path = item.toString();
            const auto suffix = QFileInfo(path).suffix();
            if (!item.isString() || path.size() > 4096 || path.contains(QChar::Null) || !QDir::isAbsolutePath(path)
                || (suffix.compare("safetensor", Qt::CaseInsensitive) && suffix.compare("safetensors", Qt::CaseInsensitive))) { valid = false; break; }
            sources.append({QFileInfo(path).fileName(), [path](const auto &consume, const auto &) {
                consume(path); return QString();
            }, path});
        }
        const bool accepted = valid && m_models.importSources(sources);
        reply.insert("accepted", accepted);
        if (accepted) reply.insert("jobId", m_models.currentJobId());
        else reply.insert("error", valid ? m_models.errorString() : QStringLiteral("Invalid model sources."));
    } else if (operation == "cancel") {
        m_models.cancel(); reply.insert("accepted", true);
    } else if (operation == "organize") {
        reply.insert("accepted", m_models.organizeModels());
    } else if (operation == "recategorize") {
        reply.insert("accepted", m_models.recategorize(request.value("path").toString(), request.value("category").toString()));
    } else if (operation == "refreshJobs") {
        m_models.refreshJobs(); reply.insert("accepted", true);
    } else if (operation == "selectJob") {
        m_models.selectJob(request.value("jobId").toString()); reply.insert("accepted", true);
    } else if (operation == "loadMoreJobItems") {
        m_models.loadMoreJobItems(); reply.insert("accepted", true);
    } else if (operation == "retryFailed") {
        reply.insert("accepted", m_models.retryFailed(request.value("jobId").toString()));
    } else {
        reply.insert("error", "Unknown backend operation.");
    }
    if (!directoryOperation && operation!="fileOperation") reply.insert("state", state());
    if (directoryOperation && directory) directory->rememberReply(id, bytes, reply);
    else if (!directoryOperation) {
        m_replies.insert(id, {bytes, reply}); m_replyOrder.enqueue(id);
        if (m_replyOrder.size() > 256) m_replies.remove(m_replyOrder.dequeue());
    }
    send(socket, reply);
}

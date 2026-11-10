#include "UiBridgeRegistration.h"
#include "Shared/LocalBackendAddress.h"
#include "Shared/LocalBackendPeer.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QFutureWatcher>
#include <QUuid>
#include <QtConcurrent/QtConcurrentRun>
#include <QtEndian>
#include <utility>

namespace { constexpr qsizetype maximumFrame = 1024 * 1024; }
UiBridgeRegistration::UiBridgeRegistration(std::shared_ptr<iiMultimodalAgent::agent::ToolRunner> runner,
                                             QObject *parent, PeerVerifier verifier)
    : QObject(parent), m_runner(std::move(runner)), m_verifyDaemon(std::move(verifier))
{
    m_questionPool.setMaxThreadCount(4);
    m_actionPool.setMaxThreadCount(2);
    if (!m_verifyDaemon) m_verifyDaemon=[](QLocalSocket *socket) {
        const auto expected=QDir(QCoreApplication::applicationDirPath()).filePath("SocietyDaemon");
        return societyBackendPeerSameUser(socket) && societyBackendPeerMatchesExecutable(socket,expected);
    };
    m_retry.setSingleShot(true);
    connect(&m_retry,&QTimer::timeout,this,&UiBridgeRegistration::connectIfNeeded);
    connect(&m_socket,&QLocalSocket::connected,this,[this] {
        if (!m_verifyDaemon(&m_socket)) {
            m_socket.abort(); scheduleRetry(); return;
        }
        m_input.clear(); m_requestId=QUuid::createUuid().toString(QUuid::WithoutBraces);
        send({{"id",m_requestId},{"op","uiRegister"}});
        const auto requestId=m_requestId;
        QTimer::singleShot(10000,this,[this,requestId] {
            if (m_enabled && !requestId.isEmpty() && m_requestId==requestId) m_socket.abort();
        });
    });
    connect(&m_socket,&QLocalSocket::readyRead,this,&UiBridgeRegistration::readMessages);
    connect(&m_socket,&QLocalSocket::disconnected,this,[this] {cancelPending();m_input.clear();m_requestId.clear();scheduleRetry();});
    connect(&m_socket,&QLocalSocket::errorOccurred,this,[this](QLocalSocket::LocalSocketError) {scheduleRetry();});
}
UiBridgeRegistration::~UiBridgeRegistration() { stop(); }
void UiBridgeRegistration::start() { if (!m_enabled) {m_enabled=true;connectIfNeeded();} }
void UiBridgeRegistration::stop() {m_enabled=false;m_retry.stop();cancelPending();m_socket.abort();m_input.clear();m_requestId.clear();}
void UiBridgeRegistration::cancelPending() {
    for (auto it=m_pending.begin();it!=m_pending.end();++it) it.value().cancel();
    m_pending.clear();
}
void UiBridgeRegistration::send(const QJsonObject &message) {
    const auto body=QJsonDocument(message).toJson(QJsonDocument::Compact);
    if (body.isEmpty() || body.size()>maximumFrame || m_socket.bytesToWrite()+body.size()>2*maximumFrame) {m_socket.abort();return;}
    QByteArray frame(4,'\0');qToBigEndian(quint32(body.size()),frame.data());frame+=body;
    if (m_socket.write(frame)!=frame.size()) m_socket.abort();
}
void UiBridgeRegistration::scheduleRetry() {if (m_enabled && !m_retry.isActive())m_retry.start(1000);}
void UiBridgeRegistration::connectIfNeeded()
{
    if (!m_enabled || m_socket.state()!=QLocalSocket::UnconnectedState) return;
    if (qEnvironmentVariableIsSet("SOCIETY_TEST_OUTPUT_DIR") && !qEnvironmentVariableIsSet("SOCIETY_LOCAL_BROKER_DIR")) return;
    const auto directory=qEnvironmentVariable("SOCIETY_LOCAL_BROKER_DIR",QDir::homePath()+"/.iisacc-society-ipc-v1");
    m_socket.connectToServer(societyBackendAddress(directory));
    const auto now=QDateTime::currentMSecsSinceEpoch();
    if (!qEnvironmentVariableIsSet("SOCIETY_DISABLE_BACKEND_AUTOSTART") && now-m_lastActivationMs>=3000) {
        m_lastActivationMs=now;
        QTimer::singleShot(250,this,[this] {
            if (!m_enabled || m_socket.state()!=QLocalSocket::UnconnectedState
                || qEnvironmentVariableIsSet("SOCIETY_DISABLE_BACKEND_AUTOSTART")) return;
            const auto executable=QDir(QCoreApplication::applicationDirPath()).filePath("SocietyDaemon");
            if (QFileInfo(executable).isExecutable()) QProcess::startDetached(executable,{"--license-broker"});
        });
    }
}
void UiBridgeRegistration::readMessages()
{
    m_input+=m_socket.readAll();
    if (m_input.size()>maximumFrame+4) {m_socket.abort();return;}
    while (m_input.size()>=4) {
        const auto size=qFromBigEndian<quint32>(m_input.constData());
        if (!size || size>maximumFrame) {m_socket.abort();return;}
        if (m_input.size()<qsizetype(size+4)) return;
        QJsonParseError error;
        const auto doc=QJsonDocument::fromJson(m_input.mid(4,size),&error); m_input.remove(0,size+4);
        if (error.error!=QJsonParseError::NoError || !doc.isObject()) {m_socket.abort();return;}
        const auto response=doc.object();
        if (response.value("type")=="result" && response.value("id").toString()==m_requestId) {
            m_requestId.clear();
            if (!response.value("accepted").toBool()) {m_socket.abort();return;}
        } else if (response.value("type")=="uiCancel") {
            const auto id=response.value("id").toString();
            if (auto it=m_pending.find(id);it!=m_pending.end()) {it.value().cancel();m_pending.erase(it);}
        } else if (response.value("type")=="uiInvoke") {
            const auto id=response.value("id").toString(), name=response.value("tool").toString();
            if (!m_runner || id.isEmpty() || id.size()>128 || name.isEmpty() || !response.value("arguments").isObject()
                || m_pending.contains(id) || m_pending.size()>=64) {m_socket.abort();return;}
            iiMultimodalAgent::CancellationToken cancel; m_pending.insert(id,cancel);
            auto *watcher=new QFutureWatcher<iiMultimodalAgent::agent::ToolResult>(this);
            connect(watcher,&QFutureWatcher<iiMultimodalAgent::agent::ToolResult>::finished,this,[this,watcher,id] {
                const auto result=watcher->result();watcher->deleteLater();
                if (!m_pending.remove(id)) return;
                send({{"id",id},{"op","uiResult"},{"result",QJsonObject{{"text",result.text},{"data",result.data},
                    {"isError",result.isError},{"content",result.content},{"metadata",result.metadata}}}});
            });
            const auto runner=m_runner;
            const auto args=response.value("arguments").toObject();
            auto *pool=name==QStringLiteral("AskUserQuestion") ? &m_questionPool : &m_actionPool;
            watcher->setFuture(QtConcurrent::run(pool,[runner,id,name,args,cancel] {
                try {
                    iiMultimodalAgent::agent::ToolContext context;context.sessionId=QStringLiteral("society-window");
                    context.runId=id;context.cancellation=cancel;context.workingDirectory=QDir::currentPath();
                    return runner->run({id,name,args},context);
                } catch (const std::exception &) {
                    return iiMultimodalAgent::agent::ToolResult{QStringLiteral("The window request was interrupted."),{},true};
                } catch (...) {
                    return iiMultimodalAgent::agent::ToolResult{QStringLiteral("The window request failed unexpectedly."),{},true};
                }
            }));
        }
    }
}

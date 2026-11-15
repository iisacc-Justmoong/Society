#include "ModelBackendClient.h"
#include "Shared/LocalBackendAddress.h"
#include "Shared/LocalBackendPeer.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QUuid>
#include <QtEndian>
#include <utility>

namespace { constexpr qsizetype maxFrame = 1024 * 1024; }

ModelBackendClient::ModelBackendClient(QObject *parent, int requestTimeoutMs, std::function<void()> startService)
    : QObject(parent), m_requestTimeoutMs(qMax(1, requestTimeoutMs)), m_startService(std::move(startService))
{
    if (!m_startService) m_startService = [] {
#ifdef Q_OS_WIN
        const auto executable = QDir(QCoreApplication::applicationDirPath()).filePath("SocietyDaemon.exe");
        const QStringList arguments{"--sync"};
#else
        const auto executable = QDir(QCoreApplication::applicationDirPath()).filePath("SocietyDaemon");
        const QStringList arguments{"--license-broker"};
#endif
        if (QFileInfo(executable).isExecutable()) QProcess::startDetached(executable, arguments);
    };
    m_retry.setInterval(1000);
    connect(&m_retry, &QTimer::timeout, this, &ModelBackendClient::connectIfNeeded);
    connect(&m_socket, &QLocalSocket::connected, this, [this] {
#ifdef Q_OS_WIN
        if (!societyBackendSameWindowsUser(&m_socket, false)) {
            m_socket.abort();
            emit requestError(tr("The Society background service identity could not be verified."));
            return;
        }
#endif
        m_input.clear(); emit connectedChanged();
        if (!m_container.isEmpty()) request(QStringLiteral("snapshot"));
    });
    connect(&m_socket, &QLocalSocket::disconnected, this, [this] {
        const bool uncertain = !m_mutations.isEmpty();
        m_pending.clear(); m_pendingOperations.clear(); m_pendingAgentContainers.clear(); m_pendingAgentGenerations.clear(); m_agentSubscription.clear(); m_suggestedSubscription.clear(); m_recentSubscription.clear(); m_mutations.clear(); m_input.clear();
        emit connectedChanged(); emit pendingChanged();
        if (uncertain && m_enabled) emit requestError(tr("Connection interrupted. Check the operation status before retrying."));
    });
    connect(&m_socket, &QLocalSocket::readyRead, this, &ModelBackendClient::readMessages);
}
ModelBackendClient::~ModelBackendClient()
{
    m_enabled = false;
    m_retry.stop();
    disconnect(&m_socket, nullptr, this, nullptr);
    m_socket.abort();
}
void ModelBackendClient::start()
{
    if (m_enabled) return;
    m_enabled = true; m_retry.start(); connectIfNeeded();
}
void ModelBackendClient::stop()
{
    m_enabled = false; m_retry.stop(); m_socket.abort();
    m_pending.clear(); m_pendingOperations.clear(); m_pendingAgentContainers.clear(); m_pendingAgentGenerations.clear(); m_agentSubscription.clear(); m_suggestedSubscription.clear(); m_recentSubscription.clear(); m_mutations.clear(); m_input.clear();
}
void ModelBackendClient::setContainerPath(const QString &path)
{
    const auto clean = path.isEmpty() ? QString() : QDir::cleanPath(path);
    if (m_container == clean) return;
    m_container = clean; ++m_selectionGeneration; m_agentSubscription.clear(); m_suggestedSubscription.clear(); m_recentSubscription.clear();
    if (connected() && !m_container.isEmpty()) request(QStringLiteral("snapshot"));
    else if (m_enabled) connectIfNeeded();
}
bool ModelBackendClient::connected() const { return m_socket.state() == QLocalSocket::ConnectedState; }

void ModelBackendClient::connectIfNeeded()
{
    if (!m_enabled || m_socket.state() != QLocalSocket::UnconnectedState) return;
    // Test processes must never attach to the user's live service merely
    // because a fixture did not provide its own isolated channel.
    if (qEnvironmentVariableIsSet("SOCIETY_TEST_OUTPUT_DIR")
        && !qEnvironmentVariableIsSet("SOCIETY_LOCAL_BROKER_DIR")) return;
    const auto directory = qEnvironmentVariable("SOCIETY_LOCAL_BROKER_DIR", QDir::homePath() + "/.iisacc-society-ipc-v1");
    m_socket.connectToServer(societyBackendAddress(directory));
    const auto now = QDateTime::currentMSecsSinceEpoch();
    if (!qEnvironmentVariableIsSet("SOCIETY_DISABLE_BACKEND_AUTOSTART") && now - m_lastActivationMs >= 3000) {
        m_lastActivationMs = now;
        // Do not spawn a second daemon while an existing local server is
        // accepting the asynchronous connection, especially on Windows.
        QTimer::singleShot(250, this, [this] {
            if (m_enabled && m_socket.state() == QLocalSocket::UnconnectedState
                && !qEnvironmentVariableIsSet("SOCIETY_DISABLE_BACKEND_AUTOSTART")) m_startService();
        });
    }
}

bool ModelBackendClient::request(const QString &operation, QJsonObject arguments, bool mutation, QString *requestId)
{
    if (requestId) requestId->clear();
    if (!connected() || m_container.isEmpty() || m_pending.size() >= 64) {
        if (mutation) emit requestError(tr("The Society background service is unavailable. Try again when it reconnects."));
        return false;
    }
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (operation=="agentSubscribe") {
        m_agentSubscription=QUuid::createUuid().toString(QUuid::WithoutBraces);
        arguments.insert("subscription",m_agentSubscription);
    }
    if (operation=="suggestedSubscribe") {
        m_suggestedSubscription=QUuid::createUuid().toString(QUuid::WithoutBraces);
        arguments.insert("subscription",m_suggestedSubscription);
    }
    if (operation=="recentSubscribe") {
        m_recentSubscription=QUuid::createUuid().toString(QUuid::WithoutBraces);
        arguments.insert("subscription",m_recentSubscription);
    }
    arguments.insert("id", id); arguments.insert("op", operation); arguments.insert("container", m_container);
    const auto body = QJsonDocument(arguments).toJson(QJsonDocument::Compact);
    if (body.isEmpty() || body.size() > maxFrame || m_socket.bytesToWrite() + body.size() > 2 * maxFrame) {
        if (mutation) emit requestError(tr("The background service request is too large."));
        return false;
    }
    QByteArray frame(4, '\0'); qToBigEndian(quint32(body.size()), frame.data());
    if (m_socket.write(frame + body) != frame.size() + body.size()) {
        m_socket.abort();
        if (mutation) emit requestError(tr("The background service could not accept the request."));
        return false;
    }
    m_pending.insert(id);
    m_pendingOperations.insert(id,operation);
    if (operation.startsWith("agent") || operation.startsWith("suggested") || operation.startsWith("recent") || operation.startsWith("file")) {
        m_pendingAgentContainers.insert(id,m_container); m_pendingAgentGenerations.insert(id,m_selectionGeneration);
    }
    if (mutation) { m_mutations.insert(id); emit pendingChanged(); }
    if (requestId) *requestId=id;
    QTimer::singleShot(m_requestTimeoutMs, this, [this, id] {
        if (!m_pending.remove(id)) return;
        const auto operation=m_pendingOperations.take(id);
        const auto container=m_pendingAgentContainers.take(id);
        const auto generation=m_pendingAgentGenerations.take(id);
        const bool current=generation==m_selectionGeneration && societySameBackendPath(container,m_container);
        if (operation.startsWith("file") && current)
            emit fileResultReceived(operation,id,{{"accepted",false},{"error",tr("The background service did not respond. Check the job before retrying.")}});
        if (!m_mutations.remove(id)) {
            if (operation.startsWith("suggested") && generation==m_selectionGeneration && societySameBackendPath(container,m_container))
                emit suggestedResultReceived(operation,false,tr("The background service did not respond."));
            if (operation.startsWith("recent") && generation==m_selectionGeneration && societySameBackendPath(container,m_container))
                emit recentResultReceived(operation,false,tr("The background service did not respond."));
            return;
        }
        emit pendingChanged();
        if ((operation.startsWith("agent") || operation.startsWith("suggested") || operation.startsWith("recent") || operation.startsWith("file")) && !current) return;
        emit requestError(tr("The background service did not respond. Check the operation status before retrying."));
        m_socket.abort();
    });
    return true;
}

void ModelBackendClient::readMessages()
{
    m_input += m_socket.readAll();
    if (m_input.size() > maxFrame + 4) { m_socket.abort(); return; }
    while (m_input.size() >= 4) {
        const auto size = qFromBigEndian<quint32>(m_input.constData());
        if (!size || size > maxFrame) { m_socket.abort(); return; }
        if (m_input.size() < qsizetype(size + 4)) break;
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(m_input.mid(4, size), &error);
        m_input.remove(0, size + 4);
        if (error.error != QJsonParseError::NoError || !document.isObject()) { m_socket.abort(); return; }
        handle(document.object());
        if (!connected()) return;
    }
}
void ModelBackendClient::handle(const QJsonObject &message)
{
    const auto type = message.value("type").toString();
    if (type == "result") {
        const auto id = message.value("id").toString();
        if (!m_pending.remove(id)) return;
        const auto operation = m_pendingOperations.take(id);
        const auto agentContainer=m_pendingAgentContainers.take(id);
        const auto agentGeneration=m_pendingAgentGenerations.take(id);
        const bool mutation = m_mutations.remove(id);
        if (mutation) emit pendingChanged();
        if ((operation.startsWith("agent") || operation.startsWith("suggested") || operation.startsWith("recent") || operation.startsWith("file")) && (agentGeneration!=m_selectionGeneration || !societySameBackendPath(agentContainer,m_container))) return;
        if (operation=="fileOperation" || operation=="fileJobStatus") emit fileResultReceived(operation,id,message);
        if (operation.startsWith("agent")) {
            if (message.value("agent").isObject()) emit agentStateReceived(message.value("agent").toObject());
            emit agentResultReceived(operation,message.value("accepted").toBool(),message.value("error").toString());
        }
        if (operation.startsWith("suggested")) {
            if (message.value("suggested").isObject()) emit suggestedStateReceived(message.value("suggested").toObject());
            emit suggestedResultReceived(operation,message.value("accepted").toBool(),message.value("error").toString());
        }
        if (operation.startsWith("recent")) {
            if (message.value("recent").isObject()) emit recentStateReceived(message.value("recent").toObject());
            emit recentResultReceived(operation,message.value("accepted").toBool(),message.value("error").toString());
        }
        if (mutation && !message.value("accepted").toBool()) {
            auto error = message.value("error").toString();
            if (error.isEmpty()) error = tr("The background service could not start the operation.");
            emit requestError(error);
        }
    }
    if ((type == "state" || type == "result") && message.value("state").isObject()) {
        const auto state = message.value("state").toObject();
        // Drive ownership is independent of the model importer's current
        // container, which can remain on an older selection until a job ends.
        if (!m_container.isEmpty() && state.value("systemDrive").isObject()) {
            const auto drive = societySameBackendPath(state.value("selectedContainer").toString(), m_container)
                ? state.value("systemDrive").toObject() : QJsonObject{};
            emit systemDriveStateReceived(drive);
        }
        if (societySameBackendPath(state.value("containerPath").toString(), m_container)
            && societySameBackendPath(state.value("selectedContainer").toString(), m_container)) emit stateReceived(state);
    } else if (type == "agentState" && !m_agentSubscription.isEmpty() && message.value("subscription").toString()==m_agentSubscription
               && societySameBackendPath(message.value("container").toString(), m_container)
               && message.value("agent").isObject()) {
        emit agentStateReceived(message.value("agent").toObject());
    } else if (type == "suggestedState" && !m_suggestedSubscription.isEmpty() && message.value("subscription").toString()==m_suggestedSubscription
               && societySameBackendPath(message.value("container").toString(), m_container)
               && message.value("suggested").isObject()) {
        emit suggestedStateReceived(message.value("suggested").toObject());
    } else if (type == "recentState" && !m_recentSubscription.isEmpty() && message.value("subscription").toString()==m_recentSubscription
               && societySameBackendPath(message.value("container").toString(), m_container)
               && message.value("recent").isObject()) {
        emit recentStateReceived(message.value("recent").toObject());
    } else if (type == "fileJobChanged" && societySameBackendPath(message.value("container").toString(),m_container)) {
        emit fileJobChanged(message);
    } else if ((type == "finished" || type == "organized" || type == "recategorized")
               && message.value("container").toString() == m_container) {
        emit eventReceived(message);
    }
}

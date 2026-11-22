#include "BackendCommand.h"
#include "LocalBackendAddress.h"
#include "LocalBackendPeer.h"
#include <QDir>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLocalSocket>
#include <QThread>
#include <QUuid>
#include <QtEndian>
#include <utility>

#if defined(Q_OS_UNIX)
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {
constexpr quint32 maximumFrame = 1024 * 1024;

bool sameUser(QLocalSocket &socket)
{
#if defined(Q_OS_DARWIN) || defined(Q_OS_FREEBSD)
    uid_t uid{}; gid_t gid{};
    return ::getpeereid(int(socket.socketDescriptor()), &uid, &gid) == 0 && uid == ::geteuid();
#elif defined(Q_OS_LINUX)
    struct ucred peer{}; socklen_t size = sizeof(peer);
    return ::getsockopt(int(socket.socketDescriptor()), SOL_SOCKET, SO_PEERCRED, &peer, &size) == 0
        && peer.uid == ::geteuid();
#elif defined(Q_OS_WIN)
    return societyBackendSameWindowsUser(&socket, false);
#else
    Q_UNUSED(socket); return false;
#endif
}

int remaining(const QElapsedTimer &timer, int timeoutMs)
{
    return int(qMax<qint64>(0, qint64(timeoutMs) - timer.elapsed()));
}

bool readExactly(QLocalSocket &socket, QByteArray &out, qsizetype size,
                 const QElapsedTimer &timer, int timeoutMs)
{
    while (out.size() < size) {
        if (!socket.bytesAvailable()) {
            const auto wait = remaining(timer, timeoutMs);
            if (wait <= 0 || !socket.waitForReadyRead(wait)) return false;
        }
        const auto chunk = socket.read(size - out.size());
        if (chunk.isEmpty()) return false;
        out += chunk;
    }
    return true;
}

bool readFrame(QLocalSocket &socket, QJsonObject &message,
               const QElapsedTimer &timer, int timeoutMs, QString &error)
{
    QByteArray header;
    if (!readExactly(socket, header, 4, timer, timeoutMs)) {
        error = QStringLiteral("The Society backend disconnected or did not respond."); return false;
    }
    const auto size = qFromBigEndian<quint32>(header.constData());
    if (!size || size > maximumFrame) { error = QStringLiteral("Invalid Society backend frame."); return false; }
    QByteArray body;
    if (!readExactly(socket, body, size, timer, timeoutMs)) {
        error = QStringLiteral("The Society backend returned an incomplete response."); return false;
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        error = QStringLiteral("Invalid Society backend response."); return false;
    }
    message = document.object(); return true;
}
}

SocietyBackendCommandReply societyBackendCommand(const QString &brokerDirectory, const QString &operation,
                                                  QJsonObject arguments, const QString &expectedContainer,
                                                  int timeoutMs)
{
    SocietyBackendCommandReply reply;
    auto fail = [&](QString error, bool uncertain = false) {
        reply.error = std::move(error); reply.uncertain = uncertain; return reply;
    };
    if (timeoutMs <= 0 || timeoutMs > 60000 || !QDir::isAbsolutePath(brokerDirectory)
        || (operation != "status" && operation != "fileOperation" && operation != "fileJobStatus"
            && operation != "import" && operation != "agentSubscribe" && operation != "agentSend"
            && operation != "agentSelect" && operation != "agentNew" && operation != "agentCancel"
            && operation != "spaceSubscribe" && operation != "spaceSend" && operation != "spaceCancel"
            && operation != "suggestedSubscribe" && operation != "suggestedRefresh"
            && operation != "recentSubscribe" && operation != "recentRefresh" && operation != "resourceInspect"
            && operation != "resourceDuplicates")
        || arguments.contains("id") || arguments.contains("op") || arguments.contains("container"))
        return fail(QStringLiteral("Invalid Society backend command."));
    QElapsedTimer timer; timer.start();
    QLocalSocket socket; socket.connectToServer(societyBackendAddress(brokerDirectory));
    if (!socket.waitForConnected(remaining(timer, timeoutMs)))
        return fail(QStringLiteral("The Society backend is unavailable: ") + socket.errorString());
    if (!sameUser(socket)) return fail(QStringLiteral("The Society backend identity could not be verified."));
    QJsonObject message;
    if (!readFrame(socket, message, timer, timeoutMs, reply.error)) return reply;
    if (message.value("type") != "state" || !message.value("state").isObject())
        return fail(QStringLiteral("The Society backend did not provide its selected drive."));
    const auto state = message.value("state").toObject();
    const auto container = state.value("selectedContainer").toString();
    if (!expectedContainer.isEmpty() && !societySameBackendPath(container, expectedContainer))
        return fail(QStringLiteral("The selected Society drive does not match the requested drive."));
    if (operation == "status") { reply.response = {{"accepted", true}, {"state", state}}; return reply; }
    if (container.isEmpty()) return fail(QStringLiteral("No Society drive is selected."));
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    arguments.insert("id", id); arguments.insert("op", operation); arguments.insert("container", container);
    const auto body = QJsonDocument(arguments).toJson(QJsonDocument::Compact);
    if (body.isEmpty() || body.size() > maximumFrame) return fail(QStringLiteral("The Society request is too large."));
    QByteArray frame(4, '\0'); qToBigEndian(quint32(body.size()), frame.data()); frame += body;
    const auto sent = socket.write(frame);
    const bool uncertain = (operation == "fileOperation" || operation == "import" || operation == "agentSend"
                            || operation == "agentSelect" || operation == "agentNew" || operation == "agentCancel"
                            || operation == "spaceSend" || operation == "spaceCancel") && sent > 0;
    if (sent != frame.size()) return fail(QStringLiteral("Could not send the Society request."), uncertain);
    if (socket.bytesToWrite() && !socket.waitForBytesWritten(remaining(timer, timeoutMs)))
        return fail(QStringLiteral("The Society request may not have been delivered."), uncertain);
    while (remaining(timer, timeoutMs) > 0) {
        if (!readFrame(socket, message, timer, timeoutMs, reply.error)) { reply.uncertain = uncertain; return reply; }
        if (message.value("type") != "result" || message.value("id").toString() != id) continue;
        reply.response = message;
        if (!message.value("accepted").toBool())
            reply.error = message.value("error").toString(QStringLiteral("Society rejected the request."));
        return reply;
    }
    return fail(QStringLiteral("The Society backend did not respond."), uncertain);
}

SocietyBackendCommandReply societySpaceCommand(const QString &brokerDirectory,const QString &operation,
                                               QJsonObject arguments,const QString &expectedContainer,int timeoutMs)
{
    if (operation!="spaceSubscribe" && operation!="spaceSend" && operation!="spaceCancel")
        return {{},QStringLiteral("Invalid Space command."),false};
    if (operation!="spaceSend") return societyBackendCommand(brokerDirectory,operation,std::move(arguments),expectedContainer,timeoutMs);
    QElapsedTimer timer; timer.start();
    while (remaining(timer,timeoutMs)>0) {
        const auto state=societyBackendCommand(brokerDirectory,"spaceSubscribe",{{"spaceId",arguments.value("spaceId")}},
                                               expectedContainer,qMin(remaining(timer,timeoutMs),2000));
        if (!state.ok()) return state;
        if (state.response.value("space").toObject().value("loaded").toBool()) {
            const auto budget=remaining(timer,timeoutMs);
            if (budget<=0) break;
            return societyBackendCommand(brokerDirectory,operation,std::move(arguments),expectedContainer,budget);
        }
        QThread::msleep(100);
    }
    return {{},QStringLiteral("The Space conversation did not become ready."),false};
}

SocietyBackendCommandReply societyBackendDirectoryPage(const QString &brokerDirectory, const QString &path,
                                                        int offset, int limit, int timeoutMs, const QString &expectedContainer)
{
    SocietyBackendCommandReply reply;
    auto fail = [&](QString error) { reply.error = std::move(error); return reply; };
    if (!QDir::isAbsolutePath(brokerDirectory) || path.isEmpty() || path.size() > 4096
        || offset < 0 || limit < 1 || limit > 200 || timeoutMs <= 0 || timeoutMs > 60000)
        return fail(QStringLiteral("Invalid Society directory request."));

    QElapsedTimer timer; timer.start();
    QLocalSocket socket; socket.connectToServer(societyBackendAddress(brokerDirectory));
    if (!socket.waitForConnected(remaining(timer, timeoutMs)))
        return fail(QStringLiteral("The Society backend is unavailable: ") + socket.errorString());
    if (!sameUser(socket)) return fail(QStringLiteral("The Society backend identity could not be verified."));
    QJsonObject message;
    if (!readFrame(socket, message, timer, timeoutMs, reply.error)) return reply;
    if (message.value("type") != "state" || !message.value("state").isObject())
        return fail(QStringLiteral("The Society backend did not provide its selected drive."));
    const auto container = message.value("state").toObject().value("selectedContainer").toString();
    if (!expectedContainer.isEmpty() && !societySameBackendPath(container, expectedContainer))
        return fail(QStringLiteral("The selected Society drive does not match the requested drive."));
    if (container.isEmpty()) return fail(QStringLiteral("No Society drive is selected."));

    const auto send = [&](QJsonObject command) {
        const auto body = QJsonDocument(command).toJson(QJsonDocument::Compact);
        if (body.isEmpty() || body.size() > maximumFrame) return false;
        QByteArray frame(4, '\0'); qToBigEndian(quint32(body.size()), frame.data()); frame += body;
        if (socket.write(frame) != frame.size()) return false;
        return !socket.bytesToWrite() || socket.waitForBytesWritten(remaining(timer, timeoutMs));
    };
    const auto openId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!send({{"id",openId},{"op","directoryOpen"},{"container",container},{"path",path}}))
        return fail(QStringLiteral("Could not request the Society directory."));
    QJsonObject directory, latest;
    bool opened = false;
    while (remaining(timer, timeoutMs) > 0) {
        if (!readFrame(socket, message, timer, timeoutMs, reply.error)) return reply;
        if (message.value("type") == "directoryChanged") latest = message.value("directory").toObject();
        if (message.value("type") == "result" && message.value("id").toString() == openId) {
            if (!message.value("accepted").toBool())
                return fail(message.value("error").toString(QStringLiteral("Society could not open the directory.")));
            directory = message.value("directory").toObject(); opened = true;
        }
        if (!opened) continue;
        if (latest.value("generation") == directory.value("generation") && latest.value("path") == directory.value("path"))
            directory = latest;
        if (directory.value("status") == "ready") break;
        if (directory.value("status") == "closed") return fail(QStringLiteral("The Society directory is unavailable."));
    }
    if (!opened || directory.value("status") != "ready") return fail(QStringLiteral("The Society directory did not become ready."));
    QJsonArray entries; QJsonObject page;
    while (entries.size()<limit && remaining(timer,timeoutMs)>0) {
        const auto pageId=QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (!send({{"id",pageId},{"op","directoryPage"},{"container",container},
                   {"generation",directory.value("generation")},{"revision",directory.value("revision")},
                   {"offset",offset+entries.size()},{"limit",qMin(64,limit-int(entries.size()))}}))
            return fail(QStringLiteral("Could not request the Society directory page."));
        bool received=false;
        while (remaining(timer,timeoutMs)>0) {
            if (!readFrame(socket,message,timer,timeoutMs,reply.error)) return reply;
            if (message.value("type")!="result" || message.value("id").toString()!=pageId) continue;
            if (!message.value("accepted").toBool() || !message.value("entries").isArray())
                return fail(message.value("error").toString(QStringLiteral("Society could not read the directory page.")));
            page=message; received=true; break;
        }
        if (!received) break;
        const auto chunk=page.value("entries").toArray();
        for (const auto &entry:chunk) entries.append(entry);
        if (chunk.isEmpty() || offset+entries.size()>=page.value("count").toInt()) break;
    }
    if (!page.isEmpty() && remaining(timer,timeoutMs)<=0 && entries.size()<limit
        && offset+entries.size()<page.value("count").toInt())
        return fail(QStringLiteral("The Society directory did not respond."));
    if (!page.isEmpty()) {
        page.insert("entries",entries);page.insert("offset",offset);page.insert("path",path);
        page.insert("hasMore",offset+entries.size()<page.value("count").toInt());
        reply.response=page;return reply;
    }
    return fail(QStringLiteral("The Society directory did not respond."));
}

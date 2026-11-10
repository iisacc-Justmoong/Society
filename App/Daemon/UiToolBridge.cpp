#include "UiToolBridge.h"
#include "Shared/LocalBackendAddress.h"
#include "Shared/LocalBackendPeer.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QUuid>
#include <QtEndian>

namespace a = iiMultimodalAgent::agent;

a::ToolResult societyUiToolCall(const QString &brokerDirectory, const QString &name,
                                 const QJsonObject &arguments, const a::ToolContext &context, int timeoutMs)
{
    context.cancellation.throwIfCancelled();
    if (timeoutMs<=0 || timeoutMs>60000) return {QStringLiteral("Invalid window request timeout."),{},true};
    QElapsedTimer elapsed; elapsed.start();
    QLocalSocket socket;
    socket.connectToServer(societyBackendAddress(brokerDirectory));
    if (!socket.waitForConnected(qMin(timeoutMs,5000)) || !societyBackendPeerSameUser(&socket)
        || societyBackendPeerPid(&socket)!=QCoreApplication::applicationPid())
        return {QStringLiteral("A verified Society window is not connected."),{},true};
    const auto id=QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto body=QJsonDocument(QJsonObject{{"id",id},{"op","uiInvoke"},{"tool",name},{"arguments",arguments}})
        .toJson(QJsonDocument::Compact);
    if (body.isEmpty() || body.size()>1024*1024) return {QStringLiteral("The window request is too large."),{},true};
    QByteArray frame(4,'\0'); qToBigEndian(quint32(body.size()),frame.data()); frame+=body;
    if (socket.write(frame)!=frame.size() || (socket.bytesToWrite() && !socket.waitForBytesWritten(qMax(1,timeoutMs-int(elapsed.elapsed())))))
        return {QStringLiteral("The Society window connection was interrupted."),{},true};
    QByteArray input;
    while (elapsed.elapsed()<timeoutMs && socket.state()==QLocalSocket::ConnectedState) {
        context.cancellation.throwIfCancelled();
        if (!socket.bytesAvailable()) socket.waitForReadyRead(50);
        input+=socket.readAll();
        if (input.size()>1024*1024+4) break;
        while (input.size()>=4) {
            const auto size=qFromBigEndian<quint32>(input.constData());
            if (!size || size>1024*1024) return {QStringLiteral("Invalid Society window response."),{},true};
            if (input.size()<qsizetype(size+4)) break;
            QJsonParseError error; const auto document=QJsonDocument::fromJson(input.mid(4,size),&error); input.remove(0,size+4);
            if (error.error!=QJsonParseError::NoError || !document.isObject())
                return {QStringLiteral("Invalid Society window response."),{},true};
            const auto response=document.object();
            if (response.value("id").toString()!=id || response.value("type")!="result") continue;
            if (!response.value("accepted").toBool())
                return {response.value("error").toString(QStringLiteral("The Society window is unavailable.")),{},true};
            const auto result=response.value("result").toObject();
            return {result.value("text").toString(),result.value("data").toObject(),result.value("isError").toBool(),
                    result.value("content").toArray(),result.value("metadata").toObject()};
        }
    }
    context.cancellation.throwIfCancelled();
    return {QStringLiteral("The Society window connection was interrupted."),{},true};
}

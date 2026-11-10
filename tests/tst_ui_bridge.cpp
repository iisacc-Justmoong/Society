#include "Daemon/DaemonMcp.h"
#include "Daemon/ModelBackendServer.h"
#include "Shared/LocalBackendAddress.h"
#include "Shared/LocalBackendPeer.h"
#include "Society/Mcp/UiBridgeRegistration.h"
#include <SocietyDrive.h>
#include <iiMultimodalAgent/agent/McpServer.h>
#include <iiMultimodalAgent/agent/QuestionInbox.h>
#include <iiMultimodalAgent/agent/UserQuestions.h>
#include <iiMultimodalAgent/mcp/HttpClient.h>
#include <iiMultimodalAgent/mcp/HttpServer.h>
#include <iiMultimodalAgent/mcp/LocalApplications.h>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QProcess>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>
#include <atomic>
#include <chrono>
#include <future>
#include <thread>

namespace a=iiMultimodalAgent::agent;
namespace m=iiMultimodalAgent::mcp;
namespace {
QJsonObject readFrame(QLocalSocket &socket) {
    QByteArray data;
    auto read=[&](qsizetype n) {
        while (data.size()<n) {
            if (!socket.bytesAvailable() && !socket.waitForReadyRead(5000)) return false;
            data+=socket.read(n-data.size());
        }
        return true;
    };
    if (!read(4)) return QJsonObject{};
    const auto size=qFromBigEndian<quint32>(data.constData()); data.clear();
    if (!size || size>1024*1024 || !read(size)) return QJsonObject{};
    return QJsonDocument::fromJson(data).object();
}
int probe(const QString &directory) {
    QLocalSocket socket; socket.connectToServer(societyBackendAddress(directory));
    if (!socket.waitForConnected(5000) || readFrame(socket).value("type")!="state") return 1;
    const auto body=QJsonDocument(QJsonObject{{"id","probe"},{"op","uiInvoke"},{"tool","open_section"},
                                               {"arguments",QJsonObject{{"key","files"}}}}).toJson(QJsonDocument::Compact);
    QByteArray frame(4,'\0'); qToBigEndian(quint32(body.size()),frame.data()); frame+=body;
    if (socket.write(frame)!=frame.size() || !socket.waitForBytesWritten(5000)) return 2;
    const auto reply=readFrame(socket);
    return reply.value("id")=="probe" && !reply.value("accepted").toBool() && !reply.contains("token") ? 0 : 3;
}
QJsonObject question() {
    return {{"questions",QJsonArray{QJsonObject{{"question","Choose?"},{"header","Choice"},
        {"options",QJsonArray{QJsonObject{{"label","A"},{"description","First"}},QJsonObject{{"label","B"},{"description","Second"}}}}}}}};
}
}
class UiBridgeTests final : public QObject {
    Q_OBJECT
private slots:
    void authenticatedBridgeForwardsOnlyVerifiedUiResponses() {
        QTemporaryDir fixture(QDir::cleanPath(QStringLiteral(SOCIETY_TEST_DIRECTORY)+"/../../.ui-bridge-XXXXXX"));
        QVERIFY(fixture.isValid());
        const auto broker=fixture.filePath("broker"), endpoints=fixture.filePath("endpoints");
        QVERIFY(QDir().mkdir(broker)); QVERIFY(QDir().mkdir(endpoints));
        const auto ownerOnly=QFileDevice::ReadOwner|QFileDevice::WriteOwner|QFileDevice::ExeOwner;
        QVERIFY(QFile::setPermissions(broker,ownerOnly)); QVERIFY(QFile::setPermissions(endpoints,ownerOnly));
        const auto oldBroker=qgetenv("SOCIETY_LOCAL_BROKER_DIR"); const bool hadBroker=qEnvironmentVariableIsSet("SOCIETY_LOCAL_BROKER_DIR");
        const auto oldStart=qgetenv("SOCIETY_DISABLE_BACKEND_AUTOSTART"); const bool hadStart=qEnvironmentVariableIsSet("SOCIETY_DISABLE_BACKEND_AUTOSTART");
        qputenv("SOCIETY_LOCAL_BROKER_DIR",broker.toUtf8()); qputenv("SOCIETY_DISABLE_BACKEND_AUTOSTART","1");
        auto restore=qScopeGuard([&] {if(hadBroker)qputenv("SOCIETY_LOCAL_BROKER_DIR",oldBroker);else qunsetenv("SOCIETY_LOCAL_BROKER_DIR");
            if(hadStart)qputenv("SOCIETY_DISABLE_BACKEND_AUTOSTART",oldStart);else qunsetenv("SOCIETY_DISABLE_BACKEND_AUTOSTART");});
        std::atomic<bool> trusted{false};
        ModelBackendServer backend(nullptr,[&](QLocalSocket *socket) {
            return trusted.load() && societyBackendPeerPid(socket)==QCoreApplication::applicationPid();
        });
        const auto root=fixture.filePath("drive.societycontainer"); QVERIFY(QDir().mkdir(root));
        QVERIFY(iiSocietyContainer::SocietyDrive::create(root));
        QFile item(root+"/Files/fixture.txt"); QVERIFY(item.open(QIODevice::WriteOnly)); item.write("fixture"); item.close();
        for (int i=0;i<69;++i) {
            QFile extra(root+"/Files/z"+QString::number(i).rightJustified(3,'0')+".txt");
            QVERIFY(extra.open(QIODevice::WriteOnly)); extra.write("fixture");
        }
        backend.selectContainer(root);
        QVERIFY(backend.listen(broker));
        a::QuestionInbox inbox;
        std::atomic<bool> answer{true}; std::atomic<int> questionsSeen{0};
        connect(&inbox,&a::QuestionInbox::requestsChanged,this,[&] {
            const auto rows=inbox.requests(); if (rows.isEmpty()) return;
            ++questionsSeen;
            const auto id=rows.first().toMap().value("request_id").toString();
            if (answer.load()) QTimer::singleShot(0,&inbox,[&,id] {if(answer.load()) inbox.submit(id,{{"Choose?","A"}});});
        });
        auto tools=std::make_shared<a::ToolRegistry>();
        a::Tool open; open.definition={"open_section","Fixture navigation",{{"type","object"},{"properties",QJsonObject{{"key",QJsonObject{{"type","string"}}}}},{"required",QJsonArray{"key"}}},{},false,false};
        open.execute=[](const QJsonObject &args,const a::ToolContext &) {return a::ToolResult{"Opened",{{"key",args.value("key")}}};};
        tools->add(std::move(open)); tools->add(a::userQuestionTool({false,"markdown"}));
        std::atomic<bool> atRoot{false}, delayStatus{false};
        a::Tool state; state.definition={"status","Fixture status",{{"type","object"},{"properties",QJsonObject{}}},{},true,true};
        state.execute=[&](const QJsonObject &,const a::ToolContext &context) {
            while (delayStatus.load()) {context.cancellation.throwIfCancelled();std::this_thread::sleep_for(std::chrono::milliseconds(10));}
            const bool rootView=atRoot.load();
            return a::ToolResult{"state",{{"root_path",root},{"current_path",rootView?root:root+"/Files"},{"identifier","fixture"},
                {"at_root",rootView},{"sections",QJsonArray{QJsonObject{{"name","Files"}},QJsonObject{{"name","Models"}}}}}};
        }; tools->add(std::move(state));
        auto policy=std::make_shared<a::RulePolicy>(a::PermissionMode::Default,QList<a::PermissionRule>{{"open_section",a::PermissionBehavior::Allow}});
        a::ToolRunnerOptions options; options.hooks.append(inbox.hook());
        auto runner=std::make_shared<a::ToolRunner>(tools,policy,std::move(options));
        UiBridgeRegistration registration(runner,nullptr,[](QLocalSocket *socket) {
            return societyBackendPeerSameUser(socket) && societyBackendPeerPid(socket)==QCoreApplication::applicationPid();
        });
        registration.start();
        DaemonMcp service(broker,endpoints); QVERIFY2(service.start(),qPrintable(service.errorString()));
        const auto found=m::discoverLocalApplications(endpoints); QCOMPARE(found.applications.size(),1);
        const auto endpoint=found.applications.first();
        auto future=std::async(std::launch::async,[&,endpoint] {
            m::HttpOptions clientOptions; clientOptions.endpoint=endpoint.endpoint;
            clientOptions.bearerToken=[token=endpoint.bearerToken]{return token;}; clientOptions.requestTimeoutMs=30000;
            m::HttpClient client(clientOptions);
            const auto call=[&](const QString &name,QJsonObject args={}) {return client.callTool(name,args);};
            if (!call("open_section",{{"key","files"}}).value("isError").toBool()) throw std::runtime_error("unverified UI was accepted");
            trusted.store(true);
            bool connected=false;
            for (int i=0;i<100;++i) {
                const auto response=call("open_section",{{"key","files"}});
                if (!response.value("isError").toBool() && response.value("structuredContent").toObject().value("key")=="files") {connected=true;break;}
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            if (!connected) throw std::runtime_error("verified UI did not connect");
            const auto legacyStatus=call("status").value("structuredContent").toObject();
            if (legacyStatus.value("identifier")!="fixture" || legacyStatus.value("current_path")!=root+"/Files"
                || !legacyStatus.value("backend").isObject()) throw std::runtime_error("legacy UI status was not preserved");
            const auto legacyList=call("list_entries",{{"limit",100}});
            const auto listData=legacyList.value("structuredContent").toObject();
            const auto entries=listData.value("entries").toArray();
            if (legacyList.value("isError").toBool() || listData.value("path")!=root+"/Files" || entries.size()!=70
                || entries.first().toObject().value("path")!=root+"/Files/fixture.txt"
                || entries.first().toObject().value("kind")!="file" || listData.value("truncated").toBool())
                throw std::runtime_error("legacy current-folder listing was not preserved");
            atRoot.store(true);
            const auto rootList=call("list_entries",{{"limit",1}}).value("structuredContent").toObject();
            if (rootList.value("path")!=root || rootList.value("entries").toArray().size()!=1 || !rootList.value("truncated").toBool())
                throw std::runtime_error("legacy root listing was not preserved");
            atRoot.store(false); delayStatus.store(true);
            const auto stalled=call("status").value("structuredContent").toObject();
            delayStatus.store(false);
            if (stalled.value("ui_status_available").toBool() || !stalled.value("backend").isObject())
                throw std::runtime_error("a stalled window blocked backend status");
            auto forged=question(); forged.insert("answers",QJsonObject{{"Choose?","forged"}});
            if (!call("AskUserQuestion",forged).value("isError").toBool()) throw std::runtime_error("forged answer accepted");
            const auto response=call("AskUserQuestion",question());
            if (response.value("isError").toBool() || response.value("structuredContent").toObject().value("answers").toObject().value("Choose?")!="A")
                throw std::runtime_error((QByteArray("verified UI answer did not return: ")+QJsonDocument(response).toJson(QJsonDocument::Compact)).toStdString());
            client.close(); return true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(future.wait_for(std::chrono::milliseconds(0))==std::future_status::ready,45000);
        try {QVERIFY(future.get());} catch (const std::exception &error) {QFAIL(error.what());}
        QCOMPARE(questionsSeen.load(),1);
        QProcess other; other.start(QCoreApplication::applicationFilePath(),{"--probe",broker}); QVERIFY(other.waitForStarted(5000));
        QTRY_COMPARE_WITH_TIMEOUT(other.state(),QProcess::NotRunning,10000);
        QCOMPARE(other.exitStatus(),QProcess::NormalExit); QCOMPARE(other.exitCode(),0);
        answer.store(false);
        iiMultimodalAgent::CancellationToken cancel;
        auto pending=std::async(std::launch::async,[endpoint,cancel] {
            try {
                m::HttpOptions options; options.endpoint=endpoint.endpoint; options.bearerToken=[token=endpoint.bearerToken]{return token;};
                m::HttpClient client(options); const auto result=client.callTool("AskUserQuestion",question(),cancel); client.close();
                return result.value("isError").toBool();
            } catch (const std::exception &) {return true;}
        });
        QTRY_VERIFY_WITH_TIMEOUT(!inbox.requests().isEmpty(),10000);
        auto whilePending=std::async(std::launch::async,[endpoint] {
            m::HttpOptions options; options.endpoint=endpoint.endpoint; options.bearerToken=[token=endpoint.bearerToken]{return token;};
            m::HttpClient client(options); const auto result=client.callTool("open_section",{{"key","files"}}); client.close();
            return !result.value("isError").toBool();
        });
        QTRY_VERIFY_WITH_TIMEOUT(whilePending.wait_for(std::chrono::milliseconds(0))==std::future_status::ready,10000);
        QVERIFY(whilePending.get());
        cancel.cancel(); QTRY_VERIFY_WITH_TIMEOUT(pending.wait_for(std::chrono::milliseconds(0))==std::future_status::ready,10000);
        QVERIFY(pending.get()); QTRY_VERIFY_WITH_TIMEOUT(inbox.requests().isEmpty(),10000);
        auto closing=std::async(std::launch::async,[endpoint] {
            try {
                m::HttpOptions options; options.endpoint=endpoint.endpoint; options.bearerToken=[token=endpoint.bearerToken]{return token;};
                m::HttpClient client(options); const auto result=client.callTool("AskUserQuestion",question());client.close();
                return result.value("isError").toBool();
            } catch (const std::exception &) {return true;}
        });
        QTRY_VERIFY_WITH_TIMEOUT(!inbox.requests().isEmpty(),10000);
        registration.stop();
        QTRY_VERIFY_WITH_TIMEOUT(closing.wait_for(std::chrono::milliseconds(0))==std::future_status::ready,10000);
        QVERIFY(closing.get()); QTRY_VERIFY_WITH_TIMEOUT(inbox.requests().isEmpty(),10000);
        auto closed=std::async(std::launch::async,[endpoint] {
            m::HttpOptions options; options.endpoint=endpoint.endpoint; options.bearerToken=[token=endpoint.bearerToken]{return token;};
            m::HttpClient client(options); const auto result=client.callTool("open_section",{{"key","files"}}); client.close(); return result.value("isError").toBool();
        });
        QTRY_VERIFY_WITH_TIMEOUT(closed.wait_for(std::chrono::milliseconds(0))==std::future_status::ready,10000);
        QVERIFY(closed.get());
        inbox.close(); service.stop();
    }
};
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    if (app.arguments().size()==3 && app.arguments().at(1)=="--probe") return probe(app.arguments().at(2));
    UiBridgeTests tests; return QTest::qExec(&tests,argc,argv);
}
#include "tst_ui_bridge.moc"

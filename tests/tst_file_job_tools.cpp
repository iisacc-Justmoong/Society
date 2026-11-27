#include "Society/Mcp/FileJobTools.h"
#include <iiMultimodalAgent>
#include <QDir>
#include <QTest>
#include <atomic>
#include <chrono>
#include <future>

namespace a = iiMultimodalAgent::agent;
namespace m = iiMultimodalAgent::mcp;
class FileJobToolsTests final : public QObject {
    Q_OBJECT
private slots:
    void rejectsAutomatedDeletionBeforeTransport() {
        int calls=0;
        const auto tools=societyFileJobTools([&](const QString &,QJsonObject,const a::ToolContext &) {
            ++calls; return SocietyBackendCommandReply{{{"accepted",true}},{},false};
        });
        for (const auto &action:{"trash","remove"}) {
            const auto result=tools.first().execute({{"action",action},{"path","Files/a.txt"},{"approved",true}},{});
            QVERIFY(result.isError); QVERIFY(result.text.contains("cleanup",Qt::CaseInsensitive));
        }
        for (const auto &destination:{"Deleted","deleted/subfolder"}) {
            const auto result=tools.first().execute({{"action","move"},{"path","Files/a.txt"},{"argument",destination}},{});
            QVERIFY(result.isError);
        }
        QCOMPARE(calls,0);
        const auto ordinary=tools.first().execute({{"action","move"},{"path","Files/a.txt"},{"argument","Files/Deleted"}},{});
        QVERIFY(!ordinary.isError); QCOMPARE(calls,1);
    }
    void forwardsJobsAndReportsUncertainResults() {
        int calls=0; QString operation; QJsonObject arguments;
        a::ToolRegistry registry;
        for (auto &tool : societyFileJobTools([&](const QString &op,QJsonObject args,const a::ToolContext &) {
            ++calls; operation=op; arguments=std::move(args);
            return SocietyBackendCommandReply{{{"accepted",true},{"jobId","job-1"},{"state","queued"}}, {}, false};
        })) registry.add(std::move(tool));
        const QJsonObject request{{"action","rename"},{"path","Files/a.txt"},{"argument","b.txt"}};
        registry.validateInput("file_job_submit",request);
        const auto result=registry.get("file_job_submit").execute(request,{});
        QVERIFY(!result.isError); QCOMPARE(result.data.value("state").toString(),QStringLiteral("queued"));
        QCOMPARE(operation,QStringLiteral("fileOperation")); QCOMPARE(arguments.value("path").toString(),QStringLiteral("Files/a.txt"));
        QCOMPARE(calls,1);
        bool rejected=false;
        try { registry.validateInput("file_job_submit",{{"action","unknown"},{"path","Files/a"}}); }
        catch (...) { rejected=true; }
        QVERIFY(rejected); QCOMPARE(calls,1);
        const auto status=registry.get("file_job_status").execute({{"jobId","job-1"}},{});
        QVERIFY(!status.isError); QCOMPARE(operation,QStringLiteral("fileJobStatus")); QCOMPARE(arguments.value("jobId").toString(),QStringLiteral("job-1"));
        auto failed=societyFileJobTools([](const QString &,QJsonObject,const a::ToolContext &) {
            return SocietyBackendCommandReply{{},QStringLiteral("Connection interrupted"),true};
        });
        const auto uncertain=failed.first().execute(request,{});
        QVERIFY(uncertain.isError); QVERIFY(uncertain.data.value("uncertain").toBool());
    }
    void exposesJobsThroughAuthenticatedMcp() {
        std::atomic<int> calls{0};
        auto registry=std::make_shared<a::ToolRegistry>();
        for (auto &tool:societyFileJobTools([&](const QString &op,QJsonObject args,const a::ToolContext &) {
            ++calls;
            if (op=="fileOperation" && args.value("path")=="Files/a.txt")
                return SocietyBackendCommandReply{{{"accepted",true},{"state","queued"},{"jobId","job-1"}}, {}, false};
            return SocietyBackendCommandReply{{},QStringLiteral("Unexpected request"),false};
        })) registry->add(std::move(tool));
        auto policy=std::make_shared<a::RulePolicy>(a::PermissionMode::DontAsk,
            QList<a::PermissionRule>{{"file_job_submit",a::PermissionBehavior::Allow}});
        m::HttpServerOptions http; http.authenticate=[](const QByteArray &token) { return token=="fixture-token"?QStringLiteral("fixture"):QString(); };
        m::HttpServer server([registry,policy](const QString &) {
            a::McpServerOptions options; options.workingDirectory=QDir::currentPath();
            return a::mcpServerOptions(registry,policy,std::move(options));
        },http);
        QVERIFY2(server.listen(),qPrintable(server.errorString()));
        auto future=std::async(std::launch::async,[endpoint=server.endpoint()] {
            m::HttpOptions options; options.endpoint=endpoint; options.bearerToken=[] {return QByteArray("fixture-token");};
            options.initializeTimeoutMs=5000; options.requestTimeoutMs=10000;
            m::HttpClient client(options);
            auto response=client.request("tools/call",{{"name","file_job_submit"},
                {"arguments",QJsonObject{{"action","rename"},{"path","Files/a.txt"},{"argument","b.txt"}}}});
            client.close(); return response;
        });
        QTRY_VERIFY_WITH_TIMEOUT(future.wait_for(std::chrono::milliseconds(0))==std::future_status::ready,20000);
        const auto response=future.get();
        QVERIFY(!response.value("isError").toBool());
        QCOMPARE(response.value("structuredContent").toObject().value("state").toString(),QStringLiteral("queued"));
        QCOMPARE(calls.load(),1); server.close();
    }
};
QTEST_GUILESS_MAIN(FileJobToolsTests)
#include "tst_file_job_tools.moc"

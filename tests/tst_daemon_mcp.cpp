#include "Daemon/DaemonMcp.h"
#include "Daemon/ModelBackendServer.h"
#include <SocietyDrive.h>
#include <iiMultimodalAgent/mcp/HttpClient.h>
#include <iiMultimodalAgent/mcp/LocalApplications.h>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QProcessEnvironment>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>
#include <chrono>
#include <future>
#include <thread>

namespace m=iiMultimodalAgent::mcp;
class DaemonMcpTests final : public QObject {
    Q_OBJECT
private slots:
    void daemonOwnsEndpointForItsLifetime() {
        QTemporaryDir fixture(QDir::cleanPath(QStringLiteral(SOCIETY_TEST_DIRECTORY)+"/../../.daemon-process-mcp-XXXXXX"));
        QVERIFY(fixture.isValid());
        const auto root=fixture.filePath("drive.societycontainer"); QVERIFY(QDir().mkdir(root));
        QVERIFY(iiSocietyContainer::SocietyDrive::create(root));
        const auto registry=fixture.filePath("endpoints"); QVERIFY(QDir().mkdir(registry));
        QVERIFY(QFile::setPermissions(registry,QFileDevice::ReadOwner|QFileDevice::WriteOwner|QFileDevice::ExeOwner));
        auto env=QProcessEnvironment::systemEnvironment();
        env.insert("SOCIETY_TEST_OUTPUT_DIR",fixture.path()); env.insert("SOCIETY_TEST_ENABLE_DAEMON_MCP","1");
        env.insert("SOCIETY_CONTAINER_PATH",root); env.insert("SOCIETY_STORAGE_SETTINGS_PATH",fixture.filePath("storage.json"));
        env.insert("SOCIETY_LOCAL_BROKER_DIR",fixture.filePath("broker")); env.insert("SOCIETY_JOB_STATE_ROOT",fixture.filePath("jobs"));
        env.insert("SOCIETY_DISABLE_SESSION_RESTORE","1"); env.insert("SOCIETY_DISABLE_NATIVE_DRIVE","1");
        env.insert("IILOCALLLM_APP_ENDPOINTS",registry); env.remove("IILOCALLLM_DISABLE_APP_MCP");
        QProcess daemon; daemon.setProcessEnvironment(env);
        const auto cleanup=qScopeGuard([&] { if (daemon.state()!=QProcess::NotRunning) {
            daemon.terminate(); if (!daemon.waitForFinished(5000)) {daemon.kill();daemon.waitForFinished(5000);} } });
        daemon.start(QStringLiteral(SOCIETY_DAEMON_EXECUTABLE),{"--license-broker"});
        QVERIFY(daemon.waitForStarted(5000));
        QTRY_COMPARE_WITH_TIMEOUT(m::discoverLocalApplications(registry).applications.size(),1,30000);
        const auto endpoint=m::discoverLocalApplications(registry).applications.first();
        QCOMPARE(endpoint.application.id,QStringLiteral("com.iisacc.society"));
        auto future=std::async(std::launch::async,[endpoint] {
            m::HttpOptions options; options.endpoint=endpoint.endpoint;
            options.bearerToken=[token=endpoint.bearerToken]{return token;}; options.requestTimeoutMs=10000;
            m::HttpClient client(options); const auto reply=client.request("tools/call",{{"name","status"},{"arguments",QJsonObject{}}});
            client.close(); return reply.value("structuredContent").toObject().value("selectedContainer").toString();
        });
        QTRY_VERIFY_WITH_TIMEOUT(future.wait_for(std::chrono::milliseconds(0))==std::future_status::ready,15000);
        try { QCOMPARE(future.get(),root); } catch (const std::exception &error) { QFAIL(error.what()); }
        daemon.terminate(); QTRY_COMPARE_WITH_TIMEOUT(daemon.state(),QProcess::NotRunning,15000);
        QCOMPARE(daemon.exitStatus(),QProcess::NormalExit); QCOMPARE(daemon.exitCode(),0);
        QTRY_VERIFY_WITH_TIMEOUT(m::discoverLocalApplications(registry).applications.isEmpty(),5000);
    }
    void fileJobsWorkWithoutAnyWindow() {
        QTemporaryDir fixture(QDir::cleanPath(QStringLiteral(SOCIETY_TEST_DIRECTORY)+"/../../.daemon-mcp-XXXXXX"));
        QVERIFY(fixture.isValid());
        const auto previous=qgetenv("SOCIETY_JOB_STATE_ROOT"); const bool hadPrevious=qEnvironmentVariableIsSet("SOCIETY_JOB_STATE_ROOT");
        qputenv("SOCIETY_JOB_STATE_ROOT",fixture.filePath("jobs").toUtf8());
        auto restore=qScopeGuard([&]{if(hadPrevious)qputenv("SOCIETY_JOB_STATE_ROOT",previous);else qunsetenv("SOCIETY_JOB_STATE_ROOT");});
        const auto root=fixture.filePath("drive.societycontainer"); QVERIFY(QDir().mkdir(root)); QVERIFY(iiSocietyContainer::SocietyDrive::create(root));
        QFile file(root+"/Files/example.txt"); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("fixture"); file.close();
        QFile outsideFile(fixture.filePath("imported.txt")); QVERIFY(outsideFile.open(QIODevice::WriteOnly)); outsideFile.write("outside"); outsideFile.close();
        const auto outsideSource=QFileInfo(outsideFile).canonicalFilePath();
        QFile sidecar(root+"/Files/internal.iisacc.json"); QVERIFY(sidecar.open(QIODevice::WriteOnly)); sidecar.write("{}"); sidecar.close();
        const auto modelSource=fixture.filePath("mcp.safetensors");
        const QByteArray payload(4096,'m');
        const auto header=QJsonDocument(QJsonObject{{"tensor",QJsonObject{{"dtype","U8"},{"shape",QJsonArray{payload.size()}},
            {"data_offsets",QJsonArray{0,payload.size()}}}}}).toJson(QJsonDocument::Compact);
        QByteArray prefix(8,'\0'); qToLittleEndian(quint64(header.size()),prefix.data());
        QFile model(modelSource); QVERIFY(model.open(QIODevice::WriteOnly));
        QCOMPARE(model.write(prefix+header+payload),qint64(prefix.size()+header.size()+payload.size())); model.close();
        const auto broker=fixture.filePath("broker"), registry=fixture.filePath("endpoints");
        QVERIFY(QDir().mkdir(broker)); QVERIFY(QDir().mkdir(registry));
        const auto ownerOnly=QFileDevice::ReadOwner|QFileDevice::WriteOwner|QFileDevice::ExeOwner;
        QVERIFY(QFile::setPermissions(broker,ownerOnly)); QVERIFY(QFile::setPermissions(registry,ownerOnly));
        ModelBackendServer backend; backend.selectContainer(root); QVERIFY(backend.listen(broker));
        DaemonMcp service(broker,registry); QVERIFY2(service.start(),qPrintable(service.errorString()));
        const auto found=m::discoverLocalApplications(registry); QCOMPARE(found.applications.size(),1);
        QCOMPARE(found.applications.first().application.id,QStringLiteral("com.iisacc.society"));
        const auto endpoint=found.applications.first();
        auto future=std::async(std::launch::async,[endpoint,modelSource,outsideSource] {
            m::HttpOptions options; options.endpoint=endpoint.endpoint;
            options.bearerToken=[token=endpoint.bearerToken]{return token;};
            options.initializeTimeoutMs=5000; options.requestTimeoutMs=15000;
            m::HttpClient client(options);
            const auto call=[&](const QString &name,QJsonObject args={}) {
                return client.request("tools/call",{{"name",name},{"arguments",args}});
            };
            const auto state=call("status");
            if (state.value("isError").toBool() || state.value("structuredContent").toObject().value("selectedContainer").toString().isEmpty())
                throw std::runtime_error("daemon status failed");
            if (!call("open_section",{{"key","files"}}).value("isError").toBool())
                throw std::runtime_error("window navigation must fail cleanly without a UI");
            if (!call("navigate",{{"path","/fixture"}}).value("isError").toBool()
                || !call("refresh").value("isError").toBool())
                throw std::runtime_error("window controls must fail cleanly without a UI");
            const QJsonObject question{{"questions",QJsonArray{QJsonObject{{"question","Choose?"},{"header","Choice"},
                {"options",QJsonArray{QJsonObject{{"label","A"},{"description","First"}},QJsonObject{{"label","B"},{"description","Second"}}}}}}}};
            auto forged=question; forged.insert("answers",QJsonObject{{"Choose?","A"}});
            if (!call("AskUserQuestion",forged).value("isError").toBool())
                throw std::runtime_error("a remote caller forged a user answer");
            if (!call("AskUserQuestion",question).value("isError").toBool())
                throw std::runtime_error("headless question must report unavailable UI");
            const auto listed=call("list_entries",{{"path","Files"},{"limit",64}});
            const auto items=listed.value("structuredContent").toObject().value("entries").toArray();
            if (listed.value("isError").toBool() || items.size()!=1 || items.first().toObject().value("name")!="example.txt")
                throw std::runtime_error("headless listing failed or exposed metadata");
            const auto outside=call("list_entries",{{"path","../outside"}});
            if (!outside.value("isError").toBool()) throw std::runtime_error("listing escaped the container");
            const auto before=call("semantic_read",{{"address","files/example.txt"}});
            const auto record=before.value("structuredContent").toObject().value("record").toObject();
            if (before.value("isError").toBool() || record.value("revision").toString().isEmpty())
                throw std::runtime_error("headless semantic read failed");
            const auto updated=call("semantic_set_metadata",{{"address","files/example.txt"},
                {"metadata",QJsonObject{{"purpose","fixture"}}},{"revision",record.value("revision")},{"actor","urn:user:test"}});
            if (updated.value("isError").toBool()) throw std::runtime_error("headless semantic write failed");
            const auto after=call("semantic_read",{{"address","files/example.txt"}});
            if (after.value("structuredContent").toObject().value("record").toObject()
                    .value("metadata").toObject().value("purpose")!="fixture")
                throw std::runtime_error("headless semantic update not preserved");
            const auto response=call("file_job_submit",{{"action","rename"},{"path","Files/example.txt"},{"argument","renamed.txt"}});
            const auto accepted=response.value("structuredContent").toObject();
            if (response.value("isError").toBool() || accepted.value("state")!="queued") throw std::runtime_error("job was not queued");
            const auto id=accepted.value("jobId").toString();
            bool done=false;
            for (int attempt=0;attempt<200;++attempt) {
                const auto answer=call("file_job_status",{{"jobId",id}});
                const auto job=answer.value("structuredContent").toObject().value("job").toObject();
                if (job.value("state")=="succeeded") {done=true; break;}
                if (job.value("state")=="failed" || job.value("state")=="partial" || job.value("state")=="cancelled")
                    throw std::runtime_error((QStringLiteral("headless job failed: ")+job.value("error").toString()).toStdString());
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            if (!done) throw std::runtime_error("headless job did not complete");
            const auto moved=call("semantic_read",{{"address","files/renamed.txt"}});
            const auto metadata=moved.value("structuredContent").toObject().value("record").toObject().value("metadata").toObject();
            if (moved.value("isError").toBool() || metadata.value("purpose")!="fixture")
                throw std::runtime_error("headless rename did not preserve semantics");
            const auto copied=call("file_job_submit",{{"action","copy"},{"path",outsideSource},{"argument","Files"}});
            const auto copyId=copied.value("structuredContent").toObject().value("jobId").toString();
            if (copied.value("isError").toBool() || copyId.isEmpty()) throw std::runtime_error("headless copy was not queued");
            bool copiedDone=false;
            for (int attempt=0;attempt<200;++attempt) {
                const auto answer=call("file_job_status",{{"jobId",copyId}});
                const auto job=answer.value("structuredContent").toObject().value("job").toObject();
                if (job.value("state")=="succeeded") {copiedDone=true; break;}
                if (job.value("state")=="failed" || job.value("state")=="partial" || job.value("state")=="cancelled")
                    throw std::runtime_error((QStringLiteral("headless copy failed: ")+job.value("error").toString()).toStdString());
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            if (!copiedDone) throw std::runtime_error("headless copy did not complete");
            const auto imported=call("model_import_submit",{{"sources",QJsonArray{modelSource}}});
            const auto importId=imported.value("structuredContent").toObject().value("jobId").toString();
            if (imported.value("isError").toBool() || importId.isEmpty()) throw std::runtime_error("headless model import was not queued");
            bool importedDone=false;
            for (int attempt=0;attempt<200;++attempt) {
                const auto status=call("status").value("structuredContent").toObject();
                if (status.value("currentJobId")==importId && !status.value("busy").toBool()) {
                    if (!status.value("errorString").toString().isEmpty()) throw std::runtime_error("headless model import failed");
                    importedDone=true; break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            if (!importedDone) throw std::runtime_error("headless model import did not finish");
            client.close(); return true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(future.wait_for(std::chrono::milliseconds(0))==std::future_status::ready,45000);
        try { QVERIFY(future.get()); } catch (const std::exception &error) { QFAIL(error.what()); }
        QVERIFY(QFileInfo::exists(root+"/Files/renamed.txt"));
        QVERIFY(QFileInfo::exists(root+"/Files/imported.txt")); QVERIFY(QFileInfo::exists(outsideSource));
        QDirIterator models(root+"/Models",{"mcp.safetensors"},QDir::Files,QDirIterator::Subdirectories);
        QVERIFY(models.hasNext()); const auto importedPath=models.next();
        QVERIFY(QFileInfo::exists(root+"/.society-runtime/classifications/models/"
            +QDir(root+"/Models").relativeFilePath(importedPath)+".iisacc.json"));
        QVERIFY(!QFileInfo::exists(importedPath+".iisacc.json")); QVERIFY(QFileInfo::exists(modelSource));
        service.stop(); QVERIFY(m::discoverLocalApplications(registry).applications.isEmpty());
    }
};
QTEST_GUILESS_MAIN(DaemonMcpTests)
#include "tst_daemon_mcp.moc"

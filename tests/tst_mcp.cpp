#include <mcp/LocalApplications.h>
#include <mcp/HttpClient.h>
#include <agent/McpTools.h>
#include <atomic>
#include <future>
#include <agent/McpConnections.h>
#include <SocietyDrive.h>
#include <QtTest/QtTest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QSet>
#ifdef SOCIETY_MCP_TEST_GGUF
#include <agent/Engine.h>
#include <filesystem>
#endif

namespace m = iiLocalLLM::mcp;
namespace a = iiLocalLLM::agent;
namespace {
// The real executable owns the controllers; the test uses only its public MCP API.
struct AppProcess : QProcess {
    QByteArray output;
    QString logPath;
    ~AppProcess() override {
        if (state() != NotRunning) { terminate(); if (!waitForFinished(3000)) { kill(); waitForFinished(3000); } }
        output += readAll();
        QFile log(logPath); if (log.open(QIODevice::WriteOnly)) log.write(output);
    }
    void startApp(const QString& base, bool enabled, const QStringList &links = {}) {
        auto env = QProcessEnvironment::systemEnvironment();
        for (const auto* name : {"DYLD_LIBRARY_PATH", "DYLD_FRAMEWORK_PATH", "DYLD_FALLBACK_LIBRARY_PATH", "QML_IMPORT_PATH", "QML2_IMPORT_PATH"}) env.remove(name);
        env.insert("SOCIETY_HELPER_DIRECTORY", base + "/helper");
        env.insert("SOCIETY_STORAGE_SETTINGS_PATH", base + "/storage.json");
        env.insert("SOCIETY_CONTAINER_PATH", base + "/container");
        env.insert("SOCIETY_DISABLE_SESSION_RESTORE", "1");
        env.insert("IILOCALLLM_APP_ENDPOINTS", base + "/apps");
        env.insert("IILOCALLLM_DISABLE_APP_MCP", enabled ? "0" : "1");
        env.insert("QT_QPA_PLATFORM", "offscreen"); env.insert("QT_QUICK_BACKEND", "software");
        env.insert("QML_DISABLE_DISK_CACHE", "1");
        setProcessEnvironment(env); setProcessChannelMode(MergedChannels); setWorkingDirectory(base);
        logPath = base + "/app.log";
        start(MCP_APP_EXECUTABLE, QStringList{"--container", base + "/container"} + links);
    }
    bool waitForRoot() {
        QElapsedTimer timer; timer.start();
        while (state() != NotRunning && timer.elapsed() < 25000) {
            waitForReadyRead(100); output += readAll();
            if (output.contains("bootstrap.entry.root-loaded")) return true;
        }
        return false;
    }
};
m::HttpOptions clientOptions(const m::LocalApplicationEndpoint& endpoint) {
    m::HttpOptions options; options.endpoint = endpoint.endpoint;
    options.bearerToken = [token = endpoint.bearerToken] { return token; };
    options.initializeTimeoutMs = 3000; options.requestTimeoutMs = 5000;
    return options;
}
QJsonObject call(m::Client& client, const QString& name, QJsonObject arguments = {}) {
    return client.request("tools/call", {{"name", name}, {"arguments", arguments}});
}
#ifdef SOCIETY_MCP_TEST_GGUF
void verifyNativeAgent(a::Tool tool, const QString& base, const QString& expectedIdentifier) {
    // Match the SDK's pinned native acceptance model; the answer never enters the prompt/schema.
    const auto modelPath = base + "/llm-models/app-fixture";
    if (!QDir().mkpath(modelPath)) throw std::runtime_error("Cannot create model fixture directory");
    std::error_code error;
    std::filesystem::create_hard_link(SOCIETY_MCP_TEST_GGUF, (modelPath + "/model.gguf").toStdString(), error);
    if (error && !QFile::copy(SOCIETY_MCP_TEST_GGUF, modelPath + "/model.gguf")) throw std::runtime_error("Cannot provision model fixture");
    QFile file(modelPath + "/manifest.json");
    if (!file.open(QIODevice::WriteOnly)) throw std::runtime_error("Cannot write model manifest");
    file.write(QJsonDocument(QJsonObject{{"schema_version", 1}, {"id", "app-fixture"}, {"architecture", "qwen2"},
        {"format", "gguf"}, {"quantization", "Q4_K_M"}, {"context_length", 32768}, {"entry_point", "model.gguf"},
        {"capabilities", QJsonArray{"text-generation", "chat"}},
        {"files", QJsonArray{QJsonObject{{"path", "model.gguf"}, {"size", 491400032},
            {"sha256", "74a4da8c9fdbcd15bd1f6d01d621410d31c6fc00986f5eb687824e7b93d7a9db"}}}}}).toJson());
    file.close();
    iiLocalLLM::ServiceOptions serviceOptions; serviceOptions.modelsDirectory = base + "/llm-models";
    iiLocalLLM::Service service(serviceOptions);
    (void)service.loadModel({"model://app-fixture", 4096}).get();
    auto registry = std::make_shared<a::ToolRegistry>();
    tool.definition.deferred = false; const auto name = tool.definition.name; registry->add(std::move(tool));
    a::EngineOptions options; options.sessionsDirectory = base + "/agent-sessions";
    a::Engine engine(std::make_shared<a::ServiceModel>(service), registry,
        std::make_shared<a::RulePolicy>(a::PermissionMode::DontAsk, QList<a::PermissionRule>{{name, a::PermissionBehavior::Allow}}), options);
    const auto workspace = base + "/agent-workspace";
    if (!QDir().mkpath(workspace)) throw std::runtime_error("Cannot create agent workspace");
    const auto session = engine.createSession("model://app-fixture", workspace);
    a::RunRequest request{session.id, "Call " + name + " to read the actual Society state. Then return only the exact identifier value from the tool result. Do not guess."};
    request.generation.temperature = 0; request.generation.maxTokens = 512; request.maxTurns = 4;
    bool called = false;
    const auto result = engine.run(request, [&](const a::Event& event) {
        if (event.kind == a::EventKind::ToolStarted && event.data["name"] == name) called = true;
    }).result.get();
    if (!called || result.status != a::RunStatus::Completed || !result.text.contains(expectedIdentifier)
        || result.turns < 2 || result.usage.generatedTokens < 1 || !a::pendingToolCalls(engine.session(session.id).messages).isEmpty())
        throw std::runtime_error(("Native app control acceptance failed: " + result.errorMessage + " / " + result.text).toStdString());
    qInfo() << "Native Qwen2.5 0.5B called the discovered Society status tool and returned its actual container identifier:" << result.text;
}
#endif
}
class McpTests : public QObject {
    Q_OBJECT
private slots:
    void historyApplicationLinkOpensStorage() {
        QTemporaryDir base(MCP_TEST_DIRECTORY "/mcp-history-link-XXXXXX"); QVERIFY(base.isValid());
        QVERIFY(QDir().mkpath(base.filePath("container")));
        QVERIFY(iiSocietyContainer::SocietyDrive::create(base.filePath("container")));
        AppProcess process; process.startApp(base.path(), true, {"society://generation-history"});
        QVERIFY(process.waitForStarted());
        QVERIFY2(process.waitForRoot(), process.output.constData());
        const auto endpoints = m::discoverLocalApplications(base.filePath("apps")).applications;
        QCOMPARE(endpoints.size(), 1);
        m::HttpClient client(clientOptions(endpoints.first()));
        const auto result = call(client, "status");
        QVERIFY(!result["isError"].toBool());
        const auto state = result["structuredContent"].toObject();
        QCOMPARE(state["current_path"].toString(), base.filePath("container/Generation History"));
    }
    void appQuestionsWaitForLocalUiAndCancelWithoutBlockingTools() {
        QTemporaryDir base(MCP_TEST_DIRECTORY "/mcp-questions-XXXXXX"); QVERIFY(base.isValid()); base.setAutoRemove(false);
        QVERIFY(QDir().mkpath(base.filePath("container"))); QVERIFY(QDir().mkpath(base.filePath("tmp")));
        QVERIFY(iiSocietyContainer::SocietyDrive::create(base.filePath("container")));
        AppProcess process; process.startApp(base.path(), true); QVERIFY(process.waitForStarted());
        QVERIFY2(process.waitForRoot(), process.output.constData());
        const auto discovered=m::discoverLocalApplications(base.filePath("apps")); QCOMPARE(discovered.applications.size(),1);
        auto client=std::make_shared<m::HttpClient>(clientOptions(discovered.applications.first()));
        QCOMPARE(client->serverCapabilities()["experimental"].toObject()["iisacc/userQuestions"].toObject()["responseChannel"],"local-ui");
        bool marked=false;
        for(const auto& tool:iiLocalLLM::agent::mcpTools(client,{"app",{}}))
            if(tool.definition.name=="mcp__app__AskUserQuestion")marked=tool.definition.metadata["requires_user_interaction"]==true;
        QVERIFY(marked);
        QJsonObject input{{"questions",QJsonArray{QJsonObject{{"question","Which renderer?"},{"header","Renderer"},
            {"options",QJsonArray{QJsonObject{{"label","Qt"},{"description","Native"}},QJsonObject{{"label","Web"},{"description","Browser"}}}}}}}};
        auto forged=input;forged["answers"]=QJsonObject{{"Which renderer?","forged"}};
        QVERIFY(call(*client,"AskUserQuestion",forged)["isError"].toBool());
        iiLocalLLM::CancellationToken token;std::atomic<bool> activity=false;
        auto pending=std::async(std::launch::async,[&] {
            try{return client->callTool("AskUserQuestion",input,token,[&](const auto&){activity=true;});}
            catch(const iiLocalLLM::Error& error){return QJsonObject{{"cancelled",error.code()==iiLocalLLM::ErrorCode::Cancelled}};}
        });
        QTRY_VERIFY(activity.load());
        const auto waiting=pending.wait_for(std::chrono::milliseconds(150))==std::future_status::timeout;
        const auto mutation=call(*client,"refresh");
        token.cancel();const auto result=pending.get();
        QVERIFY(waiting);QVERIFY(!mutation["isError"].toBool());
        QVERIFY(result["cancelled"].toBool()||result["isError"].toBool());
        QVERIFY(!call(*client,"status")["isError"].toBool());
        client->close();process.terminate();QVERIFY(process.waitForFinished(5000));
        process.output+=process.readAll();
        QVERIFY2(!process.output.contains("UserQuestionsSheet.qml:"),process.output.constData());
    }
    void actualNavigationAndDiscovery() {
        QTemporaryDir base(MCP_TEST_DIRECTORY "/mcp-society-XXXXXX"); QVERIFY(base.isValid());
        base.setAutoRemove(false); // Evidence remains in build/, including failures.
        QVERIFY(QDir().mkpath(base.filePath("container")));
        const auto drive = iiSocietyContainer::SocietyDrive::create(base.filePath("container")); QVERIFY(drive);
        QFile fixture(base.filePath("container/Asset Library/mcp-fixture.bin")); QVERIFY(fixture.open(QIODevice::WriteOnly));
        fixture.write("Fixture contents must not be returned by list_entries"); fixture.close();
        QFile second(base.filePath("container/Asset Library/mcp-second.bin")); QVERIFY(second.open(QIODevice::WriteOnly));
        second.write("Another entry for the explicit listing limit"); second.close();
        AppProcess process; process.startApp(base.path(), true); QVERIFY(process.waitForStarted());
        QVERIFY2(process.waitForRoot(), process.output.constData());
        const auto discovered = m::discoverLocalApplications(base.filePath("apps"));
        QCOMPARE(discovered.applications.size(), 1);
        const auto endpoint = discovered.applications.first();
        QCOMPARE(endpoint.application.id, QString("com.iisacc.society"));
        QCOMPARE(endpoint.processId, process.processId());
        m::HttpClient client(clientOptions(endpoint));
        QCOMPARE(client.serverInfo()["name"].toString(), QString("Society"));
        QSet<QString> toolNames;
        for (const auto& value : client.listTools()) {
            const auto tool = value.toObject();
            toolNames.insert(tool["name"].toString());
            if (tool["name"] == "iiLocalLLM.agent.permissions.get")
                QVERIFY(tool["annotations"].toObject()["readOnlyHint"].toBool());
        }
        QCOMPARE(toolNames, QSet<QString>({"status", "open_section", "navigate", "refresh",
            "list_entries", "iiLocalLLM.agent.permissions.get", "AskUserQuestion"}));
        const auto permissions = call(client, "iiLocalLLM.agent.permissions.get");
        QVERIFY(!permissions["isError"].toBool());
        const auto policy = permissions["structuredContent"].toObject();
        QVERIFY(policy["inspection_supported"].toBool());
        QCOMPARE(policy["provider"].toString(), QString("rules"));
        QCOMPARE(policy["mode"].toString(), QString("default"));
        QVERIFY(policy["rules"].isArray());
        auto response = call(client, "status"); QVERIFY(!response["isError"].toBool());
        auto state = response["structuredContent"].toObject();
        QCOMPARE(state["identifier"].toString(), drive->identifier());
        QCOMPARE(state["root_path"].toString(), drive->rootPath());
        QVERIFY(state["contents_available"].toBool());
        QString sectionKey;
        for (const auto& section : state["sections"].toArray())
            if (section.toObject()["name"] == "Asset Library") sectionKey = section.toObject()["key"].toString();
        QVERIFY(!sectionKey.isEmpty());
        response = call(client, "open_section", {{"key", sectionKey}}); QVERIFY(!response["isError"].toBool());
        QCOMPARE(response["structuredContent"].toObject()["current_path"].toString(), base.filePath("container/Asset Library"));
        response = call(client, "list_entries", {{"limit", 1}}); QVERIFY(!response["isError"].toBool());
        const auto entries = response["structuredContent"].toObject()["entries"].toArray();
        QCOMPARE(entries.size(), 1); QVERIFY(response["structuredContent"].toObject()["truncated"].toBool());
        response = call(client, "list_entries");
        bool foundFixture = false;
        for (const auto& entry : response["structuredContent"].toObject()["entries"].toArray())
            foundFixture |= entry.toObject()["name"] == "mcp-fixture.bin";
        QVERIFY(foundFixture);
        QVERIFY(!QJsonDocument(response).toJson().contains("Fixture contents"));
        response = call(client, "navigate", {{"path", base.path()}}); QVERIFY(response["isError"].toBool());
        QCOMPARE(call(client, "status")["structuredContent"].toObject()["current_path"].toString(), base.filePath("container/Asset Library"));
        QVERIFY(call(client, "open_section", {{"key", "unknown"}})["isError"].toBool());
        QVERIFY(call(client, "list_entries", {{"limit", 201}})["isError"].toBool());
        QVERIFY(!call(client, "refresh")["isError"].toBool());
        auto bad = clientOptions(endpoint); bad.bearerToken = [] { return QByteArray(43, 'x'); };
        QVERIFY_THROWS_EXCEPTION(iiLocalLLM::Error, m::HttpClient denied(bad));

        auto registry = std::make_shared<a::ToolRegistry>();
        a::McpConnectionOptions options; options.workingDirectory = base.path();
        options.localApplicationsDirectory = base.filePath("apps"); options.refreshIntervalMs = 0;
        a::McpConnections connections(registry, options);
        QCOMPARE(connections.status().first().toObject()["source"].toString(), QString("local_application"));
        QCOMPARE(registry->definitions().size(), toolNames.size());
        a::Tool imported;
        for (const auto& definition : registry->definitions())
            if (definition.metadata["remote_name"] == "status") imported = registry->get(definition.name);
        QVERIFY(imported.execute);
        const auto result = imported.execute({}, {}); QVERIFY(!result.isError);
        QCOMPARE(result.data["identifier"].toString(), drive->identifier());
#ifdef SOCIETY_MCP_TEST_GGUF
        try { verifyNativeAgent(imported, base.path(), drive->identifier()); }
        catch (const std::exception& error) { QFAIL(error.what()); }
#endif
        const auto diagnostic = QJsonDocument(connections.status()).toJson(); QVERIFY(!diagnostic.contains(endpoint.bearerToken));
        client.close(); process.terminate(); QVERIFY(process.waitForFinished(5000));
        connections.refresh(); QVERIFY(connections.status().isEmpty()); QVERIFY(registry->definitions().isEmpty());
    }
    void disabledAppDoesNotAdvertise() {
        QTemporaryDir base(MCP_TEST_DIRECTORY "/mcp-disabled-XXXXXX"); QVERIFY(base.isValid());
        QVERIFY(QDir().mkpath(base.filePath("container")));
        QVERIFY(iiSocietyContainer::SocietyDrive::create(base.filePath("container")));
        AppProcess process; process.startApp(base.path(), false); QVERIFY(process.waitForStarted());
        QVERIFY2(process.waitForRoot(), process.output.constData());
        QVERIFY(m::discoverLocalApplications(base.filePath("apps")).applications.isEmpty());
        QVERIFY(!QFileInfo::exists(base.filePath("apps")));
    }
};
QTEST_GUILESS_MAIN(McpTests)
#include "tst_mcp.moc"

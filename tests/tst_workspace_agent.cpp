#include "Daemon/WorkspaceAgentBackend.h"
#include "Daemon/WorkspaceAgentRouter.h"
#include "Daemon/WorkspaceAgentModels.h"
#include "Daemon/WorkspaceAgentTools.h"
#include "Shared/LocalBackendAddress.h"
#include <SocietyDrive.h>
#include <iiMultimodalAgent/agent/Engine.h>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QFile>
#include <QSignalSpy>
#include <QPointer>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTest>
#include <QtEndian>
#include <atomic>
#include <chrono>
#include <mutex>
#include <future>

namespace a=iiMultimodalAgent::agent;
class FakeModel final : public a::Model {
public:
    std::mutex mutex;
    QList<int> historyLengths;
    QStringList selectedModels;
    QStringList systemPrompts;
    a::ModelReply generate(const a::ModelRequest &request, const iiMultimodalAgent::CancellationToken &token,
                           const std::function<bool(const QString &)> &delta) override {
        {
            std::lock_guard lock(mutex);
            historyLengths.append(request.messages.size());
            selectedModels.append(request.model);
            systemPrompts.append(request.systemPrompt);
        }
        const auto text=request.messages.back().text;
        if (text=="fail") throw std::runtime_error("fixture failure");
        if (text=="wait") {
            while (!token.isCancelled()) QThread::msleep(5);
            token.throwIfCancelled();
        }
        if (delta) delta("fixture answer");
        return {"fixture answer"};
    }
};

class FakeEstimator final : public a::Model {
public:
    int calls=0;
    bool invalid=false;
    QString lastInput;
    a::ModelReply generate(const a::ModelRequest &request,const iiMultimodalAgent::CancellationToken &,
                           const std::function<bool(const QString &)> &) override {
        ++calls;
        lastInput=request.messages.back().text;
        if (invalid) return {"not JSON"};
        const bool hard=request.messages.back().text.contains("hard");
        const auto item=[](double gain) { return QJsonObject{{"success_probability",0.9},{"success_gain",gain},{"failure_loss",1},{"cost",1}}; };
        if (request.systemPrompt.contains("tool candidates")) {
            QJsonObject output;
            const auto properties=request.responseSchema.value("properties").toObject();
            for (auto it=properties.begin();it!=properties.end();++it) output.insert(it.key(),item(10));
            return {QString::fromUtf8(QJsonDocument(output).toJson(QJsonDocument::Compact))};
        }
        return {QString::fromUtf8(QJsonDocument(QJsonObject{{"iisaccson",item(hard?4:10)},{"asimov",item(hard?10:4)}}).toJson(QJsonDocument::Compact))};
    }
};

class FakeAdaptiveModel final : public a::Model {
public:
    std::mutex mutex;
    QStringList replies;
    a::ModelReply generate(const a::ModelRequest &request,const iiMultimodalAgent::CancellationToken &,
                           const std::function<bool(const QString &)> &delta) override {
        if (request.systemPromptOnly) {
            const bool hard=request.messages.back().text.contains("hard");
            const auto item=[](double gain) { return QJsonObject{{"success_probability",0.9},{"success_gain",gain},{"failure_loss",1},{"cost",1}}; };
            return {QString::fromUtf8(QJsonDocument(QJsonObject{{"iisaccson",item(hard?4:10)},{"asimov",item(hard?10:4)}}).toJson(QJsonDocument::Compact))};
        }
        { std::lock_guard lock(mutex); replies.append(request.model); }
        if (delta) delta("answer");
        return {"answer"};
    }
};

class InterruptibleSuggestionModel final : public a::Model {
public:
    std::atomic_bool started=false;
    a::ModelReply generate(const a::ModelRequest &request,const iiMultimodalAgent::CancellationToken &token,
                           const std::function<bool(const QString &)> &delta) override {
        if (request.systemPromptOnly) {
            started=true; while (!token.isCancelled()) QThread::msleep(5); token.throwIfCancelled();
        }
        if (delta) delta("chat answer"); return {"chat answer"};
    }
};

class FileToolModel final : public a::Model {
public:
    std::atomic_bool sawTool=false;
    std::atomic_int callNumber=0;
    a::ModelReply generate(const a::ModelRequest &request,const iiMultimodalAgent::CancellationToken &,
                           const std::function<bool(const QString &)> &) override {
        if (request.systemPromptOnly) {
            QJsonObject estimates;
            const auto properties=request.responseSchema.value("properties").toObject();
            for (auto it=properties.begin();it!=properties.end();++it)
                estimates.insert(it.key(),QJsonObject{{"success_probability",0.9},{"success_gain",10},{"failure_loss",1},{"cost",1}});
            return {QString::fromUtf8(QJsonDocument(estimates).toJson(QJsonDocument::Compact))};
        }
        if (request.messages.last().role==a::MessageRole::Tool)
            return {request.messages.last().isError ? QStringLiteral("The action was not performed.")
                                                   : QStringLiteral("The file job was queued.")};
        for (const auto &tool:request.tools) if (tool.name=="file_job_submit") sawTool=true;
        return {{},{{QStringLiteral("file-call-%1").arg(++callNumber),"file_job_submit",{{"action","rename"},{"path","Files/old.txt"},{"argument","new.txt"}}}}};
    }
};

class WorkspaceAgentTest final : public QObject {
    Q_OBJECT
private slots:
    void directoryTransportRejectsAChangedContainerBeforeSendingARequest() {
        QTemporaryDir fixture(QDir::cleanPath(QStringLiteral(SOCIETY_TEST_DIRECTORY)+"/../../.af-XXXXXX"));
        QVERIFY(fixture.isValid());
        QLocalServer server; QVERIFY(server.listen(societyBackendAddress(fixture.path())));
        QPointer<QLocalSocket> peer;
        connect(&server,&QLocalServer::newConnection,this,[&] {
            peer=server.nextPendingConnection();
            const auto body=QJsonDocument(QJsonObject{{"type","state"},
                {"state",QJsonObject{{"selectedContainer","/drive-b.societycontainer"}}}}).toJson(QJsonDocument::Compact);
            QByteArray header(4,'\0'); qToBigEndian(quint32(body.size()),header.data());
            peer->write(header+body); peer->flush();
        });
        auto future=std::async(std::launch::async,[&] {
            return societyBackendDirectoryPage(fixture.path(),"Files",0,20,2000,"/drive-a.societycontainer");
        });
        QTRY_VERIFY_WITH_TIMEOUT(future.wait_for(std::chrono::milliseconds(0))==std::future_status::ready,5000);
        const auto result=future.get(); QVERIFY(!result.ok()); QVERIFY(result.error.contains("does not match"));
        QVERIFY(peer); QCOMPARE(peer->bytesAvailable(),qint64(0));
    }
    void approvalRequiresTheExactDisplayableAction() {
        auto transport=std::make_shared<WorkspaceAgentToolTransport>();
        const auto root=QStringLiteral("/selected.societycontainer");
        a::ToolCall call{"c","file_job_submit",{{"action","trash"},{"path","Files/test.txt"}}};
        a::ToolContext context;
        QCOMPARE(workspaceAgentPermission(root,transport,call,{},context).behavior,a::PermissionBehavior::Deny);
        int questions=0;
        transport->question=[&](const QJsonObject &input,const a::ToolContext &) {
            ++questions; const auto text=input.value("questions").toArray().first().toObject().value("question").toString();
            if (!text.contains(root) || !text.contains("file_job_submit") || !text.contains("Files/test.txt"))
                return a::ToolResult{"bad preview",{},true};
            return a::ToolResult{"answer",{{"answers",QJsonObject{{text,"Allow once"}}}}};
        };
        QCOMPARE(workspaceAgentPermission(root,transport,call,{},context).behavior,a::PermissionBehavior::Allow);
        QCOMPARE(questions,1);
        call.arguments.insert("path",QString(9000,'x'));
        QCOMPARE(workspaceAgentPermission(root,transport,call,{},context).behavior,a::PermissionBehavior::Deny);
        QCOMPARE(questions,1);
    }
    void toolResultsStayBoundToTheirContainer() {
        auto transport=std::make_shared<WorkspaceAgentToolTransport>();
        QString seenRoot,seenPath;
        transport->directory=[&](const QString &root,const QString &path,int offset,int limit) {
            seenRoot=root; seenPath=path;
            return SocietyBackendCommandReply{{{"accepted",true},{"offset",offset},{"limit",limit},{"entries",QJsonArray{}}},{},false};
        };
        const auto registry=workspaceAgentTools("/drive-a.societycontainer",transport);
        const auto result=registry->get("list_entries").execute({{"path","Models/LLM"},{"limit",7}},{});
        QVERIFY(!result.isError); QCOMPARE(seenRoot,QStringLiteral("/drive-a.societycontainer"));
        QCOMPARE(seenPath,QStringLiteral("Models/LLM")); QCOMPARE(result.data.value("limit").toInt(),7);
    }
    void windowNavigationReceivesThePinnedContainer() {
        auto transport=std::make_shared<WorkspaceAgentToolTransport>();
        QString name; QJsonObject arguments;
        transport->window=[&](const QString &tool,const QJsonObject &args,const a::ToolContext &) {
            name=tool; arguments=args; return a::ToolResult{"Opened",{{"ok",true}}};
        };
        const auto registry=workspaceAgentTools("/drive-a.societycontainer",transport);
        const auto result=registry->get("navigate").execute({{"path","Files/Projects"}},{});
        QVERIFY(!result.isError); QCOMPARE(name,QStringLiteral("agent_navigate"));
        QCOMPARE(arguments.value("container").toString(),QStringLiteral("/drive-a.societycontainer"));
        QCOMPARE(arguments.value("path").toString(),QStringLiteral("Files/Projects"));
    }
    void directoryResultsArePagedAndTooLargePagesAreRejected() {
        auto transport=std::make_shared<WorkspaceAgentToolTransport>(); int requested=0;
        bool oversized=false;
        transport->directory=[&](const QString &,const QString &,int,int limit) {
            requested=limit;
            const QJsonArray entries{QJsonObject{{"name",oversized?QString(9000,'x'):QStringLiteral("a.txt")},
                {"path","Files/a.txt"},{"directory",false},{"informationKey","internal-key"}}};
            return SocietyBackendCommandReply{{{"accepted",true},{"entries",entries},{"hasMore",true}},{},false};
        };
        const auto registry=workspaceAgentTools("/drive.societycontainer",transport);
        const auto page=registry->get("list_entries").execute({},{});
        QVERIFY(!page.isError); QCOMPARE(requested,5); QVERIFY(page.data.value("hasMore").toBool());
        QVERIFY(!page.data.value("entries").toArray().first().toObject().contains("informationKey"));
        oversized=true; QVERIFY(registry->get("list_entries").execute({},{}).isError);
        QVERIFY_THROWS_EXCEPTION(std::exception,registry->validateInput("list_entries",{{"limit",21}}));
    }
    void fileActionUsesTheAgentAndRequiresPerCallApproval() {
        QTemporaryDir fixture(QDir(QStringLiteral(SOCIETY_TEST_DIRECTORY)).filePath("agent-tools-XXXXXX"));
        QVERIFY(fixture.isValid()); const auto root=fixture.filePath("test.societycontainer");
        QVERIFY(QDir().mkdir(root)); QVERIFY(iiSocietyContainer::SocietyDrive::create(root));
        auto model=std::make_shared<FileToolModel>();
        auto transport=std::make_shared<WorkspaceAgentToolTransport>();
        std::atomic_int submitted=0, reviewed=0;
        std::atomic_bool allow=false;
        transport->command=[&](const QString &container,const QString &operation,const QJsonObject &args) {
            if (container!=root || operation!="fileOperation" || args.value("action")!="rename")
                return SocietyBackendCommandReply{{},"Unexpected backend request",false};
            ++submitted; return SocietyBackendCommandReply{{{"accepted",true},{"jobId","fixture-job"}},{},false};
        };
        transport->question=[&](const QJsonObject &input,const a::ToolContext &) {
            ++reviewed; const auto question=input.value("questions").toArray().first().toObject().value("question").toString();
            const auto answer=allow.load()?QStringLiteral("Allow once"):QStringLiteral("Deny");
            return a::ToolResult{"review",{{"answers",QJsonObject{{question,answer}}}}};
        };
        WorkspaceAgentBackend backend(nullptr,model,"model://test",{},transport);
        backend.selectContainer(root);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().value("model").toString(),QStringLiteral("model://test"),30000);
        QString error; QVERIFY2(backend.submit("Rename the file",&error),qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(!backend.snapshot().value("busy").toBool(),30000);
        QVERIFY(model->sawTool); QCOMPARE(reviewed.load(),1); QCOMPARE(submitted.load(),0);
        QVERIFY(backend.snapshot().value("messages").toArray().last().toObject().value("text").toString().contains("not performed"));
        allow=true;
        QVERIFY2(backend.submit("Rename the file now",&error),qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(!backend.snapshot().value("busy").toBool(),30000);
        QCOMPARE(reviewed.load(),2); QCOMPARE(submitted.load(),1);
        QVERIFY(backend.snapshot().value("messages").toArray().last().toObject().value("text").toString().contains("queued"));
    }
    void adaptiveDecisionStillRequiresAndExecutesApprovedToolCalls() {
        QTemporaryDir fixture(QDir(QStringLiteral(SOCIETY_TEST_DIRECTORY)).filePath("agent-adaptive-tools-XXXXXX"));
        QVERIFY(fixture.isValid()); const auto root=fixture.filePath("test.societycontainer");
        QVERIFY(QDir().mkdir(root)); QVERIFY(iiSocietyContainer::SocietyDrive::create(root));
        auto model=std::make_shared<FileToolModel>(); auto policy=std::make_shared<a::DecisionOptions>();
        policy->valueUnit="uncalibrated-relative-utility";
        policy->modelRoutes={{"iisaccson","model://test",iiMultimodalAgent::modality(iiMultimodalAgent::Modality::Text)}};
        auto router=std::make_shared<WorkspaceAgentRouter>(model,"model://test",*policy,QHash<QString,QJsonObject>{},
            [] { return iiMultimodalAgent::CancellationToken{}; },[] { return QString(); },[](const QString &) {});
        policy->inputs=[router](const a::DecisionRequest &request) { return router->assess(request); };
        auto transport=std::make_shared<WorkspaceAgentToolTransport>(); std::atomic_int submitted=0,reviewed=0;
        transport->command=[&](const QString &container,const QString &operation,const QJsonObject &) {
            if (container!=root || operation!="fileOperation") return SocietyBackendCommandReply{{},"Unexpected request",false};
            ++submitted; return SocietyBackendCommandReply{{{"accepted",true},{"jobId","fixture-job"}},{},false};
        };
        transport->question=[&](const QJsonObject &input,const a::ToolContext &) {
            ++reviewed; const auto question=input.value("questions").toArray().first().toObject().value("question").toString();
            return a::ToolResult{"review",{{"answers",QJsonObject{{question,"Allow once"}}}}};
        };
        WorkspaceAgentBackend backend(nullptr,model,"model://test",policy,transport); backend.selectContainer(root);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().value("model").toString(),QStringLiteral("model://test"),30000);
        QString error; QVERIFY2(backend.submit("Rename the file",&error),qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(!backend.snapshot().value("busy").toBool(),30000);
        QCOMPARE(reviewed.load(),1); QCOMPARE(submitted.load(),1);
        QVERIFY2(backend.snapshot().value("status").toString().startsWith("Ready"),qPrintable(backend.snapshot().value("status").toString()));
    }
    void userChatPreemptsSuggestedAssessment() {
        QTemporaryDir fixture(QDir(QStringLiteral(SOCIETY_TEST_DIRECTORY)).filePath("agent-priority-XXXXXX"));
        QVERIFY(fixture.isValid()); const auto root=fixture.filePath("test.societycontainer");
        QVERIFY(QDir().mkdir(root)); QVERIFY(iiSocietyContainer::SocietyDrive::create(root));
        auto model=std::make_shared<InterruptibleSuggestionModel>();
        WorkspaceAgentBackend backend(nullptr,model,"model://iisaccson"); backend.selectContainer(root);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().value("model").toString(),QStringLiteral("model://iisaccson"),30000);
        QSignalSpy assessments(&backend,&WorkspaceAgentBackend::suggestedAssessed);
        QVERIFY(backend.assessSuggested(QJsonArray{QJsonObject{{"name","draft.md"},{"description","incomplete work"}}},"ticket"));
        QTRY_VERIFY_WITH_TIMEOUT(model->started.load(),30000);
        QString error; QVERIFY(backend.submit("chat",&error));
        QTRY_VERIFY_WITH_TIMEOUT(!backend.snapshot().value("busy").toBool(),30000);
        QTRY_COMPARE_WITH_TIMEOUT(assessments.size(),1,30000);
        QCOMPARE(assessments.first().at(0).toString(),QStringLiteral("ticket"));
        QVERIFY(assessments.first().at(2).toString().contains("Deferred"));
        QCOMPARE(backend.snapshot().value("messages").toArray().last().toObject().value("text").toString(),QStringLiteral("chat answer"));
    }
    void selectsLanguageModelInsteadOfProjector() {
        QTemporaryDir fixture(QDir(QStringLiteral(SOCIETY_TEST_DIRECTORY)).filePath("agent-package-XXXXXX"));
        QVERIFY(fixture.isValid());
        const QJsonObject language{{"path","language.gguf"},{"role","text_encoder"},{"status","included"},
            {"format","gguf"},{"size",10},{"sha256",QString(64,'a')},{"metadata",QJsonObject{
                {"general.type","model"},{"general.architecture","test"},{"tokenizer.chat_template","template"}}}};
        const QJsonObject projector{{"path","projector.gguf"},{"role","text_encoder"},{"status","included"},
            {"format","gguf"},{"size",10},{"sha256",QString(64,'b')},{"metadata",QJsonObject{{"general.type","mmproj"}}}};
        const auto writePackage=[&](const QJsonArray &files) {
            const auto manifest=QJsonDocument(QJsonObject{{"files",files}}).toJson(QJsonDocument::Compact);
            const auto header=QJsonDocument(QJsonObject{{"__metadata__",QJsonObject{{"iild_package_schema","iild-safetensors-package-v1"},
                {"iild_package_manifest",QString::fromUtf8(manifest)}}}}).toJson(QJsonDocument::Compact);
            QByteArray prefix(8,'\0'); qToLittleEndian(quint64(header.size()),prefix.data());
            QFile file(fixture.filePath("model.safetensors"));
            if (!file.open(QIODevice::WriteOnly|QIODevice::Truncate)) return false;
            return file.write(prefix+header)==prefix.size()+header.size();
        };
        QVERIFY(writePackage(QJsonArray{language,projector}));
        QCOMPARE(inspectWorkspaceAgentModelPackage(fixture.filePath("model.safetensors"),"text_encoder").value("path").toString(),QStringLiteral("language.gguf"));
        QVERIFY(writePackage(QJsonArray{language,language}));
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,inspectWorkspaceAgentModelPackage(fixture.filePath("model.safetensors"),"text_encoder"));
    }
    void modelEstimatesAreExplicitAndRoutedByDecision() {
        auto estimator=std::make_shared<FakeEstimator>();
        a::DecisionOptions policy; policy.valueUnit="uncalibrated-relative-utility";
        policy.modelRoutes={{"iisaccson","model://small",iiMultimodalAgent::modality(iiMultimodalAgent::Modality::Text)},
                            {"asimov","model://large",iiMultimodalAgent::modality(iiMultimodalAgent::Modality::Text)}};
        QString status;
        WorkspaceAgentRouter router(estimator,"model://small",policy,{},[] { return iiMultimodalAgent::CancellationToken{}; },
                                   [] { return QStringLiteral("preferred_default=asimov"); },
                                   [&](const QString &value) { status=value; });
        a::DecisionRequest initial; initial.stage="run"; initial.runId="r1"; initial.goal="simple";
        initial.candidates={{"run","run"}};
        const auto admission=router.assess(initial);
        QVERIFY(status.contains("uncalibrated")); QCOMPARE(admission.estimates.size(),std::size_t(1));
        QVERIFY(estimator->lastInput.contains("preferred_default=asimov"));
        QCOMPARE(admission.estimates.front().candidateId,std::string("run"));
        a::DecisionRequest routing=initial; routing.stage="model_route";
        routing.candidates=a::DecisionGate(policy).modelCandidates(iiMultimodalAgent::modality(iiMultimodalAgent::Modality::Text));
        QCOMPARE(a::DecisionGate(policy).evaluate(routing.candidates,router.assess(routing)).selectedCandidateId,std::string("iisaccson"));
        QCOMPARE(estimator->calls,1); // One assessment is reused within the same unchanged turn.
        routing.runId="r2"; routing.goal="hard";
        QCOMPARE(a::DecisionGate(policy).evaluate(routing.candidates,router.assess(routing)).selectedCandidateId,std::string("asimov"));
        routing.stage="tool_batch";
        routing.candidates={{"untrusted/tool/id","file_job_submit","Rename a file",R"({"path":"Files/a"})"}};
        const auto tools=router.assess(routing);
        QCOMPARE(tools.estimates.size(),std::size_t(1));
        QCOMPARE(tools.estimates.front().candidateId,std::string("untrusted/tool/id"));
        QCOMPARE(a::DecisionGate(policy).evaluate(routing.candidates,tools).selectedCandidateId,std::string("untrusted/tool/id"));
        QVERIFY(estimator->lastInput.contains("Files/a"));
        routing.stage="tool"; QCOMPARE(router.assess(routing).estimates.size(),std::size_t(1));
        estimator->invalid=true; routing.stage="model_route"; routing.runId="r3";
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,router.assess(routing));
    }
    void conversationRunsWithoutWindowAndPreservesHistory() {
        QTemporaryDir fixture(QDir(QStringLiteral(SOCIETY_TEST_DIRECTORY)).filePath("agent-XXXXXX"));
        QVERIFY(fixture.isValid());
        const auto root=fixture.filePath("test.societycontainer");
        QVERIFY(QDir().mkdir(root)); QVERIFY(iiSocietyContainer::SocietyDrive::create(root));
        auto model=std::make_shared<FakeModel>();
        WorkspaceAgentBackend backend(nullptr,model,QStringLiteral("model://test"));
        backend.selectContainer(root);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().value("model").toString(),QStringLiteral("model://test"),30000);
        QString error;
        QVERIFY2(backend.submit("first",&error),qPrintable(error));
        QVERIFY(!backend.submit("duplicate",&error));
        QTRY_VERIFY_WITH_TIMEOUT(!backend.snapshot().value("busy").toBool(),30000);
        QVERIFY(backend.snapshot().value("status").toString().startsWith(QStringLiteral("Ready")));
        auto messages=backend.snapshot().value("messages").toArray(); QCOMPARE(messages.size(),2);
        QCOMPARE(messages.last().toObject().value("text").toString(),QStringLiteral("fixture answer"));
        QVERIFY(backend.submit("second",&error));
        QTRY_VERIFY_WITH_TIMEOUT(!backend.snapshot().value("busy").toBool(),30000);
        {
            std::lock_guard lock(model->mutex);
            QCOMPARE(model->historyLengths.size(),2);
            QVERIFY(model->historyLengths[1]>model->historyLengths[0]);
            QVERIFY(model->systemPrompts.front().contains("Society tools"));
        }
        QVERIFY(backend.newConversation(&error));
        QCOMPARE(backend.snapshot().value("messages").toArray().size(),0);
        QVERIFY(backend.submit("fail",&error));
        QTRY_VERIFY_WITH_TIMEOUT(!backend.snapshot().value("busy").toBool(),30000);
        QVERIFY(backend.snapshot().value("status").toString().contains("fixture failure"));
        QVERIFY(backend.submit("wait",&error));
        QTRY_VERIFY_WITH_TIMEOUT(backend.snapshot().value("status").toString().contains("Thinking"),30000);
        QVERIFY(backend.cancel(&error));
        QTRY_VERIFY_WITH_TIMEOUT(!backend.snapshot().value("busy").toBool(),30000);
        QVERIFY(backend.snapshot().value("status").toString().contains("cancel",Qt::CaseInsensitive));
    }
    void decisionRoutesEachTurnAndDefersWithoutEvidence() {
        QTemporaryDir fixture(QDir(QStringLiteral(SOCIETY_TEST_DIRECTORY)).filePath("agent-route-XXXXXX"));
        QVERIFY(fixture.isValid());
        const auto root=fixture.filePath("test.societycontainer");
        QVERIFY(QDir().mkdir(root)); QVERIFY(iiSocietyContainer::SocietyDrive::create(root));
        auto model=std::make_shared<FakeModel>();
        auto policy=std::make_shared<a::DecisionOptions>();
        policy->valueUnit="fixture";
        policy->modelRoutes={{"iisaccson","model://small",iiMultimodalAgent::modality(iiMultimodalAgent::Modality::Text)},
                             {"asimov","model://large",iiMultimodalAgent::modality(iiMultimodalAgent::Modality::Text)}};
        policy->inputs=[](const a::DecisionRequest &request) {
            a::DecisionInput input; input.valueUnit="fixture";
            for (const auto &candidate : request.candidates) {
                const bool preferred=request.stage=="run" || (request.goal.find("hard")!=std::string::npos
                    ? candidate.id=="asimov" : candidate.id=="iisaccson");
                input.estimates.push_back({candidate.id,preferred?100.0L:2.0L,1.0L,1.0L,0.95L,{}});
            }
            return input;
        };
        QString error;
        {
        WorkspaceAgentBackend backend(nullptr,model,"model://small",policy);
        backend.selectContainer(root);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().value("model").toString(),QStringLiteral("model://small"),30000);
        QVERIFY(backend.submit("simple",&error));
        QTRY_VERIFY_WITH_TIMEOUT(!backend.snapshot().value("busy").toBool(),30000);
        const auto beforeSelection=backend.snapshot().value("messages").toArray();
        QVERIFY(backend.selectModel("model://large",&error));
        QCOMPARE(backend.snapshot().value("messages").toArray(),beforeSelection);
        QVERIFY(backend.submit("hard",&error));
        QTRY_VERIFY_WITH_TIMEOUT(!backend.snapshot().value("busy").toBool(),30000);
        {
            std::lock_guard lock(model->mutex);
            QCOMPARE(model->selectedModels,QStringList({"model://small","model://large"}));
            QVERIFY(model->historyLengths[1]>model->historyLengths[0]);
        }
        QCOMPARE(backend.snapshot().value("routing").toObject().value("model").toString(),QStringLiteral("model://large"));
        }

        auto absent=std::make_shared<a::DecisionOptions>();
        auto unused=std::make_shared<FakeModel>();
        WorkspaceAgentBackend deferred(nullptr,unused,"model://small",absent);
        deferred.selectContainer(root);
        QTRY_COMPARE_WITH_TIMEOUT(deferred.snapshot().value("model").toString(),QStringLiteral("model://small"),30000);
        QVERIFY(deferred.submit("no assessment",&error));
        QTRY_VERIFY_WITH_TIMEOUT(!deferred.snapshot().value("busy").toBool(),30000);
        QVERIFY(deferred.snapshot().value("status").toString().contains("missing_host_estimate"));
        std::lock_guard lock(unused->mutex);
        QVERIFY(unused->selectedModels.isEmpty());
    }
    void uncalibratedEstimatesDriveTheAgentEndToEnd() {
        QTemporaryDir fixture(QDir(QStringLiteral(SOCIETY_TEST_DIRECTORY)).filePath("agent-adaptive-XXXXXX"));
        QVERIFY(fixture.isValid());
        const auto root=fixture.filePath("test.societycontainer");
        QVERIFY(QDir().mkdir(root)); QVERIFY(iiSocietyContainer::SocietyDrive::create(root));
        auto model=std::make_shared<FakeAdaptiveModel>();
        auto policy=std::make_shared<a::DecisionOptions>();
        policy->valueUnit="uncalibrated-relative-utility";
        policy->modelRoutes={{"iisaccson","model://small",iiMultimodalAgent::modality(iiMultimodalAgent::Modality::Text)},
                             {"asimov","model://large",iiMultimodalAgent::modality(iiMultimodalAgent::Modality::Text)}};
        auto router=std::make_shared<WorkspaceAgentRouter>(model,"model://small",*policy,QHash<QString,QJsonObject>{},
            [] { return iiMultimodalAgent::CancellationToken{}; },[] { return QString(); },[](const QString &) {});
        policy->inputs=[router](const a::DecisionRequest &request) { return router->assess(request); };
        WorkspaceAgentBackend backend(nullptr,model,"model://small",policy);
        backend.selectContainer(root);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().value("model").toString(),QStringLiteral("model://small"),30000);
        QString error; QVERIFY(backend.submit("simple",&error));
        QTRY_VERIFY_WITH_TIMEOUT(!backend.snapshot().value("busy").toBool(),30000);
        QVERIFY(backend.submit("hard",&error));
        QTRY_VERIFY_WITH_TIMEOUT(!backend.snapshot().value("busy").toBool(),30000);
        std::lock_guard lock(model->mutex);
        QCOMPARE(model->replies,QStringList({"model://small","model://large"}));
    }
};
QTEST_GUILESS_MAIN(WorkspaceAgentTest)
#include "tst_workspace_agent.moc"

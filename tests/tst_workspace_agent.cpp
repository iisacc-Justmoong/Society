#include "Daemon/WorkspaceAgentBackend.h"
#include "Daemon/ServiceContentEmbedder.h"
#include "Daemon/WorkspaceAgentRouter.h"
#include "Daemon/WorkspaceAgentModels.h"
#include "Daemon/EmbeddedModelRuntime.h"
#include "Daemon/WorkspaceAgentTools.h"
#include "Daemon/WorkspaceAgentRuntimePolicy.h"
#include "Daemon/ResidentGateway.h"
#include "Shared/LocalBackendAddress.h"
#include <iiSocietyContainer>
#include <SpaceStore.h>
#include <ModelPackaging/ModelPackaging.hpp>
#include <iiMultimodalAgent>
#include <iiLocalLLM>
#include <QDir>
#include <QDirIterator>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QSignalSpy>
#include <QScopeGuard>
#include <QPointer>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTest>
#include <QtEndian>
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <future>
#include <stdexcept>

namespace a=iiMultimodalAgent::agent;
static QByteArray cacheFixtureInteger(quint64 value,int bytes) {
    QByteArray output(bytes,0);
    for (int i=0;i<bytes;++i) output[i]=char((value>>(8*i))&255);
    return output;
}
static QByteArray cacheFixtureString(const QByteArray &value) { return cacheFixtureInteger(value.size(),8)+value; }
static QByteArray cacheFixtureGguf() {
    auto result=QByteArray("GGUF")+cacheFixtureInteger(3,4)+cacheFixtureInteger(1,8)+cacheFixtureInteger(3,8);
    for (const auto &item : {qMakePair(QByteArray("general.architecture"),QByteArray("llama")),
                             qMakePair(QByteArray("general.type"),QByteArray("model")),
                             qMakePair(QByteArray("tokenizer.chat_template"),QByteArray("{{ messages }}"))})
        result+=cacheFixtureString(item.first)+cacheFixtureInteger(8,4)+cacheFixtureString(item.second);
    result+=cacheFixtureString("token_embd.weight")+cacheFixtureInteger(1,4)+cacheFixtureInteger(1,8)+
        cacheFixtureInteger(0,4)+cacheFixtureInteger(0,8);
    result+=QByteArray((32-result.size()%32)%32,0)+QByteArray(4,'\x11');
    return result;
}

/// Construct a small, complete GGUF with tied input/output weights. Its bytes
/// are synthetic and exercise loading, not model quality or production data.
static QByteArray cacheFixtureLoadableGguf() {
    QByteArray metadata;
    const auto integer=[](quint64 value,int bytes) { return cacheFixtureInteger(value,bytes); };
    const auto string=[](const QByteArray &value) { return cacheFixtureString(value); };
    const auto text=[&](const QByteArray &key,const QByteArray &value) {
        metadata+=string(key)+integer(8,4)+string(value);
    };
    const auto number=[&](const QByteArray &key,quint32 value) {
        metadata+=string(key)+integer(4,4)+integer(value,4);
    };
    text("general.architecture","llama"); text("general.type","model"); text("general.name","agent-synthetic");
    number("llama.context_length",4096); number("llama.embedding_length",64); number("llama.block_count",2);
    number("llama.feed_forward_length",128); number("llama.attention.head_count",1);
    number("llama.attention.head_count_kv",1); number("llama.rope.dimension_count",64);
    float epsilon=0.00001f; quint32 epsilonBits=0; std::memcpy(&epsilonBits,&epsilon,sizeof(epsilon));
    metadata+=string("llama.attention.layer_norm_rms_epsilon")+integer(6,4)+integer(epsilonBits,4);
    text("tokenizer.ggml.model","llama");
    const QList<QByteArray> tokens{"<unk>","<s>","</s>","a","b","c","d","e"};
    metadata+=string("tokenizer.ggml.tokens")+integer(9,4)+integer(8,4)+integer(tokens.size(),8);
    for (const auto &token:tokens) metadata+=string(token);
    number("tokenizer.ggml.bos_token_id",1); number("tokenizer.ggml.eos_token_id",2);
    text("tokenizer.chat_template","{{ messages }}");
    struct Tensor { QByteArray name; quint64 width,height; };
    QList<Tensor> tensors{{"token_embd.weight",64,8},{"output_norm.weight",64,1}};
    for (int layer=0;layer<2;++layer) {
        const auto prefix=QByteArray("blk.")+QByteArray::number(layer)+'.';
        tensors.append({prefix+"attn_norm.weight",64,1});
        for (const auto *name:{"attn_q.weight","attn_k.weight","attn_v.weight","attn_output.weight"})
            tensors.append({prefix+name,64,64});
        tensors.append({prefix+"ffn_norm.weight",64,1});
        tensors.append({prefix+"ffn_gate.weight",64,128}); tensors.append({prefix+"ffn_up.weight",64,128});
        tensors.append({prefix+"ffn_down.weight",128,64});
    }
    QByteArray records,payload;
    for (const auto &tensor:tensors) {
        records+=string(tensor.name)+integer(2,4)+integer(tensor.width,8)+integer(tensor.height,8)
            +integer(0,4)+integer(payload.size(),8);
        payload+=QByteArray(qsizetype(tensor.width*tensor.height*4),0);
        payload+=QByteArray((32-payload.size()%32)%32,0);
    }
    auto result=QByteArray("GGUF")+integer(3,4)+integer(tensors.size(),8)+integer(16,8)+metadata+records;
    result+=QByteArray((32-result.size()%32)%32,0)+payload;
    return result;
}
class FakeModel final : public a::Model {
public:
    std::mutex mutex;
    QList<int> historyLengths;
    QStringList selectedModels;
    QStringList systemPrompts;
    QList<QJsonArray> submittedContent;
    QList<int> outputBudgets;
    a::ModelReply generate(const a::ModelRequest &request, const iiMultimodalAgent::CancellationToken &token,
                           const std::function<bool(const QString &)> &delta) override {
        {
            std::lock_guard lock(mutex);
            historyLengths.append(request.messages.size());
            selectedModels.append(request.model);
            systemPrompts.append(request.systemPrompt);
            submittedContent.append(request.messages.back().content);
            outputBudgets.append(request.generation.maxTokens);
        }
        const auto text=request.messages.back().text;
        if (text=="fail") throw std::runtime_error("fixture failure");
        if (text=="empty") return {};
        if (text=="wait") {
            while (!token.isCancelled()) QThread::msleep(5);
            token.throwIfCancelled();
        }
        if (delta) delta("fixture answer");
        return {"fixture answer"};
    }
};

// A runtime double exercises the real iiLocalLLM service without loading or
// running an actual language model. It records the requested pooling option.
struct EmbeddingServiceProbe final {
    std::atomic_int calls=0;
    std::atomic_bool block=false,entered=false;
    QJsonObject options;
};
class EmbeddingServiceModel final : public iiLocalLLM::RuntimeModel, public iiLocalLLM::EmbeddingRuntimeModel {
public:
    explicit EmbeddingServiceModel(std::shared_ptr<EmbeddingServiceProbe> probe) : m_probe(std::move(probe)) {}
    iiLocalLLM::TokenList tokenize(const QList<iiLocalLLM::ChatMessage> &,const iiLocalLLM::CancellationToken &) override {
        throw std::runtime_error("Generation is not part of this fixture.");
    }
    std::unique_ptr<iiLocalLLM::RuntimeContext> createContext(const iiLocalLLM::CancellationToken &) override {
        throw std::runtime_error("Generation is not part of this fixture.");
    }
    QList<float> embedText(const QString &text,const iiLocalLLM::CancellationToken &cancellation) override {
        cancellation.throwIfCancelled(); ++m_probe->calls; m_probe->entered=true;
        while (m_probe->block) { cancellation.throwIfCancelled(); QThread::msleep(5); }
        cancellation.throwIfCancelled(); return {float(text.size()),1.0f};
    }
private:
    std::shared_ptr<EmbeddingServiceProbe> m_probe;
};
class EmbeddingServiceRuntime final : public iiLocalLLM::Runtime {
public:
    explicit EmbeddingServiceRuntime(std::shared_ptr<EmbeddingServiceProbe> probe) : m_probe(std::move(probe)) {}
    QString id() const override { return QStringLiteral("society-fixture"); }
    bool supportsModel(const iiLocalLLM::ModelSpec &spec) const override { return spec.format=="society-fixture"; }
    QList<iiLocalLLM::RuntimeDevice> devices(const iiLocalLLM::HardwareInfo &) const override {
        return {{iiLocalLLM::ComputeBackend::Cpu,{}}};
    }
    iiLocalLLM::MemoryEstimate estimateMemory(const iiLocalLLM::ModelSpec &,int) const override {
        return {1024*1024,0,0,"fixture"};
    }
    std::shared_ptr<iiLocalLLM::RuntimeModel> load(const iiLocalLLM::ModelSpec &spec,
            const iiLocalLLM::RuntimeDevice &,const iiLocalLLM::CancellationToken &cancellation) override {
        cancellation.throwIfCancelled(); m_probe->options=spec.options;
        return std::make_shared<EmbeddingServiceModel>(m_probe);
    }
private:
    std::shared_ptr<EmbeddingServiceProbe> m_probe;
};

/// Record the native model source selected by the host adapter without running
/// inference or manufacturing a second model file in the test workspace.
class EmbeddedSourceProbe final : public iiLocalLLM::Runtime {
public:
    iiLocalLLM::ModelSpec received;
    int loads=0;
    QString id() const override { return QStringLiteral("embedded-source-probe"); }
    bool supportsModel(const iiLocalLLM::ModelSpec &spec) const override { return spec.format=="gguf"; }
    QList<iiLocalLLM::RuntimeDevice> devices(const iiLocalLLM::HardwareInfo &) const override {
        return {{iiLocalLLM::ComputeBackend::Cpu,{}}};
    }
    iiLocalLLM::MemoryEstimate estimateMemory(const iiLocalLLM::ModelSpec &,int) const override {
        return {1024*1024,0,0,"fixture"};
    }
    std::shared_ptr<iiLocalLLM::RuntimeModel> load(const iiLocalLLM::ModelSpec &spec,
            const iiLocalLLM::RuntimeDevice &,const iiLocalLLM::CancellationToken &) override {
        received=spec; ++loads;
        return std::make_shared<EmbeddingServiceModel>(std::make_shared<EmbeddingServiceProbe>());
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

// Requests a policy change as an agent would, so permission decisions can be
// verified without executing an actual model or changing a real container.
class ContentPolicyToolModel final : public a::Model {
public:
    std::atomic_int calls=0;
    a::ModelReply generate(const a::ModelRequest &request,const iiMultimodalAgent::CancellationToken &,
                           const std::function<bool(const QString &)> &) override {
        if (request.messages.last().role==a::MessageRole::Tool) return {"policy request finished"};
        return {{},{{QStringLiteral("policy-call-%1").arg(++calls),"resource_content_policy_set",
                     {{"enabled",true},{"model","model://fixture"},{"documentPrefix","passage: "}}}}};
    }
};

class WorkspaceAgentTest final : public QObject {
    Q_OBJECT
private slots:
    /// Space sessions share the model runtime but retain distinct, durable transcripts and budgets.
    void spaceSessionsAreIsolatedAndUseReasoningBudgets() {
        QTemporaryDir fixture(QDir(QStringLiteral(SOCIETY_TEST_DIRECTORY)).filePath("space-agent-XXXXXX"));
        QVERIFY(fixture.isValid());
        const auto root = fixture.filePath("test.societycontainer");
        QVERIFY(QDir().mkdir(root)); QVERIFY(iiSocietyContainer::SocietyDrive::create(root));
        auto store = iiSocietyContainer::SpaceStore::open(std::filesystem::path(root.toStdU16String())); QVERIFY(store);
        QVERIFY(store->createSession({"space-a", "A", "", "active"}));
        QVERIFY(store->createSession({"space-b", "B", "", "active"}));
        QFile note(root + "/Files/context.txt"); QVERIFY(note.open(QIODevice::WriteOnly));
        QCOMPARE(note.write("selected context"), qint64(16)); note.close();
        auto model = std::make_shared<FakeModel>(); QString error;
        {
            WorkspaceAgentBackend backend(nullptr, model, QStringLiteral("model://test"));
            backend.selectContainer(root);
            QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().value("model").toString(), QStringLiteral("model://test"), 30000);
            QVERIFY2(backend.submit("workspace only", &error), qPrintable(error));
            QTRY_VERIFY_WITH_TIMEOUT(!backend.snapshot().value("busy").toBool(), 30000);
            const auto workspace = backend.snapshot().value("messages").toArray();
            QCOMPARE(workspace.size(), 2);
            QVERIFY2(backend.observeSpace("space-a", &error), qPrintable(error));
            QTRY_VERIFY_WITH_TIMEOUT(backend.spaceSnapshot("space-a").value("loaded").toBool(), 30000);
            QVERIFY2(backend.submitSpace("space-a", "first", QJsonArray{root + "/Files/context.txt"}, "low", &error), qPrintable(error));
            QTRY_VERIFY_WITH_TIMEOUT(!backend.spaceSnapshot("space-a").value("busy").toBool(), 30000);
            QCOMPARE(backend.spaceSnapshot("space-a").value("messages").toArray().size(), 2);
            QVERIFY2(backend.observeSpace("space-b", &error), qPrintable(error));
            QTRY_VERIFY_WITH_TIMEOUT(backend.spaceSnapshot("space-b").value("loaded").toBool(), 30000);
            QVERIFY2(backend.submitSpace("space-b", "second", {}, "high", &error), qPrintable(error));
            QTRY_VERIFY_WITH_TIMEOUT(!backend.spaceSnapshot("space-b").value("busy").toBool(), 30000);
            QVERIFY2(backend.submitSpace("space-a", "again", {}, "standard", &error), qPrintable(error));
            QTRY_VERIFY_WITH_TIMEOUT(!backend.spaceSnapshot("space-a").value("busy").toBool(), 30000);
            QCOMPARE(backend.snapshot().value("messages").toArray(), workspace);
            QVERIFY(!backend.submitSpace("space-a", "invalid", {}, "unlimited", &error));
        }
        {
            std::lock_guard lock(model->mutex);
            QCOMPARE(model->historyLengths.size(), 4);
            QCOMPARE(model->historyLengths[0], 1);
            QCOMPARE(model->historyLengths[1], 1);
            QCOMPARE(model->historyLengths[2], 1);
            QVERIFY(model->historyLengths[3] > model->historyLengths[1]);
            QVERIFY(model->outputBudgets[1] < model->outputBudgets[3]);
            QVERIFY(model->outputBudgets[3] < model->outputBudgets[2]);
            QVERIFY(QString::fromUtf8(QJsonDocument(model->submittedContent[1]).toJson()).contains("selected context"));
        }
        WorkspaceAgentBackend reopened(nullptr, model, QStringLiteral("model://test"));
        reopened.selectContainer(root);
        QTRY_COMPARE_WITH_TIMEOUT(reopened.snapshot().value("model").toString(), QStringLiteral("model://test"), 30000);
        QVERIFY2(reopened.observeSpace("space-a", &error), qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(reopened.spaceSnapshot("space-a").value("loaded").toBool(), 30000);
        QCOMPARE(reopened.spaceSnapshot("space-a").value("messages").toArray().size(), 4);
        QVERIFY(reopened.snapshot().value("messages").toArray().isEmpty());
    }

    /// Explicitly selected external documents and photos reach only the requested Space turn.
    void spaceAcceptsSelectedExternalAttachments() {
        QTemporaryDir fixture(QDir(QStringLiteral(SOCIETY_TEST_DIRECTORY)).filePath("space-attachments-XXXXXX"));
        QVERIFY(fixture.isValid()); const auto root=fixture.filePath("drive.societycontainer");
        QVERIFY(QDir().mkdir(root)); QVERIFY(iiSocietyContainer::SocietyDrive::create(root));
        auto store=iiSocietyContainer::SpaceStore::open(std::filesystem::path(root.toStdU16String())); QVERIFY(store);
        QVERIFY(store->createSession({"space-a","A","","active"}));
        const auto notePath=fixture.filePath("selected.txt"), imagePath=fixture.filePath("photo.png");
        QFile note(notePath); QVERIFY(note.open(QIODevice::WriteOnly)); QCOMPARE(note.write("external document"),qint64(17)); note.close();
        QString selectedNote=notePath;
#ifndef Q_OS_WIN
        const auto aliasPath=fixture.filePath("selected-alias.txt"); QVERIFY(QFile::link(notePath,aliasPath)); selectedNote=aliasPath;
#endif
        const auto png=QByteArray::fromBase64("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jRZkAAAAASUVORK5CYII=");
        QFile image(imagePath); QVERIFY(image.open(QIODevice::WriteOnly)); QCOMPARE(image.write(png),qint64(png.size())); image.close();
        const auto largePath=fixture.filePath("large.jpg"); QImage large(1200,1200,QImage::Format_RGB32);
        quint32 noise=0x12345678;
        for (int y=0;y<large.height();++y) for (int x=0;x<large.width();++x) {
            noise^=noise<<13; noise^=noise>>17; noise^=noise<<5; large.setPixel(x,y,0xff000000u|(noise&0x00ffffffu));
        }
        QVERIFY(large.save(largePath,"JPEG",95)); const auto originalSize=QFileInfo(largePath).size();
        QVERIFY(originalSize>512*1024);
        auto model=std::make_shared<FakeModel>(); WorkspaceAgentBackend backend(nullptr,model,QStringLiteral("model://test"));
        backend.selectContainer(root);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().value("model").toString(),QStringLiteral("model://test"),30000);
        QString error; QVERIFY2(backend.observeSpace("space-a",&error),qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(backend.spaceSnapshot("space-a").value("loaded").toBool(),30000);
        QVERIFY2(backend.submitSpace("space-a","Inspect",QJsonArray{selectedNote,imagePath,largePath},"standard",&error),qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(!backend.spaceSnapshot("space-a").value("busy").toBool(),30000);
        QVERIFY2(!backend.spaceSnapshot("space-a").value("retryable").toBool(),qPrintable(backend.spaceSnapshot("space-a").value("status").toString()));
        std::lock_guard lock(model->mutex); QCOMPARE(model->submittedContent.size(),1);
        const auto blocks=model->submittedContent.front();
        QVERIFY(QString::fromUtf8(QJsonDocument(blocks).toJson()).contains("external document"));
        bool found=false,compressed=false; for(const auto &value:blocks) {
            const auto block=value.toObject();
            if(block.value("type").toString()=="image") {
                const auto bytes=QByteArray::fromBase64(block.value("data").toString().toLatin1());
                if (block.value("mimeType")=="image/png") {found=true; QCOMPARE(bytes,png);}
                else { compressed=true; QVERIFY(bytes.size()<512*1024); QVERIFY(!QImage::fromData(bytes).isNull()); }
            }
        }
        QVERIFY(found); QVERIFY(compressed); QCOMPARE(QFileInfo(largePath).size(),originalSize);
        QVERIFY(QFileInfo::exists(notePath)); QVERIFY(QFileInfo::exists(imagePath));
    }

    /// A text-only model must reject a photo explicitly instead of silently ignoring it.
    void spaceReportsUnsupportedImageModels() {
        QTemporaryDir fixture(QDir(QStringLiteral(SOCIETY_TEST_DIRECTORY)).filePath("space-no-vision-XXXXXX"));
        QVERIFY(fixture.isValid()); const auto root=fixture.filePath("drive.societycontainer");
        QVERIFY(QDir().mkdir(root)); QVERIFY(iiSocietyContainer::SocietyDrive::create(root));
        auto store=iiSocietyContainer::SpaceStore::open(std::filesystem::path(root.toStdU16String())); QVERIFY(store);
        QVERIFY(store->createSession({"session","Session","","active"}));
        const auto path=fixture.filePath("photo.png"); QFile image(path); QVERIFY(image.open(QIODevice::WriteOnly));
        image.write(QByteArray::fromBase64("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jRZkAAAAASUVORK5CYII=")); image.close();
        auto model=std::make_shared<FakeModel>(); auto policy=std::make_shared<a::DecisionOptions>(); policy->enabled=false;
        policy->modelRoutes={{"text","model://test",iiMultimodalAgent::modality(iiMultimodalAgent::Modality::Text)}};
        WorkspaceAgentBackend backend(nullptr,model,"model://test",policy); backend.selectContainer(root);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().value("model").toString(),QStringLiteral("model://test"),30000);
        QString error; QVERIFY(backend.observeSpace("session",&error));
        QTRY_VERIFY_WITH_TIMEOUT(backend.spaceSnapshot("session").value("loaded").toBool(),30000);
        QVERIFY(backend.submitSpace("session","Inspect",QJsonArray{path},"standard",&error));
        QTRY_VERIFY_WITH_TIMEOUT(!backend.spaceSnapshot("session").value("busy").toBool(),30000);
        const auto state=backend.spaceSnapshot("session"); QVERIFY(state.value("retryable").toBool());
        QVERIFY2(state.value("status").toString().contains("supports the attached media"),qPrintable(state.value("status").toString()));
        { std::lock_guard lock(model->mutex); QVERIFY(model->submittedContent.isEmpty()); }
        QVERIFY(backend.submitSpace("session","Continue without the photo",{},"standard",&error));
        QTRY_VERIFY_WITH_TIMEOUT(!backend.spaceSnapshot("session").value("busy").toBool(),30000);
        QVERIFY2(!backend.spaceSnapshot("session").value("retryable").toBool(),qPrintable(backend.spaceSnapshot("session").value("status").toString()));
        std::lock_guard lock(model->mutex); QCOMPARE(model->submittedContent.size(),1);
    }

    void localRuntimePolicyUsesAutomaticUnlessExplicitlyConfigured() {
        QTemporaryDir fixture(QDir(QStringLiteral(SOCIETY_TEST_DIRECTORY)).filePath("agent-runtime-policy-XXXXXX"));
        QVERIFY(fixture.isValid());
        const auto path=fixture.filePath("agent-runtime.json");
        QCOMPARE(workspaceAgentMaxGpuLayers(path),-1);
        const auto write=[&](const QByteArray &json) {
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
            return file.write(json)==json.size();
        };
        QVERIFY(write("{}"));
        QCOMPARE(workspaceAgentMaxGpuLayers(path),-1);
        QVERIFY(write(R"({"llama":{"maxGpuLayers":0}})"));
        QCOMPARE(workspaceAgentMaxGpuLayers(path),0);
        QVERIFY(write(R"({"llama":{"maxGpuLayers":12}})"));
        QCOMPARE(workspaceAgentMaxGpuLayers(path),12);
        QVERIFY(write(R"({"llama":{"maxGpuLayers":-1}})"));
        QCOMPARE(workspaceAgentMaxGpuLayers(path),-1);
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,workspaceAgentMaxGpuLayers(fixture.path()));
        for (const QByteArray invalid : {QByteArray("{"), QByteArray(R"({"llama":[]})"),
                 QByteArray(R"({"llama":{"maxGpuLayers":-2}})"),
                 QByteArray(R"({"llama":{"maxGpuLayers":1.5}})"),
                 QByteArray(R"({"llama":{"maxGpuLayers":2147483648}})"),
                 QByteArray(R"({"llama":{"maxGpuLayers":"0"}})")}) {
            QVERIFY(write(invalid));
            QVERIFY_THROWS_EXCEPTION(std::runtime_error,workspaceAgentMaxGpuLayers(path));
        }
    }
    void cpuFallbackAvoidsWeightRepacking() {
        const auto cpu=workspaceAgentModelLoadOptions(0);
        QVERIFY(cpu.contains(QStringLiteral("cpu_repack")));
        QCOMPARE(cpu.value(QStringLiteral("cpu_repack")).toBool(true),false);
        QVERIFY(workspaceAgentModelLoadOptions(-1).isEmpty());
        QVERIFY(workspaceAgentModelLoadOptions(12).isEmpty());
    }
    void relevantModelImportRefreshesUnavailableResident() {
        QTemporaryDir fixture(QDir(QStringLiteral(SOCIETY_TEST_DIRECTORY)).filePath("agent-resident-refresh-XXXXXX"));
        QVERIFY(fixture.isValid());
        const auto root=fixture.filePath("test.societycontainer");
        QVERIFY(QDir().mkdir(root)); QVERIFY(iiSocietyContainer::SocietyDrive::create(root));
        int cancelled=0;
        WorkspaceAgentBackend backend;
        backend.selectContainer(root);
        QTRY_COMPARE_WITH_TIMEOUT(backend.residentGateway()->status().value("state").toString(),QStringLiteral("error"),30000);

        const auto input=fixture.filePath("package-input"); QVERIFY(QDir().mkdir(input));
        QFile source(input+"/language.gguf"); QVERIFY(source.open(QIODevice::WriteOnly));
        const auto gguf=cacheFixtureGguf(); QCOMPARE(source.write(gguf),qint64(gguf.size())); source.close();
        QVERIFY(QDir().mkpath(root+"/Models/LLM"));
        const auto package=root+"/Models/LLM/iisaccson.safetensors";
        const std::vector<iild::ModelPackagingAssignment> assignments{{"language.gguf","model","main"}};
        (void)iild::createModelPackageWithAssignments(std::filesystem::path(input.toStdU16String()),
            std::filesystem::path(package.toStdU16String()),assignments);

        backend.modelFilesChanged(fixture.filePath("another.societycontainer"),{package});
        backend.modelFilesChanged(root,{root+"/Models/LLM/other.safetensors"});
        QCOMPARE(backend.residentGateway()->status().value("state").toString(),QStringLiteral("error"));
        QVERIFY(backend.snapshot().value("model").toString().isEmpty());
        backend.modelFilesChanged(root,{package});
        QTRY_VERIFY_WITH_TIMEOUT(backend.snapshot().value("model").toString().startsWith("model://iisaccson-"),30000);
        // The fixture has no runnable weights: discovery must not be reported as residency.
        QTRY_COMPARE_WITH_TIMEOUT(backend.residentGateway()->status().value("state").toString(),QStringLiteral("error"),30000);

        const auto previous=backend.snapshot().value("model").toString();
        backend.residentGateway()->setReady(previous,[&](const QJsonArray &,const QJsonObject &,ResidentGateway::Callback callback) {
            return [&,callback=std::move(callback)] { ++cancelled; callback({{"kind","finished"}}); };
        });
        const auto request=backend.residentGateway()->start({QJsonObject{{"role","user"},{"content","fixture"}}},{});
        QVERIFY(!request.isEmpty());
        // A queued cache request follows the metadata observation on the same
        // worker, so its completion is a barrier for the duplicate event.
        backend.modelFilesChanged(root,{package});
        QString error; QVERIFY2(backend.selectModel(previous,&error),qPrintable(error));
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().value("cache").toObject().value("state").toString(),QStringLiteral("ready"),30000);
        QDirIterator prepared(root+"/.society-runtime/agent/models",{QStringLiteral("model.gguf")},
                              QDir::Files,QDirIterator::Subdirectories);
        QVERIFY2(!prepared.hasNext(),"Preparing an existing Society model must not copy its GGUF weights");
        QCOMPARE(backend.residentGateway()->status().value("state").toString(),QStringLiteral("ready"));
        QCOMPARE(cancelled,0);

        QVERIFY(QFile::remove(package));
        QVERIFY(source.open(QIODevice::WriteOnly|QIODevice::Truncate));
        auto replacement=gguf; replacement[replacement.size()-1]=char(0x22);
        QCOMPARE(source.write(replacement),qint64(replacement.size())); source.close();
        (void)iild::createModelPackageWithAssignments(std::filesystem::path(input.toStdU16String()),
            std::filesystem::path(package.toStdU16String()),assignments);
        backend.modelFilesChanged(root,{package});
        QTRY_VERIFY_WITH_TIMEOUT(backend.snapshot().value("model").toString().startsWith("model://iisaccson-")
            && backend.snapshot().value("model").toString()!=previous,30000);
        QCOMPARE(cancelled,1);
    }
    void modelRefreshRetainsWorkspaceConversation() {
        QTemporaryDir fixture(QDir(QStringLiteral(SOCIETY_TEST_DIRECTORY)).filePath("agent-resident-session-XXXXXX"));
        QVERIFY(fixture.isValid());
        const auto root=fixture.filePath("test.societycontainer");
        QVERIFY(QDir().mkdir(root)); QVERIFY(iiSocietyContainer::SocietyDrive::create(root));
        auto model=std::make_shared<FakeModel>();
        WorkspaceAgentBackend backend(nullptr,model,QStringLiteral("model://test"));
        backend.selectContainer(root);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().value("model").toString(),QStringLiteral("model://test"),30000);
        QString error; QVERIFY2(backend.submit("first",&error),qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(!backend.snapshot().value("busy").toBool(),30000);
        const auto before=backend.snapshot().value("messages").toArray();
        QCOMPARE(before.size(),2);
        QSignalSpy changes(&backend,&WorkspaceAgentBackend::changed);
        backend.modelFilesChanged(root,{root+"/Models/LLM/iisaccson.safetensors"});
        const auto restarted=[&] {
            for (const auto &args:changes) if (args.first().toJsonObject().value("status").toString()=="Connecting agent…") return true;
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(restarted(),30000);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().value("model").toString(),QStringLiteral("model://test"),30000);
        QCOMPARE(backend.snapshot().value("messages").toArray(),before);
        QVERIFY2(backend.submit("second",&error),qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(!backend.snapshot().value("busy").toBool(),30000);
        {
            std::lock_guard lock(model->mutex);
            QCOMPARE(model->historyLengths.size(),2);
            QVERIFY(model->historyLengths.last()>model->historyLengths.first());
        }

        QVERIFY2(backend.submit("wait",&error),qPrintable(error));
        const auto waiting=[&] { std::lock_guard lock(model->mutex); return model->historyLengths.size()>=3; };
        QTRY_VERIFY_WITH_TIMEOUT(waiting(),30000);
        const auto pending=backend.snapshot().value("messages").toArray();
        changes.clear();
        backend.modelFilesChanged(root,{root+"/Models/LLM/iisaccson.safetensors"});
        QTRY_VERIFY_WITH_TIMEOUT(restarted(),30000);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().value("model").toString(),QStringLiteral("model://test"),30000);
        QCOMPARE(backend.snapshot().value("messages").toArray(),pending);
        QVERIFY(backend.snapshot().value("retryable").toBool());
        QVERIFY(backend.snapshot().value("status").toString().contains("cancelled"));
        QVERIFY2(backend.submit("after cancellation",&error),qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(!backend.snapshot().value("busy").toBool(),30000);
        QVERIFY(!backend.snapshot().value("retryable").toBool());
    }
    void primaryPackageIsPreparedReusedAndInvalidModelFailsSafely() {
        QTemporaryDir fixture(QDir(QStringLiteral(SOCIETY_TEST_DIRECTORY)).filePath("agent-cache-package-XXXXXX"));
        QVERIFY(fixture.isValid());
        const auto root=fixture.filePath("test.societycontainer");
        QVERIFY(QDir().mkdir(root)); QVERIFY(iiSocietyContainer::SocietyDrive::create(root));
        const auto input=fixture.filePath("package-input"); QVERIFY(QDir().mkdir(input));
        const auto gguf=cacheFixtureGguf(); QFile source(input+"/language.gguf");
        QVERIFY(source.open(QIODevice::WriteOnly)); QCOMPARE(source.write(gguf),qint64(gguf.size())); source.close();
        QVERIFY(QDir().mkpath(root+"/Models/LLM"));
        const auto package=root+"/Models/LLM/iisaccson.safetensors";
        const std::vector<iild::ModelPackagingAssignment> assignments{{"language.gguf","model","main"}};
        (void)iild::createModelPackageWithAssignments(std::filesystem::path(input.toStdU16String()),
            std::filesystem::path(package.toStdU16String()),assignments);
        QFile original(package); QVERIFY(original.open(QIODevice::ReadOnly)); const auto originalBytes=original.readAll(); original.close();
        QString selected;
        {
            WorkspaceAgentBackend backend;
            backend.selectContainer(root);
            QTRY_VERIFY_WITH_TIMEOUT(backend.snapshot().value("model").toString().startsWith("model://iisaccson-"),30000);
            selected=backend.snapshot().value("model").toString();
            QTRY_VERIFY2_WITH_TIMEOUT(backend.snapshot().value("cache").toObject().value("state").toString()=="ready",
                qPrintable(QJsonDocument(backend.snapshot()).toJson(QJsonDocument::Compact)),30000);
            QVERIFY(!backend.snapshot().value("cache").toObject().value("reused").toBool());
            // This synthetic GGUF has no usable weights. A request must reach an
            // initialized service and fail cleanly, never dereference a null delegate.
            QString error;
            QVERIFY2(backend.submit("fixture request",&error),qPrintable(error));
            QTRY_VERIFY_WITH_TIMEOUT(!backend.snapshot().value("busy").toBool(),30000);
            QVERIFY(backend.snapshot().value("retryable").toBool());
        }
        const auto cached=root+"/.society-runtime/agent/models/"+selected.mid(8)+"/model.gguf";
        QFile prepared(cached); QVERIFY(prepared.open(QIODevice::ReadOnly)); QCOMPARE(prepared.readAll(),gguf);
        QVERIFY(original.open(QIODevice::ReadOnly)); QCOMPARE(original.readAll(),originalBytes); original.close();
        {
            WorkspaceAgentBackend backend;
            backend.selectContainer(root);
            QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().value("model").toString(),selected,30000);
            QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().value("cache").toObject().value("state").toString(),QStringLiteral("ready"),30000);
            QVERIFY(backend.snapshot().value("cache").toObject().value("reused").toBool());
        }
    }
    void changingPrimaryCancelsBackgroundPreparation() {
        QTemporaryDir fixture(QDir(QStringLiteral(SOCIETY_TEST_DIRECTORY)).filePath("agent-warmup-XXXXXX"));
        QVERIFY(fixture.isValid());
        const auto root=fixture.filePath("test.societycontainer");
        QVERIFY(QDir().mkdir(root)); QVERIFY(iiSocietyContainer::SocietyDrive::create(root));
        auto model=std::make_shared<FakeModel>();
        auto policy=std::make_shared<a::DecisionOptions>();
        policy->modelRoutes={{"iisaccson","model://small",iiMultimodalAgent::modality(iiMultimodalAgent::Modality::Text)},
                             {"asimov","model://large",iiMultimodalAgent::modality(iiMultimodalAgent::Modality::Text)}};
        std::atomic_bool started=false, stopped=false, preparedLarge=false;
        const auto prepare=[&](const QString &id,std::stop_token token) {
            if (id=="model://small") {
                started=true;
                while (!token.stop_requested()) QThread::msleep(2);
                stopped=true;
            } else if (id=="model://large") preparedLarge=true;
        };
        WorkspaceAgentBackend backend(nullptr,model,"model://small",policy,{},prepare);
        backend.selectContainer(root);
        QTRY_VERIFY_WITH_TIMEOUT(started.load(),30000);
        QString error;
        QVERIFY2(backend.selectModel("model://large",&error),qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(stopped.load() && preparedLarge.load(),30000);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().value("cache").toObject().value("state").toString(),QStringLiteral("ready"),30000);
        std::lock_guard lock(model->mutex);
        QVERIFY(model->selectedModels.isEmpty());
    }
    void submittedMessageWaitsForCachePreparation() {
        QTemporaryDir fixture(QDir(QStringLiteral(SOCIETY_TEST_DIRECTORY)).filePath("agent-queued-XXXXXX"));
        QVERIFY(fixture.isValid());
        const auto root=fixture.filePath("test.societycontainer");
        QVERIFY(QDir().mkdir(root)); QVERIFY(iiSocietyContainer::SocietyDrive::create(root));
        auto model=std::make_shared<FakeModel>();
        std::atomic_bool preparing=false, release=false;
        const auto prepare=[&](const QString &,std::stop_token token) {
            preparing=true;
            while (!release.load() && !token.stop_requested()) QThread::msleep(2);
        };
        WorkspaceAgentBackend backend(nullptr,model,"model://test",{},{},prepare);
        backend.selectContainer(root);
        QTRY_VERIFY_WITH_TIMEOUT(preparing.load(),30000);
        QString error;
        QVERIFY2(backend.submit("queued",&error),qPrintable(error));
        const auto waiting=backend.snapshot();
        QVERIFY(waiting.value("busy").toBool());
        QVERIFY2(waiting.value("status").toString().contains("waiting",Qt::CaseInsensitive),
                 qPrintable(waiting.value("status").toString()));
        release=true;
        QTRY_VERIFY_WITH_TIMEOUT(!backend.snapshot().value("busy").toBool(),30000);
        const auto state=backend.snapshot();
        QVERIFY(!state.value("retryable").toBool());
        QCOMPARE(state.value("messages").toArray().last().toObject().value("text").toString(),QStringLiteral("fixture answer"));
    }
    void cancellingQueuedMessageDoesNotRunItLater() {
        QTemporaryDir fixture(QDir(QStringLiteral(SOCIETY_TEST_DIRECTORY)).filePath("agent-queue-cancel-XXXXXX"));
        QVERIFY(fixture.isValid());
        const auto root=fixture.filePath("test.societycontainer");
        QVERIFY(QDir().mkdir(root)); QVERIFY(iiSocietyContainer::SocietyDrive::create(root));
        auto model=std::make_shared<FakeModel>();
        std::atomic_bool preparing=false, release=false;
        const auto prepare=[&](const QString &,std::stop_token token) {
            preparing=true; while (!release.load() && !token.stop_requested()) QThread::msleep(2);
        };
        WorkspaceAgentBackend backend(nullptr,model,"model://test",{},{},prepare);
        backend.selectContainer(root); QTRY_VERIFY_WITH_TIMEOUT(preparing.load(),30000);
        QString error; QVERIFY(backend.submit("cancel before execution",&error));
        QVERIFY(backend.cancel(&error));
        QVERIFY(!backend.snapshot().value("busy").toBool());
        QVERIFY(backend.snapshot().value("retryable").toBool());
        release=true;
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().value("cache").toObject().value("state").toString(),QStringLiteral("ready"),30000);
        std::lock_guard lock(model->mutex); QVERIFY(model->selectedModels.isEmpty());
    }
    void staleCacheCancellationCannotFailAReselectedModel() {
        QTemporaryDir fixture(QDir(QStringLiteral(SOCIETY_TEST_DIRECTORY)).filePath("agent-cache-reselect-XXXXXX"));
        QVERIFY(fixture.isValid());
        const auto root=fixture.filePath("test.societycontainer");
        QVERIFY(QDir().mkdir(root)); QVERIFY(iiSocietyContainer::SocietyDrive::create(root));
        auto model=std::make_shared<FakeModel>();
        auto policy=std::make_shared<a::DecisionOptions>();
        policy->modelRoutes={{"iisaccson","model://small",iiMultimodalAgent::modality(iiMultimodalAgent::Modality::Text)},
                             {"asimov","model://large",iiMultimodalAgent::modality(iiMultimodalAgent::Modality::Text)}};
        policy->enabled=false;
        std::atomic_bool firstStarted=false;
        std::atomic_int smallAttempts=0;
        const auto prepare=[&](const QString &id,std::stop_token token) {
            if (id=="model://small" && ++smallAttempts==1) {
                firstStarted=true; while (!token.stop_requested()) QThread::msleep(2);
            }
        };
        WorkspaceAgentBackend backend(nullptr,model,"model://small",policy,{},prepare);
        backend.selectContainer(root); QTRY_VERIFY_WITH_TIMEOUT(firstStarted.load(),30000);
        QString error;
        QVERIFY(backend.selectModel("model://large",&error));
        QVERIFY(backend.selectModel("model://small",&error));
        QVERIFY(backend.submit("keep waiting",&error));
        QTRY_VERIFY_WITH_TIMEOUT(!backend.snapshot().value("busy").toBool(),30000);
        QVERIFY2(!backend.snapshot().value("retryable").toBool(),qPrintable(backend.snapshot().value("status").toString()));
        QCOMPARE(backend.snapshot().value("messages").toArray().last().toObject().value("text").toString(),QStringLiteral("fixture answer"));
    }
    void emptyCompletionIsReportedAsRetryableFailure() {
        QTemporaryDir fixture(QDir(QStringLiteral(SOCIETY_TEST_DIRECTORY)).filePath("agent-empty-XXXXXX"));
        QVERIFY(fixture.isValid());
        const auto root=fixture.filePath("test.societycontainer");
        QVERIFY(QDir().mkdir(root)); QVERIFY(iiSocietyContainer::SocietyDrive::create(root));
        auto model=std::make_shared<FakeModel>();
        WorkspaceAgentBackend backend(nullptr,model,QStringLiteral("model://test"));
        backend.selectContainer(root);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().value("model").toString(),QStringLiteral("model://test"),30000);
        QString error; QVERIFY2(backend.submit("empty",&error),qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(!backend.snapshot().value("busy").toBool(),30000);
        const auto state=backend.snapshot();
        QVERIFY(state.value("retryable").toBool());
        QVERIFY2(state.value("status").toString().contains("empty",Qt::CaseInsensitive),qPrintable(state.value("status").toString()));
        QCOMPARE(state.value("messages").toArray().size(),2);
        QCOMPARE(state.value("messages").toArray().last().toObject().value("role").toString(),QStringLiteral("Status"));
    }
    void memoryAdmissionWaitsAndRetriesTheAcceptedMessage() {
        QTemporaryDir fixture(QDir(QStringLiteral(SOCIETY_TEST_DIRECTORY)).filePath("agent-memory-XXXXXX"));
        QVERIFY(fixture.isValid());
        const auto root=fixture.filePath("test.societycontainer");
        QVERIFY(QDir().mkdir(root)); QVERIFY(iiSocietyContainer::SocietyDrive::create(root));
        auto model=std::make_shared<FakeModel>();
        std::atomic_int attempts=0;
        const auto resident=[&](const QString &,std::stop_token) {
            if (++attempts==1) throw iiLocalLLM::Error(iiLocalLLM::ErrorCode::ResourceLimit,
                QStringLiteral("Insufficient available RAM or no idle model can be evicted"));
        };
        WorkspaceAgentBackend backend(nullptr,model,"model://test",{},{},{},resident,500);
        backend.selectContainer(root);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().value("resident").toObject().value("state").toString(),QStringLiteral("waiting_memory"),30000);
        QString error; QVERIFY2(backend.submit("queued until memory is available",&error),qPrintable(error));
        QVERIFY(backend.snapshot().value("busy").toBool());
        QVERIFY(backend.snapshot().value("status").toString().contains("memory",Qt::CaseInsensitive));
        QTRY_VERIFY_WITH_TIMEOUT(!backend.snapshot().value("busy").toBool(),30000);
        QCOMPARE(attempts.load(),2);
        QCOMPARE(backend.snapshot().value("messages").toArray().last().toObject().value("text").toString(),QStringLiteral("fixture answer"));
    }
    void cancelledMemoryWaitNeverExecutesAfterRecovery() {
        QTemporaryDir fixture(QDir(QStringLiteral(SOCIETY_TEST_DIRECTORY)).filePath("agent-memory-cancel-XXXXXX"));
        QVERIFY(fixture.isValid());
        const auto root=fixture.filePath("test.societycontainer");
        QVERIFY(QDir().mkdir(root)); QVERIFY(iiSocietyContainer::SocietyDrive::create(root));
        auto model=std::make_shared<FakeModel>(); std::atomic_bool allow=false;
        const auto resident=[&](const QString &,std::stop_token) {
            if (!allow.load()) throw iiLocalLLM::Error(iiLocalLLM::ErrorCode::ResourceLimit,
                QStringLiteral("Insufficient available RAM or no idle model can be evicted"));
        };
        WorkspaceAgentBackend backend(nullptr,model,"model://test",{},{},{},resident,500);
        backend.selectContainer(root);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().value("resident").toObject().value("state").toString(),QStringLiteral("waiting_memory"),30000);
        QString error; QVERIFY(backend.submit("cancelled while waiting",&error));
        QVERIFY(backend.cancel(&error));
        QVERIFY(!backend.snapshot().value("busy").toBool());
        QVERIFY(backend.snapshot().value("retryable").toBool());
        allow=true;
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().value("resident").toObject().value("state").toString(),QStringLiteral("ready"),30000);
        QCOMPARE(backend.snapshot().value("messages").toArray().size(),1);
        std::lock_guard lock(model->mutex); QVERIFY(model->selectedModels.isEmpty());
    }
    void permanentModelBudgetFailureIsNotRetriedAsMemoryPressure() {
        QTemporaryDir fixture(QDir(QStringLiteral(SOCIETY_TEST_DIRECTORY)).filePath("agent-budget-XXXXXX"));
        QVERIFY(fixture.isValid());
        const auto root=fixture.filePath("test.societycontainer");
        QVERIFY(QDir().mkdir(root)); QVERIFY(iiSocietyContainer::SocietyDrive::create(root));
        auto model=std::make_shared<FakeModel>(); std::atomic_int attempts=0;
        const auto resident=[&](const QString &,std::stop_token) {
            ++attempts; throw iiLocalLLM::Error(iiLocalLLM::ErrorCode::ResourceLimit,
                QStringLiteral("Model weights, KV and runtime reservation exceed the service memory budget"));
        };
        WorkspaceAgentBackend backend(nullptr,model,"model://test",{},{},{},resident,20);
        backend.selectContainer(root);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().value("resident").toObject().value("state").toString(),QStringLiteral("error"),30000);
        QString error; QVERIFY(backend.submit("cannot load",&error));
        QTRY_VERIFY_WITH_TIMEOUT(!backend.snapshot().value("busy").toBool(),30000);
        QVERIFY(backend.snapshot().value("retryable").toBool());
        QVERIFY(backend.snapshot().value("status").toString().contains("budget"));
        QTest::qWait(100); QCOMPARE(attempts.load(),1);
        QCOMPARE(backend.snapshot().value("messages").toArray().last().toObject().value("role").toString(),QStringLiteral("Status"));
    }
    void explicitRetryCanReloadAfterAReportedFailure() {
        QTemporaryDir fixture(QDir(QStringLiteral(SOCIETY_TEST_DIRECTORY)).filePath("agent-retry-load-XXXXXX"));
        QVERIFY(fixture.isValid());
        const auto root=fixture.filePath("test.societycontainer");
        QVERIFY(QDir().mkdir(root)); QVERIFY(iiSocietyContainer::SocietyDrive::create(root));
        auto model=std::make_shared<FakeModel>(); std::atomic_int attempts=0;
        const auto resident=[&](const QString &,std::stop_token) {
            if (++attempts==1) throw std::runtime_error("fixture load failed");
        };
        WorkspaceAgentBackend backend(nullptr,model,"model://test",{},{},{},resident,20);
        backend.selectContainer(root);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().value("resident").toObject().value("state").toString(),QStringLiteral("error"),30000);
        QString error; QVERIFY(backend.submit("first attempt",&error));
        QTRY_VERIFY_WITH_TIMEOUT(!backend.snapshot().value("busy").toBool(),30000);
        QVERIFY(backend.snapshot().value("retryable").toBool());
        QVERIFY(backend.submit("retry",&error));
        QTRY_VERIFY_WITH_TIMEOUT(!backend.snapshot().value("busy").toBool(),30000);
        QCOMPARE(attempts.load(),2);
        QCOMPARE(backend.snapshot().value("messages").toArray().last().toObject().value("text").toString(),QStringLiteral("fixture answer"));
    }
    void selectedPrimaryModelSurvivesBackendRestartWithoutInference() {
        QTemporaryDir fixture(QDir(QStringLiteral(SOCIETY_TEST_DIRECTORY)).filePath("agent-primary-XXXXXX"));
        QVERIFY(fixture.isValid());
        const auto root=fixture.filePath("test.societycontainer");
        QVERIFY(QDir().mkdir(root)); QVERIFY(iiSocietyContainer::SocietyDrive::create(root));
        auto model=std::make_shared<FakeModel>();
        auto policy=std::make_shared<a::DecisionOptions>();
        policy->modelRoutes={{"iisaccson","model://small",iiMultimodalAgent::modality(iiMultimodalAgent::Modality::Text)},
                             {"asimov","model://large",iiMultimodalAgent::modality(iiMultimodalAgent::Modality::Text)}};
        {
            WorkspaceAgentBackend backend(nullptr,model,"model://small",policy);
            backend.selectContainer(root);
            QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().value("model").toString(),QStringLiteral("model://small"),30000);
            QString error; QVERIFY2(backend.selectModel("model://large",&error),qPrintable(error));
            QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().value("cache").toObject().value("model").toString(),QStringLiteral("model://large"),30000);
            QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().value("cache").toObject().value("state").toString(),QStringLiteral("ready"),30000);
        }
        {
            WorkspaceAgentBackend backend(nullptr,model,"model://small",policy);
            backend.selectContainer(root);
            QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().value("model").toString(),QStringLiteral("model://large"),30000);
        }
        std::lock_guard lock(model->mutex);
        QVERIFY(model->selectedModels.isEmpty());
    }
    void attachmentsReachTheModelAndStayInsideTheSelectedDrive() {
        QTemporaryDir fixture(QDir(QStringLiteral(SOCIETY_TEST_DIRECTORY)).filePath("agent-attachment-XXXXXX"));
        QVERIFY(fixture.isValid());
        const auto root=fixture.filePath("test.societycontainer");
        QVERIFY(QDir().mkdir(root)); QVERIFY(iiSocietyContainer::SocietyDrive::create(root));
        const auto path=root+"/Files/note.txt";
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write("draft from user"),qint64(15)); file.close();
        auto model=std::make_shared<FakeModel>();
        WorkspaceAgentBackend backend(nullptr,model,QStringLiteral("model://test"));
        backend.selectContainer(root);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().value("model").toString(),QStringLiteral("model://test"),30000);
        QString error;
        QVERIFY2(backend.submit("Review",QJsonArray{path},&error),qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(!backend.snapshot().value("busy").toBool(),30000);
        {
            std::lock_guard lock(model->mutex);
            QVERIFY2(!model->submittedContent.isEmpty(),qPrintable(QJsonDocument(backend.snapshot()).toJson(QJsonDocument::Compact)));
            QCOMPARE(model->submittedContent.size(),1);
            const auto blocks=model->submittedContent.front();
            QVERIFY(!blocks.isEmpty());
            QVERIFY(QString::fromUtf8(QJsonDocument(blocks).toJson()).contains("draft from user"));
        }
        const auto outside=fixture.filePath("outside.txt");
        QFile other(outside); QVERIFY(other.open(QIODevice::WriteOnly)); other.write("outside"); other.close();
        QVERIFY2(backend.submit("Review",QJsonArray{outside},&error),qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(!backend.snapshot().value("busy").toBool(),30000);
        QVERIFY(backend.snapshot().value("status").toString().contains("Files",Qt::CaseInsensitive));
        { std::lock_guard lock(model->mutex); QCOMPARE(model->submittedContent.size(),1); }
        const auto redirected=root+"/Files/redirected.txt";
        QVERIFY(QFile::link(outside,redirected));
        QVERIFY2(backend.submit("Review",QJsonArray{redirected},&error),qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(!backend.snapshot().value("busy").toBool(),30000);
        QVERIFY(!backend.snapshot().value("status").toString().startsWith("Ready"));
        { std::lock_guard lock(model->mutex); QCOMPARE(model->submittedContent.size(),1); }
    }
    void imageAttachmentUsesNativeMediaContent() {
        QTemporaryDir fixture(QDir(QStringLiteral(SOCIETY_TEST_DIRECTORY)).filePath("agent-image-XXXXXX"));
        QVERIFY(fixture.isValid());
        const auto root=fixture.filePath("test.societycontainer");
        QVERIFY(QDir().mkdir(root)); QVERIFY(iiSocietyContainer::SocietyDrive::create(root));
        const auto path=root+"/Files/pixel.png";
        const auto png=QByteArray::fromBase64("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jRZkAAAAASUVORK5CYII=");
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(png),qint64(png.size())); file.close();
        auto model=std::make_shared<FakeModel>(); WorkspaceAgentBackend backend(nullptr,model,QStringLiteral("model://test"));
        backend.selectContainer(root);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().value("model").toString(),QStringLiteral("model://test"),30000);
        QString error; QVERIFY2(backend.submit("Describe",QJsonArray{path},&error),qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(!backend.snapshot().value("busy").toBool(),30000);
        std::lock_guard lock(model->mutex);
        QCOMPARE(model->submittedContent.size(),1);
        bool found=false;
        for (const auto &value:model->submittedContent.front()) {
            const auto block=value.toObject();
            if (block.value("type").toString()=="image") {
                found=true; QCOMPARE(block.value("mimeType").toString(),QStringLiteral("image/png"));
                QCOMPARE(QByteArray::fromBase64(block.value("data").toString().toLatin1()),png);
            }
        }
        QVERIFY(found);
    }
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
        a::ToolCall call{"c","file_job_submit",{{"action","rename"},{"path","Files/test.txt"},{"argument","renamed.txt"}}};
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
        call.arguments={{"action","trash"},{"path","Files/test.txt"}};
        QCOMPARE(workspaceAgentPermission(root,transport,call,{},context).behavior,a::PermissionBehavior::Deny);
        QCOMPARE(questions,1);
        call.arguments={{"action","move"},{"path","Files/test.txt"},{"argument","Deleted/Folder"}};
        QCOMPARE(workspaceAgentPermission(root,transport,call,{},context).behavior,a::PermissionBehavior::Deny);
        QCOMPARE(questions,1);
        call.arguments.insert("action","rename");
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
    void duplicateCandidatesUseThePinnedContainer() {
        auto transport=std::make_shared<WorkspaceAgentToolTransport>();
        QString root,operation; QJsonObject arguments; bool oversized=false;
        transport->command=[&](const QString &container,const QString &op,const QJsonObject &args) {
            root=container; operation=op; arguments=args;
            return SocietyBackendCommandReply{{{"accepted",true},{"duplicates",QJsonObject{{"completeness","unknown"},
                {"candidates",QJsonArray{}},{"reference",oversized?QString(9000,'x'):QStringLiteral("short")}}}},{},false};
        };
        const auto registry=workspaceAgentTools("/drive-a.societycontainer",transport);
        const auto result=registry->get("resource_duplicate_candidates").execute({{"path","Files/a.txt"},{"limit",2}},{});
        QVERIFY(!result.isError); QCOMPARE(root,QStringLiteral("/drive-a.societycontainer"));
        QCOMPARE(operation,QStringLiteral("resourceDuplicates")); QCOMPARE(arguments.value("path").toString(),QStringLiteral("Files/a.txt"));
        QCOMPARE(result.data.value("completeness").toString(),QStringLiteral("unknown"));
        oversized=true;
        const auto tooLarge=registry->get("resource_duplicate_candidates").execute({{"path","Files/a.txt"},{"limit",1}},{});
        QVERIFY(tooLarge.isError); QVERIFY(tooLarge.text.contains("CLI"));
        QVERIFY_THROWS_EXCEPTION(std::exception,registry->validateInput("resource_duplicate_candidates",{{"path","Files/a.txt"},{"limit",21}}));
    }
    void indexedSearchUsesThePinnedContainer() {
        auto transport=std::make_shared<WorkspaceAgentToolTransport>();
        QString root,operation; QJsonObject arguments;
        transport->command=[&](const QString &container,const QString &op,const QJsonObject &args) {
            root=container; operation=op; arguments=args;
            return SocietyBackendCommandReply{{{"accepted",true},{"search",QJsonObject{{"completeness","unknown"},
                {"items",QJsonArray{}}}}},{},false};
        };
        const auto registry=workspaceAgentTools("/drive-a.societycontainer",transport);
        const QJsonObject query{{"section","files"},{"text","campaign"}};
        const auto result=registry->get("resource_search").execute({{"query",query},{"limit",5}},{});
        QVERIFY(!result.isError); QCOMPARE(root,QStringLiteral("/drive-a.societycontainer"));
        QCOMPARE(operation,QStringLiteral("resourceSearch")); QCOMPARE(arguments.value("query").toObject(),query);
        QCOMPARE(result.data.value("completeness").toString(),QStringLiteral("unknown"));
    }
    void lineageUsesThePinnedContainerAndBoundedReadOnlyTool() {
        auto transport=std::make_shared<WorkspaceAgentToolTransport>();
        QString root,operation; QJsonObject arguments; bool oversized=false;
        transport->command=[&](const QString &container,const QString &op,const QJsonObject &args) {
            root=container; operation=op; arguments=args;
            return SocietyBackendCommandReply{{{"accepted",true},{"lineage",QJsonObject{{"completeness","unknown"},
                {"cleanupDecision","review_required"},{"note",oversized?QString(9000,'x'):QStringLiteral("short")}}}},{},false};
        };
        const auto registry=workspaceAgentTools("/drive-a.societycontainer",transport);
        const auto result=registry->get("resource_lineage").execute({{"path","Files/a.txt"},{"direction","incoming"},{"depth",2}},{});
        QVERIFY(!result.isError); QCOMPARE(root,QStringLiteral("/drive-a.societycontainer"));
        QCOMPARE(operation,QStringLiteral("resourceLineage")); QCOMPARE(arguments.value("direction").toString(),QStringLiteral("incoming"));
        QCOMPARE(result.data.value("cleanupDecision").toString(),QStringLiteral("review_required"));
        oversized=true;
        QVERIFY(registry->get("resource_lineage").execute({{"path","Files/a.txt"}},{}).isError);
        QVERIFY_THROWS_EXCEPTION(std::exception,registry->validateInput("resource_lineage",{{"path","Files/a.txt"},{"depth",4}}));
    }
    void cleanupPlanUsesThePinnedContainer() {
        auto transport=std::make_shared<WorkspaceAgentToolTransport>();
        QString root,operation; QJsonObject arguments;
        transport->command=[&](const QString &container,const QString &op,const QJsonObject &args) {
            root=container; operation=op; arguments=args;
            return SocietyBackendCommandReply{{{"accepted",true},{"cleanupPlan",QJsonObject{{"policy",QJsonObject{
                {"decision","blocked"},{"executable",false}}}}}},{},false};
        };
        const auto registry=workspaceAgentTools("/drive-a.societycontainer",transport);
        const auto result=registry->get("resource_cleanup_plan").execute({{"path","Files/a.txt"},{"limit",2}},{});
        QVERIFY(!result.isError); QCOMPARE(root,QStringLiteral("/drive-a.societycontainer"));
        QCOMPARE(operation,QStringLiteral("resourceCleanupPlan")); QCOMPARE(arguments.value("limit").toInt(),2);
        QCOMPARE(result.data.value("policy").toObject().value("decision").toString(),QStringLiteral("blocked"));
        QVERIFY_THROWS_EXCEPTION(std::exception,registry->validateInput("resource_cleanup_plan",{{"path","Files/a.txt"},{"limit",21}}));
    }
    void byteComparisonUsesThePinnedContainer() {
        auto transport=std::make_shared<WorkspaceAgentToolTransport>();
        QString root,operation; QJsonObject arguments;
        transport->command=[&](const QString &container,const QString &op,const QJsonObject &args) {
            root=container; operation=op; arguments=args;
            return SocietyBackendCommandReply{{{"accepted",true},{"byteComparison",QJsonObject{
                {"comparison","equal"},{"snapshot","non_atomic"},{"deduplicationAuthorized",false}}}},{},false};
        };
        const auto registry=workspaceAgentTools("/drive-a.societycontainer",transport);
        const QJsonObject request{{"path","Files/a.txt"},{"otherPath","Models/b.bin"}};
        registry->validateInput("resource_compare_bytes",request);
        const auto result=registry->get("resource_compare_bytes").execute(request,{});
        QVERIFY(!result.isError); QCOMPARE(root,QStringLiteral("/drive-a.societycontainer"));
        QCOMPARE(operation,QStringLiteral("resourceCompareBytes")); QCOMPARE(arguments,request);
        QCOMPARE(result.data.value("snapshot").toString(),QStringLiteral("non_atomic"));
        QVERIFY(!result.data.value("deduplicationAuthorized").toBool());
        QVERIFY_THROWS_EXCEPTION(std::exception,registry->validateInput("resource_compare_bytes",{{"path","Files/a.txt"}}));
    }
    void semanticTextToolsUseThePinnedContainerAndModel() {
        auto transport=std::make_shared<WorkspaceAgentToolTransport>();
        QString root,operation; QJsonObject arguments;
        transport->command=[&](const QString &container,const QString &op,const QJsonObject &args) {
            root=container; operation=op; arguments=args;
            if (op=="resourceContentPolicyGet" || op=="resourceContentPolicySet")
                return SocietyBackendCommandReply{{{"accepted",true},{"policy",QJsonObject{{"enabled",true},{"model","model://fixture"}}}},{},false};
            return SocietyBackendCommandReply{{{"accepted",true},{"content",QJsonObject{
                {"state","indexed"},{"evidence","local_text_embeddings"},{"completeness","unknown"}}}},{},false};
        };
        const auto registry=workspaceAgentTools("/drive-a.societycontainer",transport);
        const QJsonObject index{{"path","Files/a.txt"},{"model","model://fixture"},{"documentPrefix","passage: "}};
        registry->validateInput("resource_content_index",index);
        QVERIFY(!registry->get("resource_content_index").definition.readOnly);
        QVERIFY(!registry->get("resource_content_index").execute(index,{}).isError);
        QCOMPARE(root,QStringLiteral("/drive-a.societycontainer")); QCOMPARE(operation,QStringLiteral("resourceContentIndex")); QCOMPARE(arguments,index);
        const QJsonObject search{{"query","waves"},{"model","model://fixture"},{"limit",3}};
        registry->validateInput("resource_content_search",search);
        const auto result=registry->get("resource_content_search").execute(search,{});
        QVERIFY(!result.isError); QCOMPARE(operation,QStringLiteral("resourceContentSearch")); QCOMPARE(arguments,search);
        QCOMPARE(result.data.value("completeness").toString(),QStringLiteral("unknown"));
        QVERIFY_THROWS_EXCEPTION(std::exception,registry->validateInput("resource_content_search",{{"query","waves"}}));
        QVERIFY(registry->get("resource_content_policy").definition.readOnly);
        QVERIFY(!registry->get("resource_content_policy").execute({},{}).isError);
        QCOMPARE(operation,QStringLiteral("resourceContentPolicyGet"));
        const QJsonObject policy{{"enabled",true},{"model","model://fixture"},{"documentPrefix","passage: "}};
        registry->validateInput("resource_content_policy_set",policy);
        QVERIFY(!registry->get("resource_content_policy_set").definition.readOnly);
        QVERIFY(!registry->get("resource_content_policy_set").execute(policy,{}).isError);
        QCOMPARE(root,QStringLiteral("/drive-a.societycontainer")); QCOMPARE(operation,QStringLiteral("resourceContentPolicySet"));
        QCOMPARE(arguments,policy);
    }
    void contentProviderIsRevokedWhenTheContainerChanges() {
        QTemporaryDir fixture(QDir(QStringLiteral(SOCIETY_TEST_DIRECTORY)).filePath("embedding-selection-XXXXXX"));
        QVERIFY(fixture.isValid());
        const auto first=fixture.filePath("first.societycontainer"),second=fixture.filePath("second.societycontainer");
        QVERIFY(QDir().mkdir(first)); QVERIFY(iiSocietyContainer::SocietyDrive::create(first));
        QVERIFY(QDir().mkdir(second)); QVERIFY(iiSocietyContainer::SocietyDrive::create(second));
        WorkspaceAgentBackend backend;
        backend.selectContainer(first);
        QTRY_VERIFY_WITH_TIMEOUT(backend.contentEmbedder().embedder!=nullptr,30000);
        const auto previous=backend.contentEmbedder(); QVERIFY(!previous.cancellation.stop_requested());
        backend.selectContainer(second); QVERIFY(previous.cancellation.stop_requested());
        QTRY_VERIFY_WITH_TIMEOUT(backend.contentEmbedder().embedder!=nullptr,30000);
        QVERIFY(backend.contentEmbedder().embedder!=previous.embedder);
    }
    void contentEmbeddingUsesTheHostLocalServiceAndCapability() {
        QTemporaryDir fixture(QDir(QStringLiteral(SOCIETY_TEST_DIRECTORY)).filePath("embedding-service-XXXXXX"));
        QVERIFY(fixture.isValid());
        iiLocalLLM::ServiceOptions options; options.modelsDirectory=fixture.filePath("models"); options.keepAliveMs=300000;
        auto service=std::make_shared<iiLocalLLM::Service>(options);
        auto probe=std::make_shared<EmbeddingServiceProbe>();
        service->registerRuntime(std::make_shared<EmbeddingServiceRuntime>(probe)).get();
        const auto install=[&](const QString &id,const QStringList &capabilities) {
            const auto package=fixture.filePath(id); if (!QDir().mkdir(package)) return false;
            const iiLocalLLM::ModelManifest manifest{id,"fixture","society-fixture","none",128,capabilities,"model.test"};
            QFile metadata(package+"/manifest.json");
            if (!metadata.open(QIODevice::WriteOnly) || metadata.write(QJsonDocument(iiLocalLLM::manifestObject(manifest)).toJson())<1) return false;
            metadata.close(); QFile weights(package+"/model.test");
            if (!weights.open(QIODevice::WriteOnly) || weights.write("fixture")!=7) return false;
            weights.close(); (void)service->installModel(package).get(); return true;
        };
        QVERIFY(install("embedding",{"embedding"})); QVERIFY(install("chat-only",{"chat"}));
        ServiceContentEmbedder embedder([service](const QString &,std::stop_token) {
            return ServiceContentEmbedder::ServiceAccess{service,{{"fixture_option",true}}};
        });
        const auto revision=embedder.revision("model://embedding",{}); QCOMPARE(revision.size(),64);
        const auto values=embedder.embed("model://embedding","local text",{});
        QCOMPARE(values,QList<float>({10.0f,1.0f})); QCOMPARE(probe->calls.load(),1);
        QCOMPARE(probe->options.value("embedding_pooling").toString(),QStringLiteral("model"));
        QVERIFY(probe->options.value("fixture_option").toBool());
        QCOMPARE(embedder.revision("model://embedding",{}),revision);
        QVERIFY_THROWS_EXCEPTION(std::exception,(void)embedder.revision("model://chat-only",{}));
        ServiceContentEmbedder otherOptions([service](const QString &,std::stop_token) {
            return ServiceContentEmbedder::ServiceAccess{service,{{"fixture_option",false}}};
        });
        QVERIFY(otherOptions.revision("model://embedding",{})!=revision);
        QVERIFY_THROWS_EXCEPTION(std::exception,(void)otherOptions.embed("model://embedding","different options",{}));
        std::stop_source cancelled; cancelled.request_stop();
        QVERIFY_THROWS_EXCEPTION(std::exception,(void)embedder.embed("model://embedding","ignored",cancelled.get_token()));
        QCOMPARE(probe->calls.load(),1);
        probe->entered=false; probe->block=true;
        std::stop_source active;
        auto work=std::async(std::launch::async,[&] { return embedder.embed("model://embedding","running",active.get_token()); });
        const auto release=qScopeGuard([&] { active.request_stop(); probe->block=false; });
        QTRY_VERIFY_WITH_TIMEOUT(probe->entered.load(),10000);
        active.request_stop();
        QVERIFY(work.wait_for(std::chrono::seconds(10))==std::future_status::ready);
        QVERIFY_THROWS_EXCEPTION(std::exception,(void)work.get()); QCOMPARE(probe->calls.load(),2);
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
    void automaticContentPolicyRequiresPerCallApproval() {
        QTemporaryDir fixture(QDir(QStringLiteral(SOCIETY_TEST_DIRECTORY)).filePath("content-policy-approval-XXXXXX"));
        QVERIFY(fixture.isValid()); const auto root=fixture.filePath("test.societycontainer");
        QVERIFY(QDir().mkdir(root)); QVERIFY(iiSocietyContainer::SocietyDrive::create(root));
        auto model=std::make_shared<ContentPolicyToolModel>();
        auto transport=std::make_shared<WorkspaceAgentToolTransport>();
        std::atomic_int submitted=0,reviewed=0; std::atomic_bool allow=false,completePreview=false;
        transport->command=[&](const QString &container,const QString &operation,const QJsonObject &args) {
            if (container!=root || operation!="resourceContentPolicySet" || !args.value("enabled").toBool() ||
                args.value("model")!="model://fixture" || args.value("documentPrefix")!="passage: ")
                return SocietyBackendCommandReply{{},"Unexpected policy request",false};
            ++submitted; return SocietyBackendCommandReply{{{"accepted",true},{"policy",QJsonObject{{"enabled",true}}}},{},false};
        };
        transport->question=[&](const QJsonObject &input,const a::ToolContext &) {
            ++reviewed; const auto question=input.value("questions").toArray().first().toObject().value("question").toString();
            completePreview=question.contains(root) && question.contains("resource_content_policy_set") &&
                question.contains("model://fixture") && question.contains("passage: ");
            const auto answer=allow.load()?QStringLiteral("Allow once"):QStringLiteral("Deny");
            return a::ToolResult{"review",{{"answers",QJsonObject{{question,answer}}}}};
        };
        WorkspaceAgentBackend backend(nullptr,model,"model://test",{},transport); backend.selectContainer(root);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().value("model").toString(),QStringLiteral("model://test"),30000);
        QString error; QVERIFY2(backend.submit("Enable indexing",&error),qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(!backend.snapshot().value("busy").toBool(),30000);
        QVERIFY(completePreview); QCOMPARE(reviewed.load(),1); QCOMPARE(submitted.load(),0);
        allow=true; QVERIFY2(backend.submit("Enable indexing now",&error),qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(!backend.snapshot().value("busy").toBool(),30000);
        QCOMPARE(reviewed.load(),2); QCOMPARE(submitted.load(),1);
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
    void locatesPackagedModelWithoutCopyingWeights() {
        QTemporaryDir fixture(QDir(QStringLiteral(SOCIETY_TEST_DIRECTORY)).filePath("agent-direct-model-XXXXXX"));
        QVERIFY(fixture.isValid());
        const auto input=fixture.filePath("input"); QVERIFY(QDir().mkdir(input));
        const auto gguf=cacheFixtureGguf();
        QFile source(input+"/language.gguf"); QVERIFY(source.open(QIODevice::WriteOnly));
        QCOMPARE(source.write(gguf),qint64(gguf.size())); source.close();
        const auto package=fixture.filePath("iisaccson.safetensors");
        const std::vector<iild::ModelPackagingAssignment> assignments{{"language.gguf","model","main"}};
        (void)iild::createModelPackageWithAssignments(std::filesystem::path(input.toStdU16String()),
            std::filesystem::path(package.toStdU16String()),assignments);
        const auto range=inspectWorkspaceAgentModelRange(package,QStringLiteral("model"));
        QCOMPARE(range.value("size").toInteger(),qint64(gguf.size()));
        QFile stored(package); QVERIFY(stored.open(QIODevice::ReadOnly));
        QVERIFY(stored.seek(range.value("offset").toInteger())); QCOMPARE(stored.read(gguf.size()),gguf);
        QVERIFY(!QFileInfo::exists(fixture.filePath("model.gguf")));
        QVERIFY(!QFileInfo::exists(fixture.filePath("agent/models")));
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,inspectWorkspaceAgentModelRange(package,QStringLiteral("text_encoder")));
    }
    void embeddedRuntimeUsesAndVerifiesTheModelsFile() {
        QTemporaryDir fixture(QDir(QStringLiteral(SOCIETY_TEST_DIRECTORY)).filePath("agent-runtime-source-XXXXXX"));
        QVERIFY(fixture.isValid());
        const auto root=fixture.filePath("Models"); QVERIFY(QDir().mkpath(root+"/LLM"));
        const auto model=root+"/LLM/iisaccson.safetensors";
        const QByteArray prefix("unaligned-prefix"),weights("GGUF-fixture-weights"),tail("projector");
        QFile package(model); QVERIFY(package.open(QIODevice::WriteOnly));
        QCOMPARE(package.write(prefix+weights+tail),qint64(prefix.size()+weights.size()+tail.size())); package.close();
        const auto digest=QString::fromLatin1(QCryptographicHash::hash(weights,QCryptographicHash::Sha256).toHex());
        auto delegate=std::make_shared<EmbeddedSourceProbe>();
        const auto runtime=createEmbeddedModelRuntime(root,{{"source",{model,digest,quint64(prefix.size()),quint64(weights.size())}}},delegate);
        iiLocalLLM::ModelSpec spec{"source",fixture.filePath("reference.json"),2048,{},"society-embedded-gguf"};
        QVERIFY(runtime->supportsModel(spec));
        QVERIFY(runtime->load(spec,{iiLocalLLM::ComputeBackend::Cpu,{}},{}));
        QCOMPARE(delegate->loads,1); QCOMPARE(delegate->received.path,model);
        QCOMPARE(delegate->received.options.value("__iilocal_gguf_offset").toInteger(),qint64(prefix.size()));
        QCOMPARE(delegate->received.options.value("__iilocal_gguf_size").toInteger(),qint64(weights.size()));
        QVERIFY(!QFileInfo::exists(fixture.filePath("model.gguf")));
        QVERIFY(package.open(QIODevice::ReadWrite)); QVERIFY(package.seek(prefix.size()+4));
        QCOMPARE(package.write("!",1),qint64(1)); package.close();
        QVERIFY_THROWS_EXCEPTION(std::exception,runtime->load(spec,{iiLocalLLM::ComputeBackend::Cpu,{}},{}));
        QCOMPARE(delegate->loads,1);
    }
    void residentLoadsPackagedGgufWithoutAnExtractedCopy() {
        QTemporaryDir fixture(QDir(QStringLiteral(SOCIETY_TEST_DIRECTORY)).filePath("agent-resident-direct-XXXXXX"));
        QVERIFY(fixture.isValid());
        const auto root=fixture.filePath("test.societycontainer");
        QVERIFY(QDir().mkdir(root)); QVERIFY(iiSocietyContainer::SocietyDrive::create(root));
        const auto input=fixture.filePath("input"); QVERIFY(QDir().mkdir(input));
        QFile source(input+"/language.gguf"); QVERIFY(source.open(QIODevice::WriteOnly));
        const auto gguf=cacheFixtureLoadableGguf(); QCOMPARE(source.write(gguf),qint64(gguf.size())); source.close();
        QVERIFY(QDir().mkpath(root+"/Models/LLM"));
        const auto package=root+"/Models/LLM/iisaccson.safetensors";
        const std::vector<iild::ModelPackagingAssignment> assignments{{"language.gguf","model","main"}};
        (void)iild::createModelPackageWithAssignments(std::filesystem::path(input.toStdU16String()),
            std::filesystem::path(package.toStdU16String()),assignments);
        const auto before=QFileInfo(package).size();
        const auto digest=QString::fromLatin1(QCryptographicHash::hash(gguf,QCryptographicHash::Sha256).toHex());
        const auto old=root+"/.society-runtime/agent/models/iisaccson-"+digest;
        QVERIFY(QDir().mkpath(old));
        QFile oldWeights(old+"/model.gguf"),oldManifest(old+"/manifest.json");
        QVERIFY(oldWeights.open(QIODevice::WriteOnly)); QCOMPARE(oldWeights.write(gguf),qint64(gguf.size())); oldWeights.close();
        const QJsonObject manifest{{"schema_version",1},{"id","iisaccson-"+digest},{"architecture","llama"},
            {"format","gguf"},{"quantization","unknown"},{"context_length",2048},{"capabilities",QJsonArray{"chat"}},
            {"entry_point","model.gguf"},{"files",QJsonArray{QJsonObject{{"path","model.gguf"},{"size",gguf.size()},{"sha256",digest}}}}};
        const auto manifestBytes=QJsonDocument(manifest).toJson(QJsonDocument::Compact);
        QVERIFY(oldManifest.open(QIODevice::WriteOnly)); QCOMPARE(oldManifest.write(manifestBytes),qint64(manifestBytes.size())); oldManifest.close();
        WorkspaceAgentBackend backend; backend.selectContainer(root);
        QTRY_VERIFY_WITH_TIMEOUT(backend.residentGateway()->status().value("state")=="ready" ||
                                 backend.residentGateway()->status().value("state")=="error",30000);
        const auto status=backend.residentGateway()->status();
        QVERIFY2(status.value("state")=="ready",qPrintable(QString::fromUtf8(QJsonDocument(status).toJson(QJsonDocument::Compact))));
        QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(old),30000);
        QDirIterator extracted(root+"/.society-runtime/agent/models",{QStringLiteral("model.gguf")},
                               QDir::Files,QDirIterator::Subdirectories);
        QVERIFY(!extracted.hasNext());
        QCOMPARE(QFileInfo(package).size(),before);
    }
    void removesOnlyVerifiedRedundantLegacyModelCache() {
        QTemporaryDir fixture(QDir(QStringLiteral(SOCIETY_TEST_DIRECTORY)).filePath("agent-old-cache-XXXXXX"));
        QVERIFY(fixture.isValid());
        const auto models=fixture.filePath("Models"),catalog=fixture.filePath("agent/models");
        QVERIFY(QDir().mkpath(models+"/LLM")); QVERIFY(QDir().mkpath(catalog));
        const QByteArray prefix("prefix"),weights("GGUF-identical-weights"),tail("other asset");
        const auto package=models+"/LLM/iisaccson.safetensors";
        QFile original(package); QVERIFY(original.open(QIODevice::WriteOnly));
        QCOMPARE(original.write(prefix+weights+tail),qint64(prefix.size()+weights.size()+tail.size())); original.close();
        const auto digest=QString::fromLatin1(QCryptographicHash::hash(weights,QCryptographicHash::Sha256).toHex());
        const EmbeddedModelSource source{package,digest,quint64(prefix.size()),quint64(weights.size())};
        const auto old=catalog+"/iisaccson-"+digest;
        const QJsonObject manifest{{"schema_version",1},{"id","iisaccson-"+digest},{"architecture","llama"},
            {"format","gguf"},{"quantization","unknown"},{"context_length",2048},{"capabilities",QJsonArray{"chat"}},
            {"entry_point","model.gguf"},{"files",QJsonArray{QJsonObject{{"path","model.gguf"},{"size",weights.size()},{"sha256",digest}}}}};
        const auto makeCache=[&](const QByteArray &contents) {
            if (!QDir().mkpath(old)) return false;
            QFile blob(old+"/model.gguf"),info(old+"/manifest.json");
            const auto json=QJsonDocument(manifest).toJson(QJsonDocument::Compact);
            return blob.open(QIODevice::WriteOnly) && blob.write(contents)==contents.size() &&
                info.open(QIODevice::WriteOnly) && info.write(json)==json.size();
        };
        QVERIFY(makeCache(weights));
        QVERIFY(removeVerifiedLegacyModelCache(models,catalog,source));
        QVERIFY(!QFileInfo::exists(old));
        QVERIFY(original.open(QIODevice::ReadOnly)); QCOMPARE(original.readAll(),prefix+weights+tail); original.close();
        QVERIFY(makeCache("GGUF-modified"));
        QVERIFY(!removeVerifiedLegacyModelCache(models,catalog,source));
        QVERIFY(QFileInfo::exists(old+"/model.gguf"));
        std::stop_source cancelled; cancelled.request_stop();
        QVERIFY(!removeVerifiedLegacyModelCache(models,catalog,source,cancelled.get_token()));
        QVERIFY(QFileInfo::exists(old+"/model.gguf"));
        QVERIFY(makeCache(weights));
        QFile extra(old+"/unknown"); QVERIFY(extra.open(QIODevice::WriteOnly)); extra.close();
        QVERIFY(!removeVerifiedLegacyModelCache(models,catalog,source));
        QVERIFY(QFileInfo::exists(old+"/model.gguf")); QVERIFY(extra.remove());
        QVERIFY(makeCache(weights+"trailing data"));
        QVERIFY(!removeVerifiedLegacyModelCache(models,catalog,source));
        QVERIFY(QFileInfo::exists(old+"/model.gguf"));
        QVERIFY(makeCache(weights));
        QVERIFY(original.open(QIODevice::ReadWrite)); QVERIFY(original.seek(prefix.size()));
        QCOMPARE(original.write("!",1),qint64(1)); original.close();
        QVERIFY(!removeVerifiedLegacyModelCache(models,catalog,source));
        QVERIFY(QFileInfo::exists(old+"/model.gguf"));
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

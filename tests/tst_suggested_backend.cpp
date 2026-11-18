#include "Daemon/SuggestedBackend.h"
#include "Daemon/WorkspaceAgentBackend.h"
#include <iiSocietyContainer>
#include <iiMultimodalAgent>
#include <QDir>
#include <QJsonArray>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <atomic>
#include <mutex>

namespace a=iiMultimodalAgent::agent;
using namespace society::suggested;
class RecommendationModel final : public a::Model {
public:
    std::atomic_int calls=0;
    std::atomic_bool omit=false;
    a::ModelReply generate(const a::ModelRequest &request,const iiMultimodalAgent::CancellationToken &,
                           const std::function<bool(const QString &)> &) override {
        { std::lock_guard lock(mutex); selected=request.model; }
        ++calls;
        if (omit.load()) return {R"({"assessments":[]})"};
        return {R"({"assessments":[{"index":0,"probability":0.9,"gain":10,"loss":1,"cost":0.1,"explanation":"The document describes unfinished work"}]})"};
    }
    QString selectedModel() const { std::lock_guard lock(mutex); return selected; }
private:
    mutable std::mutex mutex;
    QString selected;
};
class SuggestedBackendTest final : public QObject {
    Q_OBJECT
private slots:
    void rulesPrecedeTheAgentAndStaleEstimatesAreDiscarded() {
        QTemporaryDir fixture(QDir(QStringLiteral(SOCIETY_TEST_DIRECTORY)).filePath("suggested-backend-XXXXXX"));
        QVERIFY(fixture.isValid()); const auto root=fixture.filePath("test.societycontainer");
        QVERIFY(QDir().mkdir(root)); QVERIFY(iiSocietyContainer::SocietyDrive::create(root));
        auto model=std::make_shared<RecommendationModel>();
        auto routing=std::make_shared<a::DecisionOptions>(); routing->valueUnit="fixture";
        routing->modelRoutes={{"iisaccson","model://iisaccson"},{"asimov","model://asimov"}};
        routing->profiles={{"model:iisaccson",5,1,1,0.8,{}},{"model:asimov",10,1,1,0.9,{}}};
        WorkspaceAgentBackend agent(nullptr,model,"model://iisaccson",routing);
        agent.selectContainer(root);
        QTRY_COMPARE_WITH_TIMEOUT(agent.snapshot().value("model").toString(),QStringLiteral("model://iisaccson"),30000);
        Candidate draft,unknown,done;
        draft.evidence={"r1","v1","/Files/draft.txt","draft.txt","","",{"draft"},true};
        unknown.evidence={"r2","v2","/Files/unknown.txt","unknown.txt","unfinished illustration","",{},true};
        done.evidence={"r3","v3","/Files/done.txt","done.txt","","",{"completed"},true};
        std::atomic_bool valid=true;
        std::atomic_bool capped=false;
        std::atomic_bool many=false;
        std::atomic_int version=2;
        SuggestedBackend backend(&agent,nullptr,
            [&,draft,unknown,done](const std::string &,std::size_t,const std::atomic_bool *) mutable {
                unknown.evidence.version="v"+std::to_string(version.load());
                if (many.load()) {
                    CatalogSnapshot scan;
                    for (int i=0;i<70;++i) {
                        auto item=unknown; item.evidence.resourceId="unknown-"+std::to_string(i);
                        scan.items.push_back(std::move(item));
                    }
                    return scan;
                }
                if (capped.load()) {
                    CatalogSnapshot scan;
                    for (int i=0;i<200;++i) {
                        auto item=draft; item.evidence.resourceId="rule-"+std::to_string(i);
                        scan.items.push_back(std::move(item));
                    }
                    scan.items.push_back(unknown);
                    return scan;
                }
                return CatalogSnapshot{{draft,unknown,done}};
            },
            [&](const std::string &,const Candidate &) { return valid.load(); });
        backend.selectContainer(root); backend.refresh();
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().value("items").toArray().size(),2,30000);
        const auto items=backend.snapshot().value("items").toArray();
        QCOMPARE(items[0].toObject().value("source").toString(),QStringLiteral("rule"));
        QCOMPARE(items[1].toObject().value("source").toString(),QStringLiteral("model_estimate"));
        QCOMPARE(model->calls.load(),1);
        QCOMPARE(model->selectedModel(),QStringLiteral("model://asimov"));
        backend.refresh(); QTRY_VERIFY_WITH_TIMEOUT(!backend.snapshot().value("assessing").toBool(),30000);
        QCOMPARE(model->calls.load(),1); // unchanged content reuses its in-memory estimate
        valid=false; version=3; backend.refresh();
        QTRY_VERIFY_WITH_TIMEOUT(model->calls.load()>=2,30000);
        QTRY_VERIFY_WITH_TIMEOUT(!backend.snapshot().value("assessing").toBool(),30000);
        QCOMPARE(backend.snapshot().value("items").toArray().size(),1);
        QVERIFY(backend.snapshot().value("partial").toBool());
        valid=true; version=4; model->omit=true; backend.refresh();
        QTRY_VERIFY_WITH_TIMEOUT(model->calls.load()>=3,30000);
        QTRY_VERIFY_WITH_TIMEOUT(!backend.snapshot().value("assessing").toBool(),30000);
        QCOMPARE(backend.snapshot().value("items").toArray().size(),1);
        QCOMPARE(backend.snapshot().value("unassessable").toInt(),1);
        capped=true; version=5; backend.refresh();
        QTRY_VERIFY_WITH_TIMEOUT(!backend.snapshot().value("assessing").toBool(),30000);
        QCOMPARE(model->calls.load(),3);
        QCOMPARE(backend.snapshot().value("items").toArray().size(),200);
        QVERIFY(backend.snapshot().value("partial").toBool());
        capped=false; many=true; version=6; backend.refresh();
        QTRY_VERIFY_WITH_TIMEOUT(!backend.snapshot().value("assessing").toBool(),30000);
        QCOMPARE(model->calls.load(),19); // 64 uncertain files, in batches of four
        QCOMPARE(backend.snapshot().value("unassessable").toInt(),64);
        QVERIFY(backend.snapshot().value("partial").toBool());
    }
};
QTEST_GUILESS_MAIN(SuggestedBackendTest)
#include "tst_suggested_backend.moc"

#include "Daemon/SuggestedModel.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QTest>
#include <iiMultimodalAgent/agent/Types.h>

namespace a=iiMultimodalAgent::agent;
class FixtureModel final : public a::Model {
public:
    int calls=0; a::ModelRequest request; QString response;
    a::ModelReply generate(const a::ModelRequest &value,const iiMultimodalAgent::CancellationToken &,
                           const std::function<bool(const QString&)> &) override {
        ++calls; request=value; return {.text=response};
    }
};
class SuggestedModelTest final : public QObject {
    Q_OBJECT
private slots:
    void requestsBoundedLocalEstimatesWithoutTools() {
        FixtureModel model;
        model.response=R"({"assessments":[{"index":0,"probability":0.9,"gain":10,"loss":1,"cost":0.1,"explanation":"Open sections remain"}]})";
        society::suggested::Evidence evidence; evidence.name="design.md"; evidence.path="/private/path/design.md";
        evidence.resourceId="private-resource-id"; evidence.description="A draft of the design"; evidence.text=std::string(9000,'a');
        const auto result=society::suggested::assess(model,"model://iisaccson",{evidence},{});
        QCOMPARE(model.calls,1); QCOMPARE(result.size(),1); QVERIFY(result[0].has_value());
        QCOMPARE(result[0]->explanation,std::string("Open sections remain"));
        QCOMPARE(model.request.model,QStringLiteral("model://iisaccson"));
        QCOMPARE(model.request.toolChoice,QStringLiteral("none")); QVERIFY(model.request.systemPromptOnly);
        const auto input=model.request.messages.first().text;
        QVERIFY(!input.contains("private-resource-id")); QVERIFY(!input.contains("/private/path"));
        QVERIFY(input.size()<4096); QVERIFY(!model.request.responseSchema.isEmpty());
    }
    void rejectsMalformedAndDuplicateEstimates() {
        FixtureModel model; society::suggested::Evidence evidence; evidence.description="unfinished art";
        model.response=R"({"assessments":[{"index":0,"probability":2,"gain":1,"loss":0,"cost":0,"explanation":"bad"},{"index":0,"probability":0.9,"gain":10,"loss":1,"cost":0,"explanation":"duplicate"}]})";
        const auto result=society::suggested::assess(model,"model://iisaccson",{evidence},{});
        QCOMPARE(result.size(),1); QVERIFY(!result[0].has_value());
        model.response="not-json";
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,society::suggested::assess(model,"model://iisaccson",{evidence},{}));
    }
    void choosesAnEligibleModelByDecision() {
        a::DecisionOptions options; options.valueUnit="relative";
        options.modelRoutes={{"iisaccson","model://iisaccson"},{"asimov","model://asimov"}};
        options.profiles={{"model:iisaccson",5,1,1,0.8,{}},{"model:asimov",10,1,1,0.9,{}}};
        QCOMPARE(society::suggested::selectRoute(options,"Assess this file","test-run"),QStringLiteral("model://asimov"));
        options.profiles.clear();
        QVERIFY(society::suggested::selectRoute(options,"Assess this file","test-run").isEmpty());
    }
};
QTEST_GUILESS_MAIN(SuggestedModelTest)
#include "tst_suggested_model.moc"

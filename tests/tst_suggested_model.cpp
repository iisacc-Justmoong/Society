#include "Daemon/SuggestedModel.h"
#include <QJsonArray>
#include <QJsonDocument>
#include "CTestSupport.h"
#include <QCoreApplication>
#include <QEvent>
#include <iiMultimodalAgent>

namespace a=iiMultimodalAgent::agent;
class FixtureModel final : public a::Model {
public:
    int calls=0; a::ModelRequest request; QString response;
    a::ModelReply generate(const a::ModelRequest &value,const iiMultimodalAgent::CancellationToken &,
                           const std::function<bool(const QString&)> &) override {
        ++calls; request=value; return {.text=response};
    }
};
class SuggestedModelTest final {
public:
    void requestsBoundedLocalEstimatesWithoutTools() {
        FixtureModel model;
        model.response=R"({"assessments":[{"index":0,"probability":0.9,"gain":10,"loss":1,"cost":0.1,"explanation":"Open sections remain"}]})";
        society::suggested::Evidence evidence; evidence.name="design.md"; evidence.path="/private/path/design.md";
        evidence.resourceId="private-resource-id"; evidence.description="A draft of the design"; evidence.text=std::string(9000,'a');
        const auto result=society::suggested::assess(model,"model://iisaccson",{evidence},{});
        CTEST_COMPARE(model.calls,1); CTEST_COMPARE(result.size(),1); CTEST_CHECK(result[0].has_value());
        CTEST_COMPARE(result[0]->explanation,std::string("Open sections remain"));
        CTEST_COMPARE(model.request.model,QStringLiteral("model://iisaccson"));
        CTEST_COMPARE(model.request.toolChoice,QStringLiteral("none")); CTEST_CHECK(model.request.systemPromptOnly);
        const auto input=model.request.messages.first().text;
        CTEST_CHECK(!input.contains("private-resource-id")); CTEST_CHECK(!input.contains("/private/path"));
        CTEST_CHECK(input.size()<4096); CTEST_CHECK(!model.request.responseSchema.isEmpty());
    }
    void rejectsMalformedAndDuplicateEstimates() {
        FixtureModel model; society::suggested::Evidence evidence; evidence.description="unfinished art";
        model.response=R"({"assessments":[{"index":0,"probability":2,"gain":1,"loss":0,"cost":0,"explanation":"bad"},{"index":0,"probability":0.9,"gain":10,"loss":1,"cost":0,"explanation":"duplicate"}]})";
        const auto result=society::suggested::assess(model,"model://iisaccson",{evidence},{});
        CTEST_COMPARE(result.size(),1); CTEST_CHECK(!result[0].has_value());
        model.response="not-json";
        CTEST_THROWS(std::runtime_error,society::suggested::assess(model,"model://iisaccson",{evidence},{}));
    }
    void choosesAnEligibleModelByDecision() {
        a::DecisionOptions options; options.valueUnit="relative";
        options.modelRoutes={{"iisaccson","model://iisaccson"},{"asimov","model://asimov"}};
        options.profiles={{"model:iisaccson",5,1,1,0.8,{}},{"model:asimov",10,1,1,0.9,{}}};
        CTEST_COMPARE(society::suggested::selectRoute(options,"Assess this file","test-run"),QStringLiteral("model://asimov"));
        options.profiles.clear();
        CTEST_CHECK(society::suggested::selectRoute(options,"Assess this file","test-run").isEmpty());
    }
};
int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    society_ctest::Runner runner(argc == 2 ? argv[1] : "");
    SuggestedModelTest tests;
    struct FlushDeferredDeletes {
        ~FlushDeferredDeletes() { QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); }
    };
    runner.run("requestsBoundedLocalEstimatesWithoutTools", [&] { FlushDeferredDeletes cleanup; tests.requestsBoundedLocalEstimatesWithoutTools(); });
    runner.run("rejectsMalformedAndDuplicateEstimates", [&] { FlushDeferredDeletes cleanup; tests.rejectsMalformedAndDuplicateEstimates(); });
    runner.run("choosesAnEligibleModelByDecision", [&] { FlushDeferredDeletes cleanup; tests.choosesAnEligibleModelByDecision(); });
    return runner.result();
}

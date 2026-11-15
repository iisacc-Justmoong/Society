#include "Daemon/SuggestedPolicy.h"
#include <cmath>
#include <stdexcept>

using namespace society::suggested;
static void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }

int main() {
    Policy policy;
    Evidence file; file.resourceId="resource-1"; file.version="revision-1";
    file.path="Files/project.iisc"; file.name="project.iisc"; file.description="A character illustration";
    int calls=0;
    const auto estimator=[&](const Evidence &) -> std::optional<Estimate> {
        ++calls; return Estimate{0.9, 10.0, 1.0, 0.1, "The description indicates remaining work"};
    };

    file.statuses={"draft"};
    auto result=policy.evaluate(file,estimator);
    check(result && result->source==Source::Rule && result->reason=="Rule: draft", "an explicit draft is suggested by rule");
    check(calls==0, "a conclusive rule never invokes the model");
    file.statuses={"completed"}; file.name="project-draft.iisc"; file.text="TODO: finish";
    check(!policy.evaluate(file,estimator), "explicit completion overrides weak draft markers");
    check(calls==0, "completed files do not invoke the model");
    file.statuses={}; file.name="project-wip.iisc"; file.text.clear();
    result=policy.evaluate(file,estimator);
    check(result && result->source==Source::Rule && calls==0, "a clear filename marker is a rule signal");
    file.name="draftsman.iisc";
    check(policy.classify(file).kind==RuleKind::Unknown, "partial word matches are not draft markers");
    file.name="brief.md"; file.text="- [ ] finish the introduction\n- [x] outline";
    result=policy.evaluate(file,estimator);
    check(result && result->reason=="Open checklist" && calls==0, "an open checkbox is a rule signal");
    file.text="A complete outline";
    result=policy.evaluate(file,estimator);
    check(result && result->source==Source::ModelEstimate && result->reason=="AI estimate" && calls==1,
          "ambiguous evidence uses an explicitly uncalibrated model estimate");
    check(result->resourceId==file.resourceId && result->version==file.version, "the exact resource and version are retained");
    check(!policy.evaluate(file,[&](const Evidence &) -> std::optional<Estimate> {
        return Estimate{0.2, 10.0, 1.0, 0.1, "Low confidence"};
    }), "iiDecision rejects a low-confidence estimate");
    check(!policy.evaluate(file,[&](const Evidence &) -> std::optional<Estimate> {
        return Estimate{std::nan(""), 10.0, 1.0, 0.1, "invalid"};
    }), "invalid estimates defer");
    file.description.clear(); file.text.clear();
    check(!policy.evaluate(file,estimator) && calls==1, "a filename alone is not enough for a model judgment");
    file.statuses={"draft","published"}; file.description="Conflicting metadata";
    check(policy.classify(file).kind==RuleKind::Conflicting, "contradictory status is not silently resolved");
    check(!policy.evaluate(file,estimator) && calls==1, "a model does not override contradictory authored status");
    file.statuses={"in_progress"}; file.available=false;
    check(!policy.evaluate(file,estimator), "unavailable originals are not presented as openable recommendations");
}

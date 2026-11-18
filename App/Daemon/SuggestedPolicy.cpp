#include "SuggestedPolicy.h"
#include <iiMultimodalAgent>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <regex>

namespace society::suggested {
namespace {
std::string normalize(std::string value) {
    for (char &c : value) {
        if (c=='_' || c=='-') c=' ';
        else if (static_cast<unsigned char>(c)<128) c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    const auto first=value.find_first_not_of(" \t\r\n"), last=value.find_last_not_of(" \t\r\n");
    return first==std::string::npos ? std::string{} : value.substr(first,last-first+1);
}
bool containsToken(std::string value, std::string token) {
    value=normalize(std::move(value));
    for (std::size_t i=value.find(token);i!=std::string::npos;i=value.find(token,i+1)) {
        const auto word=[](unsigned char c) { return c>=128 || std::isalnum(c); };
        if ((i==0 || !word(static_cast<unsigned char>(value[i-1]))) &&
            (i+token.size()==value.size() || !word(static_cast<unsigned char>(value[i+token.size()])))) return true;
    }
    return false;
}
bool finiteRange(double n, double maximum) { return std::isfinite(n) && n>=0 && n<=maximum; }
}

RuleResult Policy::classify(const Evidence &e) const {
    if (!e.available || e.resourceId.empty() || e.version.empty() || e.path.empty()) return {RuleKind::Exclude,{}};
    bool open=false, closed=false; std::string openReason;
    for (const auto &raw : e.statuses) {
        const auto status=normalize(raw);
        if (status=="draft" || status=="wip" || status=="work in progress" || status=="in progress" ||
            status=="needs revision" || status=="unfinished" || status=="초안" || status=="진행 중" || status=="미완성") {
            open=true;
            if (openReason.empty()) openReason="Rule: "+status;
        }
        if (status=="complete" || status=="completed" || status=="finished" || status=="approved" ||
            status=="released" || status=="published" || status=="완료" || status=="완성" || status=="게시됨" || status=="승인됨") closed=true;
    }
    if (open && closed) return {RuleKind::Conflicting,{}};
    if (closed) return {RuleKind::Exclude,{}};
    if (open) return {RuleKind::Recommend,openReason};
    static const std::regex checkbox(R"((^|\n)[ \t]*[-*+][ \t]+\[ \])");
    if (std::regex_search(e.text,checkbox)) return {RuleKind::Recommend,"Open checklist"};
    if (containsToken(e.text,"todo") || containsToken(e.text,"fixme")) return {RuleKind::Recommend,"Open task marker"};
    if (containsToken(e.name,"draft") || containsToken(e.name,"wip") || containsToken(e.name,"unfinished"))
        return {RuleKind::Recommend,"Filename: draft"};
    return {};
}

std::optional<Recommendation> Policy::evaluate(const Evidence &e, const Estimator &estimator) const {
    const auto rule=classify(e);
    if (rule.kind==RuleKind::Recommend) return Recommendation{e.resourceId,e.version,e.path,rule.reason,{},Source::Rule};
    if (rule.kind!=RuleKind::Unknown || !estimator || (e.description.empty() && e.text.empty())) return {};
    const auto estimate=estimator(e);
    if (!estimate || !finiteRange(estimate->successProbability,1) || !finiteRange(estimate->successGain,10000) ||
        !finiteRange(estimate->failureLoss,10000) || !finiteRange(estimate->cost,10000) || estimate->explanation.empty()) return {};
    namespace a=iiMultimodalAgent::agent;
    a::DecisionOptions options; options.valueUnit="uncalibrated-relative-utility";
    const std::array candidates{a::DecisionCandidate{"recommend","Recommend","Suggest potentially unfinished work",{}}};
    const a::DecisionInput input{options.valueUnit,{{"recommend",estimate->successGain,estimate->failureLoss,
        estimate->cost,estimate->successProbability,{}}},{}};
    if (!a::DecisionGate(options).evaluate(candidates,input).execute()) return {};
    return Recommendation{e.resourceId,e.version,e.path,"AI estimate",estimate->explanation.substr(0,1024),Source::ModelEstimate};
}
}

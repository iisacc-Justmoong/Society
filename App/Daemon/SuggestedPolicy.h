#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace society::suggested {

// The locator is display/opening data; resourceId and version have separate
// provider meanings and are never derived from one another or from a hash.
struct Evidence {
    std::string resourceId, version, path, name, description, text;
    std::vector<std::string> statuses;
    bool available = true;
};
enum class RuleKind { Recommend, Exclude, Unknown, Conflicting };
struct RuleResult { RuleKind kind = RuleKind::Unknown; std::string reason; };
struct Estimate {
    double successProbability = 0, successGain = 0, failureLoss = 0, cost = 0;
    std::string explanation;
};
enum class Source { Rule, ModelEstimate };
struct Recommendation {
    std::string resourceId, version, path, reason, explanation;
    Source source = Source::Rule;
};

class Policy final {
public:
    using Estimator = std::function<std::optional<Estimate>(const Evidence &)>;
    [[nodiscard]] RuleResult classify(const Evidence &) const;
    // Model estimates are uncalibrated inputs to iiDecision, never metadata or
    // permission. Missing/invalid evidence returns no recommendation.
    [[nodiscard]] std::optional<Recommendation> evaluate(const Evidence &, const Estimator &) const;
};
}

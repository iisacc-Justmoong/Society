#pragma once
#include "SuggestedPolicy.h"
#include <iiMultimodalAgent/agent/DecisionGate.h>
#include <iiMultimodalAgent/agent/Types.h>

namespace society::suggested {
// Resolve a text-model route through the same Decision policy as the agent.
// An empty result means that no candidate has sufficient decision evidence.
QString selectRoute(const iiMultimodalAgent::agent::DecisionOptions &, const QString &goal, const QString &runId);
// Host-only, text evidence projection. No file path, resource ID, tools or
// permissions are passed to the model. Returned values are uncalibrated.
std::vector<std::optional<Estimate>> assess(iiMultimodalAgent::agent::Model &, const QString &model,
    const std::vector<Evidence> &, const iiMultimodalAgent::CancellationToken &);
}

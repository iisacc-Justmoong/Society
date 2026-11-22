#pragma once

#include "Shared/BackendCommand.h"
#include <iiMultimodalAgent>
#include <functional>

// A host supplies the container-bound transport; the tool never opens storage
// or executes file mutations itself.
using SocietyResourceInvoker = std::function<SocietyBackendCommandReply(
    const QString &, QJsonObject, const iiMultimodalAgent::agent::ToolContext &)>;

// Expose a bounded read-only page of observed-hash candidates to both the
// Workspace agent and the public daemon MCP server.
[[nodiscard]] iiMultimodalAgent::agent::Tool societyDuplicateCandidatesTool(SocietyResourceInvoker invoke);

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

// Compare two limited, separately captured snapshots without granting
// permission to delete, merge or replace either source.
[[nodiscard]] iiMultimodalAgent::agent::Tool societyCompareBytesTool(SocietyResourceInvoker invoke);

// Index one observed text revision in a named local embedding space.
[[nodiscard]] iiMultimodalAgent::agent::Tool societyContentIndexTool(SocietyResourceInvoker invoke);

// Search indexed text embeddings without treating coverage as complete.
[[nodiscard]] iiMultimodalAgent::agent::Tool societyContentSearchTool(SocietyResourceInvoker invoke);

// Read or explicitly change the container's automatic text-indexing policy.
[[nodiscard]] iiMultimodalAgent::agent::Tool societyContentPolicyTool(SocietyResourceInvoker invoke);
[[nodiscard]] iiMultimodalAgent::agent::Tool societySetContentPolicyTool(SocietyResourceInvoker invoke);

// Expose existing metadata and virtual-directory indexes without interpreting
// the results as content embeddings or a complete filesystem inventory.
[[nodiscard]] iiMultimodalAgent::agent::Tool societyIndexedSearchTool(SocietyResourceInvoker invoke);

// Trace bounded user-asserted relations without reconciling or authorizing
// changes to the selected container.
[[nodiscard]] iiMultimodalAgent::agent::Tool societyLineageTool(SocietyResourceInvoker invoke);

// Explain recorded context and policy blockers without submitting a file job.
[[nodiscard]] iiMultimodalAgent::agent::Tool societyCleanupPlanTool(SocietyResourceInvoker invoke);

#pragma once
#include <iiMultimodalAgent>
#include <functional>

// Resolve the selected container on the caller's worker, not the GUI thread.
using SocietySemanticRoot = std::function<QString(const iiMultimodalAgent::agent::ToolContext &)>;
QList<iiMultimodalAgent::agent::Tool> societySemanticTools(SocietySemanticRoot root);

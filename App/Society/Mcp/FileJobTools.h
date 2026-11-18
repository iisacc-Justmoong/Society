#pragma once
#include "Shared/BackendCommand.h"
#include <iiMultimodalAgent>
#include <functional>

using SocietyFileJobInvoker = std::function<SocietyBackendCommandReply(
    const QString &, QJsonObject, const iiMultimodalAgent::agent::ToolContext &)>;
QList<iiMultimodalAgent::agent::Tool> societyFileJobTools(SocietyFileJobInvoker invoke);

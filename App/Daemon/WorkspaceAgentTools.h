#pragma once

#include "Shared/BackendCommand.h"
#include <iiMultimodalAgent/agent/Tools.h>
#include <functional>
#include <memory>

// The daemon owns these transports. A session captures its container so an
// action cannot silently follow a later change of selected drive.
struct WorkspaceAgentToolTransport {
    using Context = iiMultimodalAgent::agent::ToolContext;
    std::function<SocietyBackendCommandReply(const QString &, const QString &, const QJsonObject &)> command;
    std::function<SocietyBackendCommandReply(const QString &, const QString &, int, int)> directory;
    std::function<iiMultimodalAgent::agent::ToolResult(const QJsonObject &, const Context &)> question;
    std::function<iiMultimodalAgent::agent::ToolResult(const QString &, const QJsonObject &, const Context &)> window;
};

std::shared_ptr<iiMultimodalAgent::agent::ToolRegistry> workspaceAgentTools(
    const QString &container, std::shared_ptr<const WorkspaceAgentToolTransport> transport);

iiMultimodalAgent::agent::PermissionResponse workspaceAgentPermission(
    const QString &container, const std::shared_ptr<const WorkspaceAgentToolTransport> &transport,
    const iiMultimodalAgent::agent::ToolCall &call,
    const iiMultimodalAgent::agent::PermissionDecision &decision,
    const iiMultimodalAgent::agent::ToolContext &context);

#pragma once
#include <iiMultimodalAgent>
#include <QJsonObject>
#include <QString>

// Invoked on an MCP worker through the daemon's authenticated local channel.
iiMultimodalAgent::agent::ToolResult societyUiToolCall(
    const QString &brokerDirectory, const QString &name, const QJsonObject &arguments,
    const iiMultimodalAgent::agent::ToolContext &context, int timeoutMs = 55000);

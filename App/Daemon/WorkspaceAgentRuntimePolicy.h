#pragma once

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QString>
#include <cmath>
#include <limits>
#include <stdexcept>

inline QString workspaceAgentRuntimeConfigPath()
{
    const auto base=QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    if (base.isEmpty()) throw std::runtime_error("The per-user configuration directory is unavailable.");
    return QDir(base).filePath(QStringLiteral("iisacc/Society/agent-runtime.json"));
}

inline int workspaceAgentMaxGpuLayers(const QString &path)
{
    QFile file(path);
    if (!file.exists()) return -1;
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error("Cannot read the workspace agent runtime configuration.");
    constexpr qint64 maxConfigBytes=64*1024;
    const auto bytes=file.read(maxConfigBytes+1);
    if (file.error()!=QFileDevice::NoError || bytes.size()>maxConfigBytes)
        throw std::runtime_error("Cannot read the workspace agent runtime configuration.");
    QJsonParseError error;
    const auto document=QJsonDocument::fromJson(bytes,&error);
    if (error.error!=QJsonParseError::NoError || !document.isObject())
        throw std::runtime_error("The workspace agent runtime configuration must be a JSON object.");
    const auto root=document.object();
    if (!root.contains(QStringLiteral("llama"))) return -1;
    const auto llama=root.value(QStringLiteral("llama"));
    if (!llama.isObject()) throw std::runtime_error("The llama runtime configuration must be an object.");
    const auto value=llama.toObject().value(QStringLiteral("maxGpuLayers"));
    if (value.isUndefined()) return -1;
    const auto layers=value.toDouble(std::numeric_limits<double>::quiet_NaN());
    if (!value.isDouble() || !std::isfinite(layers) || std::trunc(layers)!=layers || layers < -1
        || layers > std::numeric_limits<int>::max())
        throw std::runtime_error("llama.maxGpuLayers must be -1 or a nonnegative integer.");
    return int(layers);
}

inline QJsonObject workspaceAgentModelLoadOptions(int maxGpuLayers)
{
    return maxGpuLayers == 0 ? QJsonObject{{QStringLiteral("cpu_repack"), false}} : QJsonObject{};
}

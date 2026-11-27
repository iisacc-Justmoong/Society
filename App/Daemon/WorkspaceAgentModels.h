#pragma once
#include <QJsonObject>
#include <QString>

// Bounded package-header inspection only. Materialization and checksum
// validation are performed separately by iiLocalDiffusion and iiLocalLLM.
QJsonObject inspectWorkspaceAgentModelPackage(const QString &path,const QString &role);

// Resolve one complete GGUF asset to a byte range within its original package.
// This inspects bounded metadata and magic only; it does not verify the asset
// checksum, create a copy, or authorize loading a changed source file.
QJsonObject inspectWorkspaceAgentModelRange(const QString &path,const QString &role);

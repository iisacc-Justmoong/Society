#pragma once
#include <QJsonObject>
#include <QString>

// Bounded package-header inspection only. Materialization and checksum
// validation are performed separately by iiLocalDiffusion and iiLocalLLM.
QJsonObject inspectWorkspaceAgentModelPackage(const QString &path,const QString &role);

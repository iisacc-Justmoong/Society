#pragma once

#include <QJsonObject>
#include <QString>

// Blocking storage work. The daemon must invoke this on an I/O worker.
namespace society::workspace {
[[nodiscard]] QJsonObject executeAdd(const QString &container, const QString &actor,
                                     const QJsonObject &request);
}

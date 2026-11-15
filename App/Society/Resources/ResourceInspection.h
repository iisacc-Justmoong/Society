#pragma once
#include <QJsonObject>
#include <QString>

namespace society::resources {
struct Inspection { QJsonObject snapshot; QString error; };
// Blocking and read-only; the daemon must call this on an I/O worker.
[[nodiscard]] Inspection inspect(const QString &container,const QString &logicalPath);
}

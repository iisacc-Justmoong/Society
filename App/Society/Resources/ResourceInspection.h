#pragma once
#include <QJsonObject>
#include <QString>

namespace society::resources {
struct Inspection { QJsonObject snapshot; QString error; };
// Blocking and read-only; the daemon must call this on an I/O worker.
[[nodiscard]] Inspection inspect(const QString &container,const QString &logicalPath);
// Find a bounded page of independently identified files whose last observed
// SHA-256 matches the reference. This does not compare current bytes, alter
// files, or establish that the index covers every file. A continuation is
// valid only for the same reference revision and unchanged provider index.
[[nodiscard]] Inspection duplicateCandidates(const QString &container,const QString &logicalPath,
                                             int limit,const QJsonObject &cursor = {});
}

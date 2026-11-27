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
// Compare two independently captured, size-limited snapshots of observed files.
// The snapshots are not atomic together and the result does not authorize any
// deduplication or deletion. A caller may supply a private temporary root;
// an empty root uses the system temporary directory. This call blocks on I/O.
[[nodiscard]] Inspection compareBytes(const QString &container,const QString &firstPath,
                                      const QString &secondPath,const QString &temporaryRoot = {});
// Search existing classification or a saved virtual directory without
// indexing files. Results are last-observed identities, and coverage remains
// unknown; continuations are bound to the query and observed index generations.
[[nodiscard]] Inspection searchIndexed(const QString &container,const QJsonObject &query,
                                       int limit,const QJsonObject &cursor = {});
}

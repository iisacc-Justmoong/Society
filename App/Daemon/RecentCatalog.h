#pragma once

#include <QString>
#include <QVector>
#include <atomic>

namespace society::recent {
struct Activity { QString path; qint64 atMs = 0; };
struct Item { QString path, name; qint64 activityMs = 0, size = 0, modifiedMs = 0; };
struct Snapshot {
    QVector<Item> items;
    QString error;
    bool partial = false;
};

// Blocking container operation: call only from a background worker. Activity
// is associated with a verified native identity, never with a filename or hash.
class RecentCatalog final {
public:
    static Snapshot update(const QString &container, const QVector<Activity> &activities,
                           int limit = 500, const std::atomic_bool *cancel = nullptr);
};
}

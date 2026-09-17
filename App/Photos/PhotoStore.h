#pragma once
#include "PhotoLibrary.h"
#include <QJsonArray>
#include <QMap>
#include <atomic>
#include <optional>
#include <SocietyDrive.h>

namespace society::photos {
// Serialized by PhotoController's worker, with a process lock for GUI/daemon
// handoff. Only validated IDs and resource indices are accepted from peers.
class PhotoStore final {
public:
    PhotoStore(QString container, std::shared_ptr<PhotoLibrary> library);
    bool open();
    using Progress = std::function<void(const QJsonObject &)>;
    bool refresh(const Progress &progress = {});
    QJsonArray catalog();
    bool hasOriginal(const QString &id);
    bool prepare(const QString &id);
    bool consume(const QString &id);
    bool trash(const QString &id);
    bool addFiles(const QStringList &paths);
    QJsonObject command(const QJsonObject &request);
    QString resourcePath(const QString &id, int index) const;
    QString previewPath(const QString &id) const;
    QString originalForViewing(const QString &id);
    QString errorString() const { return m_error; }
    QString containerId() const;
    void cancel() { m_cancelled = true; m_library->cancel(); }
    bool cancelled() const { return m_cancelled; }
    void releaseExports();
    static bool validRecord(const QJsonObject &record);
    static QString digestFile(const QString &path, QString *error = nullptr,
                              const std::function<bool()> &cancelled = {});
    static constexpr qint64 ChunkBytes = 256 * 1024;
private:
    bool intact();
    bool fail(const QString &error);
    bool load();
    bool save();
    bool saveReference(const QString &id, const QJsonObject &reference);
    bool writeRecord(const QJsonObject &record, const QString &path = {});
    bool remember(const Asset &asset, const QString &id, const QJsonObject &record);
    bool importLooseFiles();
    bool refreshImpl(const Progress &progress = {});
    void pruneExports();
    void releasePhoto(const QString &id);
    bool consumeImpl(const QString &id);
    QJsonObject readRecord(const QString &id) const;
    bool cacheValid(const QJsonObject &resource) const;
    QString hashResource(const QString &path, QString *error = nullptr) const;
    QString cachePath(const QString &hash) const;
    QString m_root, m_private, m_photos, m_error;
    std::optional<iiSocietyContainer::SocietyDrive> m_drive;
    std::shared_ptr<PhotoLibrary> m_library;
    QJsonObject m_local;
    QHash<QString, QJsonObject> m_references;
    QMap<QString, QString> m_paths;
    QHash<QString, QHash<QString, qint64>> m_leases;
    std::atomic_bool m_cancelled{false};
};
}

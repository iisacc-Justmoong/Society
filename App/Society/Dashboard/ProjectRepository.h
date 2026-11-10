#pragma once

#include <QString>
#include <QVariantList>
#include <QVariantMap>

// Application composition: iiProjectManager owns semantics, iiFileProvider
// owns package I/O and cataloging, and iiSocietyContainer owns the root.
class ProjectRepository final {
  public:
    struct Snapshot {
        QVariantList projects, resources, entities, relations, history;
        QVariantMap project, schema;
        QString selectedKey, warning;
    };
    ProjectRepository(QString containerPath, QString actorId);
    [[nodiscard]] Snapshot load(const QString &selectedKey = {}) const;
    [[nodiscard]] QString create(const QVariantMap &draft) const;
    void save(const QString &key, const QString &expectedRevision, const QVariantMap &draft) const;
    void addResource(const QString &key, const QString &expectedRevision, const QVariantMap &draft) const;
    void updateResource(const QString &key, const QString &expectedRevision, const QString &id,
                        const QVariantMap &draft) const;
    void removeResource(const QString &key, const QString &expectedRevision, const QString &id) const;
    void addEntity(const QString &key, const QString &expectedRevision, const QVariantMap &draft) const;
    void addRelation(const QString &key, const QString &expectedRevision, const QVariantMap &draft) const;
    void confirmRelation(const QString &key, const QString &expectedRevision, const QString &id) const;
    void removeRelation(const QString &key, const QString &expectedRevision, const QString &id) const;
    void addEntityType(const QString &key, const QString &expectedRevision, const QVariantMap &draft) const;
    void addRelationType(const QString &key, const QString &expectedRevision, const QVariantMap &draft) const;
    void addConstraint(const QString &key, const QString &expectedRevision, const QVariantMap &draft) const;
    void rebuildIndex(const QString &key) const;
    [[nodiscard]] QVariantMap validate(const QString &key, bool availability) const;
    [[nodiscard]] QString resourcePath(const QString &key, const QString &resourceId) const;

  private:
    QString container_, actor_;
};

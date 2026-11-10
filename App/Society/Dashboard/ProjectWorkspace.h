#pragma once
#include "ProjectRepository.h"
#include <QFutureWatcher>
#include <QObject>
#include <QUrl>
#include <QtQml/qqmlregistration.h>
#include <functional>

// View state is independent of the durable project model. All package I/O is
// dispatched away from the GUI thread and bound to the current container.
class ProjectWorkspace : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString containerPath READ containerPath WRITE setContainerPath NOTIFY configurationChanged)
    Q_PROPERTY(QString actorId READ actorId WRITE setActorId NOTIFY configurationChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY configurationChanged)
    Q_PROPERTY(QString viewMode READ viewMode WRITE setViewMode NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString errorString READ errorString NOTIFY changed)
    Q_PROPERTY(QString warning READ warning NOTIFY changed)
    Q_PROPERTY(QString selectedKey READ selectedKey NOTIFY changed)
    Q_PROPERTY(QVariantList projects READ projects NOTIFY changed)
    Q_PROPERTY(QVariantMap project READ project NOTIFY changed)
    Q_PROPERTY(QVariantList resources READ resources NOTIFY changed)
    Q_PROPERTY(QVariantList entities READ entities NOTIFY changed)
    Q_PROPERTY(QVariantList relations READ relations NOTIFY changed)
    Q_PROPERTY(QVariantList history READ history NOTIFY changed)
    Q_PROPERTY(QVariantMap schema READ schema NOTIFY changed)
    Q_PROPERTY(QVariantMap validation READ validation NOTIFY changed)
  public:
    explicit ProjectWorkspace(QObject *parent = nullptr);
    ~ProjectWorkspace() override;
    QString containerPath() const {
        return container_;
    }
    void setContainerPath(const QString &path);
    QString actorId() const {
        return actor_;
    }
    void setActorId(const QString &actor);
    bool active() const {
        return active_;
    }
    void setActive(bool active);
    QString viewMode() const {
        return mode_;
    }
    void setViewMode(const QString &mode);
    bool busy() const {
        return busy_;
    }
    QString errorString() const {
        return error_;
    }
    QString warning() const {
        return state_.warning;
    }
    QString selectedKey() const {
        return state_.selectedKey;
    }
    QVariantList projects() const {
        return state_.projects;
    }
    QVariantMap project() const {
        return state_.project;
    }
    QVariantList resources() const {
        return state_.resources;
    }
    QVariantList entities() const {
        return state_.entities;
    }
    QVariantList relations() const {
        return state_.relations;
    }
    QVariantList history() const {
        return state_.history;
    }
    QVariantMap schema() const {
        return state_.schema;
    }
    QVariantMap validation() const {
        return validation_;
    }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void openProject(const QString &key);
    Q_INVOKABLE void createProject(const QVariantMap &draft);
    Q_INVOKABLE void saveProject(const QVariantMap &draft);
    Q_INVOKABLE void addResource(const QVariantMap &draft);
    Q_INVOKABLE void updateResource(const QString &id, const QVariantMap &draft);
    Q_INVOKABLE void removeResource(const QString &id);
    Q_INVOKABLE void addEntity(const QVariantMap &draft);
    Q_INVOKABLE void addRelation(const QVariantMap &draft);
    Q_INVOKABLE void confirmRelation(const QString &id);
    Q_INVOKABLE void removeRelation(const QString &id);
    Q_INVOKABLE void addEntityType(const QVariantMap &draft);
    Q_INVOKABLE void addRelationType(const QVariantMap &draft);
    Q_INVOKABLE void addConstraint(const QVariantMap &draft);
    Q_INVOKABLE void rebuildIndex();
    Q_INVOKABLE void validateProject(bool checkAvailability = true);
    Q_INVOKABLE void openResource(const QString &id);
  signals:
    void configurationChanged();
    void changed();
    void resourceReady(const QUrl &url);

  private:
    struct Result {
        ProjectRepository::Snapshot snapshot;
        QVariantMap validation;
        QString error, resourcePath, targetKey;
        quint64 generation = 0;
        bool loaded = false, hasTarget = false;
    };
    using Operation = std::function<Result(ProjectRepository &, const QString &, const QString &)>;
    void run(Operation operation);
    void invalidate();
    QString effectiveActor() const;
    QFutureWatcher<Result> watcher_;
    ProjectRepository::Snapshot state_;
    QVariantMap validation_;
    QString container_, actor_, error_, mode_ = QStringLiteral("focus");
    bool active_ = false, busy_ = false, pending_ = false;
    quint64 generation_ = 0;
};

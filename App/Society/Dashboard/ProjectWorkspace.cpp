#include "ProjectWorkspace.h"
#include <QCryptographicHash>
#include <QSysInfo>
#include <QtConcurrent/QtConcurrentRun>
#include <utility>

ProjectWorkspace::ProjectWorkspace(QObject *parent) : QObject(parent) {
    connect(&watcher_, &QFutureWatcher<Result>::finished, this, [this] {
        const auto result = watcher_.result();
        busy_ = false;
        if (result.generation == generation_) {
            if (result.loaded)
                state_ = result.snapshot;
            error_ = result.error;
            validation_ = result.validation;
            if (!result.resourcePath.isEmpty())
                emit resourceReady(QUrl::fromLocalFile(result.resourcePath));
        }
        emit changed();
        if (pending_ && active_) {
            pending_ = false;
            refresh();
        }
    });
}
ProjectWorkspace::~ProjectWorkspace() {
    watcher_.waitForFinished();
}
void ProjectWorkspace::invalidate() {
    ++generation_;
    state_ = {};
    validation_.clear();
    error_.clear();
    pending_ = busy_;
    emit changed();
    if (!busy_ && active_)
        refresh();
}
void ProjectWorkspace::setContainerPath(const QString &path) {
    if (container_ == path)
        return;
    container_ = path;
    emit configurationChanged();
    invalidate();
}
void ProjectWorkspace::setActorId(const QString &actor) {
    if (actor_ == actor)
        return;
    actor_ = actor;
    emit configurationChanged();
    invalidate();
}
void ProjectWorkspace::setActive(bool active) {
    if (active_ == active)
        return;
    active_ = active;
    emit configurationChanged();
    if (active_)
        refresh();
}
void ProjectWorkspace::setViewMode(const QString &mode) {
    if (mode != "focus" && mode != "catalog" && mode != "gallery" && mode != "studio")
        return;
    if (mode_ == mode)
        return;
    mode_ = mode;
    emit changed();
}
QString ProjectWorkspace::effectiveActor() const {
    if (!actor_.trimmed().isEmpty())
        return actor_.trimmed();
    // Explicitly local attribution; this never claims an iiAccountManager user.
    auto id = QSysInfo::machineUniqueId();
    if (id.isEmpty())
        id = QByteArrayLiteral("anonymous-local-device");
    return QStringLiteral("urn:iisacc:local-actor:") +
           QString::fromLatin1(QCryptographicHash::hash(id, QCryptographicHash::Sha256).toHex().left(24));
}
void ProjectWorkspace::run(Operation operation) {
    if (busy_) {
        error_ = tr("Please wait for the current project operation to finish.");
        emit changed();
        return;
    }
    if (container_.isEmpty()) {
        error_ = tr("Open Society storage to use Projects.");
        emit changed();
        return;
    }
    busy_ = true;
    error_.clear();
    emit changed();
    const auto container = container_, actor = effectiveActor(), key = state_.selectedKey;
    const auto revision = state_.project.value(QStringLiteral("revision")).toString();
    const auto generation = generation_;
    watcher_.setFuture(
        QtConcurrent::run([container, actor, key, revision, generation, operation = std::move(operation)] {
            Result result;
            result.generation = generation;
            ProjectRepository repo(container, actor);
            try {
                result = operation(repo, key, revision);
                result.generation = generation;
                if (!result.loaded) {
                    result.snapshot = repo.load(result.hasTarget ? result.targetKey : key);
                    result.loaded = true;
                }
            } catch (const std::exception &e) {
                result.error = QString::fromUtf8(e.what());
                // A commit can succeed before Catalog publication fails. Refresh
                // even on error so the UI reflects the canonical current state.
                try {
                    result.snapshot = repo.load(key);
                    result.loaded = true;
                } catch (...) {
                    try {
                        result.snapshot = repo.load();
                        result.loaded = true;
                    } catch (...) {
                    }
                }
            }
            return result;
        }));
}
void ProjectWorkspace::refresh() {
    if (!active_)
        return;
    if (busy_) {
        pending_ = true;
        return;
    }
    if (container_.isEmpty()) {
        state_ = {};
        emit changed();
        return;
    }
    run([](auto &, const auto &, const auto &) { return Result{}; });
}
void ProjectWorkspace::openProject(const QString &key) {
    run([key](auto &, const auto &, const auto &) {
        Result r;
        r.targetKey = key;
        r.hasTarget = true;
        return r;
    });
}
void ProjectWorkspace::createProject(const QVariantMap &draft) {
    run([draft](auto &repo, const auto &, const auto &) {
        Result r;
        r.targetKey = repo.create(draft);
        r.hasTarget = true;
        return r;
    });
}
void ProjectWorkspace::saveProject(const QVariantMap &draft) {
    run([draft](auto &repo, const auto &key, const auto &revision) {
        const auto expected = draft.value(QStringLiteral("_expectedRevision"), revision).toString();
        repo.save(key, expected, draft);
        return Result{};
    });
}
void ProjectWorkspace::addResource(const QVariantMap &draft) {
    run([draft](auto &repo, const auto &key, const auto &revision) {
        repo.addResource(key, revision, draft);
        return Result{};
    });
}
void ProjectWorkspace::updateResource(const QString &id, const QVariantMap &draft) {
    run([id, draft](auto &repo, const auto &key, const auto &revision) {
        repo.updateResource(key, draft.value(QStringLiteral("_expectedRevision"), revision).toString(), id,
                            draft);
        return Result{};
    });
}
void ProjectWorkspace::removeResource(const QString &id) {
    run([id](auto &repo, const auto &key, const auto &revision) {
        repo.removeResource(key, revision, id);
        return Result{};
    });
}
void ProjectWorkspace::addEntity(const QVariantMap &draft) {
    run([draft](auto &repo, const auto &key, const auto &revision) {
        repo.addEntity(key, revision, draft);
        return Result{};
    });
}
void ProjectWorkspace::addRelation(const QVariantMap &draft) {
    run([draft](auto &repo, const auto &key, const auto &revision) {
        repo.addRelation(key, revision, draft);
        return Result{};
    });
}
void ProjectWorkspace::confirmRelation(const QString &id) {
    run([id](auto &repo, const auto &key, const auto &revision) {
        repo.confirmRelation(key, revision, id);
        return Result{};
    });
}
void ProjectWorkspace::removeRelation(const QString &id) {
    run([id](auto &repo, const auto &key, const auto &revision) {
        repo.removeRelation(key, revision, id);
        return Result{};
    });
}
void ProjectWorkspace::addEntityType(const QVariantMap &draft) {
    run([draft](auto &repo, const auto &key, const auto &revision) {
        repo.addEntityType(key, revision, draft);
        return Result{};
    });
}
void ProjectWorkspace::addRelationType(const QVariantMap &draft) {
    run([draft](auto &repo, const auto &key, const auto &revision) {
        repo.addRelationType(key, revision, draft);
        return Result{};
    });
}
void ProjectWorkspace::addConstraint(const QVariantMap &draft) {
    run([draft](auto &repo, const auto &key, const auto &revision) {
        repo.addConstraint(key, revision, draft);
        return Result{};
    });
}
void ProjectWorkspace::rebuildIndex() {
    run([](auto &repo, const auto &key, const auto &) {
        repo.rebuildIndex(key);
        return Result{};
    });
}
void ProjectWorkspace::validateProject(bool availability) {
    run([availability](auto &repo, const auto &key, const auto &) {
        Result r;
        r.validation = repo.validate(key, availability);
        return r;
    });
}
void ProjectWorkspace::openResource(const QString &id) {
    run([id](auto &repo, const auto &key, const auto &) {
        Result r;
        r.resourcePath = repo.resourcePath(key, id);
        return r;
    });
}

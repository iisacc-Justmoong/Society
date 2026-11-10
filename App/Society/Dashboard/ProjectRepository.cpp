#include "ProjectRepository.h"
#include "ProjectResourceBinding.h"
#include <ProviderPackage.h>
#include <iiProjectManager/ProjectManager.h>
#include <FileCatalog.h>
#include <SharedStorage.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QRegularExpression>
#include <QUrl>
#include <QUuid>
#include <algorithm>
#include <filesystem>
#include <stdexcept>

namespace fs = std::filesystem;
using namespace iiProjectManager;
using iiProjectManagerFileProvider::ProviderPackage;
namespace {
std::string utf8(const QString &s) {
    const auto bytes = s.toUtf8();
    return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
}
QString text(const std::string &s) {
    return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
}
fs::path nativePath(const QString &s) {
#ifdef Q_OS_WIN
    return fs::path(s.toStdWString());
#else
    const auto bytes = QFile::encodeName(s);
    return fs::path(std::string(bytes.constData(), static_cast<std::size_t>(bytes.size())));
#endif
}
QString qpath(const fs::path &p) {
#ifdef Q_OS_WIN
    return QString::fromStdWString(p.native());
#else
    const auto &s = p.native();
    return QFile::decodeName(QByteArray(s.data(), static_cast<qsizetype>(s.size())));
#endif
}
std::vector<std::string> strings(const QVariant &value) {
    std::vector<std::string> out;
    for (const auto &item : value.toStringList())
        if (!item.trimmed().isEmpty())
            out.push_back(utf8(item.trimmed()));
    return out;
}
QStringList qstrings(const std::vector<std::string> &values) {
    QStringList out;
    for (const auto &v : values)
        out.append(text(v));
    return out;
}
QString uuid() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}
std::string newId(const char *kind) {
    return std::string("urn:iisacc:") + kind + ":" + utf8(uuid());
}
Actor actor(const QString &id) {
    if (id.trimmed().isEmpty())
        throw std::runtime_error("A user identity is required to edit a project.");
    return {ActorKind::user, utf8(id)};
}
QString rootPath(const QString &container, bool create) {
    QString error;
    auto storage = iiSocietyContainer::SharedStorage::open(container, &error);
    if (!storage)
        throw std::runtime_error(
            utf8(error.isEmpty() ? QStringLiteral("Society storage is unavailable.") : error));
    auto path = create ? storage->ensureDirectory(iiSocietyContainer::StoreSection::Files,
                                                  QStringLiteral("Projects"), &error)
                       : storage->filePath(iiSocietyContainer::StoreSection::Files,
                                           QStringLiteral("Projects"), &error);
    if (path.isEmpty())
        throw std::runtime_error(
            utf8(error.isEmpty() ? QStringLiteral("Projects directory is unavailable.") : error));
    return path;
}
QString catalogRoot(const QString &container) {
    QString error;
    auto storage = iiSocietyContainer::SharedStorage::open(container, &error);
    if (!storage)
        throw std::runtime_error(
            utf8(error.isEmpty() ? QStringLiteral("Society storage is unavailable.") : error));
    const auto path = storage->filePath(iiSocietyContainer::StoreSection::Files, {}, &error);
    if (path.isEmpty())
        throw std::runtime_error(
            utf8(error.isEmpty() ? QStringLiteral("Files directory is unavailable.") : error));
    return path;
}
std::string status(const Manifest &m) {
    auto doc = QJsonDocument::fromJson(QByteArray::fromStdString(m.extensionsJson));
    return utf8(doc.object().value(QStringLiteral("society:status")).toString(QStringLiteral("draft")));
}
QVariantMap info(const Project &p, const QString &key, const QString &container,
                 iiFileProvider::FileCatalog &catalog) {
    const auto m = p.manifest();
    const auto g = p.goal();
    const auto extensions = QJsonDocument::fromJson(QByteArray::fromStdString(m.extensionsJson)).object();
    const auto cover = extensions.value(QStringLiteral("society:coverResource")).toString();
    QString preview;
    if (!cover.isEmpty()) {
        const auto graph = p.snapshot();
        const auto resource = std::ranges::find(graph.resources, utf8(cover), &Resource::id);
        if (resource != graph.resources.end() && resource->mediaType.starts_with("image/")) {
            try {
                const auto resolved = ProjectResourceBinding::resolve(*resource, p, container, catalog);
                if (resolved.availability == Availability::available)
                    preview = QUrl::fromLocalFile(text(resolved.locator)).toString();
            } catch (const std::exception &) {
                // A missing or inaccessible cover never hides the project.
            }
        }
    }
    return {{"key", key},
            {"projectId", text(m.projectId)},
            {"name", text(m.name)},
            {"owner", text(m.owner)},
            {"createdAt", text(m.createdAt)},
            {"revision", QString::number(m.revision)},
            {"goalId", text(g.id)},
            {"goal", text(g.statement)},
            {"requirements", qstrings(g.requirements)},
            {"constraints", qstrings(g.constraints)},
            {"completionConditions", qstrings(g.completionConditions)},
            {"specifiedBy", text(g.specifiedBy)},
            {"status", text(status(m))},
            {"coverResource", cover},
            {"preview", preview}};
}
std::string relPath(const fs::path &p) {
    auto v = p.generic_u8string();
    return {reinterpret_cast<const char *>(v.data()), v.size()};
}
struct Opened {
    iiFileProvider::CatalogFile item;
    std::shared_ptr<ProviderPackage> port;
    Project project;
    iiFileProvider::FileCatalog *catalog;
};
Opened open(iiFileProvider::FileCatalog &catalog, const QString &key, const QString &actorId) {
    const auto current = catalog.lookup(utf8(key));
    if (!current)
        throw std::runtime_error("The project is no longer registered.");
    if (catalog.itemKind(current->key) != iiFileProvider::CatalogEntryKind::Package)
        throw std::runtime_error("The selected item is not a project package.");
    const auto refreshed = catalog.registerPackage(current->path);
    const auto path = catalog.resolvePackage(refreshed.file.key, refreshed.file.contentVersion);
    auto port = std::make_shared<ProviderPackage>(path);
    auto p = Project::open(port, actor(actorId), Capabilities::editor());
    return {refreshed.file, std::move(port), std::move(p), &catalog};
}
void expected(const Project &p, const QString &revision) {
    bool ok = false;
    const auto n = revision.toULongLong(&ok);
    if (!ok || n != p.manifest().revision)
        throw std::runtime_error("The project changed since it was opened. Reload before saving.");
}
void sync(iiFileProvider::FileCatalog &catalog, const iiFileProvider::CatalogFile &item, const Project &p) {
    auto metadata = item.metadata;
    metadata.title = p.manifest().name;
    metadata.description = p.goal().statement;
    metadata.fields["iisacc:type"] = "project";
    metadata.fields["society:status"] = status(p.manifest());
    const auto result = catalog.registerPackage(item.path, metadata);
    if (!result.metadataWritten)
        throw std::runtime_error("Project saved, but its catalog metadata is pending: " +
                                 result.metadataError);
}
template <class F>
void change(const QString &container, const QString &actorId, const QString &key, const QString &revision,
            F fn) {
    iiFileProvider::FileCatalog catalog(nativePath(catalogRoot(container)),
                                        iiFileProvider::FileCatalog::Access::ReadWrite);
    auto item = open(catalog, key, actorId);
    expected(item.project, revision);
    auto transaction = item.project.begin();
    fn(item, transaction);
    (void)transaction.commit();
    sync(catalog, item.item, item.project);
}
QVariantList entries(const std::vector<EntityType> &types) {
    QVariantList out;
    for (const auto &v : types)
        out.append(QVariantMap{{"id", text(v.id)},
                               {"label", text(v.label)},
                               {"description", text(v.description)},
                               {"parents", qstrings(v.parents)}});
    return out;
}
QVariantList relations(const std::vector<RelationType> &types) {
    QVariantList out;
    for (const auto &v : types)
        out.append(QVariantMap{{"id", text(v.id)},
                               {"label", text(v.label)},
                               {"description", text(v.description)},
                               {"sourceTypes", qstrings(v.sourceTypes)},
                               {"targetTypes", qstrings(v.targetTypes)},
                               {"acyclic", v.acyclic}});
    return out;
}
std::string origin(RelationOrigin o) {
    switch (o) {
    case RelationOrigin::userAsserted:
        return "user_asserted";
    case RelationOrigin::inferred:
        return "inferred";
    case RelationOrigin::observed:
        return "observed";
    }
    return {};
}
ValueType valueType(const QString &v) {
    if (v == "string")
        return ValueType::string;
    if (v == "integer")
        return ValueType::integer;
    if (v == "number")
        return ValueType::number;
    if (v == "boolean")
        return ValueType::boolean;
    if (v == "object")
        return ValueType::object;
    if (v == "array")
        return ValueType::array;
    if (v == "any")
        return ValueType::any;
    throw std::runtime_error("Unknown property value type.");
}
} // namespace

ProjectRepository::ProjectRepository(QString containerPath, QString actorId)
    : container_(std::move(containerPath)), actor_(std::move(actorId)) {}
ProjectRepository::Snapshot ProjectRepository::load(const QString &selectedKey) const {
    Snapshot out;
    const auto root = rootPath(container_, false);
    if (!QFileInfo::exists(root))
        return out;
    iiFileProvider::FileCatalog catalog(nativePath(catalogRoot(container_)));
    // Register only direct package directories, never their assets. Invalid
    // packages remain untouched and are reported to the user.
    for (const auto &entry : fs::directory_iterator(nativePath(root))) {
        std::error_code ec;
        const auto state = entry.symlink_status(ec);
        if (ec || fs::is_symlink(state) || !fs::is_directory(state) ||
            entry.path().extension() != ".iiproject")
            continue;
        try {
            auto port = std::make_shared<ProviderPackage>(entry.path());
            auto p = Project::open(port, actor(actor_), Capabilities::readOnly());
            const auto relative = std::string("Projects/") + relPath(entry.path().filename());
            auto prior = catalog.lookupPath(relative);
            iiFileProvider::FileMetadata metadata = prior ? prior->metadata : iiFileProvider::FileMetadata{};
            metadata.title = p.manifest().name;
            metadata.description = p.goal().statement;
            metadata.fields["iisacc:type"] = "project";
            metadata.fields["society:status"] = status(p.manifest());
            const auto record = catalog.registerPackage(relative, metadata);
            if (!record.metadataWritten)
                out.warning +=
                    QObject::tr("Catalog metadata pending for %1. ").arg(qpath(entry.path().filename()));
            out.projects.append(info(p, text(record.file.key), container_, catalog));
        } catch (const std::exception &e) {
            out.warning += QObject::tr("Could not load %1: %2. ")
                               .arg(qpath(entry.path().filename()), QString::fromUtf8(e.what()));
        }
    }
    std::sort(out.projects.begin(), out.projects.end(), [](const QVariant &a, const QVariant &b) {
        return QString::localeAwareCompare(a.toMap().value("name").toString(),
                                           b.toMap().value("name").toString()) < 0;
    });
    if (selectedKey.isEmpty())
        return out;
    auto opened = open(catalog, selectedKey, actor_);
    const auto graph = opened.project.snapshot();
    out.selectedKey = selectedKey;
    out.project = info(opened.project, selectedKey, container_, catalog);
    for (const auto &v : graph.resources)
        out.resources.append(
            QVariantMap{{"id", text(v.id)},
                        {"name", text(v.name)},
                        {"purpose", text(v.purpose)},
                        {"locator", ProjectResourceBinding::displayLocator(v, container_, catalog)},
                        {"assetId", v.location == ResourceLocation::reference
                                        ? ProjectResourceBinding::assetId(ProjectResourceBinding::stored(v))
                                        : QString()},
                        {"mediaType", text(v.mediaType)},
                        {"location", v.location == ResourceLocation::embedded ? QStringLiteral("embedded")
                                                                              : QStringLiteral("reference")},
                        {"derived", v.derived},
                        {"selectedBy", text(v.selectedBy)},
                        {"sha256", v.sha256 ? text(*v.sha256) : QString()},
                        {"size", v.size ? QString::number(*v.size) : QString()}});
    for (const auto &v : graph.entities)
        out.entities.append(QVariantMap{{"id", text(v.id)},
                                        {"type", text(v.type)},
                                        {"name", text(v.name)},
                                        {"description", text(v.description)},
                                        {"declaredBy", text(v.declaredBy)}});
    for (const auto &v : graph.relations)
        out.relations.append(QVariantMap{{"id", text(v.id)},
                                         {"type", text(v.type)},
                                         {"subject", text(v.subject)},
                                         {"object", text(v.object)},
                                         {"note", text(v.note)},
                                         {"origin", text(origin(v.origin))},
                                         {"actor", text(v.actorId)},
                                         {"confirmedBy", v.confirmedBy ? text(*v.confirmedBy) : QString()},
                                         {"confirmedAt", v.confirmedAt ? text(*v.confirmedAt) : QString()}});
    const auto schema = opened.project.schema();
    out.schema = {{"entities", entries(schema.entities)},
                  {"relations", relations(schema.relations)},
                  {"constraintCount", static_cast<int>(schema.constraints.size())}};
    for (const auto &v : opened.project.history(64).entries)
        out.history.append(QVariantMap{{"revision", QString::number(v.revision)},
                                       {"actor", text(v.actor.id)},
                                       {"timestamp", text(v.timestamp)},
                                       {"changes", qstrings(v.changes)}});
    return out;
}
QString ProjectRepository::create(const QVariantMap &draft) const {
    auto creator = actor(actor_);
    const auto root = rootPath(container_, true);
    iiFileProvider::FileCatalog catalog(nativePath(catalogRoot(container_)));
    const auto name = draft.value("name").toString().trimmed(),
               statement = draft.value("goal").toString().trimmed();
    if (name.isEmpty() || statement.isEmpty())
        throw std::runtime_error("Project name and goal are required.");
    const auto leaf = "Project-" + utf8(uuid()) + ".iiproject";
    const auto relative = std::string("Projects/") + leaf;
    auto port = std::make_shared<ProviderPackage>(nativePath(root) / fs::path(leaf));
    Manifest manifest{.projectId = newId("project"), .name = utf8(name)};
    Goal goal{.id = newId("goal"),
              .statement = utf8(statement),
              .requirements = strings(draft.value("requirements")),
              .constraints = strings(draft.value("constraints")),
              .completionConditions = strings(draft.value("completionConditions"))};
    auto p = Project::create(port, std::move(manifest), std::move(goal), defaultSchema(), creator,
                             Capabilities::editor());
    iiFileProvider::FileMetadata metadata;
    metadata.title = p.manifest().name;
    metadata.description = p.goal().statement;
    metadata.fields = {{"iisacc:type", "project"}, {"society:status", "draft"}};
    const auto result = catalog.registerPackage(relative, metadata);
    if (!result.metadataWritten)
        throw std::runtime_error("Project was created, but catalog metadata is pending: " +
                                 result.metadataError);
    return text(result.file.key);
}
void ProjectRepository::save(const QString &key, const QString &revision, const QVariantMap &draft) const {
    const auto requestedStatus = draft.value("status", QStringLiteral("draft")).toString();
    if (requestedStatus != "draft" && requestedStatus != "in_progress" && requestedStatus != "completed" &&
        requestedStatus != "on_hold")
        throw std::runtime_error("Unknown project status.");
    change(container_, actor_, key, revision, [&](auto &item, auto &tx) {
        auto goal = item.project.goal();
        goal.statement = utf8(draft.value("goal").toString().trimmed());
        goal.requirements = strings(draft.value("requirements"));
        goal.constraints = strings(draft.value("constraints"));
        goal.completionConditions = strings(draft.value("completionConditions"));
        tx.setName(utf8(draft.value("name").toString().trimmed()));
        tx.setGoal(std::move(goal));
        QJsonObject extensions{
            {QStringLiteral("society:status"), draft.value("status", QStringLiteral("draft")).toString()}};
        if (draft.contains("coverResource")) {
            const auto cover = draft.value("coverResource").toString();
            if (!cover.isEmpty()) {
                const auto graph = item.project.snapshot();
                const auto match = std::ranges::find(graph.resources, utf8(cover), &Resource::id);
                if (match == graph.resources.end() || !match->mediaType.starts_with("image/"))
                    throw std::runtime_error("Choose an image resource for the project cover.");
            }
            extensions.insert(QStringLiteral("society:coverResource"), cover);
        }
        tx.mergeManifestExtensions(QJsonDocument(extensions).toJson(QJsonDocument::Compact).toStdString());
    });
}
void ProjectRepository::addResource(const QString &key, const QString &revision,
                                    const QVariantMap &draft) const {
    bool copied = false;
    try {
        change(container_, actor_, key, revision, [&](auto &item, auto &tx) {
            const auto loc = draft.value("location").toString();
            if (loc != "embedded" && loc != "reference")
                throw std::runtime_error("Choose embedded or reference storage.");
            auto source = draft.value("locator").toString().trimmed();
            const auto url = QUrl(source);
            if (url.isLocalFile())
                source = url.toLocalFile();
            Resource resource{.id = newId("resource"),
                              .name = utf8(draft.value("name").toString().trimmed()),
                              .purpose = utf8(draft.value("purpose").toString().trimmed()),
                              .locator = utf8(source),
                              .mediaType = utf8(draft.value("mediaType").toString().trimmed()),
                              .location = loc == "embedded" ? ResourceLocation::embedded
                                                            : ResourceLocation::reference,
                              .derived = draft.value("derived").toBool()};
            if (resource.name.empty() || resource.purpose.empty() || resource.locator.empty())
                throw std::runtime_error("Resource name, purpose and locator are required.");
            const auto hash = draft.value("sha256").toString().trimmed();
            if (!hash.isEmpty()) {
                if (!QRegularExpression(QStringLiteral("^[0-9a-f]{64}$")).match(hash).hasMatch())
                    throw std::runtime_error("SHA-256 must contain 64 lowercase hexadecimal characters.");
                resource.sha256 = utf8(hash);
            }
            const auto binding = ProjectResourceBinding::capture(container_, *item.catalog, source,
                                                                   resource.location == ResourceLocation::embedded);
            const auto graph = item.project.snapshot();
            for (const auto &existing : graph.resources) {
                if (!ProjectResourceBinding::sameInput(existing, binding, source,
                                                       resource.location == ResourceLocation::embedded))
                    continue;
                if (existing.name == resource.name && existing.purpose == resource.purpose &&
                    existing.mediaType == resource.mediaType && existing.derived == resource.derived &&
                    existing.sha256 == resource.sha256)
                    return;
                throw std::runtime_error("This source is already in the project. Edit its existing entry instead.");
            }
            ProjectResourceBinding::attach(resource, binding);
            if (resource.location == ResourceLocation::embedded) {
                const QFileInfo file(source);
                if (!file.isAbsolute() || !file.isFile())
                    throw std::runtime_error("Choose an existing local file to embed.");
                QString safe = file.fileName();
                safe.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")), QStringLiteral("_"));
                if (safe.isEmpty() || safe == "." || safe == "..")
                    safe = "asset";
                resource.locator = "assets/" + utf8(uuid()) + "/" + utf8(safe);
                item.port->importFile(nativePath(source), resource.locator);
                copied = true;
                resource.size = static_cast<std::uint64_t>(file.size());
            }
            tx.putResource(std::move(resource));
        });
    } catch (const std::exception &e) {
        if (copied)
            throw std::runtime_error(
                std::string(e.what()) +
                " A copied asset may remain in the package; refresh the project before retrying.");
        throw;
    }
}
void ProjectRepository::updateResource(const QString &key, const QString &revision, const QString &id,
                                       const QVariantMap &draft) const {
    change(container_, actor_, key, revision, [&](auto &item, auto &tx) {
        const auto graph = item.project.snapshot();
        const auto match = std::ranges::find(graph.resources, utf8(id), &Resource::id);
        if (match == graph.resources.end())
            throw std::runtime_error("The resource no longer exists.");
        auto resource = *match;
        resource.name = utf8(draft.value("name").toString().trimmed());
        resource.purpose = utf8(draft.value("purpose").toString().trimmed());
        resource.mediaType = utf8(draft.value("mediaType").toString().trimmed());
        resource.derived = draft.value("derived").toBool();
        const auto extensions =
            QJsonDocument::fromJson(QByteArray::fromStdString(item.project.manifest().extensionsJson))
                .object();
        if (extensions.value(QStringLiteral("society:coverResource")).toString() == id &&
            !resource.mediaType.starts_with("image/"))
            throw std::runtime_error(
                "Clear the project cover before changing this resource to a non-image type.");
        if (resource.location == ResourceLocation::reference) {
            auto locator = draft.value("locator").toString().trimmed();
            const auto url = QUrl(locator);
            if (url.isLocalFile())
                locator = url.toLocalFile();
            ProjectResourceBinding::checkLocatorEdit(resource, locator, container_, *item.catalog);
            if (ProjectResourceBinding::stored(resource).isEmpty())
                ProjectResourceBinding::attach(resource,
                    ProjectResourceBinding::capture(container_, *item.catalog, locator, false));
            if (utf8(locator) != resource.locator)
                resource.size.reset();
            resource.locator = utf8(locator);
        }
        if (resource.name.empty() || resource.purpose.empty() || resource.locator.empty())
            throw std::runtime_error("Resource name, purpose and locator are required.");
        const auto hash = draft.value("sha256").toString().trimmed();
        if (!hash.isEmpty() && !QRegularExpression(QStringLiteral("^[0-9a-f]{64}$")).match(hash).hasMatch())
            throw std::runtime_error("SHA-256 must contain 64 lowercase hexadecimal characters.");
        resource.sha256 = hash.isEmpty() ? std::nullopt : std::optional<std::string>(utf8(hash));
        tx.putResource(std::move(resource));
    });
}
void ProjectRepository::removeResource(const QString &key, const QString &revision, const QString &id) const {
    change(container_, actor_, key, revision, [&](auto &item, auto &tx) {
        tx.removeResource(utf8(id));
        const auto extensions =
            QJsonDocument::fromJson(QByteArray::fromStdString(item.project.manifest().extensionsJson))
                .object();
        if (extensions.value(QStringLiteral("society:coverResource")).toString() == id) {
            QJsonObject update{{QStringLiteral("society:coverResource"), QString()}};
            tx.mergeManifestExtensions(QJsonDocument(update).toJson(QJsonDocument::Compact).toStdString());
        }
    });
}
void ProjectRepository::addEntity(const QString &key, const QString &revision,
                                  const QVariantMap &draft) const {
    change(container_, actor_, key, revision, [&](auto &, auto &tx) {
        tx.putEntity({.id = newId("entity"),
                      .type = utf8(draft.value("type").toString()),
                      .name = utf8(draft.value("name").toString().trimmed()),
                      .description = utf8(draft.value("description").toString())});
    });
}
void ProjectRepository::addRelation(const QString &key, const QString &revision,
                                    const QVariantMap &draft) const {
    change(container_, actor_, key, revision, [&](auto &, auto &tx) {
        tx.putRelation({.id = newId("relation"),
                        .type = utf8(draft.value("type").toString()),
                        .subject = utf8(draft.value("subject").toString()),
                        .object = utf8(draft.value("object").toString()),
                        .note = utf8(draft.value("note").toString())});
    });
}
void ProjectRepository::confirmRelation(const QString &key, const QString &revision,
                                        const QString &id) const {
    change(container_, actor_, key, revision,
           [&](auto &item, auto &tx) { tx.confirmRelation(utf8(id), item.port->utcNow()); });
}
void ProjectRepository::removeRelation(const QString &key, const QString &revision, const QString &id) const {
    change(container_, actor_, key, revision, [&](auto &, auto &tx) { tx.removeRelation(utf8(id)); });
}
void ProjectRepository::addEntityType(const QString &key, const QString &revision,
                                      const QVariantMap &draft) const {
    change(container_, actor_, key, revision, [&](auto &item, auto &tx) {
        auto schema = item.project.schema();
        schema.entities.push_back({.id = utf8(draft.value("id").toString()),
                                   .label = utf8(draft.value("label").toString()),
                                   .description = utf8(draft.value("description").toString()),
                                   .parents = strings(draft.value("parents"))});
        tx.setSchema(std::move(schema));
    });
}
void ProjectRepository::addRelationType(const QString &key, const QString &revision,
                                        const QVariantMap &draft) const {
    change(container_, actor_, key, revision, [&](auto &item, auto &tx) {
        auto schema = item.project.schema();
        schema.relations.push_back({.id = utf8(draft.value("id").toString()),
                                    .label = utf8(draft.value("label").toString()),
                                    .description = utf8(draft.value("description").toString()),
                                    .sourceTypes = strings(draft.value("sourceTypes")),
                                    .targetTypes = strings(draft.value("targetTypes")),
                                    .acyclic = draft.value("acyclic").toBool()});
        tx.setSchema(std::move(schema));
    });
}
void ProjectRepository::addConstraint(const QString &key, const QString &revision,
                                      const QVariantMap &draft) const {
    change(container_, actor_, key, revision, [&](auto &item, auto &tx) {
        auto schema = item.project.schema();
        schema.constraints.push_back({.id = utf8(draft.value("id").toString()),
                                      .entityType = utf8(draft.value("entityType").toString()),
                                      .property = utf8(draft.value("property").toString()),
                                      .description = utf8(draft.value("description").toString()),
                                      .valueType = valueType(draft.value("valueType", "any").toString()),
                                      .required = draft.value("required").toBool()});
        tx.setSchema(std::move(schema));
    });
}
void ProjectRepository::rebuildIndex(const QString &key) const {
    iiFileProvider::FileCatalog catalog(nativePath(catalogRoot(container_)),
                                        iiFileProvider::FileCatalog::Access::ReadWrite);
    auto item = open(catalog, key, actor_);
    auto index = item.port->openIndex(true);
    auto project = Project::open(item.port, actor(actor_), Capabilities::editor(), index);
    project.rebuildIndex();
}
QVariantMap ProjectRepository::validate(const QString &key, bool availability) const {
    iiFileProvider::FileCatalog catalog(nativePath(catalogRoot(container_)),
                                        iiFileProvider::FileCatalog::Access::ReadWrite);
    auto item = open(catalog, key, actor_);
    auto report = item.project.validate(false);
    QStringList errors, warnings;
    for (const auto &v : report.errors)
        errors << text(v.path + ": " + v.message);
    for (const auto &v : report.warnings)
        warnings << text(v.path + ": " + v.message);
    if (availability && report.ok()) {
        for (const auto &resource : item.project.snapshot().resources) {
            try {
                const auto result = ProjectResourceBinding::resolve(resource, item.project, container_, catalog);
                if (result.availability != Availability::available)
                    warnings << text(resource.id + ": resource is not currently available: " + result.detail);
            } catch (const std::exception &error) {
                warnings << text(resource.id) + ": " + QString::fromUtf8(error.what());
            }
        }
    }
    return {{"ok", report.ok()}, {"errors", errors}, {"warnings", warnings}};
}
QString ProjectRepository::resourcePath(const QString &key, const QString &id) const {
    iiFileProvider::FileCatalog catalog(nativePath(catalogRoot(container_)),
                                        iiFileProvider::FileCatalog::Access::ReadWrite);
    auto item = open(catalog, key, actor_);
    const auto graph = item.project.snapshot();
    const auto resource = std::ranges::find(graph.resources, utf8(id), &Resource::id);
    if (resource == graph.resources.end())
        throw std::runtime_error("The resource no longer exists.");
    const auto r = ProjectResourceBinding::resolve(*resource, item.project, container_, catalog);
    if (r.availability != Availability::available)
        throw std::runtime_error("The resource is unavailable or requires an external resolver: " + r.detail);
    return text(r.locator);
}

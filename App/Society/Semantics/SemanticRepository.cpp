#include "SemanticRepository.h"
#include "PhysicalIdentity.h"
#include "RelocationVerification.h"

#include <iiFileProvider>
#include <ProviderPackage.h>
#include <iiSocietyContainer>
#include <iiProjectManager>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QUrl>
#include <QUuid>
#include <algorithm>
#include <deque>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>

namespace {
using namespace iiFileProvider;
using iiProjectManager::Actor;
using iiProjectManager::ActorKind;
using iiProjectManager::Capabilities;
using iiProjectManager::Entity;
using iiProjectManager::Project;
using iiProjectManager::Relation;
using iiProjectManager::Resource;
using iiProjectManager::ResourceLocation;
using iiProjectManagerFileProvider::ProviderPackage;
using Storage = iiSocietyContainer::SharedStorage;
using Repository = iiSocietyContainer::FileRepository;
namespace fs = std::filesystem;

constexpr auto addressClass = "society:semantic:Address";
constexpr auto relationClass = "society:semantic:Relation";
constexpr auto registryClass = "society:semantic:Registry";
constexpr auto lookupClass = "society:semantic:AddressLookup";
constexpr auto physicalClass = "society:semantic:PhysicalIdentity";
constexpr auto relocationClass = "society:semantic:Relocation";
constexpr auto addressProperty = "society:semantic:address";
constexpr auto metadataProperty = "society:semantic:metadata";
constexpr auto sourceProperty = "society:semantic:source";
constexpr auto targetProperty = "society:semantic:target";
constexpr auto payloadProperty = "society:semantic:relation";
constexpr auto projectProperty = "society:semantic:project";
constexpr auto currentProperty = "society:semantic:currentAddress";
constexpr auto bindingProperty = "society:semantic:binding";
constexpr auto physicalProperty = "society:semantic:physicalIdentity";
constexpr auto projectIdentityProperty = "society:semantic:projectIdentity";
constexpr auto relocationProperty = "society:semantic:relocation";
constexpr auto pendingProperty = "society:semantic:pendingRelocation";
constexpr auto registryKey = "urn:iisacc:semantic:project-registry";
constexpr auto projectMarker = "iisacc:semanticProject";
constexpr auto projectMetadata = "iisacc:semanticMetadata";
constexpr auto targetMarker = "iisacc:semanticTarget";
constexpr auto linkMarker = "iisacc:semanticRelation";
constexpr qsizetype maxJsonBytes = 48 * 1024;
constexpr std::size_t maxResults = 1024;

std::string utf8(const QString &value) {
    const auto bytes = value.toUtf8();
    return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
}
QString text(const std::string &value) { return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size())); }
fs::path nativePath(const QString &value) {
#ifdef Q_OS_WIN
    return fs::path(value.toStdWString());
#else
    const auto bytes = QFile::encodeName(value);
    return fs::path(std::string(bytes.constData(), static_cast<std::size_t>(bytes.size())));
#endif
}
QString qpath(const fs::path &path) {
#ifdef Q_OS_WIN
    return QString::fromStdWString(path.native());
#else
    const auto &bytes = path.native();
    return QFile::decodeName(QByteArray(bytes.data(), static_cast<qsizetype>(bytes.size())));
#endif
}
void validText(const QString &value, qsizetype maximum, const char *what) {
    if (value.trimmed().isEmpty() || value.contains(QChar::Null) || value.toUtf8().size() > maximum)
        throw std::runtime_error(std::string("Invalid semantic ") + what);
}
std::uint64_t expected(const QString &revision) {
    bool ok = false;
    const auto value = revision.toULongLong(&ok);
    if (!ok || revision != QString::number(value) || value > std::uint64_t((std::numeric_limits<std::int64_t>::max)()))
        throw std::runtime_error("An exact decimal semantic revision is required");
    return value;
}
QByteArray jsonBytes(const QJsonObject &value) {
    const auto bytes = QJsonDocument(value).toJson(QJsonDocument::Compact);
    if (bytes.size() > maxJsonBytes) throw std::runtime_error("Semantic metadata exceeds the size limit");
    return bytes;
}
QJsonObject jsonObject(const std::string &value) {
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(QByteArray(value.data(), static_cast<qsizetype>(value.size())), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        throw std::runtime_error("Invalid stored semantic JSON object");
    return document.object();
}
std::string jsonString(const QJsonObject &value) {
    const auto bytes = jsonBytes(value);
    return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
}
QString hash(const QByteArray &value) {
    return QString::fromLatin1(QCryptographicHash::hash(value, QCryptographicHash::Sha256).toHex());
}
std::string addressKey(const QString &address) {
    return utf8(QStringLiteral("urn:iisacc:semantic:address:") + hash(address.toUtf8()));
}
std::string lookupKey(const QString &address) {
    return utf8(QStringLiteral("urn:iisacc:semantic:lookup:") + hash(address.toUtf8()));
}
std::string physicalKey(const QString &identity) {
    return utf8(QStringLiteral("urn:iisacc:semantic:physical:") + hash(identity.toUtf8()));
}
std::string relocationKey(const QString &source, const QString &destination) {
    return utf8(QStringLiteral("urn:iisacc:semantic:move:")
        + hash(QJsonDocument(QJsonArray{source, destination}).toJson(QJsonDocument::Compact)));
}
QString relationId(const QString &source, const QString &predicate, const QString &target) {
    const auto bytes = QJsonDocument(QJsonArray{source, predicate, target}).toJson(QJsonDocument::Compact);
    return QStringLiteral("urn:iisacc:semantic:relation:") + hash(bytes);
}
std::string selfKey(const std::string &projectId) {
    return utf8(QStringLiteral("urn:iisacc:semantic:project:") + hash(QByteArray(projectId.data(), static_cast<qsizetype>(projectId.size()))));
}
std::string targetKey(const QString &address) {
    return utf8(QStringLiteral("urn:iisacc:semantic:target:") + hash(address.toUtf8()));
}
Storage storageFor(const QString &path) {
    QString error;
    const auto storage = Storage::open(path, &error);
    if (!storage || !storage->drive().isReady())
        throw std::runtime_error(utf8(error.isEmpty() ? QStringLiteral("Society storage is unavailable") : error));
    return *storage;
}
std::unique_ptr<Repository> openRepository(const Storage &storage, const QString &actor, bool write) {
    if (!write) {
        const auto &drive = storage.drive();
        const auto path = Repository::packagePath(nativePath(drive.rootPath())) / "objects.sqlite3";
        if (!fs::exists(path)) return {};
    }
    iiSocietyContainer::FileRepositoryContext context;
    context.author.origin = "local:semantic";
    context.author.subject = utf8(actor);
    context.description = "Addressed semantic metadata";
    return storage.fileRepository(std::move(context), write ? Repository::Access::ReadWrite : Repository::Access::ReadOnly);
}
std::optional<fs::path> projectPath(const Storage &storage, const QString &address) {
    auto withoutTrailingSlash = address;
    while (withoutTrailingSlash.endsWith('/')) withoutTrailingSlash.chop(1);
    const bool packageAddress = withoutTrailingSlash.endsWith(QStringLiteral(".iiproject"), Qt::CaseInsensitive);
    QString path;
    if (address.startsWith(QStringLiteral("file:"), Qt::CaseInsensitive)) {
        const QUrl url(address);
        if (!url.isLocalFile()) {
            if (url.path().endsWith(QStringLiteral(".iiproject"), Qt::CaseInsensitive))
                throw std::runtime_error("A project address must resolve to a local package");
            return {};
        }
        path = url.toLocalFile();
    } else if (QDir::isAbsolutePath(address)) {
        path = address;
    } else {
        const auto slash = withoutTrailingSlash.indexOf('/');
        if (slash > 0) {
            const auto key = withoutTrailingSlash.left(slash);
            for (const auto section : storage.drive().sections()) {
                if (iiSocietyContainer::storeSectionKey(section) != key) continue;
                QString error;
                path = storage.filePath(section, withoutTrailingSlash.mid(slash + 1), &error);
                if (path.isEmpty() && packageAddress)
                    throw std::runtime_error(utf8(error.isEmpty() ? QStringLiteral("Project address is unavailable") : error));
                break;
            }
        }
    }
    const auto candidate = path.isEmpty() ? withoutTrailingSlash : QDir::cleanPath(path);
    if (!candidate.endsWith(QStringLiteral(".iiproject"), Qt::CaseInsensitive)) return {};
    if (path.isEmpty() || !QDir::isAbsolutePath(path))
        throw std::runtime_error("A project address must resolve to a local package");
    const QFileInfo info(path);
    if (info.isFile() && !info.isSymLink()) return {};
    if (!info.exists() || !info.isDir() || info.isSymLink() || info.canonicalFilePath() != QDir::cleanPath(info.absoluteFilePath()))
        throw std::runtime_error("The project package is unavailable or redirected");
    return nativePath(info.absoluteFilePath());
}
QString localAddress(const Storage &storage, const QString &address) {
    if (address.startsWith(QStringLiteral("file:"), Qt::CaseInsensitive)) {
        const QUrl url(address);
        return url.isLocalFile() ? QDir::cleanPath(url.toLocalFile()) : QString();
    }
    if (QDir::isAbsolutePath(address)) return QDir::cleanPath(address);
    auto normalized = address;
    while (normalized.endsWith('/')) normalized.chop(1);
    const auto slash = normalized.indexOf('/');
    if (slash <= 0) return {};
    for (const auto section : storage.drive().sections()) {
        if (iiSocietyContainer::storeSectionKey(section) == normalized.left(slash))
            return storage.filePath(section, normalized.mid(slash + 1));
    }
    return {};
}
int addressStyle(const QString &address) {
    if (address.startsWith(QStringLiteral("file:"), Qt::CaseInsensitive)) return 1;
    return QDir::isAbsolutePath(address) ? 2 : 3;
}
std::optional<QString> physicalIdentity(const Storage &storage, const QString &address) {
    return society::semantics::physicalIdentity(localAddress(storage, address));
}
Project openProject(const fs::path &path, const QString &actor, bool write) {
    return Project::open(std::make_shared<ProviderPackage>(path), Actor{ActorKind::user, utf8(actor)},
                         write ? Capabilities::editor() : Capabilities::readOnly());
}
std::optional<QString> projectIdentity(const Storage &storage, const QString &address) {
    auto normalized = address;
    while (normalized.endsWith('/')) normalized.chop(1);
    if (!normalized.endsWith(QStringLiteral(".iiproject"), Qt::CaseInsensitive)) return {};
    const auto path = localAddress(storage, address);
    const QFileInfo info(path);
    if (path.isEmpty() || !info.isDir() || info.isSymLink() || info.isJunction()) return {};
    const auto project = openProject(nativePath(path), QStringLiteral("urn:iisacc:semantic:identity"), false);
    return QStringLiteral("project:") + text(project.manifest().projectId);
}
std::optional<QString> verificationStamp(const Storage &storage, const QString &address) {
    const auto path = localAddress(storage, address);
    if (QFileInfo(path).isFile()) {
        try { return text(iiFileProvider::ObjectSource::inspect(nativePath(path)).stamp); }
        catch (const std::exception &) { return {}; }
    }
    if (const auto physical = physicalIdentity(storage, address)) return *physical;
    return projectIdentity(storage, address);
}
society::semantics::DestinationObservation destinationObservation(const Storage &storage, const QString &address) {
    const auto physical = physicalIdentity(storage, address);
    const auto stamp = verificationStamp(storage, address);
    if (!stamp || physicalIdentity(storage, address) != physical)
        throw std::runtime_error("The asserted relocation destination cannot be verified");
    return {physical.value_or(QString()), *stamp};
}
void expectProject(const Project &project, const QString &revision) {
    if (project.manifest().revision != expected(revision)) throw std::runtime_error("Project semantic revision conflict");
}
QJsonObject refreshProjectCatalog(const Storage &storage, const fs::path &path) {
    // The project is already committed. A catalog refresh is a separate,
    // recoverable projection and never changes the project's canonical graph.
    try {
        const auto &drive = storage.drive();
        const auto absolute = qpath(path);
        const auto section = drive.sectionForPath(absolute);
        if (!section) return {{"catalogStatus", "not_registered"}};
        const auto root = drive.sectionPath(*section);
        if (!QFileInfo::exists(root + QStringLiteral("/.iisacc-file-provider/catalog.sqlite3")))
            return {{"catalogStatus", "not_registered"}};
        const auto relative = QDir(root).relativeFilePath(absolute);
        iiFileProvider::FileCatalog catalog(nativePath(root), iiFileProvider::FileCatalog::Access::ReadWrite,
            iiSocietyContainer::FileRepository::classificationDirectory(nativePath(drive.rootPath()),
                iiSocietyContainer::storeSectionKey(*section).toStdString()));
        const auto existing = catalog.lookupPath(utf8(relative));
        if (existing && catalog.itemKind(existing->key) != iiFileProvider::CatalogEntryKind::Package)
            return {{"catalogStatus", "not_registered"}};
        const auto refreshed = catalog.registerPackage(utf8(relative));
        if (!refreshed.metadataWritten)
            return {{"catalogStatus", "pending"}, {"catalogError", text(refreshed.metadataError)}};
        return {{"catalogStatus", "ready"}};
    } catch (const std::exception &error) {
        return {{"catalogStatus", "pending"}, {"catalogError", QString::fromUtf8(error.what())}};
    }
}
void appendStatus(QJsonObject &result, const QJsonObject &status) {
    for (auto it = status.begin(); it != status.end(); ++it) result.insert(it.key(), it.value());
}

std::vector<OntologyStatement> facts(const OntologyFileSystem &graph, const OntologyResource &subject) {
    std::vector<OntologyStatement> result;
    std::int64_t cursor = 0;
    for (;;) {
        const auto page = graph.statements(subject, cursor, 256);
        if (page.empty()) return result;
        result.insert(result.end(), page.begin(), page.end());
        cursor = page.back().sequence;
    }
}
std::optional<OntologyValue> fact(const std::vector<OntologyStatement> &values, const char *key) {
    const auto it = std::find_if(values.begin(), values.end(), [&](const auto &entry) { return entry.propertyKey == key; });
    return it == values.end() ? std::nullopt : std::optional(it->value);
}
std::string factText(const std::vector<OntologyStatement> &values, const char *key) {
    const auto value = fact(values, key);
    if (!value) return {};
    const auto *result = std::get_if<std::string>(&value->data);
    if (!result) throw std::runtime_error("Invalid semantic property type");
    return *result;
}
void schema(OntologyFileSystem &graph, std::vector<OntologyChange> &changes) {
    for (const auto *key : {addressClass, relationClass, registryClass, lookupClass, physicalClass, relocationClass}) {
        const auto existing = graph.ontologyClass(key);
        if (!existing) changes.emplace_back(OntologyClass{key, key, "", {}});
        else if (!existing->parents.empty()) throw std::runtime_error("Reserved semantic class has an incompatible definition");
    }
    const std::vector<OntologyProperty> properties{
        {addressProperty, "Address", OntologyValueKind::Text, addressClass, "", true},
        {metadataProperty, "Metadata", OntologyValueKind::Text, addressClass, "", true},
        {sourceProperty, "Source", OntologyValueKind::Resource, relationClass, addressClass, true},
        {targetProperty, "Target", OntologyValueKind::Resource, relationClass, addressClass, true},
        {payloadProperty, "Relation", OntologyValueKind::Text, relationClass, "", true},
        {projectProperty, "Registered project", OntologyValueKind::Resource, registryClass, addressClass, false},
        {currentProperty, "Current address", OntologyValueKind::Text, addressClass, "", true},
        {bindingProperty, "Address binding", OntologyValueKind::Resource, lookupClass, addressClass, true},
        {physicalProperty, "Physical identity", OntologyValueKind::Resource, addressClass, physicalClass, true},
        {projectIdentityProperty, "Project identity", OntologyValueKind::Resource, addressClass, physicalClass, true},
        {relocationProperty, "Relocation intent", OntologyValueKind::Text, relocationClass, "", true},
        {pendingProperty, "Pending relocation", OntologyValueKind::Resource, registryClass, relocationClass, false}};
    for (const auto &property : properties) {
        const auto existing = graph.property(property.key);
        if (!existing) changes.emplace_back(property);
        else if (existing->kind != property.kind || existing->domainClass != property.domainClass ||
                 existing->rangeClass != property.rangeClass || existing->singleValue != property.singleValue || existing->acyclic)
            throw std::runtime_error("Reserved semantic property has an incompatible definition");
    }
}
void replace(OntologyFileSystem &graph, std::vector<OntologyChange> &changes,
             const OntologyResource &subject, const char *key, const OntologyValue &value) {
    const auto old = fact(facts(graph, subject), key);
    if (old && *old == value) return;
    if (old) changes.emplace_back(OntologyAssertion{subject, key, *old, false});
    changes.emplace_back(OntologyAssertion{subject, key, value});
}
std::optional<OntologyResource> resourceFact(const std::vector<OntologyStatement> &values, const char *key) {
    const auto value = fact(values, key);
    if (!value) return {};
    const auto *result = std::get_if<OntologyResource>(&value->data);
    if (!result || result->kind != OntologyResourceKind::Entity) throw std::runtime_error("Invalid semantic resource reference");
    return *result;
}
bool hasMeaning(OntologyFileSystem &graph, const OntologyResource &resource) {
    const auto metadata = factText(facts(graph, resource), metadataProperty);
    if (!metadata.empty() && !jsonObject(metadata).isEmpty()) return true;
    for (const auto *property : {sourceProperty, targetProperty, projectProperty}) {
        if (!graph.property(property)) continue;
        const auto references = graph.traverse(resource, property, OntologyDirection::Incoming, 1, 1);
        if (references.truncated || !references.nodes.empty()) return true;
    }
    return false;
}
QString currentAddress(OntologyFileSystem &graph, const OntologyResource &resource) {
    const auto values = facts(graph, resource);
    const auto current = factText(values, currentProperty);
    return text(current.empty() ? factText(values, addressProperty) : current);
}
struct AddressResolution { OntologyResource resource; QString identity; };
std::optional<AddressResolution> resolveAddress(const Storage &storage, OntologyFileSystem &graph,
        std::vector<OntologyChange> &changes, const QString &address, bool create) {
    const auto physical = physicalIdentity(storage, address);
    const auto project = projectIdentity(storage, address);
    const auto lookup = OntologyResource::entity(lookupKey(address));
    std::optional<OntologyResource> bound;
    if (graph.entity(lookup.key)) bound = resourceFact(facts(graph, lookup), bindingProperty);
    if (!bound && graph.entity(addressKey(address))) {
        const auto legacy = OntologyResource::entity(addressKey(address));
        if (currentAddress(graph, legacy) == address) bound = legacy;
    }
    const auto matches = [&](const OntologyResource &candidate) {
        if (!graph.entity(candidate.key) || currentAddress(graph, candidate) != address) return false;
        const auto values = facts(graph, candidate);
        const auto stored = resourceFact(values, physicalProperty);
        const auto projectId = resourceFact(values, projectIdentityProperty);
        if (project && projectId && projectId->key != physicalKey(*project)) return false;
        if (!physical || !stored || stored->key == physicalKey(*physical)) return true;
        return project && projectId && projectId->key == physicalKey(*project);
    };
    if (bound && !matches(*bound)) bound.reset();
    if (bound && physical && !resourceFact(facts(graph, *bound), physicalProperty) && graph.property(physicalProperty)) {
        const auto node = OntologyResource::entity(physicalKey(*physical));
        if (graph.entity(node.key)) {
            const auto found = graph.traverse(node, physicalProperty, OntologyDirection::Incoming, 1, maxResults);
            if (found.truncated) throw std::runtime_error("Semantic physical identity is ambiguous");
            for (const auto &entry : found.nodes) {
                if (entry.resource == *bound) continue;
                const auto oldProject = resourceFact(facts(graph, entry.resource), projectIdentityProperty);
                if (project && oldProject && oldProject->key != physicalKey(*project)) continue;
                const auto old = currentAddress(graph, entry.resource);
                if (physicalIdentity(storage, old) != physical) {
                    if (hasMeaning(graph, *bound)) throw std::runtime_error("The destination already has different semantic metadata");
                    bound.reset(); break;
                }
            }
        }
    }
    if (bound && project && !resourceFact(facts(graph, *bound), projectIdentityProperty) && graph.property(projectIdentityProperty)) {
        const auto node = OntologyResource::entity(physicalKey(*project));
        if (graph.entity(node.key)) {
            const auto found = graph.traverse(node, projectIdentityProperty, OntologyDirection::Incoming, 1, maxResults);
            if (found.truncated) throw std::runtime_error("Semantic project identity is ambiguous");
            for (const auto &entry : found.nodes) {
                if (entry.resource != *bound && projectIdentity(storage, currentAddress(graph, entry.resource)) != project) {
                    if (hasMeaning(graph, *bound)) throw std::runtime_error("The destination already has different semantic metadata");
                    bound.reset(); break;
                }
            }
        }
    }
    if (!bound && physical && graph.property(physicalProperty)) {
        const auto node = OntologyResource::entity(physicalKey(*physical));
        if (graph.entity(node.key)) {
            const auto found = graph.traverse(node, physicalProperty, OntologyDirection::Incoming, 1, maxResults);
            if (found.truncated) throw std::runtime_error("Semantic physical identity is ambiguous");
            std::optional<OntologyResource> preferred, alternate;
            bool alternateAmbiguous = false;
            for (const auto &entry : found.nodes) {
                const auto oldProject = resourceFact(facts(graph, entry.resource), projectIdentityProperty);
                if (project && oldProject && oldProject->key != physicalKey(*project)) continue;
                const auto old = currentAddress(graph, entry.resource);
                if (old != address && physicalIdentity(storage, old) == physical) continue; // A live hard link is a separate address.
                if (addressStyle(old) == addressStyle(address)) {
                    if (preferred && *preferred != entry.resource) throw std::runtime_error("Semantic physical identity is ambiguous");
                    preferred = entry.resource;
                } else if (alternate && *alternate != entry.resource) alternateAmbiguous = true;
                else alternate = entry.resource;
            }
            if (preferred) bound = preferred;
            else if (alternateAmbiguous) throw std::runtime_error("Semantic physical identity is ambiguous");
            else bound = alternate;
        }
    }
    if (!bound && project && graph.property(projectIdentityProperty)) {
        const auto node = OntologyResource::entity(physicalKey(*project));
        if (graph.entity(node.key)) {
            const auto found = graph.traverse(node, projectIdentityProperty, OntologyDirection::Incoming, 1, maxResults);
            if (found.truncated) throw std::runtime_error("Semantic project identity is ambiguous");
            std::optional<OntologyResource> preferred, alternate;
            bool alternateAmbiguous = false;
            for (const auto &entry : found.nodes) {
                const auto old = currentAddress(graph, entry.resource);
                if (old != address && projectIdentity(storage, old) == project) continue;
                if (addressStyle(old) == addressStyle(address)) {
                    if (preferred && *preferred != entry.resource) throw std::runtime_error("Semantic project identity is ambiguous");
                    preferred = entry.resource;
                } else if (alternate && *alternate != entry.resource) alternateAmbiguous = true;
                else alternate = entry.resource;
            }
            if (preferred) bound = preferred;
            else if (alternateAmbiguous) throw std::runtime_error("Semantic project identity is ambiguous");
            else bound = alternate;
        }
    }
    if (!bound && !create) return {};
    QString token;
    if (!bound) {
        token = address;
        if (graph.entity(addressKey(token))) token = QStringLiteral("urn:iisacc:semantic:identity:") + QUuid::createUuid().toString(QUuid::WithoutBraces);
        bound = OntologyResource::entity(addressKey(token));
        changes.emplace_back(OntologyEntity{bound->key, utf8(token), ""});
        changes.emplace_back(OntologyTypeAssignment{*bound, addressClass});
        changes.emplace_back(OntologyAssertion{*bound, addressProperty, OntologyValue::text(utf8(token))});
        changes.emplace_back(OntologyAssertion{*bound, currentProperty, OntologyValue::text(utf8(address))});
    } else {
        const auto values = facts(graph, *bound);
        token = text(factText(values, addressProperty));
        if (token.isEmpty()) throw std::runtime_error("Semantic address identity is unavailable");
        const auto old = currentAddress(graph, *bound);
        if (old != address) {
            const auto oldLookup = OntologyResource::entity(lookupKey(old));
            if (graph.entity(oldLookup.key)) {
                const auto previous = resourceFact(facts(graph, oldLookup), bindingProperty);
                if (previous && *previous == *bound)
                    changes.emplace_back(OntologyAssertion{oldLookup, bindingProperty, OntologyValue::resource(*bound), false});
            }
            replace(graph, changes, *bound, currentProperty, OntologyValue::text(utf8(address)));
        }
    }
    if (!graph.entity(lookup.key)) {
        changes.emplace_back(OntologyEntity{lookup.key, utf8(address), ""});
        changes.emplace_back(OntologyTypeAssignment{lookup, lookupClass});
    }
    replace(graph, changes, lookup, bindingProperty, OntologyValue::resource(*bound));
    if (physical) {
        const auto node = OntologyResource::entity(physicalKey(*physical));
        if (!graph.entity(node.key)) {
            changes.emplace_back(OntologyEntity{node.key, utf8(*physical), ""});
            changes.emplace_back(OntologyTypeAssignment{node, physicalClass});
        }
        replace(graph, changes, *bound, physicalProperty, OntologyValue::resource(node));
    }
    if (project) {
        const auto node = OntologyResource::entity(physicalKey(*project));
        if (!graph.entity(node.key)) {
            changes.emplace_back(OntologyEntity{node.key, utf8(*project), ""});
            changes.emplace_back(OntologyTypeAssignment{node, physicalClass});
        }
        replace(graph, changes, *bound, projectIdentityProperty, OntologyValue::resource(node));
    }
    return AddressResolution{*bound, token};
}
std::optional<AddressResolution> synchronizeAddress(const Storage &storage, const QString &actor, const QString &address) {
    const auto reader = openRepository(storage, actor, false);
    if (!reader) return {};
    const auto refreshPackage = [&] {
        auto normalized = address;
        while (normalized.endsWith('/')) normalized.chop(1);
        if (!normalized.endsWith(QStringLiteral(".iiproject"), Qt::CaseInsensitive)) return;
        const auto path = localAddress(storage, address);
        if (path.isEmpty() || !QFileInfo(path).isDir()) return;
        const auto section = storage.drive().sectionForPath(path);
        if (!section) return;
        const auto root = storage.drive().sectionPath(*section);
        if (!QFileInfo::exists(root + QStringLiteral("/.iisacc-file-provider/catalog.sqlite3"))) return;
        {
            iiFileProvider::FileCatalog catalog(nativePath(root), iiFileProvider::FileCatalog::Access::ReadOnly);
            const auto existing = catalog.lookupPath(utf8(QDir(root).relativeFilePath(path)));
            if (existing && catalog.itemKind(existing->key) == iiFileProvider::CatalogEntryKind::Package) return;
        }
        const auto status = refreshProjectCatalog(storage, nativePath(path));
        if (status.value("catalogStatus") == "pending")
            throw std::runtime_error(utf8(status.value("catalogError").toString()));
    };
    {
        std::vector<OntologyChange> changes;
        const auto result = resolveAddress(storage, reader->ontology(), changes, address, false);
        if (!result || changes.empty()) { if (result) refreshPackage(); return result; }
    }
    auto repository = openRepository(storage, actor, true);
    auto &graph = repository->ontology();
    for (int attempt = 0; attempt != 4; ++attempt) {
        const auto revision = graph.revision();
        std::vector<OntologyChange> body;
        const auto result = resolveAddress(storage, graph, body, address, false);
        if (!result || body.empty()) { if (result) refreshPackage(); return result; }
        std::vector<OntologyChange> changes; schema(graph, changes);
        changes.insert(changes.end(), body.begin(), body.end());
        try { graph.apply(changes, revision, repository->sessionKey()); refreshPackage(); return result; }
        catch (const std::exception &) { if (graph.revision() == revision || attempt == 3) throw; }
    }
    throw std::runtime_error("Semantic address changed while being resolved");
}
QString recordedLocalAddress(const Storage &storage, const QString &address) {
    if (addressStyle(address) != 3) return localAddress(storage, address);
    auto normalized = address;
    while (normalized.endsWith('/')) normalized.chop(1);
    const auto slash = normalized.indexOf('/');
    if (slash <= 0) return {};
    const auto relative = normalized.mid(slash + 1);
    if (relative.isEmpty() || QDir::cleanPath(relative) != relative || relative.startsWith("../") || relative == "..") return {};
    for (const auto section : storage.drive().sections()) {
        if (iiSocietyContainer::storeSectionKey(section) == normalized.left(slash))
            return QDir(storage.drive().sectionPath(section)).filePath(relative);
    }
    return {};
}
QString relocatedAddress(const Storage &storage, const QString &original, const QString &path) {
    if (addressStyle(original) == 1) return QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded);
    if (addressStyle(original) == 2) return path;
    for (const auto section : storage.drive().sections()) {
        const auto root = storage.drive().sectionPath(section);
        if (path.startsWith(root + '/'))
            return iiSocietyContainer::storeSectionKey(section) + '/' + QDir(root).relativeFilePath(path);
    }
    return path; // A resource outside the container needs an absolute address.
}
std::vector<OntologyResource> journalEntities(OntologyFileSystem &graph, const char *prefix) {
    std::set<std::string> keys;
    std::int64_t cursor = 0;
    for (;;) {
        const auto page = graph.journal(cursor, 256);
        if (page.empty()) break;
        for (const auto &event : page) {
            cursor = event.sequence;
            if (event.operation != "entity" || event.afterJson.empty()) continue;
            const auto key = jsonObject(event.afterJson).value("key").toString();
            if (key.startsWith(QLatin1String(prefix))) keys.insert(utf8(key));
            if (keys.size() > 250000) throw std::runtime_error("Semantic relocation inventory exceeds the safe limit");
        }
    }
    std::vector<OntologyResource> result;
    for (const auto &key : keys) if (graph.entity(key)) result.push_back(OntologyResource::entity(key));
    return result;
}
bool reconcileExternalMoves(const Storage &storage, const QString &actor) {
    const auto repository = openRepository(storage, actor, false);
    if (!repository) return false;
    auto &graph = repository->ontology();
    if (!graph.property(physicalProperty)) return false;
    struct Missing { OntologyResource resource; QString address; };
    std::map<QString, std::vector<Missing>> missing;
    for (const auto &resource : journalEntities(graph, "urn:iisacc:semantic:address:")) {
        const auto values = facts(graph, resource);
        const auto before = currentAddress(graph, resource);
        const auto identity = resourceFact(values, physicalProperty);
        const auto projectId = resourceFact(values, projectIdentityProperty);
        QString knownProject;
        std::optional<QString> currentProject;
        if (projectId) {
            const auto identityNode = graph.entity(projectId->key);
            if (!identityNode) throw std::runtime_error("A semantic project identity is unavailable");
            knownProject = text(identityNode->label);
            try { currentProject = projectIdentity(storage, before); } catch (const std::exception &) {}
        }
        if (identity) {
            const auto physical = graph.entity(identity->key);
            if (!physical) throw std::runtime_error("A semantic physical identity is unavailable");
            const auto known = text(physical->label);
            if (physicalIdentity(storage, before) != known) {
                if (projectId && currentProject == knownProject) (void)synchronizeAddress(storage, actor, before);
                else missing[known].push_back({resource, before});
            }
        }
        if (projectId && currentProject != knownProject) missing[knownProject].push_back({resource, before});
    }
    if (missing.empty()) return false;
    std::map<QString, QString> matches;
    std::set<QString> ambiguous;
    const bool lookForProjects = std::any_of(missing.begin(), missing.end(), [](const auto &entry) {
        return entry.first.startsWith(QStringLiteral("project:"));
    });
    QStringList directories;
    for (const auto section : storage.drive().sections()) directories.append(storage.drive().sectionPath(section));
    std::size_t visited = 0;
    bool incomplete = false;
    while (!directories.isEmpty()) {
        const auto directory = directories.takeLast();
        QDirIterator entries(directory, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
        while (entries.hasNext()) {
            entries.next(); const auto entry = entries.fileInfo();
            if (++visited > 250000) { incomplete = true; directories.clear(); break; }
            if (entry.isSymLink() || entry.isJunction() || (!entry.isDir() && !entry.isFile())) continue;
            const auto name = entry.fileName();
            if (name == ".iisacc-file-provider" || name.startsWith(".society-") || name.startsWith(".iiwebserver-")) continue;
            const auto path = entry.absoluteFilePath();
            const auto remember = [&](const std::optional<QString> &identity) {
                if (!identity || !missing.contains(*identity)) return;
                const auto old = matches.find(*identity);
                if (old != matches.end() && old->second != path) ambiguous.insert(*identity);
                else matches[*identity] = path;
            };
            remember(physicalIdentity(storage, path));
            if (lookForProjects && entry.isDir() && entry.suffix().compare(QStringLiteral("iiproject"), Qt::CaseInsensitive) == 0) {
                try { remember(projectIdentity(storage, path)); } catch (const std::exception &) {}
            }
            if (entry.isDir() && entry.suffix().compare(QStringLiteral("iiproject"), Qt::CaseInsensitive) != 0)
                directories.append(path);
        }
    }
    std::set<std::string> updated, unresolved;
    for (const auto &[identity, entries] : missing)
        for (const auto &entry : entries) unresolved.insert(entry.resource.key);
    for (const auto &[identity, entries] : missing) {
        const auto match = matches.find(identity);
        if (match == matches.end() || ambiguous.contains(identity)) continue;
        for (const auto &entry : entries) {
            if (updated.contains(entry.resource.key)) continue;
            const auto address = relocatedAddress(storage, entry.address, match->second);
            const auto resolved = synchronizeAddress(storage, actor, address);
            if (!resolved || resolved->resource != entry.resource) incomplete = true;
            else updated.insert(entry.resource.key);
        }
    }
    for (const auto &key : unresolved) if (!updated.contains(key)) incomplete = true;
    return incomplete;
}
struct AddressMove { OntologyResource resource; QString before, after; };
std::vector<AddressMove> relocationPlan(const Storage &storage, OntologyFileSystem &graph,
                                        const QString &source, const QString &destination) {
    std::vector<AddressMove> result;
    for (const auto &resource : journalEntities(graph, "urn:iisacc:semantic:address:")) {
        const auto before = currentAddress(graph, resource);
        const auto path = recordedLocalAddress(storage, before);
        if (path != source && !path.startsWith(source + '/')) continue;
        const auto after = relocatedAddress(storage, before, destination + path.mid(source.size()));
        const auto lookup = OntologyResource::entity(lookupKey(after));
        auto existing = graph.entity(lookup.key) ? resourceFact(facts(graph, lookup), bindingProperty) : std::nullopt;
        if (!existing && graph.entity(addressKey(after))) {
            const auto legacy = OntologyResource::entity(addressKey(after));
            if (currentAddress(graph, legacy) == after) existing = legacy;
        }
        if (existing && *existing != resource && hasMeaning(graph, *existing))
            throw std::runtime_error("The destination already has different semantic metadata");
        result.push_back({resource, before, after});
    }
    return result;
}
void validateMovePaths(const QString &source, const QString &destination) {
    if (!QDir::isAbsolutePath(source) || !QDir::isAbsolutePath(destination)
        || source != QDir::cleanPath(source) || destination != QDir::cleanPath(destination)
        || source == destination || destination.startsWith(source + '/'))
        throw std::runtime_error("Invalid semantic relocation paths");
}
void removePending(OntologyFileSystem &graph, const std::string &session,
                   const QString &source, const QString &destination) {
    const auto node = OntologyResource::entity(relocationKey(source, destination));
    if (!graph.entity(node.key)) return;
    const auto pending = fact(facts(graph, node), relocationProperty);
    if (!pending) return;
    std::vector<OntologyChange> changes{OntologyAssertion{node, relocationProperty, *pending, false}};
    const auto registry = OntologyResource::entity(registryKey);
    if (graph.entity(registry.key) && graph.property(pendingProperty))
        changes.emplace_back(OntologyAssertion{registry, pendingProperty, OntologyValue::resource(node), false});
    graph.apply(changes, graph.revision(), session);
}
void recoverRelocation(const Storage &storage, const QString &container, const QString &actor, const QString &address) {
    const auto path = recordedLocalAddress(storage, address);
    if (path.isEmpty()) return;
    const auto repository = openRepository(storage, actor, false);
    if (!repository) return;
    auto &graph = repository->ontology();
    const auto registry = OntologyResource::entity(registryKey);
    if (!graph.entity(registry.key) || !graph.property(pendingProperty)) return;
    const auto pending = graph.traverse(registry, pendingProperty, OntologyDirection::Outgoing, 1, maxResults);
    if (pending.truncated) throw std::runtime_error("Too many pending semantic relocations to resolve safely");
    for (const auto &entry : pending.nodes) {
        const auto document = factText(facts(graph, entry.resource), relocationProperty);
        if (document.empty()) continue;
        const auto intent = jsonObject(document);
        const auto source = intent.value("source").toString(), destination = intent.value("destination").toString();
        validateMovePaths(source, destination);
        if (path != source && !path.startsWith(source + '/') && path != destination && !path.startsWith(destination + '/')) continue;
        const bool sourceExists = QFileInfo::exists(source), destinationExists = QFileInfo::exists(destination);
        if (sourceExists && !destinationExists) {
            if (path == destination || path.startsWith(destination + '/'))
                throw std::runtime_error("A semantic relocation to this address is in progress");
            continue;
        }
        const auto expectedIdentity = intent.value("physicalIdentity").toString();
        const auto actualIdentity = physicalIdentity(storage, destination);
        const auto expectedProject = intent.value("projectIdentity").toString();
        const auto actualProject = projectIdentity(storage, destination);
        const bool identityMatches = (!expectedIdentity.isEmpty() && actualIdentity == expectedIdentity)
            || (!expectedProject.isEmpty() && actualProject == expectedProject);
        if (!sourceExists && destinationExists && identityMatches) {
            SemanticRepository(container, actor).commitRelocation(source, destination);
            continue;
        }
        throw std::runtime_error("A pending semantic relocation requires recovery before this address can be used");
    }
}
std::pair<AddressResolution, std::optional<AddressResolution>> registerProject(
        const Storage &storage, const QString &actor, const QString &address, const std::optional<QString> &target = {}) {
    auto repository = openRepository(storage, actor, true);
    auto &graph = repository->ontology();
    const auto revision = graph.revision();
    std::vector<OntologyChange> changes;
    schema(graph, changes);
    const auto project = *resolveAddress(storage, graph, changes, address, true);
    const auto object = !target ? std::optional<AddressResolution>{} : *target == address
        ? std::optional(project) : resolveAddress(storage, graph, changes, *target, true);
    const auto registry = OntologyResource::entity(registryKey);
    if (!graph.entity(registryKey)) changes.emplace_back(OntologyEntity{registryKey, "Semantic project registry", ""});
    changes.emplace_back(OntologyTypeAssignment{registry, registryClass});
    changes.emplace_back(OntologyAssertion{registry, projectProperty, OntologyValue::resource(project.resource)});
    graph.apply(changes, revision, repository->sessionKey());
    return {project, object};
}
QJsonObject readAddress(const Storage &storage, const QString &actor, const QString &address) {
    const auto resolved = synchronizeAddress(storage, actor, address);
    const auto repository = openRepository(storage, actor, false);
    if (!repository) return {{"address", address}, {"owner", "container"}, {"revision", "0"}, {"metadata", QJsonObject{}}};
    auto &graph = repository->ontology();
    // The provider exposes these as separate reads. Retry if a writer commits
    // between them so an old value is never paired with a newer CAS revision.
    for (int attempt = 0; attempt != 4; ++attempt) {
        const auto before = graph.revision();
        QJsonObject metadata;
        if (resolved && graph.entity(resolved->resource.key)) {
            const auto values = facts(graph, resolved->resource);
            if (currentAddress(graph, resolved->resource) != address) throw std::runtime_error("Semantic address changed while being read");
            const auto value = factText(values, metadataProperty);
            if (!value.empty()) metadata = jsonObject(value);
        }
        const auto after = graph.revision();
        if (before == after)
            return {{"address", address}, {"owner", "container"}, {"revision", QString::number(after)}, {"metadata", metadata}};
    }
    throw std::runtime_error("Semantic metadata changed while being read; retry");
}
QJsonObject selfData(const iiProjectManager::Snapshot &snapshot, Entity *result = nullptr) {
    const auto key = selfKey(snapshot.manifest.projectId);
    const auto it = std::find_if(snapshot.entities.begin(), snapshot.entities.end(), [&](const auto &item) { return item.id == key; });
    if (it == snapshot.entities.end()) return {};
    const auto extensions = jsonObject(it->extensionsJson);
    if (extensions.value(projectMarker).toString() != QStringLiteral("1"))
        throw std::runtime_error("Reserved project semantic entity conflicts with existing data");
    if (result) *result = *it;
    const auto properties = jsonObject(it->propertiesJson);
    const auto value = properties.value(projectMetadata);
    if (!value.isUndefined() && !value.isObject()) throw std::runtime_error("Invalid project semantic metadata");
    return value.toObject();
}
Entity ensureSelf(const iiProjectManager::Snapshot &snapshot) {
    Entity result;
    (void)selfData(snapshot, &result);
    if (!result.id.empty()) return result;
    result.id = selfKey(snapshot.manifest.projectId);
    result.type = "iip:Entity";
    result.name = snapshot.manifest.name;
    result.description = "The project owning these addressed semantic statements";
    result.extensionsJson = jsonString({{projectMarker, "1"}});
    return result;
}
QJsonObject readProject(const fs::path &path, const QString &actor, const QString &address) {
    const auto project = openProject(path, actor, false);
    return {{"address", address}, {"owner", "project"}, {"revision", QString::number(project.manifest().revision)},
            {"metadata", selfData(project.snapshot())}};
}
QJsonObject linkDocument(const QString &source, const QString &predicate, const QString &target, const QJsonObject &metadata) {
    return {{"id", relationId(source, predicate, target)}, {"source", source}, {"predicate", predicate},
            {"target", target}, {"metadata", metadata}};
}
void validateLink(const QJsonObject &link) {
    const auto source = link.value("source").toString(), predicate = link.value("predicate").toString(), target = link.value("target").toString();
    validText(source, 4096, "source address"); validText(predicate, 1024, "predicate"); validText(target, 4096, "target address");
    const bool identitySeed = link.value("identitySeed").toBool();
    const auto seedSource = identitySeed ? link.value("sourceIdentity").toString() : source;
    const auto seedTarget = identitySeed ? link.value("targetIdentity").toString() : target;
    if (seedSource.isEmpty() || seedTarget.isEmpty() || !link.value("metadata").isObject()
        || link.value("id").toString() != relationId(seedSource, predicate, seedTarget))
        throw std::runtime_error("Invalid stored semantic relation");
}
QJsonObject containerLink(const OntologyResource &node, const std::vector<OntologyStatement> &values) {
    const auto payload = factText(values, payloadProperty);
    if (payload.empty()) throw std::runtime_error("Semantic relation payload is unavailable");
    auto link = jsonObject(payload); validateLink(link);
    const auto from = fact(values, sourceProperty), to = fact(values, targetProperty);
    const auto expectedFrom = OntologyValue::resource(OntologyResource::entity(addressKey(link.value("source").toString())));
    const auto expectedTo = OntologyValue::resource(OntologyResource::entity(addressKey(link.value("target").toString())));
    if (node.kind != OntologyResourceKind::Entity || node.key != utf8(link.value("id").toString()) ||
        !from || !to || *from != expectedFrom || *to != expectedTo)
        throw std::runtime_error("Semantic relation index and payload disagree");
    return link;
}
void validateProjectLink(const iiProjectManager::Snapshot &snapshot, const Relation &relation, const QJsonObject &link) {
    validateLink(link);
    const auto self = selfKey(snapshot.manifest.projectId);
    if (relation.subject != self || relation.id != utf8(link.value("id").toString()) ||
        relation.type != utf8(link.value("predicate").toString()) ||
        relation.origin != iiProjectManager::RelationOrigin::userAsserted)
        throw std::runtime_error("Project semantic relation identity conflict");
    if (link.value("identitySeed").toBool()
        && (link.value("sourceIdentity").toString() != text(self)
            || !link.value("targetIdentity").toString().startsWith(QStringLiteral("urn:iisacc:semantic:address:"))))
        throw std::runtime_error("Project semantic relation identity conflict");
    const auto source = link.value("source").toString(), target = link.value("target").toString();
    if (source == target) {
        if (relation.object != self) throw std::runtime_error("Project semantic relation target disagrees with its payload");
        return;
    }
    const auto key = targetKey(target);
    const auto resource = std::find_if(snapshot.resources.begin(), snapshot.resources.end(), [&](const auto &item) { return item.id == key; });
    if (relation.object != key || resource == snapshot.resources.end() || resource->location != ResourceLocation::reference ||
        resource->locator != utf8(target) || jsonObject(resource->extensionsJson).value(targetMarker).toString() != target)
        throw std::runtime_error("Project semantic relation target disagrees with its payload");
}
QString verifiedCurrentAddress(const Storage &storage, OntologyFileSystem &graph, const OntologyResource &node) {
    const auto address = currentAddress(graph, node);
    const auto identity = resourceFact(facts(graph, node), projectIdentityProperty);
    const auto path = localAddress(storage, address);
    if (identity && !path.isEmpty() && QFileInfo::exists(path)) {
        const auto actual = projectIdentity(storage, address);
        if (!actual || identity->key != physicalKey(*actual))
            throw std::runtime_error("A project at the recorded address has a different identity");
    }
    return address;
}
QString currentForToken(const Storage &storage, OntologyFileSystem *graph, const QString &token) {
    if (!graph) return token;
    const auto node = OntologyResource::entity(addressKey(token));
    return graph->entity(node.key) ? verifiedCurrentAddress(storage, *graph, node) : token;
}
QJsonObject presentContainerLink(const Storage &storage, OntologyFileSystem &graph, QJsonObject link) {
    for (const auto *key : {"source", "target"}) {
        const auto token = link.value(key).toString();
        link.insert(key, currentForToken(storage, &graph, token));
    }
    link.insert("owner", "container");
    return link;
}
QJsonObject presentProjectLink(const Storage &storage, OntologyFileSystem *graph, QJsonObject link, const QString &address) {
    const auto recorded = link.value("source").toString();
    if (recorded != address) {
        link.insert("recordedSource", recorded);
        link.insert("source", address);
    }
    const auto target = link.value("target").toString();
    const auto targetId = link.value("targetIdentity").toString();
    const auto node = OntologyResource::entity(targetId.isEmpty() ? addressKey(target) : utf8(targetId));
    if (graph && graph->entity(node.key)) {
        const auto current = verifiedCurrentAddress(storage, *graph, node);
        if (current != target) { link.insert("recordedTarget", target); link.insert("target", current); }
    }
    link.insert("owner", "project");
    return link;
}
QJsonArray projectLinks(const Storage &storage, const Project &project, const QString &address, OntologyFileSystem *graph,
                        std::size_t limit = (std::numeric_limits<std::size_t>::max)(), bool *truncated = nullptr) {
    const auto snapshot = project.snapshot();
    const auto self = selfKey(snapshot.manifest.projectId);
    (void)selfData(snapshot);
    QJsonArray result;
    for (const auto &relation : snapshot.relations) {
        if (relation.subject != self) continue;
        const auto extensions = jsonObject(relation.extensionsJson);
        if (!extensions.contains(linkMarker)) continue;
        if (!extensions.value(linkMarker).isObject()) throw std::runtime_error("Invalid project semantic relation");
        if (static_cast<std::size_t>(result.size()) >= limit) {
            if (truncated) *truncated = true;
            break;
        }
        auto link = extensions.value(linkMarker).toObject();
        validateProjectLink(snapshot, relation, link);
        result.append(presentProjectLink(storage, graph, link, address));
    }
    return result;
}
// Keep traversal within the visible sections of the selected container.
// External endpoints remain reportable, but are never opened by this walk.
bool visibleSemanticAddress(const QString &address) {
    auto normalized=address;
    if (normalized.endsWith('/') && normalized.left(normalized.size()-1).endsWith(".iiproject",Qt::CaseInsensitive))
        normalized.chop(1);
    const bool files=normalized.startsWith("files/"), models=normalized.startsWith("models/");
    if ((!files && !models) || normalized.size()>4096 || normalized.contains(QChar::Null) ||
        normalized.contains('\\') || QDir::cleanPath(normalized)!=normalized) return false;
    const auto relative=normalized.mid(files?6:7);
    for (const auto &part:relative.split('/'))
        if (part.isEmpty() || part=="." || part==".." || part.startsWith('.') ||
            iiFileProvider::native::Presentation::isControlFile(nativePath(part))) return false;
    return true;
}
void appendUnique(QJsonArray &out, const QJsonObject &link, std::set<QString> &seen, bool &incomplete) {
    const auto id = link.value("id").toString();
    if (!seen.insert(id).second) return;
    if (out.size() >= static_cast<qsizetype>(maxResults)) { incomplete = true; return; }
    out.append(link);
}
}

SemanticRepository::SemanticRepository(QString containerPath, QString actorId)
    : containerPath_(std::move(containerPath)), actorId_(std::move(actorId)) {
    validText(actorId_, 4096, "actor");
}
QJsonObject SemanticRepository::read(const QString &address) const {
    validText(address, 4096, "address");
    const auto storage = storageFor(containerPath_);
    recoverRelocation(storage, containerPath_, actorId_, address);
    if (const auto project = projectPath(storage, address)) {
        (void)synchronizeAddress(storage, actorId_, address);
        return readProject(*project, actorId_, address);
    }
    return readAddress(storage, actorId_, address);
}
QJsonObject SemanticRepository::setMetadata(const QString &address, const QJsonObject &metadata, const QString &expectedRevision) const {
    validText(address, 4096, "address"); (void)jsonBytes(metadata);
    const auto storage = storageFor(containerPath_);
    recoverRelocation(storage, containerPath_, actorId_, address);
    if (const auto path = projectPath(storage, address)) {
        auto project = openProject(*path, actorId_, true); expectProject(project, expectedRevision);
        (void)registerProject(storage, actorId_, address);
        const auto snapshot = project.snapshot(); auto self = ensureSelf(snapshot);
        auto properties = jsonObject(self.propertiesJson); properties.insert(projectMetadata, metadata);
        self.propertiesJson = jsonString(properties);
        auto transaction = project.begin(); transaction.putEntity(std::move(self));
        const auto revision = transaction.commit();
        QJsonObject result{{"address", address}, {"owner", "project"}, {"revision", QString::number(revision)}, {"metadata", metadata}};
        appendStatus(result, refreshProjectCatalog(storage, *path));
        return result;
    }
    auto repository = openRepository(storage, actorId_, true); auto &graph = repository->ontology();
    std::vector<OntologyChange> changes; schema(graph, changes);
    const auto subject = *resolveAddress(storage, graph, changes, address, true);
    // Even an idempotent replacement must perform the revision comparison.
    changes.emplace_back(OntologyTypeAssignment{subject.resource, addressClass});
    replace(graph, changes, subject.resource, metadataProperty, OntologyValue::text(jsonString(metadata)));
    const auto revision = graph.apply(changes, expected(expectedRevision), repository->sessionKey());
    return {{"address", address}, {"owner", "container"}, {"revision", QString::number(revision)}, {"metadata", metadata}};
}
QJsonObject SemanticRepository::putRelation(const QString &source, const QString &predicate, const QString &target,
                                             const QJsonObject &metadata, const QString &expectedRevision) const {
    return putRelationImpl(source, predicate, target, metadata, expectedRevision, false);
}
QJsonObject SemanticRepository::putRelationIfAbsent(const QString &source, const QString &predicate, const QString &target,
                                                     const QJsonObject &metadata, const QString &expectedRevision) const {
    return putRelationImpl(source, predicate, target, metadata, expectedRevision, true);
}
QJsonObject SemanticRepository::putRelationImpl(const QString &source, const QString &predicate, const QString &target,
                                                 const QJsonObject &metadata, const QString &expectedRevision, bool onlyIfAbsent) const {
    validText(source, 4096, "source address"); validText(predicate, 1024, "predicate"); validText(target, 4096, "target address");
    (void)jsonBytes(metadata);
    auto link = linkDocument(source, predicate, target, metadata);
    const auto storage = storageFor(containerPath_);
    recoverRelocation(storage, containerPath_, actorId_, source);
    if (target != source) recoverRelocation(storage, containerPath_, actorId_, target);
    if (const auto path = projectPath(storage, source)) {
        auto project = openProject(*path, actorId_, true); expectProject(project, expectedRevision);
        auto snapshot = project.snapshot(); auto self = ensureSelf(snapshot);
        const auto registered = registerProject(storage, actorId_, source, target);
        const auto targetIdentity = text(registered.second->resource.key);
        // A project's own identity moves with its package. Reuse an existing
        // source relation for the same predicate and target after relocation,
        // while preserving the original recorded address and relation ID.
        std::optional<QJsonObject> existingLink;
        for (const auto &entry : snapshot.relations) {
            if (entry.subject != self.id || entry.type != utf8(predicate)) continue;
            const auto marker = jsonObject(entry.extensionsJson).value(linkMarker);
            if (!marker.isObject()) continue;
            const auto candidate = marker.toObject(); validateProjectLink(snapshot, entry, candidate);
            const auto candidateTarget = candidate.value("targetIdentity").toString();
            const auto candidateKey = candidateTarget.isEmpty() ? text(addressKey(candidate.value("target").toString())) : candidateTarget;
            if (candidateKey != targetIdentity) continue;
            if (existingLink) throw std::runtime_error("Multiple project relations match this predicate and target");
            existingLink = candidate;
        }
        if (existingLink && onlyIfAbsent) {
            auto repository = openRepository(storage, actorId_, false);
            auto result = presentProjectLink(storage, repository ? &repository->ontology() : nullptr, *existingLink, source);
            result.insert("revision", QString::number(snapshot.manifest.revision));
            result.insert("alreadyExists", true);
            return result;
        }
        if (existingLink) { link = *existingLink; link.insert("metadata", metadata); }
        else {
            link.insert("identitySeed", true);
            link.insert("sourceIdentity", text(self.id));
            link.insert("targetIdentity", targetIdentity);
            link.insert("id", relationId(text(self.id), predicate, targetIdentity));
        }
        auto schemaValue = snapshot.schema;
        if (std::none_of(schemaValue.relations.begin(), schemaValue.relations.end(), [&](const auto &entry) { return entry.id == utf8(predicate); }))
            schemaValue.relations.push_back({utf8(predicate), utf8(predicate), "A user-declared addressed resource relation",
                                             {"iip:Entity"}, {"iip:Entity"}});
        auto transaction = project.begin();
        if (schemaValue != snapshot.schema) transaction.setSchema(std::move(schemaValue));
        transaction.putEntity(self);
        auto object = self.id;
        const auto recordedSource = link.value("source").toString();
        const auto recordedTarget = link.value("target").toString();
        if (recordedSource != recordedTarget) {
            object = targetKey(recordedTarget);
            const auto found = std::find_if(snapshot.resources.begin(), snapshot.resources.end(), [&](const auto &item) { return item.id == object; });
            if (found == snapshot.resources.end()) {
                Resource resource;
                resource.id = object; resource.name = utf8(recordedTarget); resource.purpose = "A user-declared addressed relation target";
                resource.locator = utf8(recordedTarget); resource.location = ResourceLocation::reference;
                resource.extensionsJson = jsonString({{targetMarker, recordedTarget}});
                transaction.putResource(std::move(resource));
            } else if (found->location != ResourceLocation::reference || found->locator != utf8(recordedTarget) ||
                       jsonObject(found->extensionsJson).value(targetMarker).toString() != recordedTarget)
                throw std::runtime_error("Reserved project relation target conflicts with existing data");
        }
        Relation relation;
        relation.id = utf8(link.value("id").toString()); relation.type = utf8(predicate); relation.subject = self.id; relation.object = object;
        relation.extensionsJson = jsonString({{linkMarker, link}});
        const auto previous = std::find_if(snapshot.relations.begin(), snapshot.relations.end(), [&](const auto &item) { return item.id == relation.id; });
        if (previous != snapshot.relations.end() && (!jsonObject(previous->extensionsJson).value(linkMarker).isObject() ||
            previous->subject != relation.subject || previous->object != relation.object || previous->type != relation.type))
            throw std::runtime_error("Reserved project semantic relation conflicts with existing data");
        transaction.putRelation(std::move(relation));
        const auto revision = transaction.commit();
        auto repository = openRepository(storage, actorId_, false);
        auto result = presentProjectLink(storage, repository ? &repository->ontology() : nullptr, link, source);
        result.insert("revision", QString::number(revision));
        appendStatus(result, refreshProjectCatalog(storage, *path));
        return result;
    }
    auto repository = openRepository(storage, actorId_, true); auto &graph = repository->ontology();
    std::vector<OntologyChange> changes; schema(graph, changes);
    const auto from = *resolveAddress(storage, graph, changes, source, true);
    const auto to = source == target ? from : *resolveAddress(storage, graph, changes, target, true);
    link = linkDocument(from.identity, predicate, to.identity, metadata);
    const auto id = utf8(link.value("id").toString()); const auto node = OntologyResource::entity(id);
    if (const auto existing = graph.entity(id)) {
        if (existing->label != id) throw std::runtime_error("Semantic relation identity conflict");
    } else changes.emplace_back(OntologyEntity{id, id, ""});
    changes.emplace_back(OntologyTypeAssignment{node, relationClass});
    const auto oldFacts = facts(graph, node);
    const auto old = factText(oldFacts, payloadProperty);
    if (!old.empty()) {
        const auto previous = containerLink(node, oldFacts);
        if (previous.value("source") != from.identity || previous.value("predicate") != predicate || previous.value("target") != to.identity)
            throw std::runtime_error("Semantic relation identity conflict");
        if (onlyIfAbsent) {
            const auto revision=graph.revision();
            if (revision!=expected(expectedRevision)) throw std::runtime_error("ontology revision conflict");
            auto result=presentContainerLink(storage,graph,previous);
            result.insert("revision",QString::number(revision)); result.insert("alreadyExists",true);
            return result;
        }
    }
    replace(graph, changes, node, sourceProperty, OntologyValue::resource(from.resource));
    replace(graph, changes, node, targetProperty, OntologyValue::resource(to.resource));
    replace(graph, changes, node, payloadProperty, OntologyValue::text(jsonString(link)));
    const auto revision = graph.apply(changes, expected(expectedRevision), repository->sessionKey());
    auto result = presentContainerLink(storage, graph, link); result.insert("revision", QString::number(revision)); return result;
}
QJsonObject SemanticRepository::removeRelation(const QString &source, const QString &id, const QString &expectedRevision) const {
    validText(source, 4096, "source address"); validText(id, 1024, "relation identity");
    const auto storage = storageFor(containerPath_);
    recoverRelocation(storage, containerPath_, actorId_, source);
    if (const auto path = projectPath(storage, source)) {
        auto project = openProject(*path, actorId_, true); expectProject(project, expectedRevision);
        const auto snapshot = project.snapshot();
        const auto found = std::find_if(snapshot.relations.begin(), snapshot.relations.end(), [&](const auto &item) { return item.id == utf8(id); });
        if (found == snapshot.relations.end() || found->subject != selfKey(snapshot.manifest.projectId)) throw std::runtime_error("Semantic relation not found");
        const auto marker = jsonObject(found->extensionsJson).value(linkMarker);
        if (!marker.isObject()) throw std::runtime_error("Semantic relation not found");
        const auto link = marker.toObject(); validateProjectLink(snapshot, *found, link);
        auto transaction = project.begin(); transaction.removeRelation(found->id); const auto revision = transaction.commit();
        QJsonObject result{{"id", id}, {"owner", "project"}, {"revision", QString::number(revision)}};
        appendStatus(result, refreshProjectCatalog(storage, *path));
        return result;
    }
    auto repository = openRepository(storage, actorId_, true); auto &graph = repository->ontology();
    const auto node = OntologyResource::entity(utf8(id));
    const auto values = facts(graph, node); const auto payload = factText(values, payloadProperty);
    if (payload.empty()) throw std::runtime_error("Semantic relation not found");
    const auto link = containerLink(node, values);
    if (presentContainerLink(storage, graph, link).value("source").toString() != source || link.value("id").toString() != id)
        throw std::runtime_error("Semantic relation belongs to another address");
    std::vector<OntologyChange> changes;
    for (const auto &entry : values)
        if (entry.propertyKey == sourceProperty || entry.propertyKey == targetProperty || entry.propertyKey == payloadProperty)
            changes.emplace_back(OntologyAssertion{node, entry.propertyKey, entry.value, false});
    const auto revision = graph.apply(changes, expected(expectedRevision), repository->sessionKey());
    return {{"id", id}, {"owner", "container"}, {"revision", QString::number(revision)}};
}
QJsonObject SemanticRepository::relations(const QString &address) const {
    validText(address, 4096, "address");
    const auto storage = storageFor(containerPath_);
    recoverRelocation(storage, containerPath_, actorId_, address);
    const auto reconciliationIncomplete = reconcileExternalMoves(storage, actorId_);
    const auto resolved = synchronizeAddress(storage, actorId_, address);
    QJsonArray outgoing, incoming, unavailable; std::set<QString> outgoingIds, incomingIds;
    bool incomplete = reconciliationIncomplete;
    const auto append = [&](const QJsonObject &link) {
        if (link.value("source").toString() == address) appendUnique(outgoing, link, outgoingIds, incomplete);
        if (link.value("target").toString() == address) appendUnique(incoming, link, incomingIds, incomplete);
    };
    const auto repo = openRepository(storage, actorId_, false);
    if (repo) {
        auto &graph = repo->ontology();
        const auto graphRevision = graph.revision();
        const auto registryNode = OntologyResource::entity(registryKey);
        if (graph.entity(registryNode.key) && graph.property(pendingProperty)) {
            const auto pending = graph.traverse(registryNode, pendingProperty, OntologyDirection::Outgoing, 1, 1);
            incomplete = incomplete || pending.truncated || !pending.nodes.empty();
        }
        if (resolved && graph.entity(resolved->resource.key)) {
            for (const auto *property : {sourceProperty, targetProperty}) {
                if (!graph.property(property)) continue;
                const auto walk = graph.traverse(resolved->resource, property, OntologyDirection::Incoming, 1, maxResults);
                incomplete = incomplete || walk.truncated;
                for (const auto &entry : walk.nodes) {
                    const auto link = containerLink(entry.resource, facts(graph, entry.resource));
                    append(presentContainerLink(storage, graph, link));
                }
            }
        }
        if (graph.entity(registryKey) && graph.property(projectProperty)) {
            const auto registry = graph.traverse(OntologyResource::entity(registryKey), projectProperty,
                                                 OntologyDirection::Outgoing, 1, maxResults);
            incomplete = incomplete || registry.truncated;
            for (const auto &entry : registry.nodes) {
                const auto projectAddress = currentAddress(graph, entry.resource);
                try {
                    const auto path = projectPath(storage, projectAddress);
                    if (!path) throw std::runtime_error("Registered project is unavailable");
                    const auto project = openProject(*path, actorId_, false);
                    (void)verifiedCurrentAddress(storage, graph, entry.resource);
                    for (const auto &value : projectLinks(storage, project, projectAddress, &graph)) append(value.toObject());
                } catch (const std::exception &) {
                    incomplete = true; unavailable.append(projectAddress);
                }
            }
        }
        if (graph.revision() != graphRevision) incomplete = true;
    }
    std::optional<fs::path> directProject;
    try {
        directProject = projectPath(storage, address);
    } catch (const std::exception &) {
        // An unresolved project can still be the target of a container-owned
        // relation. Return those incoming facts and expose the missing owner.
        incomplete = true;
        if (!unavailable.contains(address)) unavailable.append(address);
    }
    if (directProject) {
        const auto project = openProject(*directProject, actorId_, false);
        for (const auto &value : projectLinks(storage, project, address, repo ? &repo->ontology() : nullptr)) append(value.toObject());
    }
    return {{"address", address}, {"outgoing", outgoing}, {"incoming", incoming},
            {"incomplete", incomplete}, {"unavailableProjects", unavailable}};
}

QJsonObject SemanticRepository::traceRelations(const iiSocietyContainer::SharedStorage &storage,
                                                const QString &address, const QString &direction,
                                                int maxDepth, int maxEdges) const {
    if (!visibleSemanticAddress(address) || (direction!="incoming" && direction!="outgoing" && direction!="both") ||
        maxDepth<1 || maxDepth>3 || maxEdges<1 || maxEdges>20)
        throw std::runtime_error("Invalid bounded resource lineage request");

    // Keep a single read-only graph and cache each registered project's
    // declared links once. None of the normal relocation recovery or catalog
    // refresh functions is called from this observation path.
    const auto &drive=storage.drive();
    if (QDir::cleanPath(containerPath_).normalized(QString::NormalizationForm_C)!=
        QDir::cleanPath(drive.rootPath()).normalized(QString::NormalizationForm_C) || !drive.isReady())
        throw std::runtime_error("The selected Society storage is unavailable or changed");
    const auto containerId=drive.storageKind()==iiSocietyContainer::SocietyDrive::StorageKind::LogicalDisk
        ? drive.storageIdentifier() : drive.identifier();
    if (containerId.isEmpty()) throw std::runtime_error("Society storage identity is unavailable");
    const auto repository=openRepository(storage,actorId_,false);
    auto *graph=repository ? &repository->ontology() : nullptr;
    const auto initialRevision=graph ? std::optional(graph->revision()) : std::nullopt;
    constexpr std::size_t maxProjects=64, maxProjectLinks=1024;
    bool incomplete=false, truncated=false, reconciliationRequired=false, boundaryReached=false;
    QJsonArray unavailable, edges;
    std::set<QString> visitedProjects, unavailableSet;
    std::vector<QJsonObject> projectEdges;

    // An unavailable registered package is reported, not interpreted as a
    // project with no references. The cap prevents a traversal from opening an
    // unbounded number of packages or retaining an unbounded project graph.
    const auto readProject=[&](const QString &projectAddress, const OntologyResource *registered) {
        if (!visitedProjects.insert(projectAddress).second) return;
        if (!visibleSemanticAddress(projectAddress)) { boundaryReached=true; incomplete=true; return; }
        if (visitedProjects.size()>maxProjects) { truncated=true; incomplete=true; return; }
        try {
            const auto path=projectPath(storage,projectAddress);
            if (!path) throw std::runtime_error("Registered project is unavailable");
            const auto project=openProject(*path,actorId_,false);
            if (registered && graph) (void)verifiedCurrentAddress(storage,*graph,*registered);
            bool projectTruncated=false;
            const auto links=projectLinks(storage,project,projectAddress,graph,
                                          maxProjectLinks-projectEdges.size(),&projectTruncated);
            for (const auto &value:links) projectEdges.push_back(value.toObject());
            if (projectTruncated) { truncated=true; incomplete=true; }
        } catch (const std::exception &) {
            incomplete=true;
            if (unavailableSet.insert(projectAddress).second) unavailable.append(projectAddress);
        }
    };

    if (graph) {
        const auto registry=OntologyResource::entity(registryKey);
        if (graph->entity(registry.key) && graph->property(pendingProperty)) {
            const auto pending=graph->traverse(registry,pendingProperty,OntologyDirection::Outgoing,1,1);
            if (pending.truncated || !pending.nodes.empty()) { incomplete=true; reconciliationRequired=true; }
        }
        if (graph->entity(registry.key) && graph->property(projectProperty)) {
            const auto projects=graph->traverse(registry,projectProperty,OntologyDirection::Outgoing,1,maxProjects);
            if (projects.truncated) { truncated=true; incomplete=true; }
            for (const auto &entry:projects.nodes) readProject(currentAddress(*graph,entry.resource),&entry.resource);
        }
    }

    std::deque<std::pair<QString,int>> pending{{address,0}};
    std::set<QString> visited{address}, emitted;
    while (!pending.empty()) {
        const auto [current,depth]=pending.front(); pending.pop_front();
        if (depth>=maxDepth) continue;
        std::optional<AddressResolution> resolved;
        bool bindingUnsafe=false;
        if (graph) {
            try {
                std::vector<OntologyChange> unapplied;
                resolved=resolveAddress(storage,*graph,unapplied,current,false);
                bindingUnsafe=!unapplied.empty() || (!resolved &&
                    (graph->entity(lookupKey(current)) || graph->entity(addressKey(current))));
            } catch (const std::exception &) { bindingUnsafe=true; }
            if (bindingUnsafe) { incomplete=true; reconciliationRequired=true; continue; }
        }

        // A directly encountered package can have its own canonical graph even
        // before it has been registered in the container's reverse index.
        auto projectCandidate=current;
        if (projectCandidate.endsWith('/')) projectCandidate.chop(1);
        if (projectCandidate.endsWith(QStringLiteral(".iiproject"),Qt::CaseInsensitive)) readProject(current,nullptr);
        std::vector<QJsonObject> links;
        if (graph && resolved && graph->entity(resolved->resource.key)) {
            for (const auto *property:{sourceProperty,targetProperty}) {
                if (!graph->property(property)) continue;
                const auto walk=graph->traverse(resolved->resource,property,OntologyDirection::Incoming,1,maxResults);
                if (walk.truncated) { truncated=true; incomplete=true; }
                for (const auto &entry:walk.nodes)
                    links.push_back(presentContainerLink(storage,*graph,containerLink(entry.resource,facts(*graph,entry.resource))));
            }
        }
        links.insert(links.end(),projectEdges.begin(),projectEdges.end());
        std::sort(links.begin(),links.end(),[](const QJsonObject &a,const QJsonObject &b) {
            return std::pair(a.value("owner").toString(),a.value("id").toString()) <
                   std::pair(b.value("owner").toString(),b.value("id").toString());
        });
        for (const auto &link:links) {
            const auto source=link.value("source").toString(), target=link.value("target").toString();
            const bool outgoing=source==current && direction!="incoming";
            const bool incoming=target==current && direction!="outgoing";
            if (!outgoing && !incoming) continue;
            const auto key=link.value("owner").toString()+'\n'+link.value("id").toString();
            if (!emitted.insert(key).second) continue;
            if (edges.size()>=maxEdges) { truncated=true; incomplete=true; break; }
            QJsonObject edge{{"id",link.value("id")},{"source",source},{"predicate",link.value("predicate")},
                             {"target",target},{"owner",link.value("owner")},{"depth",depth+1}};
            for (const auto *name:{"recordedSource","recordedTarget"})
                if (link.contains(name)) edge.insert(name,link.value(name));
            edges.append(edge);
            const auto next=outgoing ? target : source;
            if (!visibleSemanticAddress(next)) { boundaryReached=true; incomplete=true; }
            else if (depth+1<maxDepth && visited.insert(next).second) pending.emplace_back(next,depth+1);
        }
        if (edges.size()>=maxEdges && truncated) break;
    }
    if (!drive.isReady()) throw std::runtime_error("The selected Society storage changed during lineage observation");
    if (graph && graph->revision()!=*initialRevision) incomplete=true;
    return {{"containerId",containerId},{"address",address},{"direction",direction},{"maxDepth",maxDepth},
            {"edges",edges},{"graphRevision",initialRevision ? QJsonValue(QString::number(*initialRevision)) : QJsonValue(QJsonValue::Null)},
            {"incomplete",incomplete},{"truncated",truncated},{"reconciliationRequired",reconciliationRequired},
            {"boundaryReached",boundaryReached},{"unavailableProjects",unavailable},
            {"coverage","container_graph_and_registered_projects"},{"completeness","unknown"},
            {"evidence","user_asserted_relations"},{"assertionAuthority","host_declared_not_authenticated"},
            {"endpointsVerified",false},{"cleanupDecision","review_required"}};
}

void SemanticRepository::prepareRelocation(const QString &source, const QString &destination) const {
    validateMovePaths(source, destination);
    const auto storage = storageFor(containerPath_);
    if (!openRepository(storage, actorId_, false)) return;
    auto repository = openRepository(storage, actorId_, true);
    auto &graph = repository->ontology();
    const auto revision = graph.revision();
    // Keep an intent even when the initial plan is empty: another writer may
    // attach semantics before the native move completes.
    (void)relocationPlan(storage, graph, source, destination);
    const auto node = OntologyResource::entity(relocationKey(source, destination));
    std::vector<OntologyChange> changes; schema(graph, changes);
    const auto identity = physicalIdentity(storage, source).value_or(QString());
    if (graph.entity(node.key)) {
        const auto existing = factText(facts(graph, node), relocationProperty);
        if (!existing.empty() && jsonObject(existing).value("physicalIdentity").toString() != identity)
            throw std::runtime_error("A previous semantic relocation requires recovery");
    }
    if (!graph.entity(node.key)) {
        changes.emplace_back(OntologyEntity{node.key, node.key, ""});
        changes.emplace_back(OntologyTypeAssignment{node, relocationClass});
    }
    const auto registry = OntologyResource::entity(registryKey);
    if (!graph.entity(registry.key)) {
        changes.emplace_back(OntologyEntity{registry.key, "Semantic project registry", ""});
        changes.emplace_back(OntologyTypeAssignment{registry, registryClass});
    }
    changes.emplace_back(OntologyAssertion{registry, pendingProperty, OntologyValue::resource(node)});
    const auto document = jsonString({{"source", source}, {"destination", destination}, {"physicalIdentity", identity},
                                      {"projectIdentity", projectIdentity(storage, source).value_or(QString())}});
    replace(graph, changes, node, relocationProperty, OntologyValue::text(document));
    graph.apply(changes, revision, repository->sessionKey());
}

void SemanticRepository::commitRelocation(const QString &source, const QString &destination) const {
    validateMovePaths(source, destination);
    const auto storage = storageFor(containerPath_);
    if (!openRepository(storage, actorId_, false)) return;
    auto repository = openRepository(storage, actorId_, true);
    auto &graph = repository->ontology();
    const auto pending = OntologyResource::entity(relocationKey(source, destination));
    if (!graph.entity(pending.key) || factText(facts(graph, pending), relocationProperty).empty()) {
        // The container may have acquired its first semantic data after the
        // prepare call observed no object store. Recheck after the native move.
        if (relocationPlan(storage, graph, source, destination).empty()) return;
        prepareRelocation(source, destination);
    }
    const auto plan = relocationPlan(storage, graph, source, destination);
    std::vector<OntologyChange> changes;
    const auto flush = [&] {
        if (changes.empty()) return;
        graph.apply(changes, graph.revision(), repository->sessionKey());
        changes.clear();
    };
    for (const auto &item : plan) {
        if (changes.size() > 220) flush();
        const auto oldLookup = OntologyResource::entity(lookupKey(item.before));
        if (graph.entity(oldLookup.key)) {
            const auto old = resourceFact(facts(graph, oldLookup), bindingProperty);
            if (old && *old == item.resource)
                changes.emplace_back(OntologyAssertion{oldLookup, bindingProperty, OntologyValue::resource(item.resource), false});
        }
        const auto nextLookup = OntologyResource::entity(lookupKey(item.after));
        if (!graph.entity(nextLookup.key)) {
            changes.emplace_back(OntologyEntity{nextLookup.key, utf8(item.after), ""});
            changes.emplace_back(OntologyTypeAssignment{nextLookup, lookupClass});
        }
        replace(graph, changes, nextLookup, bindingProperty, OntologyValue::resource(item.resource));
        replace(graph, changes, item.resource, currentProperty, OntologyValue::text(utf8(item.after)));
        const auto identity = physicalIdentity(storage, item.after);
        const auto previousIdentity = resourceFact(facts(graph, item.resource), physicalProperty);
        if (identity) {
            const auto node = OntologyResource::entity(physicalKey(*identity));
            if (!graph.entity(node.key)) {
                changes.emplace_back(OntologyEntity{node.key, utf8(*identity), ""});
                changes.emplace_back(OntologyTypeAssignment{node, physicalClass});
            }
            replace(graph, changes, item.resource, physicalProperty, OntologyValue::resource(node));
        } else if (previousIdentity) {
            changes.emplace_back(OntologyAssertion{item.resource, physicalProperty, OntologyValue::resource(*previousIdentity), false});
        }
    }
    flush();
    for (const auto &item : plan) {
        if (!item.after.endsWith(QStringLiteral(".iiproject"), Qt::CaseInsensitive)) continue;
        const auto path = localAddress(storage, item.after);
        if (path.isEmpty() || !QFileInfo(path).isDir()) continue;
        const auto status = refreshProjectCatalog(storage, nativePath(path));
        if (status.value("catalogStatus") == "pending")
            throw std::runtime_error(utf8(status.value("catalogError").toString()));
    }
    removePending(graph, repository->sessionKey(), source, destination);
}

void SemanticRepository::cancelRelocation(const QString &source, const QString &destination) const {
    validateMovePaths(source, destination);
    const auto storage = storageFor(containerPath_);
    if (!openRepository(storage, actorId_, false)) return;
    if (!QFileInfo::exists(source) || QFileInfo::exists(destination)) return;
    auto repository = openRepository(storage, actorId_, true);
    removePending(repository->ontology(), repository->sessionKey(), source, destination);
}

QJsonObject SemanticRepository::rebindRelocation(const QString &sourceAddress, const QString &destinationAddress) const {
    validText(sourceAddress, 4096, "source address"); validText(destinationAddress, 4096, "destination address");
    const auto storage = storageFor(containerPath_);
    const auto source = recordedLocalAddress(storage, sourceAddress);
    const auto destination = recordedLocalAddress(storage, destinationAddress);
    if (!source.isEmpty() && source == destination) {
        const auto initial = destinationObservation(storage, destinationAddress);
        auto repository = openRepository(storage, actorId_, true);
        auto &graph = repository->ontology();
        const auto lookup = OntologyResource::entity(lookupKey(sourceAddress));
        auto resource = graph.entity(lookup.key) ? resourceFact(facts(graph, lookup), bindingProperty) : std::nullopt;
        if (!resource && graph.entity(addressKey(sourceAddress))) resource = OntologyResource::entity(addressKey(sourceAddress));
        if (!resource || currentAddress(graph, *resource) != sourceAddress || !hasMeaning(graph, *resource))
            throw std::runtime_error("The address has no unambiguous semantic identity to retain");
        const auto projectId = resourceFact(facts(graph, *resource), projectIdentityProperty);
        if (projectId) {
            const auto project = projectIdentity(storage, destinationAddress);
            if (!project || projectId->key != physicalKey(*project))
                throw std::runtime_error("The updated project has a different project identity");
        }
        const auto revision = graph.revision();
        std::vector<OntologyChange> changes; schema(graph, changes);
        const auto physical = physicalIdentity(storage, destinationAddress);
        const auto previous = resourceFact(facts(graph, *resource), physicalProperty);
        if (physical) {
            const auto node = OntologyResource::entity(physicalKey(*physical));
            if (!graph.entity(node.key)) {
                changes.emplace_back(OntologyEntity{node.key, utf8(*physical), ""});
                changes.emplace_back(OntologyTypeAssignment{node, physicalClass});
            }
            replace(graph, changes, *resource, physicalProperty, OntologyValue::resource(node));
        } else if (previous) changes.emplace_back(OntologyAssertion{*resource, physicalProperty, OntologyValue::resource(*previous), false});
        changes.emplace_back(OntologyTypeAssignment{*resource, addressClass});
        (void)society::semantics::stabilizeDestination(initial, [&] { return destinationObservation(storage, destinationAddress); });
        if (projectId) {
            const auto project = projectIdentity(storage, destinationAddress);
            if (!project || projectId->key != physicalKey(*project))
                throw std::runtime_error("The updated project has a different project identity");
        }
        const auto updated = graph.apply(changes, revision, repository->sessionKey());
        return {{"source", sourceAddress}, {"destination", sourceAddress}, {"revision", QString::number(updated)}};
    }
    validateMovePaths(source, destination);
    if (QFileInfo::exists(source) || !QFileInfo::exists(destination))
        throw std::runtime_error("An asserted relocation requires the original to be absent and the destination to exist");
    const auto initial = destinationObservation(storage, destinationAddress);
    const auto repository = openRepository(storage, actorId_, false);
    if (!repository) throw std::runtime_error("The source has no semantic metadata to relocate");
    auto &graph = repository->ontology();
    const auto plan = relocationPlan(storage, graph, source, destination);
    if (plan.empty()) throw std::runtime_error("The source has no semantic metadata to relocate");
    const auto verifyProjects = [&] {
        for (const auto &item : plan) {
            const auto projectId = resourceFact(facts(graph, item.resource), projectIdentityProperty);
            if (!projectId) continue;
            const auto actual = projectIdentity(storage, item.after);
            if (!actual || projectId->key != physicalKey(*actual))
                throw std::runtime_error("The destination project has a different project identity");
        }
    };
    verifyProjects();
    const auto pending = OntologyResource::entity(relocationKey(source, destination));
    if (!graph.entity(pending.key) || factText(facts(graph, pending), relocationProperty).empty())
        prepareRelocation(source, destination);
    if (QFileInfo::exists(source))
        throw std::runtime_error("The asserted relocation source reappeared while being verified");
    (void)society::semantics::stabilizeDestination(initial, [&] { return destinationObservation(storage, destinationAddress); });
    if (QFileInfo::exists(source))
        throw std::runtime_error("The asserted relocation source reappeared while being verified");
    verifyProjects();
    commitRelocation(source, destination);
    const auto updated = openRepository(storage, actorId_, false);
    return {{"source", sourceAddress}, {"destination", relocatedAddress(storage, sourceAddress, destination)},
            {"revision", QString::number(updated->ontology().revision())}};
}

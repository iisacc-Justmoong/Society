#include "ProjectResourceBinding.h"

#include <FileCatalog.h>
#include <ObjectSource.h>
#include <SharedStorage.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QUrl>
#include <filesystem>
#include <stdexcept>

namespace {
constexpr auto bindingKey = "society:sourceBinding";
constexpr auto catalogKind = "files-catalog-v1";
constexpr auto nativeKind = "native-source-v1";

std::string utf8(const QString &value) {
    const auto bytes = value.toUtf8();
    return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
}
QString text(const std::string &value) {
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}
std::filesystem::path nativePath(const QString &value) {
#ifdef Q_OS_WIN
    return std::filesystem::path(value.toStdWString());
#else
    const auto bytes = QFile::encodeName(value);
    return std::filesystem::path(std::string(bytes.constData(), static_cast<std::size_t>(bytes.size())));
#endif
}
QString qpath(const std::filesystem::path &path) {
#ifdef Q_OS_WIN
    return QString::fromStdWString(path.native());
#else
    const auto &bytes = path.native();
    return QFile::decodeName(QByteArray(bytes.data(), static_cast<qsizetype>(bytes.size())));
#endif
}
iiSocietyContainer::SharedStorage storageFor(const QString &container) {
    QString error;
    auto storage = iiSocietyContainer::SharedStorage::open(container, &error);
    if (!storage)
        throw std::runtime_error(utf8(error.isEmpty() ? QStringLiteral("Society storage is unavailable.") : error));
    return *storage;
}
bool isCatalogBinding(const QJsonObject &binding) {
    return binding.value("kind").toString() == QLatin1String(catalogKind);
}
bool sameCatalog(const QJsonObject &left, const QJsonObject &right) {
    return isCatalogBinding(left) && isCatalogBinding(right)
        && left.value("container").toString() == right.value("container").toString()
        && left.value("key").toString() == right.value("key").toString();
}
QString catalogPath(const QJsonObject &binding, const QString &container,
                    iiFileProvider::FileCatalog &catalog, bool verifyIdentity) {
    const auto storage = storageFor(container);
    if (binding.value("container").toString() != storage.drive().identifier())
        throw std::runtime_error("This resource belongs to another Society storage.");
    const auto key = binding.value("key").toString();
    if (key.isEmpty()) throw std::runtime_error("The resource identity is invalid.");
    const auto item = catalog.lookup(utf8(key));
    if (!item || catalog.itemKind(item->key) != iiFileProvider::CatalogEntryKind::File)
        throw std::runtime_error("The referenced resource is not available in this catalog.");
    QString error;
    const auto path = storage.filePath(iiSocietyContainer::StoreSection::Files, text(item->path), &error);
    if (path.isEmpty())
        throw std::runtime_error(utf8(error.isEmpty() ? QStringLiteral("The resource location is unavailable.") : error));
    if (verifyIdentity) {
        // A single bounded byte validates the provider's native identity before
        // exposing a path; a replacement at an old pathname is not substituted.
        (void)catalog.read(item->key, 0, 1);
        if (!QFileInfo(path).isFile())
            throw std::runtime_error("The referenced resource is not available on this device.");
    }
    return path;
}
}

QJsonObject ProjectResourceBinding::capture(const QString &container, iiFileProvider::FileCatalog &catalog,
                                             const QString &locator, bool copy) {
    const QFileInfo candidate(locator);
    if (!candidate.isAbsolute() || !candidate.isFile()) return {};
    const auto storage = storageFor(container);
    const auto root = QDir::cleanPath(qpath(catalog.root()));
    const auto path = QDir::cleanPath(candidate.absoluteFilePath());
    const auto relative = QDir(root).relativeFilePath(path);
    if (!relative.startsWith("../") && relative != ".." && !QDir::isAbsolutePath(relative)) {
        QString error;
        const auto validated = storage.filePath(iiSocietyContainer::StoreSection::Files, relative, &error);
        if (validated.isEmpty() || QDir::cleanPath(validated) != path)
            throw std::runtime_error(utf8(error.isEmpty() ? QStringLiteral("The resource path is unavailable or redirected.") : error));
        if (const auto prior = catalog.lookupPath(utf8(relative))) {
            try {
                if (catalog.itemKind(prior->key) != iiFileProvider::CatalogEntryKind::File)
                    throw std::runtime_error("The catalog entry is not a file.");
                (void)catalog.read(prior->key, 0, 1);
            } catch (const std::exception &e) {
                throw std::runtime_error(std::string("The existing catalog entry cannot be verified. "
                    "Reconcile the file before selecting it: ") + e.what());
            }
        }
        const auto registration = catalog.registerFile(utf8(relative));
        if (!registration.metadataWritten)
            throw std::runtime_error("Resource registered, but its catalog metadata is pending: " + registration.metadataError);
        return {{"kind", QLatin1String(catalogKind)}, {"container", storage.drive().identifier()},
                {"key", text(registration.file.key)}, {"contentVersion", text(registration.file.contentVersion)}};
    }
    if (!copy) return {};
    const auto identity = iiFileProvider::ObjectSource::inspect(nativePath(path));
    return {{"kind", QLatin1String(nativeKind)}, {"stamp", text(identity.stamp)}};
}

QJsonObject ProjectResourceBinding::stored(const iiProjectManager::Resource &resource) {
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(QByteArray::fromStdString(resource.extensionsJson), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        throw std::runtime_error("The project resource extensions are invalid.");
    const auto value = document.object().value(QLatin1String(bindingKey));
    if (value.isUndefined()) return {};
    if (!value.isObject()) throw std::runtime_error("The project resource identity is invalid.");
    const auto binding = value.toObject();
    const auto kind = binding.value("kind").toString();
    if (kind != QLatin1String(catalogKind) && kind != QLatin1String(nativeKind))
        throw std::runtime_error("The project resource identity is unsupported.");
    if (isCatalogBinding(binding) && (binding.value("container").toString().isEmpty()
                                      || binding.value("key").toString().isEmpty()))
        throw std::runtime_error("The project resource identity is invalid.");
    if (kind == QLatin1String(nativeKind) && binding.value("stamp").toString().isEmpty())
        throw std::runtime_error("The project source identity is invalid.");
    return binding;
}

void ProjectResourceBinding::attach(iiProjectManager::Resource &resource, const QJsonObject &binding) {
    if (binding.isEmpty()) return;
    auto extensions = QJsonDocument::fromJson(QByteArray::fromStdString(resource.extensionsJson)).object();
    extensions.insert(QLatin1String(bindingKey), binding);
    resource.extensionsJson = QJsonDocument(extensions).toJson(QJsonDocument::Compact).toStdString();
}

QString ProjectResourceBinding::assetId(const QJsonObject &binding) {
    const auto value=resourceId(binding);
    return value ? text(value->value) : QString();
}
std::optional<ii::resource::ResourceId> ProjectResourceBinding::resourceId(const QJsonObject &binding) {
    if (!isCatalogBinding(binding) || binding.value("container").toString().isEmpty() || binding.value("key").toString().isEmpty()) return {};
    const auto legacy=QStringLiteral("urn:iisacc:asset:") + binding.value("container").toString() + ':'
        + QString::fromLatin1(QUrl::toPercentEncoding(binding.value("key").toString()));
    ii::resource::Resource value{{utf8(legacy)},"","",{}};
    if (ii::resource::validate(value)!=ii::resource::Error::none) return {};
    return value.id;
}

bool ProjectResourceBinding::sameInput(const iiProjectManager::Resource &resource,
                                       const QJsonObject &binding, const QString &locator, bool copy) {
    if ((resource.location == iiProjectManager::ResourceLocation::embedded) != copy) return false;
    const auto previous = stored(resource);
    if (sameCatalog(previous, binding))
        return !copy || previous.value("contentVersion").toString() == binding.value("contentVersion").toString();
    if (copy && previous.value("kind").toString() == QLatin1String(nativeKind)
        && binding.value("kind").toString() == QLatin1String(nativeKind))
        return previous.value("stamp").toString() == binding.value("stamp").toString();
    return !copy && (previous.isEmpty() || binding.isEmpty()) && text(resource.locator) == locator;
}

QString ProjectResourceBinding::displayLocator(const iiProjectManager::Resource &resource,
                                                const QString &container, iiFileProvider::FileCatalog &catalog) {
    const auto binding = stored(resource);
    if (resource.location != iiProjectManager::ResourceLocation::reference || !isCatalogBinding(binding))
        return text(resource.locator);
    try { return catalogPath(binding, container, catalog, false); }
    catch (const std::exception &) { return text(resource.locator); }
}

iiProjectManager::Resolution ProjectResourceBinding::resolve(const iiProjectManager::Resource &resource,
    const iiProjectManager::Project &project, const QString &container, iiFileProvider::FileCatalog &catalog) {
    const auto binding = stored(resource);
    if (resource.location == iiProjectManager::ResourceLocation::reference && isCatalogBinding(binding))
        return {resource.location, utf8(catalogPath(binding, container, catalog, true)),
                iiProjectManager::Availability::available, {}};
    return project.resolve(resource.id);
}

void ProjectResourceBinding::checkLocatorEdit(const iiProjectManager::Resource &resource,
    const QString &locator, const QString &container, iiFileProvider::FileCatalog &catalog) {
    const auto binding = stored(resource);
    if (!isCatalogBinding(binding) || locator == text(resource.locator)) return;
    if (QDir::cleanPath(locator) != QDir::cleanPath(catalogPath(binding, container, catalog, true)))
        throw std::runtime_error("This reference has a stable identity. Remove it and add the other resource instead.");
}

#pragma once

#include <QJsonObject>
#include <QString>
#include <iiProjectManager/ProjectManager.h>
#include <iiResource/Resource.h>
#include <optional>

namespace iiFileProvider { class FileCatalog; }

// Society-specific mapping of project selections to provider-owned identity.
// The project SDK remains independent of the provider and stores this as an
// optional extension; old projects continue to resolve their original locators.
class ProjectResourceBinding final {
public:
    [[nodiscard]] static QJsonObject capture(const QString &container,
        iiFileProvider::FileCatalog &catalog, const QString &locator, bool copy);
    [[nodiscard]] static QJsonObject stored(const iiProjectManager::Resource &resource);
    static void attach(iiProjectManager::Resource &resource, const QJsonObject &binding);
    [[nodiscard]] static QString assetId(const QJsonObject &binding);
    [[nodiscard]] static std::optional<ii::resource::ResourceId> resourceId(const QJsonObject &binding);
    [[nodiscard]] static bool sameInput(const iiProjectManager::Resource &resource,
        const QJsonObject &binding, const QString &locator, bool copy);
    [[nodiscard]] static QString displayLocator(const iiProjectManager::Resource &resource,
        const QString &container, iiFileProvider::FileCatalog &catalog);
    [[nodiscard]] static iiProjectManager::Resolution resolve(const iiProjectManager::Resource &resource,
        const iiProjectManager::Project &project, const QString &container,
        iiFileProvider::FileCatalog &catalog);
    static void checkLocatorEdit(const iiProjectManager::Resource &resource,
        const QString &locator, const QString &container, iiFileProvider::FileCatalog &catalog);
};

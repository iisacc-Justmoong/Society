#pragma once
#include <SharedStorage.h>
#include <QDir>

// Society's runtime writers share the selected package's private namespace.
// This check resolves paths without creating directories or selecting storage.
inline bool societyContainerRuntimePath(const QString &path)
{
    const auto storage = iiSocietyContainer::SharedStorage::open({}, nullptr, true);
    if (!storage || path.isEmpty() || !QDir::isAbsolutePath(path)) return false;
    const auto root = storage->runtimePath();
    if (root.isEmpty()) return false;
    const auto relative = QDir(root).relativeFilePath(QDir::cleanPath(path));
    return storage->runtimePath(relative == "." ? QString() : relative) == QDir::cleanPath(path);
}

#include <iiFileProviderDevice.h>
#include "SyncOwnership.h"
#include "ContainerRuntimePath.h"
#include <QJsonDocument>
#include <QJsonObject>
SyncOwnership::SyncOwnership(QString directory, bool foreground, QObject *parent)
    : QObject(parent), m_directory(std::move(directory)), m_foreground(foreground) {
    m_timer.setInterval(250);
    connect(&m_timer, &QTimer::timeout, this, &SyncOwnership::reconcile);
}
SyncOwnership::~SyncOwnership() { stop(); }
bool SyncOwnership::setDirectory(const QString &directory) {
    if (m_directory == directory && m_timer.isActive()) return true;
    stop(); m_directory = directory;
    return !m_directory.isEmpty() && start();
}
bool SyncOwnership::start() {
    if (m_timer.isActive()) return true;
    if (!societyContainerRuntimePath(m_directory) || m_directory.isEmpty() || !iiFileProvider::Directory::isAbsolutePath(m_directory) || QFileInfo(m_directory).isSymLink()
        || !iiFileProvider::Directory().mkpath(m_directory) || !iiFileProvider::FileDevice::setPermissions(m_directory, iiFileProvider::FileDevice::ReadOwner | iiFileProvider::FileDevice::WriteOwner | iiFileProvider::FileDevice::ExeOwner)) {
        m_error = "sync_runtime_directory_unavailable"; return false;
    }
    m_priority = std::make_unique<iiFileProvider::LockFile>(iiFileProvider::Directory(m_directory).filePath("foreground.lock"));
    m_owner = std::make_unique<iiFileProvider::LockFile>(iiFileProvider::Directory(m_directory).filePath("owner.lock"));
    m_priority->setStaleLockTime(0); m_owner->setStaleLockTime(0);
    m_timer.start(); reconcile(); return true;
}
void SyncOwnership::release() {
    if (!m_owned) return;
    m_owned = false;
    // Slots synchronously stop the old transport before another process enters.
    emit ownershipChanged(false); m_owner->unlock();
}
void SyncOwnership::stop() {
    m_timer.stop(); release(); m_owner.reset(); m_priority.reset();
}
void SyncOwnership::reconcile() {
    if (!societyContainerRuntimePath(m_directory)) { stop(); return; }
    if (m_foreground) {
        if (!m_priority->isLocked() && !m_priority->tryLock()) return;
    } else if (QFileInfo::exists(iiFileProvider::Directory(m_directory).filePath("foreground.lock"))) {
        // tryLock also recovers a lease left behind by a crashed GUI process.
        if (!m_priority->tryLock()) { release(); return; }
        m_priority->unlock();
    }
    if (!m_owned && m_owner->tryLock()) { m_owned = true; emit ownershipChanged(true); }
}
bool SyncOwnership::rememberContainer(const QString &path) {
    if (!m_foreground || !m_owned || !iiFileProvider::Directory::isAbsolutePath(path)) return false;
    const auto selected = iiSocietyContainer::SharedStorage::open({}, nullptr, true);
    if (!selected || selected->drive().rootPath() != iiFileProvider::Directory::cleanPath(path) || !societyContainerRuntimePath(m_directory)) return false;
    if (storedContainer() == iiFileProvider::Directory::cleanPath(path)) return true;
    iiFileProvider::SaveFile file(iiFileProvider::Directory(m_directory).filePath("container.json"));
    const auto bytes = QJsonDocument(QJsonObject{{"version", 1}, {"container", iiFileProvider::Directory::cleanPath(path)}}).toJson(QJsonDocument::Compact);
    return file.open(QIODevice::WriteOnly) && file.setPermissions(iiFileProvider::FileDevice::ReadOwner | iiFileProvider::FileDevice::WriteOwner)
        && file.write(bytes) == bytes.size() && file.commit();
}
QString SyncOwnership::storedContainer() const {
    iiFileProvider::FileDevice file(iiFileProvider::Directory(m_directory).filePath("container.json"));
    if (!file.open(QIODevice::ReadOnly) || file.size() > 16384) return {};
    const auto record = QJsonDocument::fromJson(file.readAll()).object();
    const auto path = record.value("container").toString();
    return record.value("version").toInt() == 1 && iiFileProvider::Directory::isAbsolutePath(path) ? path : QString();
}

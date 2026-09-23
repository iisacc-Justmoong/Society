#include "SyncOwnership.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

SyncOwnership::SyncOwnership(QString directory, bool foreground, QObject *parent)
    : QObject(parent), m_directory(std::move(directory)), m_foreground(foreground) {
    m_timer.setInterval(250);
    connect(&m_timer, &QTimer::timeout, this, &SyncOwnership::reconcile);
}
SyncOwnership::~SyncOwnership() { stop(); }
bool SyncOwnership::start() {
    if (m_timer.isActive()) return true;
    if (m_directory.isEmpty() || !QDir::isAbsolutePath(m_directory) || QFileInfo(m_directory).isSymLink()
        || !QDir().mkpath(m_directory) || !QFile::setPermissions(m_directory, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)) {
        m_error = "sync_runtime_directory_unavailable"; return false;
    }
    m_priority = std::make_unique<QLockFile>(QDir(m_directory).filePath("foreground.lock"));
    m_owner = std::make_unique<QLockFile>(QDir(m_directory).filePath("owner.lock"));
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
    if (m_foreground) {
        if (!m_priority->isLocked() && !m_priority->tryLock()) return;
    } else if (QFileInfo::exists(QDir(m_directory).filePath("foreground.lock"))) {
        // tryLock also recovers a lease left behind by a crashed GUI process.
        if (!m_priority->tryLock()) { release(); return; }
        m_priority->unlock();
    }
    if (!m_owned && m_owner->tryLock()) { m_owned = true; emit ownershipChanged(true); }
}
bool SyncOwnership::rememberContainer(const QString &path) {
    if (!m_foreground || !m_owned || !QDir::isAbsolutePath(path)) return false;
    if (storedContainer() == QDir::cleanPath(path)) return true;
    QSaveFile file(QDir(m_directory).filePath("container.json"));
    const auto bytes = QJsonDocument(QJsonObject{{"version", 1}, {"container", QDir::cleanPath(path)}}).toJson(QJsonDocument::Compact);
    return file.open(QIODevice::WriteOnly) && file.setPermissions(QFile::ReadOwner | QFile::WriteOwner)
        && file.write(bytes) == bytes.size() && file.commit();
}
QString SyncOwnership::storedContainer() const {
    QFile file(QDir(m_directory).filePath("container.json"));
    if (!file.open(QIODevice::ReadOnly) || file.size() > 16384) return {};
    const auto record = QJsonDocument::fromJson(file.readAll()).object();
    const auto path = record.value("container").toString();
    return record.value("version").toInt() == 1 && QDir::isAbsolutePath(path) ? path : QString();
}

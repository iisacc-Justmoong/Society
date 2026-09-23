#include "DaemonService.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(daemonLog, "iisacc.society.daemon")

SocietyDaemonService::SocietyDaemonService(QObject *parent) : QObject(parent)
{
    m_timer.setInterval(250);
    connect(&m_timer, &QTimer::timeout, this, &SocietyDaemonService::drain);
    connect(&m_helper, &iiSocietyHelper::Helper::peersChanged, this, [this] { m_snapshotDirty = true; });
    connect(qApp, &QCoreApplication::aboutToQuit, this, &SocietyDaemonService::stop);
}
SocietyDaemonService::~SocietyDaemonService() { stop(); }
bool SocietyDaemonService::fail(const QString &message)
{
    if (m_error != message) {
        m_error = message;
        qCWarning(daemonLog).noquote() << message;
        emit errorOccurred(message);
    }
    return false;
}

bool SocietyDaemonService::start(const QString &directory, int heartbeatMs, int timeoutMs)
{
    if (m_running) return fail(QStringLiteral("Society daemon is already running."));
    const auto root = directory.isEmpty() ? iiSocietyHelper::Helper::defaultDirectory(&m_error) : directory;
    if (root.isEmpty() || !QDir::isAbsolutePath(root) || QDir(root).isRoot() || QFileInfo(root).isSymLink())
        return fail(QStringLiteral("Society daemon needs a local absolute observation directory."));
    const bool existed = QFileInfo::exists(root);
    if (!QDir().mkpath(root) || (!existed && !QFile::setPermissions(root,
            QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner)))
        return fail(QStringLiteral("Cannot prepare Society daemon storage."));
    if (!m_store.open(root)) return fail(m_store.errorString());
    const auto lockPath = QDir(root).filePath("delivery/daemon.lock");
    if (QFileInfo(lockPath).isSymLink()) { m_store.close(); return fail(QStringLiteral("Invalid Society daemon lock.")); }
    m_lock = std::make_unique<QLockFile>(lockPath);
    m_lock->setStaleLockTime(0); // Never evict a live daemon based on the age of its lock.
    if (!m_lock->tryLock()) {
        m_lock.reset();
        m_store.close();
        return fail(QStringLiteral("Another Society daemon owns this observation directory."));
    }
    if (!m_helper.start({"com.iisacc.society.daemon", "Society Daemon", SOCIETY_APP_VERSION},
                         {root, heartbeatMs, timeoutMs})) {
        m_lock.reset();
        m_store.close();
        return fail(m_helper.errorString());
    }
    m_error.clear();
    m_running = true;
    m_snapshotDirty = true;
    m_snapshotAge.start();
    drain();
    m_timer.start();
    qCInfo(daemonLog).noquote() << "Society daemon ready" << m_helper.self().instanceId;
    return true;
}

void SocietyDaemonService::drain()
{
    if (!m_running) return;
    const auto count = m_store.receivePending();
    if (count < 0) { fail(m_store.errorString()); return; }
    if (count > 0) {
        qCInfo(daemonLog) << "Society daemon accepted" << count << "messages";
        emit dataAccepted(count);
    }
    if (m_snapshotDirty || m_snapshotAge.elapsed() >= 1000) {
        if (!m_store.setDaemonSnapshot({{"instanceId", m_helper.self().instanceId},
                {"processId", QCoreApplication::applicationPid()}, {"running", m_helper.isRunning()},
                {"heartbeat", QString::number(++m_heartbeat)}, {"observedAt", QDateTime::currentDateTimeUtc()},
                {"peers", m_helper.observedApplications()}})) { fail(m_store.errorString()); return; }
        m_snapshotDirty = false;
        m_snapshotAge.restart();
    }
    m_error.clear();
}

void SocietyDaemonService::stop()
{
    if (!m_running) return;
    m_timer.stop();
    m_helper.stop();
    m_snapshotDirty = true;
    drain();
    m_running = false;
    m_store.close();
    m_lock.reset();
}

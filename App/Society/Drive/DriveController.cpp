#include "DriveController.h"
#include "../Native/NativeFilesLauncher.h"
#include <iiSocietyContainer>

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFutureWatcher>
#include <QStorageInfo>
#include <QUuid>
#include <QtConcurrent/QtConcurrentRun>
#ifdef SOCIETY_DESKTOP_BACKEND
#include "../Files/ModelBackendClient.h"
#include "Shared/LocalBackendAddress.h"
#endif
#ifdef Q_OS_ANDROID
#include <iiSocietyContainer>
#endif
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
#include <QGuiApplication>
#endif
#ifdef Q_OS_IOS
#include <IosDriveBridge.h>
#include "../Files/IosFileAccess.h"
#include <QGuiApplication>
#include <QPointer>
#include <memory>
#endif
#include <utility>

using namespace iiSocietyContainer;

namespace {
bool legacyDiskImage(const QString &path)
{
    const auto suffix = QFileInfo(path).suffix().toLower();
    return suffix == "sparsebundle"
        || QFileInfo::exists(QDir(path).filePath(".society-disk.plist"))
        || (QFileInfo::exists(QDir(path).filePath("Info.plist"))
            && QFileInfo(QDir(path).filePath("bands")).isDir());
}
}

struct DriveController::ReloadResult {
    std::optional<SocietyDrive> drive;
    QString error, checkedPath;
    bool pathExists = true;
};

DriveController::DriveController(QObject *parent) : QObject(parent)
{
    m_mirrorPending = managedContainer();
    auto *volumeMonitor = new QTimer(this);
    volumeMonitor->setInterval(2000);
    connect(volumeMonitor, &QTimer::timeout, this, [this] {
        if (!m_diskImagePath.isEmpty() && hasDrive() && !busy()) {
            const QStorageInfo volume(rootPath());
            const QStorageInfo files(m_systemPath);
            if (!volume.isReady() || volume.rootPath() != rootPath()
                || !files.isReady() || files.rootPath() != m_systemPath) refreshFromDisk();
        }
    });
    volumeMonitor->start();
#ifdef SOCIETY_DESKTOP_BACKEND
    m_systemBackend = new ModelBackendClient(this);
    connect(this, &DriveController::locationChanged, this, &DriveController::synchronizeSystemSelection);
    connect(m_systemBackend, &ModelBackendClient::systemDriveStateReceived, this, &DriveController::applySystemDriveState);
    connect(m_systemBackend, &ModelBackendClient::connectedChanged, this, [this] {
        if (!m_systemBackend->connected() && hasDrive() && m_diskImagePath.isEmpty()) {
            m_systemPath.clear();
            m_systemStatus = tr("Society.societycontainer is ready. Waiting for the Society background service.");
            emit systemChanged();
        }
    });
    connect(m_systemBackend, &ModelBackendClient::requestError, this, [this](const QString &error) {
        if (!hasDrive()) return;
        m_openWhenConnected = false;
        m_systemStatus = error;
        emit systemChanged();
    });
    m_systemBackend->start();
#endif
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(35000);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        ++m_requestGeneration;
        finishNative(false, QJsonDocument(QJsonObject{{"error", tr("%1 did not respond in time.").arg(systemName())}}).toJson());
    });
    connect(qGuiApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state == Qt::ApplicationSuspended || state == Qt::ApplicationHidden) {
            // UIKit/File Provider completions may arrive after suspension.
            // Retire this request so it cannot overwrite a newer connection.
            m_timeout.stop();
            ++m_requestGeneration;
            m_action.clear();
            emit systemChanged();
        } else if (state == Qt::ApplicationActive && !busy()) {
            if (hasDrive()) refreshSystem();
            else openDefaultContainer();
        }
    });
#endif
}

bool DriveController::hasDrive() const { return m_drive.has_value(); }
QString DriveController::rootPath() const { return m_drive ? m_drive->rootPath() : QString(); }
QString DriveController::identifier() const { return m_drive ? m_drive->identifier() : QString(); }
QString DriveController::currentPath() const { return m_currentPath; }
bool DriveController::atRoot() const { return m_currentPath.isEmpty(); }
QString DriveController::errorString() const { return m_error; }
bool DriveController::busy() const { return !m_action.isEmpty(); }
QString DriveController::systemPath() const { return m_systemPath; }
QString DriveController::systemStatus() const { return m_systemStatus; }

bool DriveController::managedContainer() const
{
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    return true;
#else
    return false;
#endif
}

QString DriveController::systemName() const
{
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    return tr("Files");
#elif defined(Q_OS_WIN)
    return tr("File Explorer");
#elif defined(Q_OS_LINUX)
    return tr("File Manager");
#else
    return tr("Finder");
#endif
}

void DriveController::setAccountDriveStatus(const QString& status)
{
    if (m_accountDriveStatus == status) return;
    m_accountDriveStatus = status;
    emit accountDriveStatusChanged();
}

void DriveController::setAccountManager(iisacc::accounts::AccountManager* manager)
{
    if (m_account == manager) return;
    if (m_account) {
        disconnect(m_account, nullptr, this, nullptr);
        disconnect(m_account->account(), nullptr, this, nullptr);
    }
    m_account = manager;
    m_accountDriveRevision.clear();
    if (manager) {
        connect(manager->account(), &iisacc::accounts::Account::changed, this, &DriveController::applyAccountDrive, Qt::QueuedConnection);
        connect(manager, &iisacc::accounts::AccountManager::containerDriveSynchronized, this, &DriveController::applyAccountDrive);
        connect(manager, &iisacc::accounts::AccountManager::containerDriveUpdateFinished, this, [this](bool success, const QString& message) {
            if (success) { m_publishToAccount = false; m_pendingAccountSubject.clear(); }
            setAccountDriveStatus(success ? tr("Drive location saved to your account. Your other devices will receive this change automatically.") : message);
        });
        connect(manager, &iisacc::accounts::AccountManager::authenticatedChanged, this, [this] {
            m_accountDriveRevision.clear();
            if (!m_account->isAuthenticated()) {
                m_publishToAccount = false; m_pendingAccountSubject.clear();
                setAccountDriveStatus(tr("Sign in to synchronize your drive location."));
            }
        });
    }
    emit accountManagerChanged();
    QTimer::singleShot(0, this, &DriveController::applyAccountDrive);
}

bool DriveController::saveContainerToAccount()
{
    if (!m_account || !m_account->isAuthenticated() || !hasDrive() || busy()) return false;
    if (!m_pendingAccountSubject.isEmpty() && m_pendingAccountSubject != m_account->account()->sub()) return false;
    const auto saved = m_account->account()->societyContainerDrive();
    if (!saved.isEmpty() && (saved.value("hostDeviceId") != m_account->deviceInfo().value("id") || saved.value("containerId") != identifier())) {
        setAccountDriveStatus(tr("This account uses a different host drive. Its location can be changed on that host."));
        return false;
    }
    if (!m_account->setContainerDrive(m_diskImagePath.isEmpty() ? rootPath() : m_diskImagePath, identifier())) {
        setAccountDriveStatus(tr("Account settings are still loading. Try saving the drive location again shortly."));
        return false;
    }
    setAccountDriveStatus(tr("Saving the drive location to your account…"));
    return true;
}

bool DriveController::useMovedContainer(const QUrl& image)
{
    if (busy() || !image.isLocalFile() || !m_account || !m_account->isAuthenticated()) return false;
    const auto saved = m_account->account()->societyContainerDrive();
    if (saved.isEmpty() || saved.value("hostDeviceId") != m_account->deviceInfo().value("id"))
        return fail(tr("Select the moved folder on the device that owns this account drive."));
    prepareDisk(image.toLocalFile(), false, saved.value("containerId").toString(), true);
    return true;
}

void DriveController::applyAccountDrive()
{
    if (!m_account || !m_account->isAuthenticated() || busy()) return;
    const auto saved = m_account->account()->societyContainerDrive();
    if (m_publishToAccount) {
        if (saved.isEmpty()) saveContainerToAccount();
        return; // An explicit relocation remains selected until its server result arrives.
    }
    if (saved.isEmpty()) {
        // A verified local disk is the initial host. The account API's revision
        // precondition protects a host registered concurrently on another device.
        if (!managedContainer() && hasDrive()) saveContainerToAccount();
        else setAccountDriveStatus(tr("Open a Society folder on your desktop to connect your devices automatically."));
        return;
    }
    const auto revision = saved.value("revision").toString();
    if (revision == m_accountDriveRevision) return;
    m_accountDriveRevision = revision;
    if (saved.value("hostDeviceId") != m_account->deviceInfo().value("id")) {
        setAccountDriveStatus(tr("Your account drive is on another device. Its current location has been synchronized."));
        return; // Host filesystem paths must never replace client replica paths.
    }
    const auto image = saved.value("imagePath").toString();
    if (legacyDiskImage(image)) {
        const auto message = tr("Legacy Society disk images are not restored. Choose a Society.societycontainer package.");
        setAccountDriveStatus(message);
        fail(message);
        return;
    }
    // A selected package with the same host identity is authoritative after
    // relocating the source. Publish its new location before considering the
    // stale account path; never reopen or recreate the removed directory.
    if (hasDrive() && m_drive->isValid() && identifier() == saved.value("containerId")
        && image != rootPath() && !QFileInfo(image).exists()) {
        m_publishToAccount = true;
        m_pendingAccountSubject = m_account->account()->sub();
        saveContainerToAccount();
        return;
    }
    if (image == (m_diskImagePath.isEmpty() ? rootPath() : m_diskImagePath) && hasDrive() && identifier() == saved.value("containerId")) return;
    // Retire the stale mount before the network controller serves it again.
    m_drive.reset(); m_currentPath.clear(); m_systemPath.clear();
    emit locationChanged(); emit contentsChanged();
    prepareDisk(image, false, saved.value("containerId").toString());
}

void DriveController::openDefaultContainer()
{
    if (m_account && m_account->isAuthenticated()) {
        const auto saved = m_account->account()->societyContainerDrive();
        if (!saved.isEmpty() && saved.value("hostDeviceId") == m_account->deviceInfo().value("id")) {
            if (legacyDiskImage(saved.value("imagePath").toString())) {
                fail(tr("Legacy Society disk images are not restored. Choose a Society.societycontainer package."));
                return;
            }
            const auto selected = SharedStorage::open({}, nullptr, true);
            if (selected && selected->drive().identifier() == saved.value("containerId")
                && !QFileInfo(saved.value("imagePath").toString()).exists()) {
                prepareDisk(selected->drive().rootPath(), false, saved.value("containerId").toString());
                return;
            }
            prepareDisk(saved.value("imagePath").toString(), false, saved.value("containerId").toString());
            return;
        }
    }
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    if (managedContainer())
        startNative(QStringLiteral("default"));
    else {
#else
    {
#endif
        // A first launch is a normal setup state, not a storage failure.
        if (qEnvironmentVariableIsEmpty("SOCIETY_CONTAINER_PATH")
            && !QFileInfo::exists(SharedStorage::settingsPath())) {
            QString sharedError;
            const auto shared = SharedStorage::open({}, &sharedError, true);
            if (shared) {
                prepareDisk(shared->drive().rootPath(), false, shared->drive().identifier());
                return;
            }
            const auto group = ApplicationStorage::sharedRoot();
            if (!group.isEmpty()) {
                QString registryError;
                const auto record = ApplicationStorage::registration(group, &registryError);
                if (!record.isEmpty() || !registryError.isEmpty()) { fail(sharedError); return; }
            }
            fail({});
            return;
        }
#ifdef Q_OS_MACOS
        prepareDisk();
#else
        QString error;
        const auto storage = SharedStorage::open({}, &error, true);
        if (storage) openContainer(storage->drive().rootPath());
        else fail(error);
#endif
    }
}

QVariantList DriveController::sections() const
{
    QVariantList entries;
    if (m_drive) {
        for (const auto section : allStoreSections())
            entries.append(QVariantMap{{"key", storeSectionKey(section)}, {"name", storeSectionName(section)},
                                       {"path", m_drive->sectionPath(section)}});
    }
    return entries;
}

QString DriveController::currentSection() const
{
    return atRoot() ? QString() : m_drive->relativePath(m_currentPath).section('/', 0, 0);
}

QVariantList DriveController::breadcrumbs() const
{
    QVariantList entries;
    if (!m_drive)
        return entries;
    entries.append(QVariantMap{{"name", m_drive->displayName()}, {"path", rootPath()}});
    if (!atRoot()) {
        QString relative;
        for (const auto &component : m_drive->relativePath(m_currentPath).split('/')) {
            relative += (relative.isEmpty() ? QString() : QString("/")) + component;
            entries.append(QVariantMap{{"name", component}, {"path", m_drive->resolvePath(relative)}});
        }
    }
    return entries;
}

bool DriveController::fail(const QString &message)
{
    if (m_error == message) return false;
    m_error = message;
    emit errorChanged();
    return false;
}

bool DriveController::openContainerUrl(const QUrl &url)
{
    return url.isLocalFile() ? createContainerAt(url.toLocalFile()) : fail(tr("Select a local folder for Society."));
}

bool DriveController::openContainerImage(const QUrl &url)
{
    if (busy()) return false;
    if (managedContainer() || !url.isLocalFile() || !QFileInfo::exists(url.toLocalFile()))
        return fail(tr("Select an existing local .societycontainer package."));
    const auto saved = m_account && m_account->isAuthenticated() ? m_account->account()->societyContainerDrive() : QVariantMap{};
    if (!saved.isEmpty() && saved.value("hostDeviceId") == m_account->deviceInfo().value("id"))
        return useMovedContainer(url);
    prepareDisk(url.toLocalFile(), false, {}, true);
    return true;
}

bool DriveController::createContainerAt(const QString &location)
{
    if (busy()) return fail(tr("Wait for the folder to finish preparing."));
    if (!QDir::isAbsolutePath(location) || !QFileInfo(location).isDir())
        return fail(tr("Choose an existing absolute parent folder for Society."));
    const auto saved = m_account && m_account->isAuthenticated() ? m_account->account()->societyContainerDrive() : QVariantMap{};
    if (!saved.isEmpty() && saved.value("hostDeviceId") == m_account->deviceInfo().value("id")) {
        // Reconnecting a moved drive must never silently create a new identity.
        prepareDisk(QFileInfo::exists(QDir(location).filePath(".society-drive.json")) ? location : QDir(location).filePath("Society.societycontainer"), false, saved.value("containerId").toString(), true);
    } else prepareDisk(location, true, {}, true);
    return true;
}

void DriveController::prepareDisk(const QString &location, bool create, const QString &expectedId, bool publish)
{
    if (busy()) return;
    struct Result { std::optional<SocietyDrive> drive; QString image, error; };
    m_action = QStringLiteral("disk");
    m_systemStatus = create ? tr("Creating Society.societycontainer…") : tr("Opening Society.societycontainer…");
    fail({});
    emit systemChanged();
    const auto accountBinding = m_account ? m_account->sessionBinding() : QVariantMap{};
    auto *watcher = new QFutureWatcher<Result>(this);
    connect(watcher, &QFutureWatcher<Result>::finished, this, [this, watcher, accountBinding, publish] {
        const auto result = watcher->result();
        watcher->deleteLater();
        m_action.clear();
        QTimer::singleShot(0, this, &DriveController::applyAccountDrive);
        if (!result.drive) {
            m_systemStatus.clear();
            fail(result.error);
            emit systemChanged();
            return;
        }
        if (!accountBinding.isEmpty() && (!m_account || !m_account->matchesSessionBinding(accountBinding))) {
            m_systemStatus.clear();
            emit systemChanged();
            return;
        }
        ++m_reloadRevision;
        m_reloadPending = false;
        QString saveError;
        if (!SharedStorage::setDefaultContainer(result.drive->rootPath(), &saveError)) {
            fail(saveError); emit systemChanged(); return;
        }
        m_drive = result.drive;
        m_diskImagePath = result.image;
        m_currentPath.clear();
        m_systemPath.clear();
        m_systemStatus = m_diskImagePath.isEmpty() ? tr("Society.societycontainer is ready. Connect its drive to %1.").arg(systemName())
            : tr("Society is mounted as a legacy disk in %1.").arg(systemName());
        fail({});
        emit locationChanged();
        emit systemChanged();
        emit contentsChanged();
        if (publish) {
            m_publishToAccount = true;
            m_pendingAccountSubject = accountBinding.value("subject").toString();
            if (!m_account || !m_account->isAuthenticated())
                setAccountDriveStatus(tr("Sign in, then save this drive location to your account to share future changes."));
            else saveContainerToAccount();
        } else applyAccountDrive();
    });
    watcher->setFuture(QtConcurrent::run([location, create, expectedId] {
        Result result;
        if (create) {
            result.drive = SocietyDrive::createAt(location, &result.error);
        } else if (!location.isEmpty()) {
            if (legacyDiskImage(location)) {
                result.error = tr("Legacy Society disk images are not restored. Choose a Society.societycontainer package.");
                return result;
            } else if (QFileInfo::exists(QDir(location).filePath(".society-drive.json"))) {
                const auto storage = SharedStorage::open(location, &result.error, true);
                if (storage) result.drive = storage->drive();
            } else {
                result.error = tr("Choose an existing .societycontainer package with its drive manifest.");
                return result;
            }
        } else {
            const auto storage = SharedStorage::open({}, &result.error, true);
            if (storage) result.drive = storage->drive();
        }
        if (!result.drive) return result;
        if (!expectedId.isEmpty() && result.drive->identifier() != expectedId) {
            result.drive.reset();
            result.error = tr("This is a different drive. Select the .societycontainer package already registered to your account.");
            return result;
        }
        if (result.drive->storageKind() == SocietyDrive::StorageKind::LogicalDisk) {
            const auto group = ApplicationStorage::sharedRoot();
            if (!group.isEmpty() && ApplicationStorage::selectedRoot(group) == result.drive->rootPath()) {
                const auto adopted = ApplicationStorage::adoptInGroup(group, &result.error);
                if (!adopted) { result.drive.reset(); return result; }
                result.drive = adopted->drive();
            }
        }
        if (const auto volume = DiskImage::mountedAt(result.drive->rootPath().toStdString()))
            result.image = QString::fromStdString(volume->imagePath.string());

        return result;
    }));
}

QString DriveController::localContainerPath(const QUrl &url) const
{
    return url.isLocalFile() ? QDir::toNativeSeparators(url.toLocalFile()) : QString();
}

bool DriveController::openContainer(const QString &path)
{
    if (busy())
        return fail(tr("Wait for the %1 connection to finish.").arg(systemName()));
    if (!QDir::isAbsolutePath(path))
        return fail(tr("The container requires an absolute folder path."));
    if (legacyDiskImage(path))
        return fail(tr("Legacy Society disk images are not restored. Choose a Society.societycontainer package."));
    QString error;
    const auto storage = SharedStorage::open(path, &error, true);
    if (!storage)
        return fail(error);
    auto drive = std::optional<SocietyDrive>{storage->drive()};
    if (!SharedStorage::setDefaultContainer(drive->rootPath(), &error))
        return fail(error);
    ++m_reloadRevision;
    m_reloadPending = false;
    m_drive = std::move(drive);
    m_currentPath.clear();
    m_systemPath.clear();
    m_diskImagePath.clear();
    m_systemStatus = managedContainer() ? tr("Connect this container to %1.").arg(systemName())
        : tr("Society.societycontainer is ready. Connect its drive to %1.").arg(systemName());
    if (const auto volume = DiskImage::mountedAt(rootPath().toStdString())) {
        m_diskImagePath = QString::fromStdString(volume->imagePath.string());
        m_systemPath = m_drive->sectionPath(StoreSection::Files);
        m_systemStatus = tr("Society is mounted as a legacy disk in %1.").arg(systemName());
    }
    fail({});
    emit locationChanged();
    emit systemChanged();
    emit contentsChanged();
    applyAccountDrive();
    return true;
}

void DriveController::setMirrorPending(bool pending)
{
    if (m_mirrorPending == pending) return;
    m_mirrorPending = pending;
    emit contentsChanged();
    if (!pending && hasDrive()) {
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
        m_nativeRebind = m_nativeRebind || managedContainer();
#endif
        refreshFromDisk();
    }
}

DriveController::ReloadResult DriveController::readFromDisk(const QString &root, const QString &currentPath)
{
    ReloadResult result;
    result.drive = SocietyDrive::open(root, &result.error);
    result.checkedPath = currentPath;
    result.pathExists = currentPath.isEmpty() || QFileInfo(currentPath).isDir();
    return result;
}

bool DriveController::applyReload(const ReloadResult &result)
{
    if (!result.drive) {
        m_drive.reset();
        m_currentPath.clear();
        m_systemPath.clear();
        emit locationChanged();
        emit systemChanged();
        emit contentsChanged();
        return fail(result.error);
    }
    const bool changed = result.drive->identifier() != identifier();
    const auto previousPath = m_currentPath;
    QString error;
    if (changed && !SharedStorage::setDefaultContainer(result.drive->rootPath(), &error)) return fail(error);
    m_drive = result.drive;
    // Navigation may have changed while the background read was running.
    if (changed || (m_currentPath == result.checkedPath && !result.pathExists)) m_currentPath.clear();
    if (changed) {
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
        m_nativeRebind = m_nativeRebind || managedContainer() || !m_systemPath.isEmpty();
#endif
        m_systemPath.clear();
    }
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    if (m_nativeRebind && !m_mirrorPending && !busy()) { m_nativeRebind = false; connectToSystem(); }
#endif
    if (changed || previousPath != m_currentPath) emit locationChanged();
    if (changed) emit contentsChanged();
    return true;
}

bool DriveController::reloadFromDisk()
{
    if (!hasDrive()) return false;
    ++m_reloadRevision;
    m_reloadPending = false;
    return applyReload(readFromDisk(rootPath(), m_currentPath));
}

void DriveController::refreshFromDisk()
{
    if (!hasDrive()) return;
    if (m_reloadRunning) { m_reloadPending = true; return; }
    m_reloadRunning = true;
    m_reloadPending = false;
    const auto revision = m_reloadRevision;
    const auto root = rootPath(), path = m_currentPath;
    auto *watcher = new QFutureWatcher<ReloadResult>(this);
    connect(watcher, &QFutureWatcher<ReloadResult>::finished, this, [this, watcher, revision] {
        const auto result = watcher->result();
        watcher->deleteLater();
        m_reloadRunning = false;
        if (revision == m_reloadRevision) applyReload(result);
        if (m_reloadPending) refreshFromDisk();
    });
    watcher->setFuture(QtConcurrent::run(&DriveController::readFromDisk, root, path));
}

bool DriveController::openSection(const QString &key)
{
    if (!contentsAvailable()) return fail(tr("Wait for the host drive's initial mirror to finish."));
    if (m_drive) {
        for (const auto section : allStoreSections())
            if (storeSectionKey(section) == key)
                return navigate(m_drive->sectionPath(section));
    }
    return fail(tr("Unknown drive section."));
}

bool DriveController::navigate(const QString &path)
{
    if (!contentsAvailable()) return fail(tr("Wait for the host drive's initial mirror to finish."));
    if (!m_drive || !m_drive->isValid())
        return fail(tr("The container is unavailable or its section layout has changed."));
    const QFileInfo info(path);
    if (!info.isAbsolute() || !info.isDir() || !info.isReadable())
        return fail(tr("This folder is unavailable."));
    const QString canonical = info.canonicalFilePath();
    if (canonical == rootPath()) {
        goHome();
        return true;
    }
    const auto section = m_drive->sectionForPath(canonical);
    if (!section)
        return fail(tr("This folder is outside the drive sections."));
    if (*section == StoreSection::GenerationHistory
        && canonical != m_drive->sectionPath(StoreSection::GenerationHistory))
        return fail(tr("Generation History contains generated images without folders."));
    m_currentPath = canonical;
    fail({});
    emit locationChanged();
    return true;
}

void DriveController::goHome()
{
    m_currentPath.clear();
    fail({});
    emit locationChanged();
}

void DriveController::goUp()
{
    if (!atRoot() && !m_drive->relativePath(m_currentPath).contains('/')) goHome();
    else if (!atRoot())
        navigate(QFileInfo(m_currentPath).dir().absolutePath());
}

bool DriveController::openFile(const QString &path)
{
    if (!m_drive || !m_drive->sectionForPath(path) || !QFileInfo(path).isFile())
        return fail(tr("This file is outside the drive sections or is unavailable."));
#ifdef Q_OS_IOS
    return previewIosFile(QFileInfo(path).canonicalFilePath())
        || fail(tr("A preview is not available for this file type."));
#else
    return QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).canonicalFilePath()))
        || fail(tr("No application could open this file."));
#endif
}

bool DriveController::openNativeFiles()
{
    QString error;
    if (!society::native::launchBundledFiles(&error)) return fail(error);
    return true;
}

bool DriveController::systemSupported() const
{
    // Ordinary-folder access remains available; installed native adapters add
    // a validated connection to the platform file manager.
    return true;
}

#ifdef SOCIETY_DESKTOP_BACKEND
void DriveController::synchronizeSystemSelection()
{
    const auto source = hasDrive() ? rootPath() : QString();
    const auto id = hasDrive() ? identifier() : QString();
    if (source == m_boundSystemSource && id == m_boundSystemIdentifier) {
        // Reopening the same container clears its displayed path. Obtain a
        // fresh backend snapshot instead of waiting for a state change that
        // may never occur while the drive remains mounted.
        if (!source.isEmpty() && m_diskImagePath.isEmpty() && m_systemPath.isEmpty()
            && m_systemBackend->connected()) m_systemBackend->request(QStringLiteral("snapshot"));
        return;
    }
    m_boundSystemSource = source;
    m_boundSystemIdentifier = id;
    m_openWhenConnected = false;
    m_systemBackend->setContainerPath(source);
    if (m_diskImagePath.isEmpty() || source.isEmpty()) {
        m_systemPath.clear();
        m_systemStatus = source.isEmpty() ? QString()
            : tr("Society.societycontainer is ready. Waiting for the Society background service.");
        emit systemChanged();
    }
}

void DriveController::applySystemDriveState(const QJsonObject &state)
{
    const auto id = QUuid(identifier());
    if (!hasDrive() || !m_diskImagePath.isEmpty()) return;
    if (id.isNull() || !societySameBackendPath(state.value("sourcePath").toString(), rootPath())
        || QUuid(state.value("identifier").toString()) != id) {
        if (!m_systemPath.isEmpty()) {
            m_systemPath.clear();
            m_systemStatus = tr("The Society background service is checking the selected drive.");
            emit systemChanged();
        }
        return;
    }
    const auto path = state.value("systemPath").toString();
    const bool connected = state.value("ready").toBool() && state.value("connected").toBool()
        && QDir::isAbsolutePath(path) && !societySameBackendPath(path, rootPath());
    m_systemPath = connected ? path : QString();
    const auto error = state.value("error").toString();
    if (connected) m_systemStatus = tr("Society is mounted as a drive in %1.").arg(systemName());
    else if (!error.isEmpty()) m_systemStatus = error;
    else if (state.value("connecting").toBool()) m_systemStatus = tr("Connecting to %1…").arg(systemName());
    else m_systemStatus = tr("The Society background service is preparing the drive.");
    emit systemChanged();
    if (connected && std::exchange(m_openWhenConnected, false))
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}
#endif

void DriveController::connectToSystem()
{
#ifdef SOCIETY_DESKTOP_BACKEND
    if (!hasDrive() || !m_diskImagePath.isEmpty()) return;
    if (!m_systemBackend->connected()) {
        m_systemStatus = tr("Society.societycontainer is ready. Waiting for the Society background service.");
        emit systemChanged();
        return;
    }
    m_systemStatus = tr("Connecting to %1…").arg(systemName());
    emit systemChanged();
    m_systemBackend->request(QStringLiteral("systemDriveReconnect"), {{"identifier", identifier()}}, true);
#elif defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    if (!m_diskImagePath.isEmpty()) prepareDisk(m_diskImagePath);
    else if (!managedContainer() && hasDrive()) refreshSystem();
    else startNative(QStringLiteral("register"), true);
#endif
}

void DriveController::refreshSystem()
{
#ifdef SOCIETY_DESKTOP_BACKEND
    connectToSystem();
#elif defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    if (!m_diskImagePath.isEmpty()) prepareDisk(m_diskImagePath);
    else if (!managedContainer() && hasDrive()) {
        if (reloadFromDisk()) {
            m_systemPath.clear();
            m_systemStatus = tr("Society.societycontainer is ready. Connect its drive to %1.").arg(systemName());
            emit systemChanged();
        }
    } else startNative(QStringLiteral("refresh"));
#endif
}

void DriveController::revealInSystem()
{
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    startNative(QStringLiteral("path"));
#elif defined(SOCIETY_DESKTOP_BACKEND)
    if (!m_systemPath.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(m_systemPath));
    else { m_openWhenConnected = true; connectToSystem(); }
#endif
}

#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
void DriveController::startNative(const QString &action, bool registerBundle)
{
    if (action != QStringLiteral("default") && m_mirrorPending) {
        m_nativeRebind = true;
        m_systemStatus = tr("Connect to your desktop to prepare this Society drive.");
        emit systemChanged(); return;
    }
    if (busy() || (!m_drive && !(managedContainer() && action == QStringLiteral("default"))))
        return;
    if (!systemSupported()) {
        fail(tr("The native Society adapter is not installed."));
        return;
    }
    if (m_drive && !m_drive->isValid()) {
        fail(tr("The container is unavailable or its section layout has changed."));
        return;
    }
    m_action = action;
    m_systemStatus = tr("Connecting to %1…").arg(systemName());
    fail({});
#ifdef Q_OS_ANDROID
    Q_UNUSED(registerBundle);
    emit systemChanged();
    finishNative(true, androidDriveRequest(action, rootPath(), identifier()));
#elif defined(Q_OS_IOS)
    Q_UNUSED(registerBundle);
    struct Request { QPointer<DriveController> owner; quint64 generation; };
    auto *request = new Request{this, ++m_requestGeneration};
    emit systemChanged();
    m_timeout.start();
    society_ios_drive_request(action.toUtf8().constData(), rootPath().toUtf8().constData(),
        identifier().toUtf8().constData(), request, [](void *context, const char *output) {
            const std::unique_ptr<Request> request(static_cast<Request *>(context));
            if (request->owner && request->owner->m_requestGeneration == request->generation)
                request->owner->finishNative(true, QByteArray(output));
        });
#else
    Q_UNUSED(registerBundle);
#endif
}

void DriveController::finishNative(bool success, const QByteArray &output)
{
    m_timeout.stop();
    const auto action = std::exchange(m_action, {});
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(output, &parseError);
    const auto result = document.object();
    const auto mountPath = result.value("systemPath").toString();
    bool providerVerified = true;
#ifdef Q_OS_ANDROID
    if (action != QStringLiteral("default"))
        providerVerified = result.value("enabled").toBool()
            && result.value("connectionKind").toString() == QStringLiteral("documentsProvider")
            && QUrl(mountPath).scheme() == QStringLiteral("content")
            && QUrl(mountPath).authority() == QStringLiteral("com.iisacc.society.documents")
            && result.value("identifier").toString() == identifier();
#endif
    if (!success || parseError.error != QJsonParseError::NoError || result.contains("error") || !providerVerified) {
        m_systemPath.clear();
        m_systemStatus = result.value("error").toString(tr("The %1 connection failed.").arg(systemName()));
        fail(m_systemStatus);
    } else if (action == QStringLiteral("default")) {
        if (openContainer(result.value("sourcePath").toString()))
            connectToSystem();
    } else {
        if (result.contains("systemPath"))
            m_systemPath = result.value("systemPath").toString();
        m_systemStatus = result.contains("enabled") && !result.value("enabled").toBool()
            ? tr("Open %1 and enable Society in Locations.").arg(systemName())
            : tr("Connected to %1").arg(systemName());
    }
    emit systemChanged();
    if (m_nativeRebind && !m_mirrorPending && hasDrive()) {
        m_nativeRebind = false;
        QTimer::singleShot(0, this, &DriveController::connectToSystem);
    }
}
#endif

#include "NetworkDriveController.h"
#include "PairingCredentials.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QGuiApplication>
#include <QSslKey>
#include <QSysInfo>
#include <QUuid>
#include <utility>

namespace {
constexpr NetworkDriveController::Mode nativeDeviceMode() {
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID) || defined(Q_OS_WASM) || defined(SOCIETY_CLIENT_ONLY)
    return NetworkDriveController::ClientMode;
#else
    return NetworkDriveController::HostMode;
#endif
}
}

NetworkDriveController::NetworkDriveController(QObject *parent)
    : NetworkDriveController(nullptr, QHostAddress::AnyIPv4, parent) {}
NetworkDriveController::NetworkDriveController(DiscoveryService *service, QHostAddress bindAddress, QObject *parent)
    : NetworkDriveController(nativeDeviceMode(), service, bindAddress, parent) {}
NetworkDriveController::NetworkDriveController(Mode deviceMode, DiscoveryService *service, QHostAddress bindAddress, QObject *parent)
    : QObject(parent), m_remote([this](const auto &peer, const auto &payload) { return request(peer, payload); }),
      m_sync([this](const auto &peer, const auto &payload) { return request(peer, payload); }),
      m_photos(new society::photos::PhotoController([this](const auto &peer, const auto &payload) { return request(peer, payload); }, this)), m_nearby(service, bindAddress),
      m_automatic(&m_nearby, &m_local, [this] { return startLocalHost(true); },
          [this](const QString &link) { return joinLocalHost(link, true); }, [this] { stopTransport(); }),
      m_localBindAddress(bindAddress), m_mode(deviceMode) {
    m_local.setPairingAuthentication(
        [this](const QByteArray &context) { return pairingAuthenticated() ? SocietyPairingCredentials::sign(m_account->pairingCredentials(), context) : QJsonObject{}; },
        [this](const QByteArray &context, const QJsonObject &proof) { return pairingAuthenticated() && SocietyPairingCredentials::verify(m_account->pairingCredentials(), context, proof); });
    connect(m_photos, &society::photos::PhotoController::progress, &m_background, &MobileSyncActivity::update);
    connect(&m_sync, &iiSocietySync::Controller::verificationProgress, this,
        [this](const QString &path, qint64 done, qint64 total) {
            m_background.update("verification/" + path, done, total);
        });
    connect(m_photos, &society::photos::PhotoController::contentsChanged, &m_sync, &iiSocietySync::Controller::synchronizeNow);
    connect(m_photos, &society::photos::PhotoController::changed, this, [this] {
        updateBackgroundActivity();
        // The photo catalog emits contentsChanged and starts its remote cycle
        // after changed. Let those operations start before finishing the batch.
        QTimer::singleShot(0, this, &NetworkDriveController::finishBackgroundActivityIfIdle);
    });
    connect(&m_remote, &iiSocietySync::RemoteFiles::stateChanged, this, [this] {
        if (!m_remote.status().isEmpty()) m_status = m_remote.status();
        emit stateChanged();
    });
    connect(&m_remote, &iiSocietySync::RemoteFiles::entriesChanged, this, &NetworkDriveController::entriesChanged);
    connect(&m_remote, &iiSocietySync::RemoteFiles::downloadFinished, this, &NetworkDriveController::downloadFinished);
    connect(&m_sync, &iiSocietySync::Controller::changed, this, [this] {
        if (!m_sync.errorString().isEmpty()) invalidateHostConnection();
        if (!m_sync.busy() && !m_photos->busy() && !m_sync.errorString().isEmpty()) {
            m_continuedSyncRequested = false; m_background.release(false);
            if (!hostModeAvailable() && (m_applicationState == Qt::ApplicationSuspended || m_applicationState == Qt::ApplicationHidden))
                suspendForBackground();
        }
        updateBackgroundActivity();
        emit synchronizationChanged();
    });
    connect(&m_sync, &iiSocietySync::Controller::namespaceChanged, this, [this](const QJsonObject &state) {
        if (m_namespace == state) return;
        m_namespace = state; emit synchronizationChanged();
    });
    connect(&m_sync, &iiSocietySync::Controller::synchronized, this, [this](const QString &peer) {
        // A cached mirror or an open relay socket does not prove that its host
        // is usable. Only a completed, authenticated round in this connection
        // can release the mobile startup/reconnection gate.
        if (!hostModeAvailable() && signedIn() && connected() && m_syncHosts.contains(peer))
            m_validatedHost = peer;
        m_photos->refresh();
        if (!hostModeAvailable()) refreshContainerState();
        QTimer::singleShot(0, this, &NetworkDriveController::finishBackgroundActivityIfIdle);
        m_syncPath.clear(); emit synchronizationChanged(); emit containerSynchronized(peer);
        // Only the common idle check may finish the grant. A peer completion
        // can enqueue another file pass or the next photo transfer.
    });
    connect(&m_sync, &iiSocietySync::Controller::progress, this, [this](const QString &path, qint64 done, qint64 total) {
        m_syncPath = path; m_syncDone = done; m_syncTotal = total;
        m_background.update(path, done, total);
        emit synchronizationChanged(); emit synchronizationProgress(path, done, total);
    });
    connect(&m_sync, &iiSocietySync::Controller::mirrorChanged, this, [this](const QJsonObject &binding) {
        if (m_mirror == binding) return;
        m_mirror = binding;
        if (!hostModeAvailable()) { m_containerStateKnown = false; refreshContainerState(); }
        emit mirrorChanged(); emit synchronizationChanged();
        QTimer::singleShot(0, this, &NetworkDriveController::updateDiscovery);
    });
    connect(&m_sync, &iiSocietySync::Controller::containerInspected, this,
        [this](const QString &path, const QString &scope, const QJsonObject &binding, const QString &identifier, const QString &primary) {
            if (hostModeAvailable() || path != m_container || scope != m_inspectionScope) return;
            const bool mirrorUpdated = m_mirror != binding;
            const bool changed = !m_containerStateKnown || mirrorUpdated || m_containerIdentifier != identifier || m_primaryHost != primary;
            m_mirror = binding; m_containerIdentifier = identifier; m_primaryHost = primary; m_containerStateKnown = true;
            if (mirrorUpdated) emit mirrorChanged();
            if (changed) emit synchronizationChanged();
            updateDiscovery();
        });
    connect(&m_automatic, &AutomaticPairing::changed, this, &NetworkDriveController::discoveryChanged);
    connect(&m_automatic, &AutomaticPairing::changed, this, &NetworkDriveController::updateDiscovery, Qt::QueuedConnection);
    connect(&m_nearby, &NearbyDevices::changed, this, &NetworkDriveController::discoveryChanged);
    connect(&m_nearby, &NearbyDevices::identityEnding, &m_local, &iiServerHost::LanPeer::stop);
    connect(&m_nearby, &NearbyDevices::authenticationEnding, &m_local, &iiServerHost::LanPeer::stop);
    connect(&m_local, &iiServerHost::LanPeer::paired, &m_nearby, &NearbyDevices::complete);
    connect(&m_local, &iiServerHost::LanPeer::paired, this, [this](const QString &id) {
        if (!pairingAuthenticated() || (!m_local.hosting() && id != accountHost())) { m_local.stop(); return; }
        m_verifiedSyncPeers.insert(id);
        if (m_account && signedIn()) {
            QString name = m_local.peerName(), kind;
            for (const auto &value : m_nearby.devices()) {
                const auto row = value.toMap();
                if (row.value("id").toString() == id) { name = row.value("name").toString(); kind = row.value("kind").toString(); break; }
            }
            m_account->rememberPairedDevice(id, name, kind);
        }
        if (m_automatic.enabled() && !m_local.hosting()) browse(id);
        updateDiscovery();
    });
    m_discoveryTimer.setInterval(5000);
    connect(&m_discoveryTimer, &QTimer::timeout, this, [this] {
        if (!hostModeAvailable()) refreshContainerState();
        updateDiscovery();
    });
    m_status = tr("Connect on your local network or through your own Society server.");
    connect(&m_local, &iiServerHost::LanPeer::changed, this, [this] {
        emit discoveryChanged();
        emit synchronizationChanged();
        if (m_localActive) {
            if (!m_local.errorString().isEmpty()) m_status = m_local.errorString();
            else if (m_local.hosting()) m_status = tr("Hosting Files on the local network.");
            else if (m_local.connected()) m_status = tr("Connected directly over the local network.");
            else m_status = tr("Connecting to the desktop on the local network…");
            if (!m_local.connected() && !m_local.hosting() && m_local.phase() == "error") {
                m_remote.reset();
            }
            emit stateChanged(); emit hostsChanged();
        }
        updateSynchronization();
    });
    connect(&m_local, &iiServerHost::LanPeer::completed, this, &NetworkDriveController::response);
    connect(&m_peer, &iiServerHost::Peer::stateChanged, this, [this] {
        if (m_peer.isReady() && (!signedIn() || m_peer.accountId() != m_account->manager()->account()->sub())) {
            m_peer.stop(); fail(tr("The server verified a different account. Check its account authority."));
            emit discoveryChanged(); return;
        }
        if (!m_peer.isReady()) m_verifiedSyncPeers.clear();
        if (m_peer.isReady()) m_status = hosting() ? tr("Hosting Files for your account.") : tr("Connected to your devices.");
        else if (!m_peer.errorString().isEmpty()) m_status = m_peer.errorString();
        updateSynchronization();
        emit stateChanged();
        emit discoveryChanged();
    });
    connect(&m_peer, &iiServerHost::Peer::peersChanged, this, &NetworkDriveController::hostsChanged);
    connect(&m_peer, &iiServerHost::Peer::peersChanged, this, &NetworkDriveController::updateSynchronization);
    connect(&m_peer, &iiServerHost::Peer::completed, this, &NetworkDriveController::response);
    if (!hostModeAvailable()) {
        connect(&m_background, &MobileSyncActivity::expired, this, [this] {
            m_backgroundExpired = true;
            m_continuedSyncRequested = false;
            if (m_applicationState == Qt::ApplicationSuspended || m_applicationState == Qt::ApplicationHidden)
                suspendForBackground();
        });
        connect(this, &NetworkDriveController::stateChanged, this, &NetworkDriveController::updateBackgroundActivity);
        connect(this, &NetworkDriveController::discoveryChanged, this, &NetworkDriveController::updateBackgroundActivity);
    }
    if (auto *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        connect(app, &QGuiApplication::applicationStateChanged, this, &NetworkDriveController::setApplicationState);
        setApplicationState(app->applicationState());
    }
}
NetworkDriveController::~NetworkDriveController() {
    m_background.release();
    m_discoveryTimer.stop(); m_nearby.clear(); disconnectSessionImpl(false);
    if (!hostModeAvailable()) m_sync.shutdownAsync();
    // Members emit their final stop signals before QObject clears external
    // QPointers. No UI may inspect the already-destroyed discovery member then.
    m_local.disconnect(); m_peer.disconnect(); m_nearby.disconnect();
    m_sync.disconnect(); m_remote.disconnect();
}
void NetworkDriveController::setApplicationState(Qt::ApplicationState state) {
    m_photos->setApplicationActive(state == Qt::ApplicationActive);
    if (hostModeAvailable()) return;
    m_applicationState = state;
    if (state == Qt::ApplicationSuspended || state == Qt::ApplicationHidden) {
        if (m_background.active() && !m_suspended) m_sync.synchronizeNow();
        else suspendForBackground();
    } else if (state == Qt::ApplicationActive) {
        refreshContainerState();
        m_backgroundExpired = false;
        m_continuedSyncRequested = true;
        if (m_suspended) {
            m_suspended = false; updateDiscovery(); requestAccountPairingCredentials(); restartSession();
        }
        updateBackgroundActivity(); m_sync.synchronizeNow(); m_photos->refresh();
    }
}
void NetworkDriveController::updateBackgroundActivity() {
    if (hostModeAvailable()) return;
    if (!m_runtimeEnabled) { m_continuedSyncRequested = false; m_background.release(); return; }
    const auto photoAccess = m_photos->access();
    const bool photoWork = m_photos->active() && m_photos->busy() && (photoAccess == "full" || photoAccess == "limited");
    const bool syncWork = signedIn() && connected() && m_sync.available();
    if (m_continuedSyncRequested && m_applicationState == Qt::ApplicationActive && !m_backgroundExpired
        && (syncWork || photoWork)) {
        m_background.retainContinued(); return;
    }
    if (m_applicationState == Qt::ApplicationActive && !connected() && !m_automatic.active() && !photoWork) {
        m_background.release(); return;
    }
    if (m_applicationState == Qt::ApplicationActive && !m_backgroundExpired && signedIn()) m_background.retain();
}
void NetworkDriveController::finishBackgroundActivityIfIdle() {
    if (hostModeAvailable() || !m_background.batchActive() || m_suspended || m_sync.busy() || m_photos->busy()
        || !m_sync.errorString().isEmpty()
        || (m_backgroundExpired && m_applicationState != Qt::ApplicationActive)) return;
    m_continuedSyncRequested = false;
    m_background.release(true);
    if (m_applicationState == Qt::ApplicationSuspended || m_applicationState == Qt::ApplicationHidden)
        suspendForBackground();
}
void NetworkDriveController::synchronizeNow() {
#if defined(Q_OS_IOS)
    societyRestartSyncPresentation();
#endif
    if (!hostModeAvailable() && m_applicationState == Qt::ApplicationActive && m_runtimeEnabled && signedIn()) {
        m_continuedSyncRequested = true; m_backgroundExpired = false;
        if (m_suspended) { m_suspended = false; updateDiscovery(); restartSession(); }
        updateBackgroundActivity();
    }
    m_sync.synchronizeNow(); m_photos->refresh();
}
void NetworkDriveController::suspendForBackground() {
    if (m_suspended) { m_background.release(); return; }
    m_suspended = true; m_nearby.clear(); stopTransport();
    // stopTransport cancels both workers. Drain before iOS can suspend us with
    // PhotoStore's operation.lock (or a replica lock) still held.
    m_photos->waitForDone(); m_sync.closeAndWait();
    m_clientLease.reset();
    m_background.release();
    m_status = tr("Sync will resume when Society becomes active."); emit stateChanged();
}
void NetworkDriveController::setAccountSession(AccountController *account) {
    if (account == m_account || (account && account->thread() != thread())) return;
    disconnectSessionImpl(false);
    if (m_account) {
        disconnect(m_account, nullptr, this, nullptr);
        disconnect(m_account->manager(), nullptr, this, nullptr);
        disconnect(m_account->manager()->account(), nullptr, this, nullptr);
    }
    m_account = account;
    if (m_applicationState == Qt::ApplicationActive) m_continuedSyncRequested = true;
    m_relayUrl = QUrl(); m_serverEnabled = false; emit configurationChanged();
    m_discoverySession.clear(); m_automatic.setEnabled(true);
    m_nearby.clear(); m_discoveryTimer.stop();
    if (account) {
        connect(account->manager()->account(), &iisacc::accounts::Account::changed, this, &NetworkDriveController::updateDiscovery);
        connect(account, &AccountController::changed, this, [this] {
            if (!signedIn()) {
                disconnectSessionImpl(false); m_relayUrl = QUrl(); emit configurationChanged();
            }
            updateDiscovery();
            emit authChanged();
        });
        connect(account, &AccountController::sessionEnding, this, [this] { m_nearby.clear(); disconnectSessionImpl(false); });
        connect(account, &AccountController::pairingStateRestored, this, [this] {
            restoreServerConfiguration();
            m_automatic.setEnabled(m_relayUrl.isEmpty() && m_account->automaticPairingEnabled()); updateDiscovery();
            requestAccountPairingCredentials();
        });
        connect(account, &AccountController::pairingCredentialsRequired, this, &NetworkDriveController::requestAccountPairingCredentials);
        connect(account, &AccountController::pairingCredentialsChanged, this, &NetworkDriveController::updateDiscovery);
        connect(account, &QObject::destroyed, this, [this] {
            m_account = nullptr; m_nearby.clear(); m_discoveryTimer.stop(); disconnectSessionImpl(false);
            m_relayUrl = QUrl(); emit configurationChanged(); emit authChanged();
        });
        m_discoveryTimer.start();
    }
    restoreServerConfiguration();
    updateDiscovery();
    requestAccountPairingCredentials();
    emit authChanged();
}
void NetworkDriveController::requestAccountPairingCredentials() {
#ifdef SOCIETY_DISABLE_SESSION_RESTORE
    if (qEnvironmentVariableIntValue("SOCIETY_TEST_ACCOUNT_DISCOVERY") != 1) return;
#else
    if (qEnvironmentVariableIntValue("SOCIETY_DISABLE_SESSION_RESTORE") == 1) return;
#endif
    if (m_account && !m_suspended && m_runtimeEnabled) m_account->requestPairingCredentials();
}
QVariantList NetworkDriveController::nearbyDevices() const {
    auto result = m_nearby.devices(); const auto connected = m_local.pairedDeviceIds();
    for (auto &value : result) {
        auto row = value.toMap(); row.insert("connected", connected.contains(row.value("id").toString()));
        for (const auto &entry : m_automatic.queue()) if (entry.toMap().value("id") == row.value("id")) row.insert("pairingState", entry.toMap().value("state"));
        value = row;
    }
    return result;
}
void NetworkDriveController::updateDiscovery() {
    const auto registeredHost = accountHost(), registeredContainer = accountContainer(), registeredScope = accountSyncScope();
    if (m_registeredHost != registeredHost || m_registeredContainer != registeredContainer || m_registeredScope != registeredScope) {
        m_registeredHost = registeredHost; m_registeredContainer = registeredContainer; m_registeredScope = registeredScope;
        stopTransport(); // Cancel queued transfers before applying another account authority.
    }
    if (m_clientLease && (m_suspended || !m_runtimeEnabled || !signedIn())) {
        m_sync.closeAndWait(); m_clientLease.reset();
    }
    if (!hostModeAvailable() && !m_suspended && m_runtimeEnabled && signedIn() && !m_container.isEmpty()) {
        // A consumer app can run the same Society SDK while this UI is asleep.
        // Only one process owns the device's authenticated replication session.
        const auto storage = iiSocietyContainer::SharedStorage::open(m_container, nullptr, true);
        if (storage && QDir(storage->drive().rootPath()).exists(".society-sync")) {
            if (!m_clientLease) {
                m_clientLease = std::make_unique<QLockFile>(QDir(storage->drive().rootPath()).filePath(".society-sync/client-session.lock"));
                m_clientLease->setStaleLockTime(0);
            }
            if (!m_clientLease->isLocked() && !m_clientLease->tryLock()) {
                m_nearby.clear(); m_sync.close();
                m_status = tr("Society storage is active in another app on this device."); emit stateChanged(); return;
            }
        }
    }
    if (!m_relayUrl.isEmpty()) {
        m_automatic.setEnabled(false);
        if (m_nearby.active()) m_nearby.clear();
        updateServerSession(); updateSynchronization(); return;
    }
#ifdef SOCIETY_DISABLE_SESSION_RESTORE
    if (qEnvironmentVariableIntValue("SOCIETY_TEST_ACCOUNT_DISCOVERY") != 1) return;
#else
    if (qEnvironmentVariableIntValue("SOCIETY_DISABLE_SESSION_RESTORE") == 1) return;
#endif
    if (!m_runtimeEnabled || m_suspended || !signedIn()) { m_nearby.clear(); updateSynchronization(); return; }
    const auto session = m_account->manager()->loginSession().value("id").toString();
    if (session != m_discoverySession) { m_discoverySession = session; m_automatic.setEnabled(m_account->automaticPairingEnabled()); }
    const auto credentials = m_account->pairingCredentials();
    const auto device = m_account->manager()->deviceInfo();
    const auto origin = m_account->manager()->serviceUrl().adjusted(QUrl::RemovePath | QUrl::RemoveQuery | QUrl::RemoveFragment | QUrl::RemoveUserInfo).toString();
    const auto scope = credentials.isEmpty() ? NearbyDevices::accountScope(origin, m_account->userId()) : credentials.value("scope").toString();
    const auto name = device.value("name").toString().isEmpty() ? QSysInfo::machineHostName() : device.value("name").toString();
    m_nearby.setIdentity(scope, device.value("id").toString(), name, device.value("type").toString(), hostModeAvailable());
    if (!hostModeAvailable()) {
        if (m_inspectionScope != scope) {
            m_inspectionScope = scope; m_containerStateKnown = false; refreshContainerState();
        }
        // Keep automatic host selection closed until the local binding is known.
        if (!m_containerStateKnown) { m_nearby.setCredentials({}, false, {}); updateSynchronization(); return; }
    }
    const auto primary = accountHost();
    if (primary.isEmpty() || accountContainer().isEmpty()) { m_nearby.setCredentials({}, false, {}); updateSynchronization(); return; }
    m_nearby.setCredentials(credentials, hostModeAvailable() && primary == device.value("id").toString()
        && accountContainer() == ([this] { const auto drive = iiSocietyContainer::SocietyDrive::open(m_container); return drive ? drive->identifier() : QString(); })()
        && m_mirror.isEmpty() && !m_container.isEmpty() && m_automatic.enabled(), primary);
    updateSynchronization();
}
void NetworkDriveController::updateSynchronization() {
    if (m_localActive && (m_local.connected() || m_local.hosting())
        && (!pairingAuthenticated() || accountHost().isEmpty()
            || (m_local.hosting() ? accountHost() != m_account->manager()->deviceInfo().value("id").toString()
                                 : !m_local.pairedDeviceIds().contains(accountHost())))) { m_local.stop(); }
    if (!m_runtimeEnabled || m_suspended || !signedIn() || !connected()
        || (m_localActive && !m_nearby.authenticated())) {
        m_syncHosts.clear(); invalidateHostConnection();
    }
    if (m_clientLease && !m_clientLease->isLocked()) {
        m_syncHosts.clear(); invalidateHostConnection();
        m_sync.close(); m_photos->configure(m_container, false); return;
    }
    const bool photosActive = m_runtimeEnabled && !m_suspended && signedIn() && containerReady();
    if (!m_runtimeEnabled || m_suspended || !signedIn()) {
        if (!m_namespace.isEmpty()) { m_namespace = {}; emit synchronizationChanged(); }
        m_photos->configure(m_container, photosActive); m_sync.close(); return;
    }
    if (!connected() || (m_localActive && !m_nearby.authenticated())) {
        m_photos->configure(m_container, photosActive);
        if (m_mirror.isEmpty() && (!hostModeAvailable() || (!m_relayUrl.isEmpty() && m_mode == ClientMode))) {
            m_sync.close(); return;
        }
        // Network availability affects replication, never the host's authority.
        // Stable account identity survives expiry of short-lived LAN proofs;
        // this local scope grants no transport authentication or peer access.
        const auto scope = accountSyncScope();
        if (scope.isEmpty()) { m_sync.close(); return; }
        m_sync.open(m_container, scope); m_sync.setPeers({}, {}); return;
    }
    const auto scope = m_localActive ? m_account->pairingCredentials().value("scope").toString() : accountSyncScope();
    QStringList authorized, hosts;
    if (m_localActive) {
        for (const auto &id : m_local.pairedDeviceIds()) {
            if (!m_verifiedSyncPeers.contains(id)) continue;
            authorized.append(id); if (!m_local.hosting()) hosts.append(id);
        }
    } else if (m_peer.accountId() == m_account->manager()->account()->sub()) {
        authorized = m_verifiedSyncPeers.values();
        for (const auto &value : m_peer.peers()) {
            const auto id = value.toObject().value("peerId").toString(); authorized.append(id);
            if (m_mode == ClientMode || !m_mirror.isEmpty()) hosts.append(id);
        }
    }
    if (!hosting()) {
        const auto expected = accountHost();
        hosts = !expected.isEmpty() && !accountContainer().isEmpty() && hosts.contains(expected) ? QStringList{expected} : QStringList{};
        authorized = hosts;
    }
    m_syncHosts = hosts;
    if (!hosts.contains(m_validatedHost)) invalidateHostConnection();
    m_photos->configure(m_container, photosActive, authorized, hosts);
    m_sync.open(m_container, scope);
    if (hosting() && (m_localActive ? !authorized.isEmpty() : m_accountSession))
        m_sync.claimPrimaryHost(m_localActive ? m_nearby.deviceId() : m_session.peerId);
    m_sync.setPeers(authorized, hosts, hosting() ? QString() : accountHost(), hosting() ? QString() : accountContainer());
}
void NetworkDriveController::refreshContainerState() {
    if (hostModeAvailable()) return;
    m_sync.inspectContainer(m_container, m_inspectionScope);
}
bool NetworkDriveController::containerReady() const {
    if (!hostModeAvailable())
        return signedIn() && m_mirror.value("scope").toString() == accountSyncScope()
            && m_mirror.value("host").toString() == accountHost()
            && m_mirror.value("container").toString() == accountContainer()
            && m_containerStateKnown && !m_containerIdentifier.isEmpty() && m_mirror.value("complete").toBool()
            && m_mirror.value("container").toString() == m_containerIdentifier;
    if (!m_mirror.isEmpty()) {
        const auto drive = iiSocietyContainer::SocietyDrive::open(m_container);
        return drive && m_mirror.value("complete").toBool() && m_mirror.value("container") == drive->identifier();
    }
    if (!m_relayUrl.isEmpty() && m_mode == ClientMode) return false;
    return hostModeAvailable() && !(m_localActive && !m_local.hosting() && m_local.connected());
}
void NetworkDriveController::invalidateHostConnection() {
    if (m_validatedHost.isEmpty()) return;
    m_validatedHost.clear();
    emit synchronizationChanged();
}
bool NetworkDriveController::hostConnectionReady() const {
    return hostModeAvailable() || (m_runtimeEnabled && !m_suspended && signedIn() && connected()
        && containerReady() && !m_validatedHost.isEmpty() && m_syncHosts.contains(m_validatedHost)
        && m_validatedHost == m_mirror.value("host").toString() && m_sync.errorString().isEmpty());
}
QString NetworkDriveController::synchronizationStatus() const {
    if (!signedIn()) return tr("Sign in to connect to your account's Society host.");
    if (accountHost().isEmpty() || accountContainer().isEmpty()) return tr("Register a Society host drive with your account before syncing.");
    const auto error = m_sync.errorString();
    if (error == "container_account_binding_mismatch") return tr("This container is linked to another account.");
    if (error == "insufficient_storage") return tr("Not enough free space to continue syncing.");
    if (error == "filename_normalization_collision") return tr("Rename files whose names differ only by case or Unicode form to continue syncing.");
    if (error == "native_sync_filesystem_unsupported_platform") return tr("Container sync is unavailable on this platform.");
    if (error == "namespace_authority_protocol_required") return tr("Update Society on the host to synchronize this namespace.");
    if (!error.isEmpty()) return tr("Container sync is waiting to retry.");
    if (m_sync.busy() && m_syncTotal > 0 && !m_syncPath.isEmpty())
        return tr("Syncing %1 (%2%)…").arg(m_syncPath.section('/', -1)).arg(m_syncDone * 100 / m_syncTotal);
    if (!containerReady()) return connected() ? tr("Preparing the host's Society drive on this device…")
        : tr("Connect to your desktop to mirror your Society drive.");
    if (m_sync.busy()) return tr("Syncing your container…");
    if (m_sync.available() && !connected()) return m_mirror.isEmpty()
        ? tr("Local changes are saved on this host. Replication resumes when devices connect.")
        : tr("Offline changes are waiting for the primary host.");
    if (m_sync.available()) return tr("Container sync is active.");
    return tr("Waiting for an account-verified device to sync.");
}
QString NetworkDriveController::accountHost() const {
    return signedIn() ? m_account->manager()->account()->societyContainerDrive().value("hostDeviceId").toString() : QString();
}
QString NetworkDriveController::accountContainer() const {
    return signedIn() ? m_account->manager()->account()->societyContainerDrive().value("containerId").toString() : QString();
}
bool NetworkDriveController::pairingAuthenticated() const {
    return signedIn() && m_account->pairingCredentials().value("scope").toString() == accountSyncScope()
        && SocietyPairingCredentials::valid(m_account->pairingCredentials());
}
QString NetworkDriveController::accountSyncScope() const {
    return m_account ? m_account->storageScope() : QString();
}
QString NetworkDriveController::automaticPairingStatus() const {
    if (m_relayUrl.isEmpty()) return m_automatic.status();
    if (!m_serverEnabled) return tr("Automatic server connection is paused.");
    if (!signedIn()) return tr("Sign in to connect to your Society server.");
    if (m_mode == ClientMode && m_peer.peers().size() > 1 && m_mirror.isEmpty())
        return tr("More than one host is available. Keep one primary host online for the first synchronization.");
    return m_status;
}
bool NetworkDriveController::hostModeAvailable() const {
    return m_mode == HostMode;
}
void NetworkDriveController::setRuntimeEnabled(bool enabled) {
    if (m_runtimeEnabled == enabled) return;
    m_runtimeEnabled = enabled;
    if (!enabled) {
        m_photos->configure(m_container, false);
        m_continuedSyncRequested = false; m_background.release(false);
        m_nearby.clear(); stopTransport();
        if (hostModeAvailable()) m_sync.closeAndWait();
        else { m_sync.closeAndWait(); m_clientLease.reset(); }
    } else {
        if (m_applicationState == Qt::ApplicationActive) m_continuedSyncRequested = true;
        m_automatic.setEnabled(m_relayUrl.isEmpty() && (m_account ? m_account->automaticPairingEnabled() : true));
        updateDiscovery(); requestAccountPairingCredentials(); restartSession();
    }
}
void NetworkDriveController::setContainerPath(const QString &path) {
    if (m_container != path) m_namespace = {};
    if (path == m_container) return;
#if !defined(Q_OS_IOS) && !defined(Q_OS_ANDROID) && !defined(Q_OS_WASM)
    m_generationHost.close();
#endif
    if (m_clientLease) { m_sync.closeAndWait(); m_clientLease.reset(); }
    const bool automatic = m_automatic.enabled();
    m_container = path;
    m_syncHosts.clear(); invalidateHostConnection();
    if (hostModeAvailable()) m_mirror = iiSocietySync::Replica::binding(path);
    else {
        m_mirror = {}; m_containerIdentifier.clear(); m_primaryHost.clear();
        m_containerStateKnown = false; refreshContainerState();
    }
    if (m_mode == HostMode || m_local.hosting()) {
        if (m_localActive) disconnectSessionImpl(false); else restartSession();
    }
    m_automatic.setEnabled(automatic);
    emit configurationChanged();
    emit synchronizationChanged();
    updateDiscovery();
}
void NetworkDriveController::setRelayUrl(const QUrl &url) {
    if (url == m_relayUrl) return;
    if (!url.isEmpty() && !validServerUrl(url)) { fail(tr("Use a secure wss:// Society server address.")); return; }
    // A changed destination must not receive the previous session implicitly.
    disconnectSession(); m_relayUrl = url; emit configurationChanged();
}
bool NetworkDriveController::validServerUrl(const QUrl &url) {
    return url.isValid() && !url.host().isEmpty() && url.port() != 0 && url.toEncoded().size() <= 2048
        && url.userInfo().isEmpty() && !url.hasQuery() && !url.hasFragment()
        && (url.scheme() == "wss" || (url.scheme() == "ws" && QHostAddress(url.host()).isLoopback()));
}
bool NetworkDriveController::configureServer(const QUrl &url) {
    if (!url.isEmpty() && !validServerUrl(url)) { fail(tr("Use a secure wss:// Society server address without credentials or a query.")); return false; }
    if (!signedIn()) { fail(tr("Sign in before configuring your Society server.")); return false; }
    const QJsonObject configuration = url.isEmpty() ? QJsonObject{} : QJsonObject{{"url", url.toString()}};
    if (!m_account->setServerConfiguration(configuration)) { fail(tr("Account settings are still loading. Try again shortly.")); return false; }
    m_automatic.setEnabled(false); m_nearby.clear();
    m_session = {}; m_accountSession = false; stopTransport();
    m_relayUrl = url; m_serverEnabled = !url.isEmpty();
    m_account->setAutomaticPairingEnabled(true);
    m_automatic.setEnabled(url.isEmpty());
    emit configurationChanged(); emit discoveryChanged();
    updateDiscovery(); return true;
}
void NetworkDriveController::restoreServerConfiguration() {
    if (!signedIn()) return;
    const auto configuration = m_account->serverConfiguration();
    const QUrl url(configuration.value("url").toString());
    if (!configuration.isEmpty() && !validServerUrl(url)) { fail(tr("The saved Society server address is invalid.")); return; }
    m_relayUrl = url;
    m_serverEnabled = !url.isEmpty() && m_account->automaticPairingEnabled();
    if (!url.isEmpty()) {
        // Older versions persisted a role choice. Keep the address and remove
        // that obsolete choice; this build's platform is the role authority.
        if (configuration.contains("host")) {
            auto migrated = configuration; migrated.remove("host");
            m_account->setServerConfiguration(migrated);
        }
        m_automatic.setEnabled(false); m_nearby.clear();
    }
    emit configurationChanged(); emit discoveryChanged();
}
void NetworkDriveController::updateServerSession() {
    if (!m_serverEnabled || !m_runtimeEnabled || m_suspended || !signedIn() || m_account->busy() || !validServerUrl(m_relayUrl)) return;
    const auto credential = m_account->relayCredential();
    if (credential.isEmpty()) { fail(tr("Refresh your account session to connect to your Society server.")); return; }
    const auto device = m_account->manager()->deviceInfo();
    const auto id = device.value("id").toString();
    if (m_accountSession && m_session.relayUrl == m_relayUrl && m_session.credential == credential && m_session.peerId == id) return;
    iiServerHost::PeerOptions options;
    options.relayUrl = m_relayUrl; options.credential = credential; options.peerId = id;
    options.name = device.value("name").toString().left(128);
    if (options.name.isEmpty()) options.name = QSysInfo::machineHostName().left(128);
    startSessionImpl(options, true);
}
void NetworkDriveController::connectSession() {
    if (!m_relayUrl.isEmpty()) configureServer(m_relayUrl);
    else if (hostModeAvailable() && m_mode == HostMode) startLocalHost();
    else fail(tr("Select this device on your desktop or scan its QR code to connect."));
}
bool NetworkDriveController::startLocalHost(bool automatic) {
    const auto drive = iiSocietyContainer::SocietyDrive::open(m_container);
    if (!pairingAuthenticated() || accountHost() != m_account->manager()->deviceInfo().value("id").toString()
        || !drive || accountContainer() != drive->identifier()) { fail(tr("Sign in and register this host drive with your account before pairing.")); return false; }
    if (!m_runtimeEnabled || m_suspended) return false;
    if (!automatic) pauseAutomaticPairing();
    if (!hostModeAvailable()) { fail(tr("Only desktop Society can host Files.")); return false; }
    if (!m_mirror.isEmpty()) { fail(tr("This device mirrors its primary Society host.")); return false; }
    if (m_local.hosting()) return true;
    if (automatic) { m_session = {}; m_accountSession = false; stopTransport(); }
    else disconnectSession();
    QString error; m_storage = iiSocietyContainer::SharedStorage::open(m_container, &error);
    if (!m_storage) { fail(error.isEmpty() ? tr("Open a Society container before pairing.") : error); return false; }
    const auto files = iiSocietySync::filesHandler(m_storage->drive().rootPath());
    m_localActive = true;
    const auto id = m_nearby.active() ? m_nearby.deviceId() : m_account ? m_account->manager()->deviceInfo().value("id").toString() : QUuid::createUuid().toString(QUuid::WithoutBraces);
    return m_local.startHost(id, m_nearby.active() ? m_nearby.deviceName() : QSysInfo::machineHostName(),
        [this, files](const auto &peer, const auto &payload) {
            if (!pairingAuthenticated() || accountHost() != m_account->manager()->deviceInfo().value("id").toString()) return QJsonObject{{"ok", false}, {"error", "account_host_mismatch"}};
            if (payload.value("op") == "society.generation") return handleGeneration(peer, payload);
            if (payload.value("op") == "society.sync") return m_sync.handle(peer, payload);
            if (payload.value("op") == "society.photos") return m_photos->handle(peer, payload);
            return files(peer, payload);
        },
        m_localBindAddress.isLoopback() ? QStringList{m_localBindAddress.toString()} : QStringList{}, m_localBindAddress);
}
bool NetworkDriveController::joinLocalHost(const QString &qr, bool automatic) {
    iiServerHost::LanLink link;
    if (!pairingAuthenticated() || accountHost().isEmpty() || accountContainer().isEmpty()
        || !iiServerHost::LanLink::decode(qr, &link) || link.hostId != accountHost()) {
        stopTransport(); fail(tr("This QR code is not the host registered to your signed-in account.")); return false;
    }
    if (!m_runtimeEnabled || m_suspended) return false;
    if (automatic) { m_session = {}; m_accountSession = false; stopTransport(); }
    else disconnectSession();
    // Joining a primary host changes the transport, never the device role.
    m_localActive = true;
    const auto id = m_nearby.active() ? m_nearby.deviceId() : m_account ? m_account->manager()->deviceInfo().value("id").toString() : QUuid::createUuid().toString(QUuid::WithoutBraces);
    return m_local.join(qr, id, m_nearby.active() ? m_nearby.deviceName() : QSysInfo::machineHostName());
}
bool NetworkDriveController::startSession(iiServerHost::PeerOptions options) {
    return startSessionImpl(std::move(options), false);
}
bool NetworkDriveController::startSessionImpl(iiServerHost::PeerOptions options, bool accountSession) {
    stopTransport();
    if (!signedIn() || accountHost().isEmpty() || accountContainer().isEmpty()) { fail(tr("Register a Society host drive with your account before connecting.")); return false; }
    // Retain transport settings for reconnects. The fixed platform role
    // overrides caller-supplied hosting flags.
    m_session = options; m_accountSession = accountSession;
    if (m_suspended || !m_runtimeEnabled) { m_status = tr("Device connection paused."); emit stateChanged(); return true; }
    const bool host = hostModeAvailable() && m_mirror.isEmpty();
    if (host) {
        const auto drive = iiSocietyContainer::SocietyDrive::open(m_container);
        if (accountHost() != m_account->manager()->deviceInfo().value("id").toString() || !drive || accountContainer() != drive->identifier()) {
            fail(tr("This drive is not the host registered to your account.")); return false;
        }
        if (m_container.isEmpty()) { fail(tr("Open a Society container to host files.")); return false; }
        QString error;
        m_storage = iiSocietyContainer::SharedStorage::open(m_container, &error);
        if (!m_storage) { fail(error); return false; }
        if (accountSession) {
            const auto certificatePath = qEnvironmentVariable("SOCIETY_HOST_CERTIFICATE");
            const auto keyPath = qEnvironmentVariable("SOCIETY_HOST_KEY");
            options.localHostingEnabled = !certificatePath.isEmpty() || !keyPath.isEmpty();
            if (options.localHostingEnabled) {
                const auto certificates = QSslCertificate::fromPath(certificatePath); QFile file(keyPath);
                if (certificates.isEmpty() || !file.open(QIODevice::ReadOnly)) { fail(tr("The host TLS identity is unavailable.")); return false; }
                const auto pem = file.readAll(); QSslKey key(pem, QSsl::Rsa); if (key.isNull()) key = QSslKey(pem, QSsl::Ec);
                if (key.isNull()) { fail(tr("The host TLS key is invalid.")); return false; }
                options.localTls = QSslConfiguration::defaultConfiguration();
                options.localTls.setLocalCertificateChain(certificates); options.localTls.setPrivateKey(key);
            }
        }
    }
    options.service = "com.iisacc.society.files";
    options.hostFiles = host;
    if (!host) options.localHostingEnabled = false;
    options.metadata = m_storage ? QJsonObject{{"containerId", m_storage->drive().identifier()}, {"section", "files"}} : QJsonObject();
    options.metadata.insert("deviceId", options.peerId);
    const auto files = m_storage ? iiSocietySync::filesHandler(m_storage->drive().rootPath()) : iiServerHost::RequestHandler();
    m_status = tr("Connecting to your devices…"); emit entriesChanged();
    const bool ok = m_peer.start(options, [this, files](const QString &peer, const QJsonObject &request) {
        if (!signedIn() || m_peer.accountId() != m_account->manager()->account()->sub()
            || accountHost() != m_account->manager()->deviceInfo().value("id").toString()
            || !m_storage || accountContainer() != m_storage->drive().identifier())
            return QJsonObject{{"ok", false}, {"error", "account_mismatch"}};
        if (request.value("op") == "society.generation") return handleGeneration(peer, request);
        if (request.value("op") == "society.sync" || request.value("op") == "society.photos") {
            // The relay transport has already authenticated and isolated the
            // caller's principal; client IDs need not appear in the host list.
            if (signedIn() && m_peer.accountId() == m_account->manager()->account()->sub()) {
                m_verifiedSyncPeers.insert(peer); updateSynchronization();
            }
            return request.value("op") == "society.photos" ? m_photos->handle(peer, request) : m_sync.handle(peer, request);
        }
        return files ? files(peer, request) : QJsonObject{{"ok", false}, {"error", "container_unavailable"}};
    });
    if (!ok) fail(m_peer.errorString()); else emit stateChanged();
    return ok;
}
void NetworkDriveController::restartSession() {
    if (m_accountSession) {
        if (!signedIn() || m_session.credential.isEmpty()) return;
        auto options = m_session; options.credential = m_account->relayCredential();
        if (!options.credential.isEmpty()) startSessionImpl(options, true);
    } else if (!m_session.credential.isEmpty()) startSessionImpl(m_session, false);
    else updateServerSession();
}
void NetworkDriveController::stopTransport() {
    m_syncHosts.clear(); invalidateHostConnection();
#if !defined(Q_OS_IOS) && !defined(Q_OS_ANDROID) && !defined(Q_OS_WASM)
    // A reconnect or credential refresh must not discard an accepted job.
    if (!signedIn() || !m_runtimeEnabled || m_mode != HostMode) m_generationHost.close();
#endif
    m_photos->configure(m_container, m_runtimeEnabled && !m_suspended && containerReady());
    emit transportStopped();
    m_sync.close(); m_verifiedSyncPeers.clear(); m_remote.reset(); m_storage.reset();
    m_localActive = false; m_local.stop(); m_peer.stop(); emit entriesChanged(); emit hostsChanged();
}
QJsonObject NetworkDriveController::handleGeneration(const QString &peer, const QJsonObject &request) {
#if !defined(Q_OS_IOS) && !defined(Q_OS_ANDROID) && !defined(Q_OS_WASM)
    const bool authorized = signedIn() && (m_localActive
        ? m_nearby.authenticated() && m_local.pairedDeviceIds().contains(peer) && m_verifiedSyncPeers.contains(peer)
        : m_peer.isReady() && m_peer.accountId() == m_account->manager()->account()->sub());
    if (!authorized || !hosting() || !m_runtimeEnabled || m_suspended || !m_mirror.isEmpty())
        return {{"ok", false}, {"error", "generation_host_not_authorized"}};
    if (!m_generationHost.open(m_container)) return {{"ok", false}, {"error", "generation_container_unavailable"}};
    return m_generationHost.handle(peer, request);
#else
    Q_UNUSED(peer); Q_UNUSED(request);
    return {{"ok", false}, {"error", "generation_host_unavailable_on_mobile"}};
#endif
}
void NetworkDriveController::pauseAutomaticPairing() {
    m_serverEnabled = false;
    m_automatic.setEnabled(false);
    if (m_account && signedIn()) m_account->setAutomaticPairingEnabled(false);
}
void NetworkDriveController::resumeAutomaticPairing() {
    m_serverEnabled = !m_relayUrl.isEmpty();
    m_automatic.setEnabled(m_relayUrl.isEmpty());
    if (m_account && signedIn()) m_account->setAutomaticPairingEnabled(true);
    updateDiscovery();
    if (!m_relayUrl.isEmpty() && !connected()) restartSession();
    emit discoveryChanged();
}
void NetworkDriveController::disconnectSession() { disconnectSessionImpl(true); }
void NetworkDriveController::disconnectSessionImpl(bool remember) {
#if !defined(Q_OS_IOS) && !defined(Q_OS_ANDROID) && !defined(Q_OS_WASM)
    m_generationHost.close();
#endif
    m_serverEnabled = false;
    m_continuedSyncRequested = false; m_background.release(false);
    if (remember) pauseAutomaticPairing(); else m_automatic.setEnabled(false);
    m_nearby.cancel();
    m_session = {}; m_accountSession = false; stopTransport();
    m_status = tr("Disconnected."); emit stateChanged();
}
void NetworkDriveController::fail(const QString &message) {
    m_status = message; emit stateChanged();
}
void NetworkDriveController::refresh() {
    if (m_localActive) { if (!m_remote.host().isEmpty()) browse(m_remote.host(), m_remote.path()); emit hostsChanged(); }
    else m_peer.refreshPeers();
}
QString NetworkDriveController::request(const QString &host, const QJsonObject &payload) {
    if (!signedIn() || (!hosting() && host != accountHost()) || (m_localActive && !pairingAuthenticated())) return {};
    return m_localActive ? m_local.request(host, payload) : m_peer.request(host, payload);
}
void NetworkDriveController::browse(const QString &host, const QString &path, const QString &cursor) { m_remote.browse(host, path, cursor); }
void NetworkDriveController::download(const QString &path, const QUrl &destination) { m_remote.download(path, destination); }
void NetworkDriveController::response(const QString &id, const QJsonObject &result, const QString &transport) {
    m_sync.receive(id, result); m_remote.receive(id, result, transport); m_photos->receive(id, result);
}

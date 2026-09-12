#include "NetworkDriveController.h"
#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QSslKey>
#include <QSysInfo>
#include <QUuid>

NetworkDriveController::NetworkDriveController(QObject *parent)
    : NetworkDriveController(nullptr, QHostAddress::AnyIPv4, parent) {}
NetworkDriveController::NetworkDriveController(DiscoveryService *service, QHostAddress bindAddress, QObject *parent)
    : QObject(parent), m_remote([this](const auto &peer, const auto &payload) { return request(peer, payload); }),
      m_sync([this](const auto &peer, const auto &payload) { return request(peer, payload); }), m_nearby(service, bindAddress),
      m_automatic(&m_nearby, &m_local, [this] { return startLocalHost(true); },
          [this](const QString &link) { return joinLocalHost(link, true); }, [this] { stopTransport(); }),
      m_localBindAddress(bindAddress) {
    connect(&m_remote, &iiSocietySync::RemoteFiles::stateChanged, this, [this] {
        if (!m_remote.status().isEmpty()) m_status = m_remote.status();
        emit stateChanged();
    });
    connect(&m_remote, &iiSocietySync::RemoteFiles::entriesChanged, this, &NetworkDriveController::entriesChanged);
    connect(&m_remote, &iiSocietySync::RemoteFiles::downloadFinished, this, &NetworkDriveController::downloadFinished);
    connect(&m_sync, &iiSocietySync::Controller::changed, this, &NetworkDriveController::synchronizationChanged);
    connect(&m_sync, &iiSocietySync::Controller::synchronized, this, [this](const QString &peer) {
        m_syncPath.clear(); emit synchronizationChanged(); emit containerSynchronized(peer);
        if (!hostModeAvailable() && (m_applicationState == Qt::ApplicationSuspended || m_applicationState == Qt::ApplicationHidden))
            suspendForBackground();
    });
    connect(&m_sync, &iiSocietySync::Controller::progress, this, [this](const QString &path, qint64 done, qint64 total) {
        m_syncPath = path; m_syncDone = done; m_syncTotal = total;
        emit synchronizationChanged(); emit synchronizationProgress(path, done, total);
    });
    connect(&m_sync, &iiSocietySync::Controller::mirrorChanged, this, [this](const QJsonObject &binding) {
        if (m_mirror == binding) return;
        m_mirror = binding;
        emit mirrorChanged(); emit synchronizationChanged();
        QTimer::singleShot(0, this, &NetworkDriveController::updateDiscovery);
    });
    connect(&m_automatic, &AutomaticPairing::changed, this, &NetworkDriveController::discoveryChanged);
    connect(&m_automatic, &AutomaticPairing::changed, this, &NetworkDriveController::updateDiscovery, Qt::QueuedConnection);
    connect(&m_nearby, &NearbyDevices::changed, this, &NetworkDriveController::discoveryChanged);
    connect(&m_nearby, &NearbyDevices::identityEnding, &m_local, &iiServerHost::LanPeer::cancelPairing);
    connect(&m_local, &iiServerHost::LanPeer::paired, &m_nearby, &NearbyDevices::complete);
    connect(&m_local, &iiServerHost::LanPeer::paired, this, [this](const QString &id) {
        if (m_automatic.enabled() && m_nearby.authenticated())
            for (const auto &value : m_nearby.devices())
                if (value.toMap().value("id").toString() == id && value.toMap().value("verified").toBool()) m_verifiedSyncPeers.insert(id);
        if (m_account && signedIn()) {
            QString name = m_local.peerName(), kind;
            for (const auto &value : m_nearby.devices()) {
                const auto row = value.toMap();
                if (row.value("id").toString() == id) { name = row.value("name").toString(); kind = row.value("kind").toString(); break; }
            }
            m_account->rememberPairedDevice(id, name, kind);
        }
        if (m_automatic.enabled() && !m_local.hosting()) browse(id);
        if (m_local.hosting() && m_verifiedSyncPeers.contains(id) && m_account)
            iiSocietySync::Replica::claimPrimaryHost(m_container, m_account->pairingCredentials().value("scope").toString(), m_nearby.deviceId());
        updateDiscovery();
    });
    m_discoveryTimer.setInterval(5000);
    connect(&m_discoveryTimer, &QTimer::timeout, this, &NetworkDriveController::updateDiscovery);
    m_status = tr("Pair your devices on the same Wi-Fi or local network.");
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
        if (m_peer.isReady()) m_status = hosting() ? tr("Hosting Files for your account.") : tr("Connected to your devices.");
        else if (!m_peer.errorString().isEmpty()) m_status = m_peer.errorString();
        updateSynchronization();
        emit stateChanged();
    });
    connect(&m_peer, &iiServerHost::Peer::peersChanged, this, &NetworkDriveController::hostsChanged);
    connect(&m_peer, &iiServerHost::Peer::peersChanged, this, &NetworkDriveController::updateSynchronization);
    connect(&m_peer, &iiServerHost::Peer::completed, this, &NetworkDriveController::response);
    if (!hostModeAvailable()) {
        connect(&m_background, &MobileSyncActivity::expired, this, [this] {
            m_backgroundExpired = true;
            if (m_applicationState == Qt::ApplicationSuspended || m_applicationState == Qt::ApplicationHidden) suspendForBackground();
        });
        connect(this, &NetworkDriveController::stateChanged, this, &NetworkDriveController::updateBackgroundActivity);
        connect(this, &NetworkDriveController::discoveryChanged, this, &NetworkDriveController::updateBackgroundActivity);
        if (auto *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
            connect(app, &QGuiApplication::applicationStateChanged, this, &NetworkDriveController::setApplicationState);
        }
    }
}
NetworkDriveController::~NetworkDriveController() {
    m_background.release();
    m_discoveryTimer.stop(); m_nearby.clear(); disconnectSessionImpl(false);
    // Members emit their final stop signals before QObject clears external
    // QPointers. No UI may inspect the already-destroyed discovery member then.
    m_local.disconnect(); m_peer.disconnect(); m_nearby.disconnect();
    m_sync.disconnect(); m_remote.disconnect();
}
void NetworkDriveController::setApplicationState(Qt::ApplicationState state) {
    if (hostModeAvailable()) return;
    m_applicationState = state;
    if (state == Qt::ApplicationSuspended || state == Qt::ApplicationHidden) {
        if (m_background.active() && !m_suspended) m_sync.synchronizeNow();
        else suspendForBackground();
    } else if (state == Qt::ApplicationActive) {
        m_backgroundExpired = false;
        if (m_suspended) {
            m_suspended = false; updateDiscovery(); requestAccountPairingCredentials(); restartSession();
        }
        updateBackgroundActivity(); m_sync.synchronizeNow();
    }
}
void NetworkDriveController::updateBackgroundActivity() {
    if (hostModeAvailable()) return;
    if (!m_runtimeEnabled || !signedIn()) { m_background.release(); return; }
    if (m_applicationState == Qt::ApplicationActive && !connected() && !m_automatic.active()) { m_background.release(); return; }
    if (m_applicationState == Qt::ApplicationActive && !m_backgroundExpired) m_background.retain();
}
void NetworkDriveController::suspendForBackground() {
    m_background.release();
    if (m_suspended) return;
    m_suspended = true; m_nearby.clear(); stopTransport();
    m_status = tr("Sync will resume when Society becomes active."); emit stateChanged();
}
void NetworkDriveController::setAccountSession(AccountController *account) {
    if (account == m_account || (account && account->thread() != thread())) return;
    disconnectSessionImpl(false);
    if (m_account) {
        disconnect(m_account, nullptr, this, nullptr);
        disconnect(m_account->manager(), nullptr, this, nullptr);
    }
    m_account = account;
    m_discoverySession.clear(); m_automatic.setEnabled(true);
    m_nearby.clear(); m_discoveryTimer.stop();
    if (account) {
        connect(account, &AccountController::changed, this, [this] {
            if (m_accountSession && !signedIn()) disconnectSessionImpl(false);
            updateDiscovery();
            emit authChanged();
        });
        connect(account, &AccountController::sessionEnding, this, [this] { m_nearby.clear(); disconnectSessionImpl(false); });
        connect(account, &AccountController::pairingStateRestored, this, [this] {
            m_automatic.setEnabled(m_account->automaticPairingEnabled()); updateDiscovery();
            requestAccountPairingCredentials();
        });
        connect(account, &AccountController::pairingCredentialsRequired, this, &NetworkDriveController::requestAccountPairingCredentials);
        connect(account, &AccountController::pairingCredentialsChanged, this, &NetworkDriveController::updateDiscovery);
        connect(account, &QObject::destroyed, this, [this] {
            m_account = nullptr; m_nearby.clear(); m_discoveryTimer.stop(); disconnectSessionImpl(false); emit authChanged();
        });
        m_discoveryTimer.start();
    }
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
    const auto primary = m_mirror.isEmpty() ? iiSocietySync::Replica::primaryHost(m_container, scope) : m_mirror.value("host").toString();
    m_nearby.setCredentials(credentials, hostModeAvailable() && m_mirror.isEmpty() && !m_container.isEmpty() && m_automatic.enabled()
        && iiSocietyContainer::SharedStorage::open(m_container).has_value(), primary);
    updateSynchronization();
}
void NetworkDriveController::updateSynchronization() {
    if (!m_runtimeEnabled || m_suspended || !signedIn() || !m_nearby.authenticated() || !connected()) { m_sync.close(); return; }
    const auto scope = m_account->pairingCredentials().value("scope").toString();
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
            if (m_mode == ClientMode) hosts.append(id);
        }
    }
    if (authorized.isEmpty()) { m_sync.close(); return; }
    m_sync.open(m_container, scope);
    m_sync.setPeers(authorized, hosts);
}
bool NetworkDriveController::containerReady() const {
    if (!m_mirror.isEmpty()) {
        const auto drive = iiSocietyContainer::SocietyDrive::open(m_container);
        return drive && m_mirror.value("complete").toBool() && m_mirror.value("container") == drive->identifier();
    }
    return hostModeAvailable() && !(m_localActive && !m_local.hosting() && m_local.connected());
}
QString NetworkDriveController::synchronizationStatus() const {
    const auto error = m_sync.errorString();
    if (error == "container_account_binding_mismatch") return tr("This container is linked to another account.");
    if (error == "insufficient_storage") return tr("Not enough free space to continue syncing.");
    if (error == "filename_normalization_collision") return tr("Rename files whose names differ only by case or Unicode form to continue syncing.");
    if (error == "native_sync_filesystem_unsupported_platform") return tr("Container sync is unavailable on this platform.");
    if (!error.isEmpty()) return tr("Container sync is waiting to retry.");
    if (m_sync.busy() && m_syncTotal > 0 && !m_syncPath.isEmpty())
        return tr("Syncing %1 (%2%)…").arg(m_syncPath.section('/', -1)).arg(m_syncDone * 100 / m_syncTotal);
    if (!containerReady()) return connected() ? tr("Preparing the host's Society drive on this device…")
        : tr("Connect to your desktop to mirror your Society drive.");
    if (m_sync.busy()) return tr("Syncing your container…");
    if (m_sync.available()) return tr("Container sync is active.");
    return tr("Waiting for an account-verified device to sync.");
}
bool NetworkDriveController::hostModeAvailable() const {
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID) || defined(SOCIETY_CLIENT_ONLY)
    return false;
#else
    return true;
#endif
}
void NetworkDriveController::setMode(Mode mode) {
    const auto next = hostModeAvailable() && mode == HostMode ? HostMode : ClientMode;
    if (m_mode == next) return;
    pauseAutomaticPairing();
    m_mode = next; emit modeChanged();
    if (m_localActive) { disconnectSession(); return; }
    restartSession();
}
void NetworkDriveController::setRuntimeEnabled(bool enabled) {
    if (m_runtimeEnabled == enabled) return;
    m_runtimeEnabled = enabled;
    if (!enabled) {
        m_nearby.clear(); stopTransport();
        m_sync.closeAndWait();
    } else {
        m_automatic.setEnabled(m_account ? m_account->automaticPairingEnabled() : true);
        updateDiscovery(); requestAccountPairingCredentials(); restartSession();
    }
}
void NetworkDriveController::setContainerPath(const QString &path) {
    if (path == m_container) return;
    const bool automatic = m_automatic.enabled();
    m_container = path;
    m_mirror = iiSocietySync::Replica::binding(path);
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
    // A changed destination must not receive the previous session implicitly.
    disconnectSession(); m_relayUrl = url; emit configurationChanged();
}
void NetworkDriveController::connectSession() {
    if (hostModeAvailable() && m_mode == HostMode) startLocalHost();
    else fail(tr("Select this device on your desktop or scan its QR code to connect."));
}
bool NetworkDriveController::startLocalHost(bool automatic) {
    if (!m_runtimeEnabled || m_suspended) return false;
    if (!automatic) pauseAutomaticPairing();
    if (!hostModeAvailable()) { fail(tr("Only desktop Society can host Files.")); return false; }
    if (!m_mirror.isEmpty()) { fail(tr("This device mirrors its primary Society host.")); return false; }
    if (m_local.hosting()) return true;
    if (automatic) { m_session = {}; m_accountSession = false; stopTransport(); }
    else disconnectSession();
    m_mode = HostMode; emit modeChanged();
    QString error; m_storage = iiSocietyContainer::SharedStorage::open(m_container, &error);
    if (!m_storage) { fail(error.isEmpty() ? tr("Open a Society container before pairing.") : error); return false; }
    const auto files = iiSocietySync::filesHandler(m_storage->drive().rootPath());
    m_localActive = true;
    const auto id = m_nearby.active() ? m_nearby.deviceId() : m_account ? m_account->manager()->deviceInfo().value("id").toString() : QUuid::createUuid().toString(QUuid::WithoutBraces);
    return m_local.startHost(id, m_nearby.active() ? m_nearby.deviceName() : QSysInfo::machineHostName(),
        [this, files](const auto &peer, const auto &payload) {
            if (payload.value("op") == "society.sync") return m_sync.handle(peer, payload);
            return files(peer, payload);
        },
        m_localBindAddress.isLoopback() ? QStringList{m_localBindAddress.toString()} : QStringList{}, m_localBindAddress);
}
bool NetworkDriveController::joinLocalHost(const QString &qr, bool automatic) {
    if (!m_runtimeEnabled || m_suspended) return false;
    if (automatic) { m_session = {}; m_accountSession = false; stopTransport(); }
    else disconnectSession();
    m_mode = ClientMode; emit modeChanged(); m_localActive = true;
    const auto id = m_nearby.active() ? m_nearby.deviceId() : m_account ? m_account->manager()->deviceInfo().value("id").toString() : QUuid::createUuid().toString(QUuid::WithoutBraces);
    return m_local.join(qr, id, m_nearby.active() ? m_nearby.deviceName() : QSysInfo::machineHostName());
}
bool NetworkDriveController::startSession(iiServerHost::PeerOptions options) {
    return startSessionImpl(std::move(options), false);
}
bool NetworkDriveController::startSessionImpl(iiServerHost::PeerOptions options, bool accountSession) {
    stopTransport();
    // Keep the supplied settings for subsequent mode changes. Client restrictions
    // must not erase the host's TLS configuration when switching back to Host.
    m_session = options; m_accountSession = accountSession;
    if (m_suspended || !m_runtimeEnabled) { m_status = tr("Device connection paused."); emit stateChanged(); return true; }
    const bool host = hostModeAvailable() && m_mode == HostMode;
    if (host) {
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
    const auto files = m_storage ? iiSocietySync::filesHandler(m_storage->drive().rootPath()) : iiServerHost::RequestHandler();
    m_status = tr("Connecting to your devices…"); emit entriesChanged();
    const bool ok = m_peer.start(options, [this, files](const QString &peer, const QJsonObject &request) {
        if (request.value("op") == "society.sync") {
            // The relay transport has already authenticated and isolated the
            // caller's principal; client IDs need not appear in the host list.
            if (signedIn() && m_peer.accountId() == m_account->manager()->account()->sub()) {
                m_verifiedSyncPeers.insert(peer); updateSynchronization();
            }
            return m_sync.handle(peer, request);
        }
        return files ? files(peer, request) : QJsonObject{{"ok", false}, {"error", "container_unavailable"}};
    });
    if (!ok) fail(m_peer.errorString()); else emit stateChanged();
    return ok;
}
void NetworkDriveController::restartSession() {
    if (!m_session.credential.isEmpty()) startSessionImpl(m_session, m_accountSession);
}
void NetworkDriveController::stopTransport() {
    emit transportStopped();
    m_sync.close(); m_verifiedSyncPeers.clear(); m_remote.reset(); m_storage.reset();
    m_localActive = false; m_local.stop(); m_peer.stop(); emit entriesChanged(); emit hostsChanged();
}
void NetworkDriveController::pauseAutomaticPairing() {
    m_automatic.setEnabled(false);
    if (m_account && signedIn()) m_account->setAutomaticPairingEnabled(false);
}
void NetworkDriveController::resumeAutomaticPairing() {
    m_automatic.setEnabled(true);
    if (m_account && signedIn()) m_account->setAutomaticPairingEnabled(true);
    updateDiscovery();
}
void NetworkDriveController::disconnectSession() { disconnectSessionImpl(true); }
void NetworkDriveController::disconnectSessionImpl(bool remember) {
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
    return m_localActive ? m_local.request(host, payload) : m_peer.request(host, payload);
}
void NetworkDriveController::browse(const QString &host, const QString &path, const QString &cursor) { m_remote.browse(host, path, cursor); }
void NetworkDriveController::download(const QString &path, const QUrl &destination) { m_remote.download(path, destination); }
void NetworkDriveController::response(const QString &id, const QJsonObject &result, const QString &transport) {
    m_sync.receive(id, result); m_remote.receive(id, result, transport);
}

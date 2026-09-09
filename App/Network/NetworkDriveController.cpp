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
    : QObject(parent), m_nearby(service, bindAddress), m_localBindAddress(bindAddress) {
    connect(&m_nearby, &NearbyDevices::changed, this, &NetworkDriveController::discoveryChanged);
    connect(&m_nearby, &NearbyDevices::identityEnding, &m_local, &iiServerHost::LanPeer::cancelPairing);
    connect(&m_local, &iiServerHost::LanPeer::paired, &m_nearby, &NearbyDevices::complete);
    m_discoveryTimer.setInterval(5000);
    connect(&m_discoveryTimer, &QTimer::timeout, this, &NetworkDriveController::updateDiscovery);
    m_status = tr("Pair your devices on the same Wi-Fi or local network.");
    connect(&m_local, &iiServerHost::LanPeer::changed, this, [this] {
        emit discoveryChanged();
        if (m_localActive) {
            if (!m_local.errorString().isEmpty()) m_status = m_local.errorString();
            else if (m_local.hosting()) m_status = tr("Hosting Files on the local network.");
            else if (m_local.connected()) m_status = tr("Connected directly over the local network.");
            else m_status = tr("Connecting to the desktop on the local network…");
            if (!m_local.connected() && !m_local.hosting() && m_local.phase() == "error") {
                m_entries.clear(); m_host.clear(); m_path.clear(); m_cursor.clear(); m_transport.clear();
                emit entriesChanged();
            }
            emit stateChanged(); emit hostsChanged();
        }
    });
    connect(&m_local, &iiServerHost::LanPeer::completed, this, &NetworkDriveController::response);
    connect(&m_peer, &iiServerHost::Peer::stateChanged, this, [this] {
        if (m_peer.isReady()) m_status = hosting() ? tr("Hosting Files for your account.") : tr("Connected to your devices.");
        else if (!m_peer.errorString().isEmpty()) m_status = m_peer.errorString();
        emit stateChanged();
    });
    connect(&m_peer, &iiServerHost::Peer::peersChanged, this, &NetworkDriveController::hostsChanged);
    connect(&m_peer, &iiServerHost::Peer::completed, this, &NetworkDriveController::response);
    if (!hostModeAvailable()) {
        if (auto *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
            connect(app, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
                if (state == Qt::ApplicationSuspended || state == Qt::ApplicationHidden) {
                    m_suspended = true; m_nearby.clear(); stopTransport();
                    m_status = tr("Device connection paused."); emit stateChanged();
                } else if (m_suspended && state == Qt::ApplicationActive) {
                    m_suspended = false; updateDiscovery(); restartSession();
                }
            });
        }
    }
}
NetworkDriveController::~NetworkDriveController() {
    m_discoveryTimer.stop(); m_nearby.clear(); disconnectSession();
    // Members emit their final stop signals before QObject clears external
    // QPointers. No UI may inspect the already-destroyed discovery member then.
    m_local.disconnect(); m_nearby.disconnect();
}
void NetworkDriveController::setAccountSession(AccountController *account) {
    if (account == m_account || (account && account->thread() != thread())) return;
    disconnectSession();
    if (m_account) {
        disconnect(m_account, nullptr, this, nullptr);
        disconnect(m_account->manager(), nullptr, this, nullptr);
    }
    m_account = account;
    m_nearby.clear(); m_discoveryTimer.stop();
    if (account) {
        connect(account, &AccountController::changed, this, [this] {
            if (m_accountSession && !signedIn()) disconnectSession();
            updateDiscovery();
            emit authChanged();
        });
        connect(account, &AccountController::sessionEnding, this, [this] { m_nearby.clear(); disconnectSession(); });
        connect(account, &QObject::destroyed, this, [this] {
            m_account = nullptr; m_nearby.clear(); m_discoveryTimer.stop(); disconnectSession(); emit authChanged();
        });
        m_discoveryTimer.start();
    }
    updateDiscovery();
    emit authChanged();
}
QVariantList NetworkDriveController::nearbyDevices() const {
    auto result = m_nearby.devices(); const auto connected = m_local.pairedDeviceIds();
    for (auto &value : result) {
        auto row = value.toMap(); row.insert("connected", connected.contains(row.value("id").toString())); value = row;
    }
    return result;
}
void NetworkDriveController::updateDiscovery() {
#ifdef SOCIETY_DISABLE_SESSION_RESTORE
    return; // Test consumers do not announce the user's account or installation.
#else
    if (qEnvironmentVariableIntValue("SOCIETY_DISABLE_SESSION_RESTORE") == 1) return;
    const auto expiry = m_account ? QDateTime::fromString(m_account->manager()->loginSession().value("expiresAt").toString(), Qt::ISODate) : QDateTime();
    if (m_suspended || !signedIn() || !expiry.isValid() || expiry <= QDateTime::currentDateTimeUtc()) { m_nearby.clear(); return; }
    const auto device = m_account->manager()->deviceInfo();
    const auto origin = m_account->manager()->serviceUrl().adjusted(QUrl::RemovePath | QUrl::RemoveQuery | QUrl::RemoveFragment | QUrl::RemoveUserInfo).toString();
    const auto scope = NearbyDevices::accountScope(origin, m_account->userId());
    const auto name = device.value("name").toString().isEmpty() ? QSysInfo::machineHostName() : device.value("name").toString();
    m_nearby.setIdentity(scope, device.value("id").toString(), name, device.value("type").toString(), hostModeAvailable());
#endif
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
    m_mode = next; emit modeChanged();
    if (m_localActive) { disconnectSession(); return; }
    restartSession();
}
void NetworkDriveController::setContainerPath(const QString &path) {
    if (path == m_container) return;
    m_container = path;
    if (m_mode == HostMode) {
        if (m_localActive) disconnectSession(); else restartSession();
    }
    emit configurationChanged();
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
bool NetworkDriveController::startLocalHost() {
    if (!hostModeAvailable()) { fail(tr("Only desktop Society can host Files.")); return false; }
    if (m_local.hosting()) return true;
    disconnectSession(); m_mode = HostMode; emit modeChanged();
    QString error; m_storage = iiSocietyContainer::SharedStorage::open(m_container, &error);
    if (!m_storage) { fail(error.isEmpty() ? tr("Open a Society container before pairing.") : error); return false; }
    const auto storage = *m_storage;
    auto share = std::make_shared<iiServerHost::FileShare>(storage.filePath(iiSocietyContainer::StoreSection::Files), [storage] { return storage.drive().isValid(); });
    m_localActive = true;
    const auto id = m_nearby.active() ? m_nearby.deviceId() : m_account ? m_account->manager()->deviceInfo().value("id").toString() : QUuid::createUuid().toString(QUuid::WithoutBraces);
    return m_local.startHost(id, m_nearby.active() ? m_nearby.deviceName() : QSysInfo::machineHostName(),
        [share](const auto &, const auto &payload) { return share->handle(payload); },
        m_localBindAddress.isLoopback() ? QStringList{m_localBindAddress.toString()} : QStringList{}, m_localBindAddress);
}
bool NetworkDriveController::joinLocalHost(const QString &qr) {
    disconnectSession(); m_mode = ClientMode; emit modeChanged(); m_localActive = true;
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
    if (m_suspended) { m_status = tr("Device connection paused."); emit stateChanged(); return true; }
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
    std::shared_ptr<iiServerHost::FileShare> share;
    if (m_storage) {
        const auto storage = *m_storage;
        share = std::make_shared<iiServerHost::FileShare>(storage.filePath(iiSocietyContainer::StoreSection::Files), [storage] { return storage.drive().isValid(); });
    }
    m_status = tr("Connecting to your devices…"); emit entriesChanged();
    const bool ok = m_peer.start(options, [share](const QString &, const QJsonObject &request) {
        return share ? share->handle(request) : QJsonObject{{"ok", false}, {"error", "container_unavailable"}};
    });
    if (!ok) fail(m_peer.errorString()); else emit stateChanged();
    return ok;
}
void NetworkDriveController::restartSession() {
    if (!m_session.credential.isEmpty()) startSessionImpl(m_session, m_accountSession);
}
void NetworkDriveController::stopTransport() {
    m_download.reset(); m_request.clear(); m_operation.clear(); m_storage.reset();
    m_entries.clear(); m_host.clear(); m_path.clear(); m_cursor.clear(); m_transport.clear();
    m_localActive = false; m_local.stop(); m_peer.stop(); emit entriesChanged(); emit hostsChanged();
}
void NetworkDriveController::disconnectSession() {
    m_nearby.cancel();
    m_session = {}; m_accountSession = false; stopTransport();
    m_status = tr("Disconnected."); emit stateChanged();
}
void NetworkDriveController::fail(const QString &message) {
    m_status = message; m_request.clear(); m_download.reset(); emit stateChanged();
}
void NetworkDriveController::refresh() {
    if (m_localActive) { if (!m_host.isEmpty()) browse(m_host, m_path); emit hostsChanged(); }
    else m_peer.refreshPeers();
}
QString NetworkDriveController::request(const QString &host, const QJsonObject &payload) {
    return m_localActive ? m_local.request(host, payload) : m_peer.request(host, payload);
}
void NetworkDriveController::browse(const QString &host, const QString &path, const QString &cursor) {
    if (busy()) return;
    m_host = host; m_path = path; m_cursor.clear(); m_operation = "list";
    if (cursor.isEmpty()) m_entries.clear();
    QJsonObject request{{"op", "list"}, {"path", path}}; if (!cursor.isEmpty()) request.insert("cursor", cursor);
    m_request = this->request(host, request); emit entriesChanged(); emit stateChanged();
}
void NetworkDriveController::download(const QString &path, const QUrl &destination) {
    if (busy() || m_host.isEmpty()) return;
    if (!destination.isLocalFile() || !QDir::isAbsolutePath(destination.toLocalFile())) { fail(tr("Choose a local destination file.")); return; }
    m_download = std::make_unique<QSaveFile>(destination.toLocalFile());
    m_download->setDirectWriteFallback(false);
    if (!m_download->open(QIODevice::WriteOnly)) { fail(tr("The destination cannot be opened.")); return; }
    m_downloadPath = path; m_received = 0; m_operation = "stat";
    m_request = request(m_host, {{"op", "stat"}, {"path", path}}); emit stateChanged();
}
void NetworkDriveController::nextChunk() {
    m_operation = "read";
    m_request = request(m_host, {{"op", "read"}, {"path", m_downloadPath}, {"offset", QString::number(m_received)}, {"version", m_version}});
    emit stateChanged();
}
void NetworkDriveController::response(const QString &id, const QJsonObject &result, const QString &transport) {
    if (id != m_request) return;
    m_request.clear(); m_transport = transport;
    if (!result.value("ok").toBool()) { fail(result.value("error").toString(tr("The file request failed."))); return; }
    if (m_operation == "list") {
        m_entries.append(result.value("entries").toArray().toVariantList()); m_cursor = result.value("nextCursor").toString();
        m_status = tr("Files on your device."); emit entriesChanged(); emit stateChanged(); return;
    }
    if (!m_download) { fail(tr("The download was cancelled.")); return; }
    if (m_operation == "stat") {
        bool valid; m_expected = result.value("size").toString().toLongLong(&valid); m_version = result.value("version").toString();
        if (!valid || m_expected < 0 || m_version.isEmpty()) { fail(tr("The host returned invalid file metadata.")); return; }
        nextChunk(); return;
    }
    const auto decoded = QByteArray::fromBase64Encoding(result.value("data").toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    bool validOffset, validSize;
    const auto offset = result.value("offset").toString().toLongLong(&validOffset);
    const auto size = result.value("size").toString().toLongLong(&validSize);
    if (!decoded || !validOffset || !validSize || offset != m_received || size != m_expected || result.value("version").toString() != m_version
        || decoded.decoded.size() > iiServerHost::FileShare::ChunkBytes || decoded.decoded.size() > m_expected - m_received
        || (decoded.decoded.isEmpty() && m_received != m_expected)
        || m_download->write(decoded.decoded) != decoded.decoded.size()) { fail(tr("The file changed or the transfer is incomplete.")); return; }
    m_received += decoded.decoded.size();
    if (result.value("eof").toBool()) {
        if (m_received != m_expected) { fail(tr("The transfer ended before the file was complete.")); return; }
        const auto destination = QUrl::fromLocalFile(m_download->fileName());
        if (!m_download->commit()) { fail(tr("The downloaded file could not be saved.")); return; }
        m_download.reset(); m_status = tr("Download complete."); emit stateChanged(); emit downloadFinished(destination); return;
    }
    if (m_received == m_expected) { fail(tr("The host returned an invalid end-of-file marker.")); return; }
    nextChunk();
}

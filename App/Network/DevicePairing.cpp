#include "DevicePairing.h"
#include <QCryptographicHash>
#include <QSettings>
#include <QTimeZone>

DevicePairing::DevicePairing(QObject *parent) : QObject(parent) {
    m_countdown.setInterval(1000); m_timeout.setSingleShot(true); m_timeout.setInterval(15000);
    connect(&m_countdown, &QTimer::timeout, this, [this] {
        if (m_expires.isValid() && m_expires <= QDateTime::currentDateTimeUtc()) fail(tr("The QR code expired. Show a new code on the desktop and scan again."));
        else emit changed();
    });
    connect(&m_timeout, &QTimer::timeout, this, [this] { fail(tr("Pairing timed out. Check the host connection and try again.")); });
}
DevicePairing::~DevicePairing() { cancel(); }
int DevicePairing::secondsRemaining() const {
    return m_expires.isValid() ? qMax(0, int((QDateTime::currentDateTimeUtc().msecsTo(m_expires) + 999) / 1000)) : 0;
}
void DevicePairing::setNetwork(NetworkDriveController *network) {
    if (network == m_network || (network && network->thread() != thread())) return;
    cancel();
    if (m_network) { disconnect(m_network, nullptr, this, nullptr); disconnect(m_network->peer(), nullptr, this, nullptr); }
    m_network = network;
    m_restoreAttempted = false;
    if (network) {
        connect(network, &NetworkDriveController::stateChanged, this, &DevicePairing::connectionChanged);
        connect(network, &NetworkDriveController::hostsChanged, this, &DevicePairing::restoreHost);
        connect(network->peer(), &iiServerHost::Peer::pairingEvent, this, &DevicePairing::pairingEvent);
        connect(network->peer(), &iiServerHost::Peer::completed, this, [this](const QString &id, const QJsonObject &result, const QString &) {
            if (id != m_probeId || m_probeId.isEmpty()) return;
            m_probeId.clear();
            if (!result.value("ok").toBool() || !result.value("entries").isArray()) {
                fail(tr("The host could not open its Files. Pairing was not completed.")); return;
            }
            if (m_network) m_network->peer()->confirmPairing(m_pairId);
        });
        connect(network, &QObject::destroyed, this, [this] { m_network = nullptr; cancel(); });
    }
    emit changed();
}
void DevicePairing::cancel() {
    const auto request = m_operation; m_operation.clear(); m_probeId.clear(); m_pairId.clear();
    m_qr.clear(); m_link = {}; m_expires = {}; m_peerId.clear(); m_peerName.clear();
    m_timeout.stop(); m_countdown.stop(); m_phase = "idle"; m_message.clear();
    if (m_network && !request.isEmpty()) m_network->peer()->cancelPairing(request);
    emit changed();
}
void DevicePairing::fail(const QString &message) { cancel(); m_phase = "error"; m_message = message; emit changed(); }
bool DevicePairing::trustedRelay(const QUrl &relay) const {
    if (!m_network) return false;
    // A QR cannot redirect account cookies to an arbitrary server. Configuration
    // is trusted independently; otherwise require the account authority's origin.
    if (!m_network->activeRelayUrl().isEmpty() && relay == m_network->activeRelayUrl()) return true;
    const auto *account = m_network->accountManager();
    if (!account) return false;
    const auto authority = account->serviceUrl();
    return relay.scheme() == "wss" && authority.scheme() == "https"
        && relay.host().compare(authority.host(), Qt::CaseInsensitive) == 0
        && relay.port(443) == authority.port(443);
}
void DevicePairing::showHostQr() {
    cancel();
    if (!m_network || !m_network->hostModeAvailable()) { fail(tr("Only the desktop can show a host pairing code.")); return; }
    if (!m_network->signedIn() && !m_network->connected()) { fail(tr("Sign in to your iisacc account first.")); return; }
    if (m_network->containerPath().isEmpty()) { fail(tr("Open a Society container before pairing a device.")); return; }
    if (m_network->activeRelayUrl().isEmpty()) { fail(tr("Configure the Society relay in Devices before showing a QR code.")); return; }
    m_host = true; m_phase = "connecting"; m_message = tr("Preparing the host…"); m_timeout.start(); emit changed();
    m_network->setMode(NetworkDriveController::HostMode);
    if (!m_network->connected()) m_network->connectSession();
    connectionChanged();
}
void DevicePairing::scanCode(const QString &text) {
    cancel(); iiServerHost::PairingLink link;
    if (!iiServerHost::PairingLink::decode(text, &link)) { fail(tr("This is not a valid Society pairing QR code.")); return; }
    if (!m_network || (!m_network->signedIn() && !m_network->connected())) { fail(tr("Sign in to the same iisacc account as the desktop first.")); return; }
    if (!trustedRelay(link.relayUrl)) { fail(tr("This QR uses an untrusted relay. Configure a trusted relay in Devices first.")); return; }
    m_host = false; m_link = link; m_phase = "connecting"; m_message = tr("Connecting to the desktop…"); m_timeout.start(); emit changed();
    m_network->setMode(NetworkDriveController::ClientMode);
    if (!m_network->connected() || m_network->activeRelayUrl() != link.relayUrl) {
        m_network->setRelayUrl(link.relayUrl); m_network->connectSession();
    }
    connectionChanged();
}
void DevicePairing::connectionChanged() {
    if (!m_network) return;
    if (!m_network->connected()) {
        m_restoreAttempted = false;
        if (m_phase == "showing" || m_phase == "verifying") fail(tr("The connection closed. Show a new QR code and try again."));
        return;
    }
    if (m_phase == "connecting" && m_operation.isEmpty()) {
        if (m_host) {
            if (!m_network->hosting()) { fail(tr("The desktop is not hosting Files.")); return; }
            m_operation = m_network->peer()->createPairingOffer();
        } else {
            m_operation = m_network->peer()->claimPairingOffer(m_link.code, m_link.hostId); m_link.code.clear();
        }
    }
    restoreHost();
}
void DevicePairing::pairingEvent(const QJsonObject &event) {
    if (m_operation.isEmpty() || event.value("id").toString() != m_operation) return;
    const auto status = event.value("status").toString();
    if (status == "error") {
        const auto error = event.value("error").toString();
        fail(error == "pairing_rate_limited" ? tr("Too many pairing attempts. Wait 30 seconds and try again.")
            : tr("The QR code is expired, already used, or belongs to another account. Show a new code and try again.")); return;
    }
    if (status == "expired" || status == "cancelled") { fail(tr("Pairing ended. Show a new QR code to try again.")); return; }
    m_peerId = event.value("peerId").toString(); m_peerName = event.value("name").toString();
    if (status == "offered") {
        m_timeout.stop();
        m_expires = QDateTime::fromMSecsSinceEpoch(event.value("expires").toString().toLongLong(), QTimeZone::UTC);
        m_qr = iiServerHost::PairingLink{m_network->activeRelayUrl(), m_peerId, event.value("code").toString()}.encode();
        if (m_qr.isEmpty()) { fail(tr("The host returned an invalid pairing code.")); return; }
        m_phase = "showing"; m_message = tr("In iPhone Society, open Devices → Pair desktop → Scan QR code."); m_countdown.start();
    } else if (status == "claimed") {
        m_qr.clear(); m_pairId = event.value("pairingId").toString();
        m_expires = QDateTime::fromMSecsSinceEpoch(event.value("expires").toString().toLongLong(), QTimeZone::UTC);
        m_phase = "verifying"; m_message = tr("Checking the connection with %1…").arg(m_peerName); m_countdown.start(); m_timeout.start();
        if (!m_host) m_probeId = m_network->peer()->request(m_peerId, {{"op", "list"}, {"path", ""}});
    } else if (status == "paired") {
        m_operation.clear(); m_pairId.clear(); m_qr.clear(); m_expires = {}; m_timeout.stop(); m_countdown.stop();
        m_phase = "paired"; m_message = tr("Paired with %1.").arg(m_peerName);
        if (!m_host) {
            QSettings settings(QSettings::IniFormat, QSettings::UserScope, "iisacc", "SocietyPairing");
            const auto key = settingsKey(); settings.setValue(key + "/host", m_peerId); settings.setValue(key + "/name", m_peerName); settings.sync();
            m_restoreAttempted = true; m_network->browse(m_peerId);
        }
        emit paired(m_peerId);
    }
    emit changed();
}
QString DevicePairing::settingsKey() const {
    if (!m_network || !m_network->connected()) return {};
    const auto scope = m_network->peer()->accountId().toUtf8() + '\n' + m_network->activeRelayUrl().toEncoded();
    return "hosts/" + QString::fromLatin1(QCryptographicHash::hash(scope, QCryptographicHash::Sha256).toHex());
}
void DevicePairing::restoreHost() {
    if (!m_network || !m_network->connected() || m_network->mode() != NetworkDriveController::ClientMode
        || m_restoreAttempted || m_phase != "idle" || !m_network->currentHost().isEmpty()) return;
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "iisacc", "SocietyPairing");
    const auto saved = settings.value(settingsKey() + "/host").toString();
    if (saved.isEmpty()) return;
    for (const auto &entry : m_network->hosts()) if (entry.toMap().value("peerId").toString() == saved) {
        m_restoreAttempted = true; m_network->browse(saved); return;
    }
}

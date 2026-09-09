#include "DevicePairing.h"

DevicePairing::~DevicePairing() { cancel(); }
void DevicePairing::setNetwork(NetworkDriveController *network) {
    if (network == m_network || (network && network->thread() != thread())) return;
    cancel();
    if (m_network) disconnect(m_network->localPeer(), nullptr, this, nullptr);
    m_network = network;
    if (network) {
        connect(network->localPeer(), &iiServerHost::LanPeer::changed, this, &DevicePairing::changed);
        connect(network->localPeer(), &iiServerHost::LanPeer::paired, this, [this](const QString &id) {
            if (!m_active || !m_network) return;
            if (!m_network->localPeer()->hosting()) m_network->browse(id);
            emit paired(id); emit changed();
        });
        connect(network, &QObject::destroyed, this, [this] { m_network = nullptr; m_active = false; emit changed(); });
    }
    emit changed();
}
QString DevicePairing::phase() const {
    if (!m_error.isEmpty()) return "error";
    return m_active && m_network ? m_network->localPeer()->phase() : "idle";
}
QString DevicePairing::message() const {
    if (!m_error.isEmpty()) return m_error;
    if (!m_active || !m_network) return {};
    const auto *peer = m_network->localPeer();
    if (!peer->errorString().isEmpty()) return peer->errorString();
    if (phase() == "showing") return tr("In mobile Society, open Devices → Pair desktop → Scan QR code. Use the same Wi-Fi or LAN.");
    if (phase() == "connecting") return tr("Connecting directly to the desktop on the local network…");
    if (phase() == "verifying") return tr("Checking access to the desktop's Files…");
    if (phase() == "paired") return tr("Paired with %1 over the local network.").arg(peer->peerName());
    return {};
}
QString DevicePairing::qrText() const { return m_active && m_network ? m_network->localPeer()->qrText() : QString(); }
QString DevicePairing::peerName() const { return m_network ? m_network->localPeer()->peerName() : QString(); }
int DevicePairing::secondsRemaining() const { return m_network ? m_network->localPeer()->secondsRemaining() : 0; }
void DevicePairing::cancel() {
    m_active = false; m_error.clear();
    if (m_network) m_network->localPeer()->cancelPairing();
    emit changed();
}
void DevicePairing::showHostQr() {
    cancel(); m_active = true;
    if (!m_network || !m_network->hostModeAvailable()) m_error = tr("Only the desktop can show a pairing QR code.");
    else if (!m_network->startLocalHost()) m_error = m_network->status();
    else if (m_network->localPeer()->createOffer().isEmpty()) m_error = tr("Could not create a local pairing QR code.");
    emit changed();
}
void DevicePairing::scanCode(const QString &text) {
    cancel(); m_active = true;
    iiServerHost::LanLink link;
    if (!iiServerHost::LanLink::decode(text, &link)) m_error = tr("Scan a local-network QR code from the updated desktop Society app.");
    else if (!m_network) m_error = tr("The device connection is unavailable.");
    else if (!m_network->joinLocalHost(text)) m_error = m_network->localPeer()->errorString();
    emit changed();
}

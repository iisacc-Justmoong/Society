#include "DevicePairing.h"

DevicePairing::~DevicePairing() { cancel(); }
void DevicePairing::setNetwork(NetworkDriveController *network) {
    if (network == m_network || (network && network->thread() != thread())) return;
    cancel();
    if (m_network) {
        disconnect(m_network, nullptr, this, nullptr);
        disconnect(m_network->discovery(), nullptr, this, nullptr);
        disconnect(m_network->localPeer(), nullptr, this, nullptr);
    }
    m_network = network;
    if (network) {
        connect(network->discovery(), &NearbyDevices::changed, this, &DevicePairing::changed);
        connect(network->discovery(), &NearbyDevices::invitationReceived, this, [this] {
            m_error.clear(); m_active = true; emit changed(); emit invitationReceived();
        });
        connect(network->discovery(), &NearbyDevices::invitationEnded, this, [this](const QString &message) {
            if (m_active && m_network && m_network->localPeer()->phase() == "showing") {
                m_network->localPeer()->cancelPairing(); m_error = message; emit changed();
            }
        });
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
    if (m_active && m_network && m_network->discovery()->hasIncoming()) return "invited";
    if (m_active && m_network && !m_network->discovery()->outgoingName().isEmpty() && m_network->localPeer()->phase() == "showing") return "inviting";
    return m_active && m_network ? m_network->localPeer()->phase() : "idle";
}
QString DevicePairing::message() const {
    if (!m_error.isEmpty()) return m_error;
    if (!m_active || !m_network) return {};
    const auto *peer = m_network->localPeer();
    if (!peer->errorString().isEmpty()) return peer->errorString();
    if (phase() == "invited") return tr("%1 wants to pair with this device on your local network.").arg(incomingName());
    if (phase() == "inviting") return tr("Waiting for %1 to accept. Keep Society open on the other device.").arg(m_network->discovery()->outgoingName());
    if (phase() == "confirming") return canConfirm() ? tr("Check that this code matches the other device, then allow the connection.")
                                                   : tr("Compare this code on your desktop and allow the connection there.");
    if (phase() == "showing") return tr("In mobile Society, open Devices → Pair desktop → Scan QR code. Use the same Wi-Fi or LAN.");
    if (phase() == "connecting") return tr("Connecting directly to the desktop on the local network…");
    if (phase() == "verifying") return tr("Checking access to the desktop's Files…");
    if (phase() == "paired") return tr("Paired with %1 over the local network.").arg(peer->peerName());
    return {};
}
QString DevicePairing::qrText() const { return phase() == "showing" && m_network ? m_network->localPeer()->qrText() : QString(); }
QString DevicePairing::peerName() const { return m_network ? m_network->localPeer()->peerName() : QString(); }
int DevicePairing::secondsRemaining() const { return m_network ? m_network->localPeer()->secondsRemaining() : 0; }
void DevicePairing::cancel() {
    m_active = false; m_error.clear();
    if (m_network) { m_network->discovery()->cancel(); m_network->localPeer()->cancelPairing(); }
    emit changed();
}
void DevicePairing::begin() {
    if (m_network && m_network->discovery()->hasIncoming()) { m_active = true; emit changed(); }
    else if (m_network && m_network->hostModeAvailable()) showHostQr();
}
void DevicePairing::inviteDevice(const QString &id) {
    cancel(); m_active = true;
    if (!m_network || !m_network->hostModeAvailable()) m_error = tr("Select this device on your desktop to begin pairing.");
    else if (!m_network->startLocalHost()) m_error = m_network->status();
    else {
        const auto offer = m_network->localPeer()->createDeviceOffer(id);
        if (offer.isEmpty() || !m_network->discovery()->invite(id, offer)) {
            m_network->localPeer()->cancelPairing(); m_error = tr("This device is no longer nearby. Wait for discovery and try again.");
        }
    }
    emit changed();
}
void DevicePairing::acceptInvitation() {
    if (!m_network) return;
    const auto link = m_network->discovery()->accept(); m_active = true; m_error.clear();
    if (link.isEmpty()) m_error = tr("The request expired or the desktop is no longer nearby.");
    else if (!m_network->joinLocalHost(link)) m_error = m_network->localPeer()->errorString();
    emit changed();
}
void DevicePairing::confirmDevice() {
    if (canConfirm() && !m_network->localPeer()->confirmDevice()) m_error = tr("The device could not be paired. Try again.");
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

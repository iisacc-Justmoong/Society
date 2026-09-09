#include "NearbyDevices.h"
#include <iiServerHost.h>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QNetworkDatagram>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>
#include <algorithm>

namespace {
qint64 now() { return QDateTime::currentMSecsSinceEpoch(); }
bool token(const QString &s) { static const QRegularExpression re("\\A[A-Za-z0-9_.:-]{1,128}\\z"); return re.match(s).hasMatch(); }
bool hash(const QString &s) { static const QRegularExpression re("\\A[a-f0-9]{64}\\z"); return re.match(s).hasMatch(); }
QString nonce() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
bool validRecord(const QJsonObject &r) {
    const auto name = r.value("name").toString();
    static const QRegularExpression controls("[\\x00-\\x1f\\x7f]");
    return r.value("v") == "1" && hash(r.value("scope").toString()) && token(r.value("id").toString())
        && token(r.value("nonce").toString()) && !name.isEmpty() && name.size() <= 128 && !name.contains(controls)
        && QStringList{"pc", "phone", "tablet"}.contains(r.value("kind").toString())
        && QStringList{"0", "1"}.contains(r.value("host").toString())
        && QJsonDocument(r).toJson(QJsonDocument::Compact).size() <= 1024;
}
}
NearbyDevices::NearbyDevices(QObject *parent) : NearbyDevices(nullptr, QHostAddress::AnyIPv4, parent) {}
NearbyDevices::NearbyDevices(DiscoveryService *service, QHostAddress bindAddress, QObject *parent)
    : QObject(parent), m_service(service ? service : DiscoveryService::create(this)), m_bindAddress(bindAddress) {
    connect(m_service, &DiscoveryService::found, this, &NearbyDevices::found);
    connect(m_service, &DiscoveryService::lost, this, [this](const QString &key) { if (m_endpoints.remove(key)) emit changed(); });
    connect(m_service, &DiscoveryService::failed, this, [this](const QString &error) {
        if (!active()) return;
        m_error = error; if (!m_retryAt) m_retryAt = now() + 5000; emit changed();
    });
    connect(&m_socket, &QUdpSocket::readyRead, this, &NearbyDevices::receive);
    m_timer.setInterval(1000); connect(&m_timer, &QTimer::timeout, this, &NearbyDevices::tick);
}
NearbyDevices::~NearbyDevices() { clear(); }
QString NearbyDevices::accountScope(const QString &origin, const QString &userId) {
    if (origin.isEmpty() || userId.isEmpty()) return {};
    return QString::fromLatin1(QCryptographicHash::hash("society-nearby-v1\n" + origin.toUtf8() + '\n' + userId.toUtf8(), QCryptographicHash::Sha256).toHex());
}
void NearbyDevices::setIdentity(const QString &scope, const QString &id, const QString &name, const QString &kind, bool desktop) {
    if (active() && m_record.value("scope") == scope && deviceId() == id && deviceName() == name
        && m_record.value("kind") == kind && m_record.value("host") == (desktop ? "1" : "0")) return;
    clear();
    QJsonObject record{{"v", "1"}, {"scope", scope}, {"id", id}, {"name", name.left(128)},
                       {"kind", kind}, {"host", desktop ? "1" : "0"}, {"nonce", nonce()}};
    if (!validRecord(record)) return;
    m_record = record;
    if (!m_socket.bind(m_bindAddress, 0)) { m_error = tr("Local device discovery could not start: %1").arg(m_socket.errorString()); emit changed(); return; }
    m_timer.start(); m_service->start(m_record, m_socket.localPort()); emit changed();
}
void NearbyDevices::clear() {
    if (active() || !m_record.isEmpty()) emit identityEnding();
    cancel(); m_timer.stop(); m_service->stop(); m_socket.close(); m_record = {};
    m_endpoints.clear(); m_seenRequests.clear(); m_error.clear(); m_retryAt = 0; emit changed();
}
bool NearbyDevices::localAddress(const QHostAddress &address) const {
    return iiServerHost::LanLink::localAddress(address.toString()) && (!address.isLoopback() || m_bindAddress.isLoopback());
}
void NearbyDevices::found(const QString &service, const QJsonObject &record, const QHostAddress &address, quint16 port) {
    if (!active() || service.size() > 512 || !validRecord(record) || !port || !localAddress(address)
        || record.value("scope") != m_record.value("scope") || record.value("id") == m_record.value("id")) return;
    if (!m_endpoints.contains(service) && m_endpoints.size() >= 128) return;
    m_endpoints.insert(service, {service, record, address, port, now()}); m_error.clear(); emit changed();
}
std::optional<NearbyDevices::Endpoint> NearbyDevices::endpoint(const QString &id) const {
    for (const auto &e : m_endpoints) if (e.record.value("id") == id && now() - e.seen < 25000) return e;
    return {};
}
QVariantList NearbyDevices::devices() const {
    QVariantList result; QSet<QString> ids;
    for (const auto &e : m_endpoints) {
        const auto id = e.record.value("id").toString(); if (ids.contains(id) || now() - e.seen >= 25000) continue; ids.insert(id);
        result.append(QVariantMap{{"id", id}, {"name", e.record.value("name").toString()}, {"kind", e.record.value("kind").toString()},
                                 {"address", e.address.toString()}, {"desktop", e.record.value("host") == "1"}});
    }
    std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) { return a.toMap().value("name").toString().localeAwareCompare(b.toMap().value("name").toString()) < 0; });
    return result;
}
QString NearbyDevices::status() const {
    if (!m_error.isEmpty()) return m_error;
    if (!active()) return tr("Sign in to iisacc on both devices to discover nearby devices.");
    return devices().isEmpty() ? tr("Searching your local network… Keep Society open on your other device.")
                               : tr("Nearby devices on your iisacc account. The list updates automatically.");
}
void NearbyDevices::send(const QJsonObject &packet, const Endpoint &e) {
    const auto bytes = QJsonDocument(packet).toJson(QJsonDocument::Compact);
    if (bytes.size() <= 4096) m_socket.writeDatagram(bytes, e.address, e.port);
}
bool NearbyDevices::invite(const QString &id, const QString &text) {
    const auto peer = endpoint(id); iiServerHost::LanLink link;
    if (!active() || m_record.value("host") != "1" || !peer || !iiServerHost::LanLink::decode(text, &link)
        || link.hostId != deviceId() || link.expiresAt.toMSecsSinceEpoch() <= now()) return false;
    cancel();
    m_outgoing = {{"v", "1"}, {"op", "invite"}, {"scope", m_record.value("scope")}, {"from", deviceId()},
                  {"to", id}, {"nonce", peer->record.value("nonce")}, {"request", nonce()}, {"link", text},
                  {"name", peer->record.value("name")}, {"expires", QString::number(link.expiresAt.toMSecsSinceEpoch())}};
    send(m_outgoing, *peer); m_nextSend = now() + 2000; emit changed(); return true;
}
void NearbyDevices::receive() {
    int count = 0;
    while (m_socket.hasPendingDatagrams() && ++count <= 64) {
        if (m_socket.pendingDatagramSize() > 4096) { m_socket.readDatagram(nullptr, 0); continue; }
        const auto datagram = m_socket.receiveDatagram();
        if (!localAddress(datagram.senderAddress())) continue;
        const auto p = QJsonDocument::fromJson(datagram.data()).object();
        const auto peer = endpoint(p.value("from").toString());
        if (!peer || peer->address != datagram.senderAddress() || peer->port != datagram.senderPort()
            || p.value("v") != "1" || p.value("scope") != m_record.value("scope") || p.value("to") != m_record.value("id")
            || !token(p.value("request").toString())) continue;
        const auto op = p.value("op").toString();
        if (op == "invite") {
            iiServerHost::LanLink link; const auto request = p.value("request").toString();
            if (peer->record.value("host") != "1" || p.value("nonce") != m_record.value("nonce") || m_seenRequests.contains(request)
                || !m_incoming.isEmpty() || !iiServerHost::LanLink::decode(p.value("link").toString(), &link)
                || link.hostId != peer->record.value("id") || link.expiresAt.toMSecsSinceEpoch() <= now()
                || link.expiresAt.toMSecsSinceEpoch() > now() + 65000) continue;
            // A discovered peer cannot redirect this device to another LAN host.
            if (!link.addresses.contains(peer->address.toString())) continue;
            link.addresses = {peer->address.toString()};
            m_incoming = p; m_incoming.insert("link", link.encode()); m_incoming.insert("name", peer->record.value("name"));
            m_incoming.insert("expires", QString::number(link.expiresAt.toMSecsSinceEpoch()));
            m_seenRequests.insert(request, now() + 70000); emit changed(); emit invitationReceived();
        } else if ((op == "decline" || op == "accept") && !m_outgoing.isEmpty() && p.value("request") == m_outgoing.value("request")
                   && p.value("from") == m_outgoing.value("to")) {
            if (op == "decline") { m_outgoing = {}; emit changed(); emit invitationEnded(tr("The other device declined the request.")); }
            else m_nextSend = now() + 65000;
        } else if (op == "cancel" && !m_incoming.isEmpty() && p.value("request") == m_incoming.value("request")
                   && p.value("from") == m_incoming.value("from")) { m_incoming = {}; emit changed(); }
    }
    if (m_socket.hasPendingDatagrams()) QTimer::singleShot(0, this, &NearbyDevices::receive);
}
QString NearbyDevices::accept() {
    if (m_incoming.isEmpty() || m_incoming.value("expires").toString().toLongLong() <= now()) { decline(); return {}; }
    const auto peer = endpoint(m_incoming.value("from").toString());
    if (!peer) { decline(); return {}; }
    send({{"v", "1"}, {"op", "accept"}, {"scope", m_record.value("scope")}, {"from", deviceId()},
          {"to", peer->record.value("id")}, {"request", m_incoming.value("request")}}, *peer);
    const auto link = m_incoming.value("link").toString(); m_incoming = {}; emit changed(); return link;
}
void NearbyDevices::decline() {
    if (const auto peer = endpoint(m_incoming.value("from").toString()))
        send({{"v", "1"}, {"op", "decline"}, {"scope", m_record.value("scope")}, {"from", deviceId()},
              {"to", peer->record.value("id")}, {"request", m_incoming.value("request")}}, *peer);
    m_incoming = {}; emit changed();
}
void NearbyDevices::cancel() {
    decline();
    if (const auto peer = endpoint(m_outgoing.value("to").toString()))
        send({{"v", "1"}, {"op", "cancel"}, {"scope", m_record.value("scope")}, {"from", deviceId()},
              {"to", peer->record.value("id")}, {"request", m_outgoing.value("request")}}, *peer);
    m_outgoing = {}; emit changed();
}
void NearbyDevices::complete() { m_outgoing = {}; emit changed(); }
void NearbyDevices::tick() {
    bool removed = false;
    for (const auto &key : m_endpoints.keys()) if (now() - m_endpoints.value(key).seen >= 25000) { m_endpoints.remove(key); removed = true; }
    for (const auto &key : m_seenRequests.keys()) if (m_seenRequests.value(key) <= now()) m_seenRequests.remove(key);
    if (m_seenRequests.size() > 128) m_seenRequests.clear();
    if (m_retryAt && now() >= m_retryAt) { m_retryAt = 0; m_service->stop(); m_service->start(m_record, m_socket.localPort()); }
    if (!m_incoming.isEmpty() && m_incoming.value("expires").toString().toLongLong() <= now()) { m_incoming = {}; removed = true; }
    if (!m_outgoing.isEmpty()) {
        if (m_outgoing.value("expires").toString().toLongLong() <= now()) { m_outgoing = {}; removed = true; emit invitationEnded(tr("The connection request expired. Select the device to try again.")); }
        else if (m_nextSend <= now()) { if (const auto peer = endpoint(m_outgoing.value("to").toString())) send(m_outgoing, *peer); m_nextSend = now() + 2000; }
    }
    if (removed) emit changed();
}

#pragma once
#include "DiscoveryService.h"
#include <QDateTime>
#include <QHash>
#include <QTimer>
#include <QUdpSocket>
#include <QVariantList>
#include <optional>

class NearbyDevices final : public QObject {
    Q_OBJECT
public:
    explicit NearbyDevices(QObject *parent = nullptr);
    // Injected service and explicit loopback bind isolate discovery tests from
    // account login, the user's session store and the physical LAN.
    NearbyDevices(DiscoveryService *service, QHostAddress bindAddress, QObject *parent = nullptr);
    ~NearbyDevices() override;
    static QString accountScope(const QString &origin, const QString &userId);
    void setIdentity(const QString &scope, const QString &id, const QString &name,
                     const QString &kind, bool desktop);
    void clear();
    bool active() const { return m_socket.state() == QAbstractSocket::BoundState; }
    QVariantList devices() const;
    QString status() const;
    QString deviceId() const { return m_record.value("id").toString(); }
    QString deviceName() const { return m_record.value("name").toString(); }
    QString incomingName() const { return m_incoming.value("name").toString(); }
    QString outgoingName() const { return m_outgoing.value("name").toString(); }
    bool hasIncoming() const { return !m_incoming.isEmpty(); }
    bool invite(const QString &deviceId, const QString &link);
    QString accept();
    void decline();
    void cancel();
    void complete();
signals:
    void identityEnding();
    void changed();
    void invitationReceived();
    void invitationEnded(QString message);
private:
    struct Endpoint { QString service; QJsonObject record; QHostAddress address; quint16 port; qint64 seen; };
    bool localAddress(const QHostAddress &address) const;
    void found(const QString &service, const QJsonObject &record, const QHostAddress &address, quint16 port);
    void receive();
    void tick();
    void send(const QJsonObject &packet, const Endpoint &endpoint);
    std::optional<Endpoint> endpoint(const QString &id) const;
    DiscoveryService *m_service;
    QHostAddress m_bindAddress;
    QUdpSocket m_socket;
    QTimer m_timer;
    QJsonObject m_record, m_incoming, m_outgoing;
    QHash<QString, Endpoint> m_endpoints;
    QHash<QString, qint64> m_seenRequests;
    QString m_error;
    qint64 m_nextSend = 0, m_retryAt = 0;
};

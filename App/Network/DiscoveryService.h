#pragma once
#include <QObject>
#include <QHostAddress>
#include <QJsonObject>

// OS DNS-SD owns multicast discovery. The app only exchanges bounded unicast
// invitations; this service never opens or publishes a Files server.
class DiscoveryService : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    virtual void start(const QJsonObject &record, quint16 port) = 0;
    virtual void stop() = 0;
    static DiscoveryService *create(QObject *parent);
signals:
    void found(QString service, QJsonObject record, QHostAddress address, quint16 port);
    void lost(QString service);
    void failed(QString message);
};

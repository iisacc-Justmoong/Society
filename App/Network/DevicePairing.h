#pragma once
#include "NetworkDriveController.h"

// UI orchestration for a direct LAN connection. Account credentials never enter it.
class DevicePairing : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(NetworkDriveController *network READ network WRITE setNetwork NOTIFY changed)
    Q_PROPERTY(QString phase READ phase NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(QString qrText READ qrText NOTIFY changed)
    Q_PROPERTY(QString peerName READ peerName NOTIFY changed)
    Q_PROPERTY(int secondsRemaining READ secondsRemaining NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
public:
    explicit DevicePairing(QObject *parent = nullptr) : QObject(parent) {}
    ~DevicePairing() override;
    NetworkDriveController *network() const { return m_network; }
    void setNetwork(NetworkDriveController *network);
    QString phase() const;
    QString message() const;
    QString qrText() const;
    QString peerName() const;
    int secondsRemaining() const;
    bool busy() const { return phase() == "connecting" || phase() == "verifying"; }
    Q_INVOKABLE void showHostQr();
    Q_INVOKABLE void scanCode(const QString &code);
    Q_INVOKABLE void cancel();
signals:
    void changed();
    void paired(QString peerId);
private:
    QPointer<NetworkDriveController> m_network;
    QString m_error;
    bool m_active = false;
};

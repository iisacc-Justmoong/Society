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
    Q_PROPERTY(QString incomingName READ incomingName NOTIFY changed)
    Q_PROPERTY(QString verificationCode READ verificationCode NOTIFY changed)
    Q_PROPERTY(bool canConfirm READ canConfirm NOTIFY changed)
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
    bool busy() const { return phase() == "connecting" || phase() == "verifying" || phase() == "confirming" || phase() == "inviting"; }
    QString incomingName() const { return m_network ? m_network->discovery()->incomingName() : QString(); }
    QString verificationCode() const { return m_network ? m_network->localPeer()->verificationCode() : QString(); }
    bool canConfirm() const { return m_network && m_network->localPeer()->hosting() && phase() == "confirming"; }
    Q_INVOKABLE void begin();
    Q_INVOKABLE void inviteDevice(const QString &id);
    Q_INVOKABLE void acceptInvitation();
    Q_INVOKABLE void confirmDevice();
    Q_INVOKABLE void showHostQr();
    Q_INVOKABLE void copyPairingLink();
    Q_INVOKABLE void scanCode(const QString &code);
    Q_INVOKABLE void cancel();
signals:
    void changed();
    void paired(QString peerId);
    void invitationReceived();
private:
    QPointer<NetworkDriveController> m_network;
    QString m_error;
    bool m_active = false;
};

#pragma once
#include "NetworkDriveController.h"
#include <iiServerHost.h>
#include <QTimer>

// QR orchestration borrows the existing account transport; it never owns credentials.
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
    explicit DevicePairing(QObject *parent = nullptr);
    ~DevicePairing() override;
    NetworkDriveController *network() const { return m_network; }
    void setNetwork(NetworkDriveController *network);
    QString phase() const { return m_phase; }
    QString message() const { return m_message; }
    QString qrText() const { return m_qr; }
    QString peerName() const { return m_peerName; }
    int secondsRemaining() const;
    bool busy() const { return m_phase == "connecting" || m_phase == "verifying"; }
    Q_INVOKABLE void showHostQr();
    Q_INVOKABLE void scanCode(const QString &code);
    Q_INVOKABLE void cancel();
signals:
    void changed();
    void paired(QString peerId);
private:
    void connectionChanged();
    void pairingEvent(const QJsonObject &event);
    void fail(const QString &message);
    void restoreHost();
    QString settingsKey() const;
    bool trustedRelay(const QUrl &relay) const;
    QPointer<NetworkDriveController> m_network;
    QTimer m_countdown, m_timeout;
    QDateTime m_expires;
    QString m_phase = "idle", m_message, m_qr, m_operation, m_pairId, m_probeId, m_peerId, m_peerName;
    iiServerHost::PairingLink m_link;
    bool m_host = false, m_restoreAttempted = false;
};

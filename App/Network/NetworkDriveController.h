#pragma once
#include <iiServerHost.h>
#include <iiSocietySync.h>
#include "App/Account/AccountController.h"
#include "NearbyDevices.h"
#include "AutomaticPairing.h"
#include "MobileSyncActivity.h"
#include <SharedStorage.h>
#include <QPointer>
#include <QSet>
#include <QtQml/qqmlregistration.h>

class NetworkDriveController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString containerPath READ containerPath WRITE setContainerPath NOTIFY configurationChanged)
    Q_PROPERTY(QUrl relayUrl READ relayUrl WRITE setRelayUrl NOTIFY configurationChanged)
    Q_PROPERTY(Mode mode READ mode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(bool hostModeAvailable READ hostModeAvailable CONSTANT)
    Q_PROPERTY(bool hosting READ hosting NOTIFY stateChanged)
    Q_PROPERTY(AccountController *accountSession READ accountSession WRITE setAccountSession NOTIFY authChanged)
    Q_PROPERTY(QObject *accountManager READ accountManager NOTIFY authChanged)
    Q_PROPERTY(bool signedIn READ signedIn NOTIFY authChanged)
    Q_PROPERTY(bool authBusy READ authBusy NOTIFY authChanged)
    Q_PROPERTY(bool codeRequired READ codeRequired NOTIFY authChanged)
    Q_PROPERTY(QString authError READ authError NOTIFY authChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
    Q_PROPERTY(QVariantList hosts READ hosts NOTIFY hostsChanged)
    Q_PROPERTY(QVariantList entries READ entries NOTIFY entriesChanged)
    Q_PROPERTY(QString currentHost READ currentHost NOTIFY entriesChanged)
    Q_PROPERTY(QString currentPath READ currentPath NOTIFY entriesChanged)
    Q_PROPERTY(QString nextCursor READ nextCursor NOTIFY entriesChanged)
    Q_PROPERTY(QString transport READ transport NOTIFY stateChanged)
    Q_PROPERTY(QVariantList nearbyDevices READ nearbyDevices NOTIFY discoveryChanged)
    Q_PROPERTY(bool discovering READ discovering NOTIFY discoveryChanged)
    Q_PROPERTY(QString discoveryStatus READ discoveryStatus NOTIFY discoveryChanged)
    Q_PROPERTY(QVariantList pairingQueue READ pairingQueue NOTIFY discoveryChanged)
    Q_PROPERTY(QString automaticPairingStatus READ automaticPairingStatus NOTIFY discoveryChanged)
    Q_PROPERTY(bool automaticPairingEnabled READ automaticPairingEnabled NOTIFY discoveryChanged)
    Q_PROPERTY(bool synchronizationAvailable READ synchronizationAvailable NOTIFY synchronizationChanged)
    Q_PROPERTY(bool synchronizing READ synchronizing NOTIFY synchronizationChanged)
    Q_PROPERTY(QString synchronizationStatus READ synchronizationStatus NOTIFY synchronizationChanged)
    Q_PROPERTY(bool containerReady READ containerReady NOTIFY synchronizationChanged)
public:
    enum Mode { ClientMode, HostMode };
    Q_ENUM(Mode)
    explicit NetworkDriveController(QObject *parent = nullptr);
    NetworkDriveController(DiscoveryService *service, QHostAddress bindAddress, QObject *parent = nullptr);
    ~NetworkDriveController() override;
    Mode mode() const { return m_mode; }
    void setMode(Mode mode);
    bool hostModeAvailable() const;
    bool hosting() const { return m_mode == HostMode && m_storage.has_value() && (m_local.hosting() || m_peer.isReady()); }
    QString containerPath() const { return m_container; }
    void setContainerPath(const QString &path);
    // Desktop GUI and background service must acquire one process lease first.
    void setRuntimeEnabled(bool enabled);
    bool runtimeEnabled() const { return m_runtimeEnabled; }
    void setApplicationState(Qt::ApplicationState state);
    MobileSyncActivity *backgroundActivity() { return &m_background; }
    QUrl relayUrl() const { return m_relayUrl; }
    QUrl activeRelayUrl() const { return m_session.relayUrl.isEmpty() ? m_relayUrl : m_session.relayUrl; }
    iiServerHost::Peer *peer() { return &m_peer; }
    iiServerHost::LanPeer *localPeer() { return &m_local; }
    NearbyDevices *discovery() { return &m_nearby; }
    QVariantList nearbyDevices() const;
    bool discovering() const { return m_nearby.active(); }
    QString discoveryStatus() const { return m_nearby.status(); }
    bool startLocalHost(bool automatic = false);
    bool joinLocalHost(const QString &qr, bool automatic = false);
    QVariantList pairingQueue() const { return m_automatic.queue(); }
    QString automaticPairingStatus() const { return m_automatic.status(); }
    bool automaticPairingEnabled() const { return m_automatic.enabled(); }
    bool automaticPairingActive() const { return m_automatic.active(); }
    void pauseAutomaticPairing();
    Q_INVOKABLE void resumeAutomaticPairing();
    void setRelayUrl(const QUrl &url);
    AccountController *accountSession() const { return m_account; }
    void setAccountSession(AccountController *account);
    iisacc::accounts::AccountManager *accountManager() const { return m_account ? m_account->manager() : nullptr; }
    bool signedIn() const { return m_account && m_account->signedIn(); }
    bool authBusy() const { return m_account && m_account->busy(); }
    bool codeRequired() const { return m_account && m_account->codeRequired(); }
    QString authError() const { return m_account ? m_account->errorString() : QString(); }
    bool connected() const { return m_localActive ? m_local.hosting() || m_local.connected() : m_peer.isReady(); }
    bool busy() const { return m_remote.busy(); }
    QString status() const { return m_status; }
    QVariantList hosts() const { return (m_localActive ? m_local.peers() : m_peer.peers()).toVariantList(); }
    QVariantList entries() const { return m_remote.entries(); }
    QString currentHost() const { return m_remote.host(); }
    QString currentPath() const { return m_remote.path(); }
    QString nextCursor() const { return m_remote.nextCursor(); }
    QString transport() const { return m_remote.transport(); }
    bool synchronizationAvailable() const { return m_sync.available(); }
    bool synchronizing() const { return m_sync.busy(); }
    QString synchronizationStatus() const;
    bool containerReady() const;
    Q_INVOKABLE void synchronizeNow() { m_sync.synchronizeNow(); }
    // Native integrations may supply an authenticated session. Mode/platform
    // policy overrides host options; the relay still verifies the credential.
    bool startSession(iiServerHost::PeerOptions options);
    Q_INVOKABLE void connectSession();
    Q_INVOKABLE void disconnectSession();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void browse(const QString &host, const QString &path = {}, const QString &cursor = {});
    Q_INVOKABLE void download(const QString &path, const QUrl &destination);
signals:
    void modeChanged();
    void configurationChanged();
    void authChanged();
    void stateChanged();
    void hostsChanged();
    void entriesChanged();
    void discoveryChanged();
    void downloadFinished(QUrl file);
    void transportStopped();
    void synchronizationChanged();
    void containerSynchronized(QString peer);
    void mirrorChanged();
    void synchronizationProgress(QString path, qint64 completedBytes, qint64 totalBytes);
private:
    bool startSessionImpl(iiServerHost::PeerOptions options, bool accountSession);
    void restartSession();
    void stopTransport();
    void disconnectSessionImpl(bool remember);
    void response(const QString &id, const QJsonObject &result, const QString &transport);
    void fail(const QString &message);
    void updateDiscovery();
    void requestAccountPairingCredentials();
    void updateSynchronization();
    void updateBackgroundActivity();
    void suspendForBackground();
    QString request(const QString &host, const QJsonObject &payload);
    QPointer<AccountController> m_account;
    iiServerHost::Peer m_peer;
    iiServerHost::LanPeer m_local;
    iiSocietySync::RemoteFiles m_remote;
    iiSocietySync::Controller m_sync;
    QSet<QString> m_verifiedSyncPeers;
    NearbyDevices m_nearby;
    AutomaticPairing m_automatic;
    MobileSyncActivity m_background;
    QHostAddress m_localBindAddress;
    QTimer m_discoveryTimer;
    bool m_localActive = false;
    std::optional<iiSocietyContainer::SharedStorage> m_storage;
    QString m_container, m_status;
    QUrl m_relayUrl;
    iiServerHost::PeerOptions m_session;
    Mode m_mode = ClientMode;
    bool m_accountSession = false;
    bool m_suspended = false, m_runtimeEnabled = true;
    bool m_backgroundExpired = false;
    Qt::ApplicationState m_applicationState = Qt::ApplicationActive;
    QString m_discoverySession;
    QJsonObject m_mirror;
    QString m_syncPath;
    qint64 m_syncDone = 0, m_syncTotal = 0;
};

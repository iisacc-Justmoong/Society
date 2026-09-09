#pragma once
#include <iiServerHost.h>
#include "App/Account/AccountController.h"
#include <SharedStorage.h>
#include <QPointer>
#include <QSaveFile>
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
public:
    enum Mode { ClientMode, HostMode };
    Q_ENUM(Mode)
    explicit NetworkDriveController(QObject *parent = nullptr);
    ~NetworkDriveController() override;
    Mode mode() const { return m_mode; }
    void setMode(Mode mode);
    bool hostModeAvailable() const;
    bool hosting() const { return m_mode == HostMode && m_storage.has_value() && (m_local.hosting() || m_peer.isReady()); }
    QString containerPath() const { return m_container; }
    void setContainerPath(const QString &path);
    QUrl relayUrl() const { return m_relayUrl; }
    QUrl activeRelayUrl() const { return m_session.relayUrl.isEmpty() ? m_relayUrl : m_session.relayUrl; }
    iiServerHost::Peer *peer() { return &m_peer; }
    iiServerHost::LanPeer *localPeer() { return &m_local; }
    bool startLocalHost();
    bool joinLocalHost(const QString &qr);
    void setRelayUrl(const QUrl &url);
    AccountController *accountSession() const { return m_account; }
    void setAccountSession(AccountController *account);
    iisacc::accounts::AccountManager *accountManager() const { return m_account ? m_account->manager() : nullptr; }
    bool signedIn() const { return m_account && m_account->signedIn(); }
    bool authBusy() const { return m_account && m_account->busy(); }
    bool codeRequired() const { return m_account && m_account->codeRequired(); }
    QString authError() const { return m_account ? m_account->errorString() : QString(); }
    bool connected() const { return m_localActive ? m_local.hosting() || m_local.connected() : m_peer.isReady(); }
    bool busy() const { return !m_request.isEmpty(); }
    QString status() const { return m_status; }
    QVariantList hosts() const { return (m_localActive ? m_local.peers() : m_peer.peers()).toVariantList(); }
    QVariantList entries() const { return m_entries; }
    QString currentHost() const { return m_host; }
    QString currentPath() const { return m_path; }
    QString nextCursor() const { return m_cursor; }
    QString transport() const { return m_transport; }
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
    void downloadFinished(QUrl file);
private:
    bool startSessionImpl(iiServerHost::PeerOptions options, bool accountSession);
    void restartSession();
    void stopTransport();
    void response(const QString &id, const QJsonObject &result, const QString &transport);
    void fail(const QString &message);
    void nextChunk();
    QString request(const QString &host, const QJsonObject &payload);
    QPointer<AccountController> m_account;
    iiServerHost::Peer m_peer;
    iiServerHost::LanPeer m_local;
    bool m_localActive = false;
    std::optional<iiSocietyContainer::SharedStorage> m_storage;
    QString m_container, m_status, m_host, m_path, m_cursor, m_transport;
    QUrl m_relayUrl;
    QVariantList m_entries;
    QString m_request, m_operation, m_downloadPath, m_version;
    qint64 m_received = 0, m_expected = 0;
    std::unique_ptr<QSaveFile> m_download;
    iiServerHost::PeerOptions m_session;
    Mode m_mode = ClientMode;
    bool m_accountSession = false;
    bool m_suspended = false;
};

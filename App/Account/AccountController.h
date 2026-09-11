#pragma once

#include <iiAcountManager.h>
#include <QNetworkAccessManager>
#include <QJsonObject>
#include <QJsonArray>
#include <QPointer>
#include <QDateTime>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

// One account session for the application. UI and device transport borrow it.
class AccountController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(iisacc::accounts::AccountManager *manager READ manager CONSTANT)
    Q_PROPERTY(QObject *account READ account CONSTANT)
    Q_PROPERTY(bool signedIn READ signedIn NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool codeRequired READ codeRequired NOTIFY changed)
    Q_PROPERTY(QString errorString READ errorString NOTIFY changed)
    Q_PROPERTY(QString displayName READ displayName NOTIFY changed)
    Q_PROPERTY(QString email READ email NOTIFY changed)
    Q_PROPERTY(QString userId READ userId NOTIFY changed)
    Q_PROPERTY(QString membership READ membership NOTIFY changed)
public:
    explicit AccountController(QObject *parent = nullptr);
    explicit AccountController(const QUrl &serviceUrl, QObject *parent = nullptr);
    AccountController(const QUrl &serviceUrl, iisacc::accounts::SessionStore *store, QObject *parent);
    iisacc::accounts::AccountManager *manager() { return &m_manager; }
    QObject *account() const { return m_manager.account(); }
    bool signedIn() const { return m_manager.isAuthenticated(); }
    bool busy() const { return m_manager.isLoading() || m_manager.isRestoringSession(); }
    bool codeRequired() const { return !m_manager.loginChallenge().isEmpty(); }
    QString errorString() const { return !m_manager.errorString().isEmpty() ? m_manager.errorString()
        : !m_manager.sessionStorageError().isEmpty() ? m_manager.sessionStorageError() : m_pairingStorageError; }
    QString displayName() const { return m_manager.account()->displayLabel(); }
    QString email() const { return m_manager.account()->email(); }
    QString userId() const { return m_manager.account()->userId(); }
    QString membership() const { return m_manager.account()->toVariantMap().value("societyCloudMembership").toString(); }
    // Active cookies remain private; refresh credentials also use app-owned
    // secure storage. This compatibility API is C++ only; LAN pairing does not use it.
    QByteArray relayCredential() const;
    // C++ only; restored from an authenticated, bounded group-container cache.
    QJsonObject pairingCredentials() const { return m_pairingCredentials; }
    QJsonArray rememberedPeers() const { return m_rememberedPeers; }
    bool automaticPairingEnabled() const { return m_automaticPairingEnabled; }
    void setAutomaticPairingEnabled(bool enabled);
    void rememberPairedDevice(const QString &id, const QString &name, const QString &kind);
    void requestPairingCredentials();
    Q_INVOKABLE bool login(const QString &email, const QString &password);
    Q_INVOKABLE bool refresh();
    Q_INVOKABLE bool logout();
    Q_INVOKABLE void cancelLogin();
signals:
    void changed();
    void sessionEnding();
    void pairingCredentialsChanged();
    void pairingStateRestored();
    void pairingCredentialsRequired();
private:
    void initializePersistence(iisacc::accounts::SessionStore *store);
    void restoreCachedAccount();
    void schedulePairingExpiry();
    void restorePairingState();
    void persistPairingState();
    void discardPairingState();
    QString pairingStateKey() const;
    bool validPairingCredentials(const QJsonObject &proof) const;
    void clearPairingCredentials();
    QNetworkAccessManager m_network;
    iisacc::accounts::AccountManager m_manager;
    QPointer<QNetworkReply> m_pairingReply;
    QJsonObject m_pairingCredentials;
    QDateTime m_pairingRestoreRetryAt;
    QTimer m_pairingExpiry;
    QPointer<iisacc::accounts::SessionStore> m_stateStore;
    QJsonArray m_rememberedPeers;
    QString m_pairingLoadedSession, m_pairingStorageError;
    quint64 m_pairingRevision = 0;
    bool m_pairingRequestBlocked = false, m_verifiedThisRun = false;
    bool m_pairingLoading = false, m_automaticPairingEnabled = true, m_pairingSessionEnding = false;
};

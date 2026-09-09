#pragma once

#include <iiAcountManager.h>
#include <QNetworkAccessManager>
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
    iisacc::accounts::AccountManager *manager() { return &m_manager; }
    QObject *account() const { return m_manager.account(); }
    bool signedIn() const { return m_manager.account()->isPresent() && !m_manager.loginSession().isEmpty(); }
    bool busy() const { return m_manager.isLoading(); }
    bool codeRequired() const { return !m_manager.loginChallenge().isEmpty(); }
    QString errorString() const { return m_manager.errorString(); }
    QString displayName() const { return m_manager.account()->displayLabel(); }
    QString email() const { return m_manager.account()->email(); }
    QString userId() const { return m_manager.account()->userId(); }
    QString membership() const { return m_manager.account()->toVariantMap().value("societyCloudMembership").toString(); }
    // Cookies remain in the private in-memory jar; this is a C++ transport API.
    QByteArray relayCredential() const;
    Q_INVOKABLE bool login(const QString &email, const QString &password);
    Q_INVOKABLE bool refresh();
    Q_INVOKABLE bool logout();
    Q_INVOKABLE void cancelLogin();
signals:
    void changed();
    void sessionEnding();
private:
    QNetworkAccessManager m_network;
    iisacc::accounts::AccountManager m_manager;
};

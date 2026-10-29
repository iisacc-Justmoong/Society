#pragma once
#include <QtQml/qqmlregistration.h>
#include <iiAccountManager/LicenseManager.h>
#include <QTcpServer>
#include <iiSocietyHelper/AccountSession.h>
class QTcpSocket;
class AccountController : public AccountSession
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList connections READ connections NOTIFY connectionsChanged)
    Q_PROPERTY(bool connectionsBusy READ connectionsBusy NOTIFY connectionsChanged)
    Q_PROPERTY(QString connectionsCode READ connectionsCode NOTIFY connectionsChanged)
    Q_PROPERTY(QVariantList licenses READ licenses NOTIFY licensesChanged)
    Q_PROPERTY(bool licensesBusy READ licensesBusy NOTIFY licensesChanged)
    Q_PROPERTY(QString licensesCode READ licensesCode NOTIFY licensesChanged)
  public:
    explicit AccountController(QObject *parent = nullptr);
    explicit AccountController(const QUrl &serviceUrl, QObject *parent = nullptr);
    AccountController(const QUrl &serviceUrl, iisacc::accounts::SessionStore *store,
                      QObject *parent);
    ~AccountController() override;
    QVariantList connections() const { return connections_; }
    bool connectionsBusy() const { return !connectionsReply_.isNull(); }
    QString connectionsCode() const { return connectionsCode_; }
    Q_INVOKABLE bool refreshConnections();
    Q_INVOKABLE bool disconnectApp(const QString &sessionId);
    QVariantList licenses() { return manager()->licenseManager()->licenses(); }
    bool licensesBusy() { return manager()->licenseManager()->busy(); }
    QString licensesCode() { return manager()->licenseManager()->code(); }
    Q_INVOKABLE bool ensureLicenses() { return manager()->licenseManager()->ensure(); }
    Q_INVOKABLE bool refreshLicenses() { return manager()->licenseManager()->refresh(); }
    bool startLicenseBroker(quint16 port = 49627);
    QUrl licenseBrokerUrl() const;
  signals:
    void licensesChanged();
    void connectionsChanged();

  private:
    void mountLicenses();
    void clearConnections();
    bool requestConnections(const QJsonObject &input, bool revoke);
    QPointer<QNetworkReply> connectionsReply_;
    QTimer connectionsDeadline_;
    QByteArray connectionsBytes_;
    QVariantList connections_;
    QString connectionsCode_ = "sign_in_required";
    QTcpServer licenseBroker_;
    int brokerRequests_ = 0;
    void authorizeApp(QTcpSocket*, const QJsonObject&);
};

#pragma once
#include <QtQml/qqmlregistration.h>
#include <iiAccountManager>
#include <QTcpServer>
#include <QLocalServer>
#include <iiSocietyHelper>
#include <memory>
#include <functional>
#include <utility>
namespace iiSocietyHelper { class LocalService; }
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
    using BrokerAccountFactory = std::function<AccountController *(QObject *)>;
    void setBrokerAccountFactory(BrokerAccountFactory factory) { brokerAccountFactory_ = std::move(factory); }
    // A nonempty revision identifies the current encrypted account session.
    // An absent or changed revision invalidates the cached broker account.
    using BrokerAccountRevision = std::function<QByteArray(AccountController *)>;
    void setBrokerAccountRevision(BrokerAccountRevision revision) { brokerAccountRevision_ = std::move(revision); }
    void prewarmLocalLicenses();
    bool startLocalLicenseBroker(const QString &directory = {}, bool startHelper = true);
    bool startLocalHelperBroker(const QString &directory = {});
    QString localLicenseBrokerPath() const { return localBroker_.fullServerName(); }
    QString localHelperBrokerPath() const;
  signals:
    void licensesChanged();
    void connectionsChanged();

  private:
    void mountLicenses();
    void stopLocalLicenseBroker();
    void clearConnections();
    bool requestConnections(const QJsonObject &input, bool revoke);
    QPointer<QNetworkReply> connectionsReply_;
    QTimer connectionsDeadline_;
    QByteArray connectionsBytes_;
    QVariantList connections_;
    QString connectionsCode_ = "sign_in_required";
    QTcpServer licenseBroker_;
    int localBrokerLock_ = -1;
    int localRequests_ = 0;
    QLocalServer localBroker_;
    std::unique_ptr<iiSocietyHelper::LocalService> localHelper_;
    int brokerRequests_ = 0;
    void authorizeApp(QTcpSocket*, const QJsonObject&);
    void withBrokerAccount(QObject*, std::function<void(AccountController*)>);
    bool brokerAccountIsCurrent(AccountController*) const;
    BrokerAccountFactory brokerAccountFactory_;
    struct BrokerWaiter { QPointer<QObject> scope; std::function<void(AccountController*)> done; };
    BrokerAccountRevision brokerAccountRevision_;
    QPointer<AccountController> brokerAccount_;
    QByteArray brokerRevision_;
    QList<BrokerWaiter> brokerWaiters_;
    bool brokerRestoring_ = false;
    quint64 brokerGeneration_ = 0;
};

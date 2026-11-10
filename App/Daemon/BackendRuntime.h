#pragma once

#include <QObject>
#include <QUrl>
#include <memory>

class AccountController;
class NetworkDriveController;
class ModelBackendServer;
class SyncOwnership;
class SystemDriveCoordinator;

struct BackendOptions {
    QString directory;
    QString container;
    QString statusFile;
    QString loginFile;
    QUrl accountUrl;
    QUrl server;
    QString brokerDirectory;
    bool sync = false;
    bool localApi = false;
};

// Lives entirely in its own event-loop thread. Container I/O and network
// ownership cannot prevent the independently hosted license broker from
// answering local applications.
class BackendRuntime final : public QObject {
    Q_OBJECT
public:
    explicit BackendRuntime(BackendOptions options);
    ~BackendRuntime() override;
public slots:
    void start();
signals:
    void fatalError(const QString &message);
    void deliveryDirectorySelected(const QString &directory);
    void localApiAvailable(const QString &brokerDirectory);
private:
    void selectRuntime();
    void updateNetwork(bool owned);
    void publishStatus();
    BackendOptions m_options;
    ModelBackendServer *m_models = nullptr;
    SystemDriveCoordinator *m_systemDrive = nullptr;
    std::unique_ptr<AccountController> m_account;
    std::unique_ptr<NetworkDriveController> m_network;
    std::unique_ptr<SyncOwnership> m_ownership;
};

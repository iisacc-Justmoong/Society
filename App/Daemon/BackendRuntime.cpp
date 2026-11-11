#include "BackendRuntime.h"
#include "ModelBackendServer.h"
#include "SystemDriveCoordinator.h"
#include "Society/Account/AccountController.h"
#include "Society/Network/NetworkDriveController.h"
#include "Society/Services/SyncOwnership.h"
#include <iiFileProviderDevice.h>
#include <SharedStorage.h>
#include <QCoreApplication>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <utility>

BackendRuntime::BackendRuntime(BackendOptions options) : m_options(std::move(options)) {}
BackendRuntime::~BackendRuntime()
{
    m_ownership.reset();
    m_network.reset();
    m_account.reset();
}

void BackendRuntime::start()
{
    if (m_options.localApi) {
        m_models = new ModelBackendServer(this);
    }
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN) || (defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID))
    if (m_options.sync && qEnvironmentVariableIntValue("SOCIETY_DISABLE_NATIVE_DRIVE") != 1) {
#if defined(Q_OS_MACOS) && defined(SOCIETY_NATIVE_DRIVE_APP)
        const auto host = QString::fromUtf8(SOCIETY_NATIVE_DRIVE_APP);
        auto request = macSystemDriveRequest(host, this);
#elif defined(SOCIETY_MOUNT_EXECUTABLE)
        auto request = desktopSystemDriveRequest(QString::fromUtf8(SOCIETY_MOUNT_EXECUTABLE), this);
#else
        SystemDriveCoordinator::Request request = [](const QString &, SystemDriveCoordinator::Completion done) {
            done({}, QObject::tr("The Society filesystem host is unavailable."));
        };
#endif
        m_systemDrive = new SystemDriveCoordinator(std::move(request), isMountedSocietyFilesystem, this);
        connect(m_systemDrive, &SystemDriveCoordinator::changed, this, [this] {
            if (m_models) m_models->setSystemDriveState(m_systemDrive->state());
            if (!m_systemDrive->errorString().isEmpty())
                qWarning() << "Society system drive:" << m_systemDrive->errorString();
        });
        if (m_models) connect(m_models, &ModelBackendServer::systemDriveConnectRequested,
                              m_systemDrive, &SystemDriveCoordinator::check);
    }
#endif
    const auto registerMaintenance = [this] {
        iiSocietyContainer::SharedStorage::open(m_options.container, nullptr, true);
    };
    registerMaintenance();
    auto *maintenance = new QTimer(this);
    connect(maintenance, &QTimer::timeout, this, registerMaintenance);
    maintenance->start(5 * 60 * 1000);

    if (m_options.sync) {
        const auto selected = iiSocietyContainer::SharedStorage::open(m_options.container, nullptr, true);
        m_ownership = std::make_unique<SyncOwnership>(selected ? selected->runtimePath(QStringLiteral("sync")) : QString(), false);
        connect(m_ownership.get(), &SyncOwnership::ownershipChanged, this, &BackendRuntime::updateNetwork);
        if (!m_options.directory.isEmpty() && !m_ownership->start())
            qWarning() << "Society background synchronization:" << m_ownership->errorString();
    }
    auto *selection = new QTimer(this);
    connect(selection, &QTimer::timeout, this, &BackendRuntime::selectRuntime);
    selection->start(1000);
    selectRuntime();
    if (m_models) {
        const auto listen = [this] {
            if (m_models->listen(m_options.brokerDirectory))
                emit localApiAvailable(m_options.brokerDirectory);
            else
                qWarning() << "Society backend local channel is unavailable.";
        };
        listen();
        auto *retry = new QTimer(this);
        connect(retry, &QTimer::timeout, this, listen);
        retry->start(3000);
    }
    if (!m_options.statusFile.isEmpty()) {
        auto *status = new QTimer(this);
        connect(status, &QTimer::timeout, this, &BackendRuntime::publishStatus);
        status->start(250);
    }
}

void BackendRuntime::selectRuntime()
{
    const auto selected = iiSocietyContainer::SharedStorage::open(m_options.container, nullptr, true);
    if (m_models) m_models->selectContainer(selected ? selected->drive().rootPath() : QString());
    if (m_systemDrive) m_systemDrive->setSelection(selected ? selected->drive().rootPath() : QString(),
                                                   selected ? selected->drive().identifier() : QString(),
                                                   selected && selected->drive().isReady());
    if (!m_options.directory.isEmpty()) return;
    const auto helper = selected ? selected->runtimePath(QStringLiteral("helpers/v1")) : QString();
    emit deliveryDirectorySelected(helper);
    if (m_ownership) m_ownership->setDirectory(selected ? selected->runtimePath(QStringLiteral("sync")) : QString());
}

void BackendRuntime::updateNetwork(bool owned)
{
    m_network.reset();
    m_account.reset();
    if (!owned) return;
    const auto selected = iiSocietyContainer::SharedStorage::open(m_options.container, nullptr, true);
    const auto path = selected ? selected->drive().rootPath() : m_ownership->storedContainer();
    if (path.isEmpty() || !iiSocietyContainer::SharedStorage::open(path, nullptr, true)) return;
    m_account = m_options.loginFile.isEmpty() ? std::make_unique<AccountController>() : std::make_unique<AccountController>(m_options.accountUrl);
    m_network = std::make_unique<NetworkDriveController>();
    m_network->setContainerPath(path);
    m_network->setAccountSession(m_account.get());
    if (!m_options.server.isEmpty()) {
        m_network->setRelayUrl(m_options.server);
        const auto configured = std::make_shared<bool>(false);
        const auto configure = [this, configured] {
            if (!*configured && m_account && m_network && m_account->signedIn() && !m_account->busy())
                *configured = m_network->configureServer(m_options.server);
        };
        connect(m_account.get(), &AccountController::changed, m_network.get(), configure, Qt::QueuedConnection);
        connect(m_account.get(), &AccountController::pairingStateRestored, m_network.get(), configure, Qt::QueuedConnection);
    }
    if (!m_options.loginFile.isEmpty()) {
        iiFileProvider::FileDevice file(m_options.loginFile);
        const auto forbidden = iiFileProvider::FileDevice::ReadGroup | iiFileProvider::FileDevice::WriteGroup
            | iiFileProvider::FileDevice::ExeGroup | iiFileProvider::FileDevice::ReadOther
            | iiFileProvider::FileDevice::WriteOther | iiFileProvider::FileDevice::ExeOther;
        if (QFileInfo(m_options.loginFile).isSymLink() || !file.open(QIODevice::ReadOnly) || file.size() > 16384 || (file.permissions() & forbidden)) {
            qWarning() << "Society headless account login file is unavailable.";
            return;
        }
        const auto login = QJsonDocument::fromJson(file.readAll()).object();
        if (!m_account->login(login.value("email").toString(), login.value("password").toString()))
            qWarning() << "Society headless account login could not start.";
    }
}

void BackendRuntime::publishStatus()
{
    const QJsonObject state{{"updatedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {"pid", QString::number(QCoreApplication::applicationPid())}, {"ownsNetwork", m_ownership && m_ownership->owned()},
        {"signedIn", m_account && m_account->signedIn()}, {"connected", m_network && m_network->connected()},
        {"containerReady", m_network && m_network->containerReady()}, {"synchronizing", m_network && m_network->synchronizing()},
        {"nearbyDevices", m_network ? m_network->nearbyDevices().size() : 0},
        {"hosting", m_network && m_network->hosting()}, {"serverConfigured", m_network && !m_network->relayUrl().isEmpty()},
        {"codeRequired", m_account && m_account->codeRequired()},
        {"phase", m_network ? m_network->localPeer()->phase() : QString()},
        {"status", m_network ? m_network->synchronizationStatus() : QString()},
        {"systemDrive", m_systemDrive ? m_systemDrive->state() : QJsonObject{}}};
    iiFileProvider::SaveFile file(m_options.statusFile);
    if (file.open(QIODevice::WriteOnly) && file.setPermissions(iiFileProvider::FileDevice::ReadOwner | iiFileProvider::FileDevice::WriteOwner)) {
        file.write(QJsonDocument(state).toJson(QJsonDocument::Compact));
        file.commit();
    }
}

#include <iiFileProviderDevice.h>
#include "DaemonService.h"
#include "Society/Services/SyncOwnership.h"
#include "Society/Services/ContainerRuntimePath.h"
#include "Society/State/GroupSessionStore.h"
#include "Society/Account/AccountController.h"
#include "Society/Network/NetworkDriveController.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QTextStream>
#include <QJsonDocument>
#include <QSqlDatabase>
#include <QSslSocket>
#include <QTimer>
#include <csignal>

namespace { volatile std::sig_atomic_t stopRequested = 0; }

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
#ifdef Q_OS_MACOS
    const auto plugins = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../PlugIns");
    if (QDir(plugins).exists()) QCoreApplication::setLibraryPaths({plugins});
#endif
    QCoreApplication::setApplicationName("SocietyDaemon");
    QCoreApplication::setApplicationVersion(SOCIETY_APP_VERSION);
    QCommandLineParser parser;
    parser.setApplicationDescription("Runs Society collaboration and account-based container synchronization without a window.");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({{"d", "directory"}, "Shared Helper observation directory.", "path"});
    parser.addOption({"exit-after-ms", "Exit cleanly after a diagnostic run (zero keeps running).", "ms", "0"});
    parser.addOption({"sync", "Automatically synchronize the selected Society container while the desktop window is closed."});
    parser.addOption({"container", "Existing Society container on this host or NAS.", "path"});
    parser.addOption({"server", "Trusted Society WebSocket server (wss://).", "url"});
    parser.addOption({"host", "Compatibility option; desktop and NAS devices always use the host role."});
    parser.addOption({"login-file", "Owner-only JSON file containing email and password for a headless account login. Credentials stay in memory.", "path"});
    parser.addOption({"account-url", "Account authority for --login-file.", "url", "https://iisacc.com"});
    parser.addOption({"status-file", "Write credential-free diagnostic runtime state to this file.", "path"});
    parser.addOption({"check-runtime", "Check the packaged SQLite and TLS backends without starting services."});
    parser.process(app);
    if (parser.isSet("check-runtime")) {
        const bool sqlite = QSqlDatabase::drivers().contains("QSQLITE"), tls = QSslSocket::supportsSsl();
        QTextStream(stdout) << QJsonDocument(QJsonObject{{"sqlite", sqlite}, {"tls", tls}, {"tlsBackend", QSslSocket::activeBackend()}}).toJson(QJsonDocument::Compact) << Qt::endl;
        return sqlite && tls ? 0 : 1;
    }
    bool durationOk = false;
    const auto duration = parser.value("exit-after-ms").toInt(&durationOk);
    if (!durationOk || duration < 0) return 2;
    if ((!parser.isSet("sync") && (parser.isSet("container") || parser.isSet("server") || parser.isSet("host") || parser.isSet("login-file")))
        || (parser.isSet("host") && !parser.isSet("server"))
        || (parser.isSet("server") && !NetworkDriveController::validServerUrl(QUrl(parser.value("server"))))) return 2;
    if (parser.isSet("container") && (!QDir::isAbsolutePath(parser.value("container"))
        || !iiSocietyContainer::SharedStorage::open(parser.value("container"), nullptr, true))) return 2;
    if (parser.isSet("container")) qputenv("SOCIETY_CONTAINER_PATH", parser.value("container").toUtf8());
    if ((parser.isSet("directory") && !societyContainerRuntimePath(parser.value("directory")))
        || (parser.isSet("status-file") && !societyContainerRuntimePath(parser.value("status-file")))) return 2;
    const QUrl accountUrl(parser.value("account-url"));
    if (parser.isSet("login-file") && (!accountUrl.isValid() || accountUrl.host().isEmpty() || !accountUrl.userInfo().isEmpty()
        || accountUrl.hasQuery() || accountUrl.hasFragment() || accountUrl.port() == 0
        || (accountUrl.scheme() != "https" && !(accountUrl.scheme() == "http" && QHostAddress(accountUrl.host()).isLoopback())))) return 2;
    const auto readLogin = [&]() {
        iiFileProvider::FileDevice file(parser.value("login-file"));
        if (QFileInfo(file).isSymLink() || !file.open(QIODevice::ReadOnly) || file.size() > 16384
            || (file.permissions() & (iiFileProvider::FileDevice::ReadGroup | iiFileProvider::FileDevice::WriteGroup | iiFileProvider::FileDevice::ExeGroup | iiFileProvider::FileDevice::ReadOther | iiFileProvider::FileDevice::WriteOther | iiFileProvider::FileDevice::ExeOther))) return QJsonObject{};
        return QJsonDocument::fromJson(file.readAll()).object();
    };
    if (parser.isSet("login-file")) {
        const auto login = readLogin();
        if (login.value("email").toString().isEmpty() || login.value("password").toString().isEmpty()) return 2;
    }
    // Maintenance stays available while the window is closed and while no
    // account/network owner is active. SharedStorage rejects saved images.
    const auto registerMaintenance = [&parser] {
        iiSocietyContainer::SharedStorage::open(
            parser.isSet("container") ? parser.value("container") : QString(), nullptr, true);
    };
    registerMaintenance();
    QTimer maintenanceSelection;
    QObject::connect(&maintenanceSelection, &QTimer::timeout, &app, registerMaintenance);
    maintenanceSelection.start(5 * 60 * 1000);
    SocietyDaemonService service;
    if (parser.isSet("directory") && !service.start(parser.value("directory"))) {
        QTextStream(stderr) << service.errorString() << Qt::endl;
        return 1;
    }
    std::unique_ptr<AccountController> account;
    std::unique_ptr<NetworkDriveController> network;
    std::unique_ptr<SyncOwnership> ownership;
    if (parser.isSet("sync")) {
        const auto selectedStorage = iiSocietyContainer::SharedStorage::open({}, nullptr, true);
        const auto directory = selectedStorage ? selectedStorage->runtimePath(QStringLiteral("sync")) : QString();
        ownership = std::make_unique<SyncOwnership>(directory, false);
        QObject::connect(ownership.get(), &SyncOwnership::ownershipChanged, &app, [&](bool owned) {
            network.reset(); account.reset();
            if (!owned) return;
            const auto selected = iiSocietyContainer::SharedStorage::open(
                parser.isSet("container") ? parser.value("container") : QString(), nullptr, true);
            const auto path = selected ? selected->drive().rootPath() : ownership->storedContainer();
            if (path.isEmpty() || !iiSocietyContainer::SharedStorage::open(path, nullptr, true)) return;
            account = parser.isSet("login-file") ? std::make_unique<AccountController>(accountUrl) : std::make_unique<AccountController>();
            network = std::make_unique<NetworkDriveController>();
            network->setContainerPath(path); network->setAccountSession(account.get());
            if (parser.isSet("server")) {
                // Select the server path before login so no LAN discovery is
                // started while the account profile is arriving.
                network->setRelayUrl(QUrl(parser.value("server")));
                const auto configured = std::make_shared<bool>(false);
                const auto configure = [&, configured] {
                    if (!*configured && account->signedIn() && !account->busy())
                        *configured = network->configureServer(QUrl(parser.value("server")));
                };
                QObject::connect(account.get(), &AccountController::changed, network.get(), configure, Qt::QueuedConnection);
                QObject::connect(account.get(), &AccountController::pairingStateRestored, network.get(), configure, Qt::QueuedConnection);
            }
            if (parser.isSet("login-file")) {
                const auto login = readLogin();
                if (!account->login(login.value("email").toString(), login.value("password").toString()))
                    qWarning() << "Society headless account login could not start.";
            }
        });
        if (parser.isSet("directory") && !ownership->start()) qWarning() << "Society background synchronization:" << ownership->errorString();
        QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [&] { ownership->stop(); });
    }
    const auto selectRuntime = [&] {
        if (parser.isSet("directory")) return;
        const auto selected = iiSocietyContainer::SharedStorage::open(
            parser.isSet("container") ? parser.value("container") : QString(), nullptr, true);
        const auto helperDirectory = selected ? selected->runtimePath(QStringLiteral("helpers/v1")) : QString();
        if (service.isRunning() && service.directory() != helperDirectory) service.stop();
        if (!service.isRunning() && !helperDirectory.isEmpty()) service.start(helperDirectory);
        if (ownership) ownership->setDirectory(selected ? selected->runtimePath(QStringLiteral("sync")) : QString());
    };
    QTimer runtimeSelection;
    QObject::connect(&runtimeSelection, &QTimer::timeout, &app, selectRuntime);
    runtimeSelection.start(1000);
    selectRuntime();
    QTimer status;
    if (parser.isSet("status-file")) {
        QObject::connect(&status, &QTimer::timeout, &app, [&] {
            const QJsonObject state{{"updatedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                {"pid", QString::number(QCoreApplication::applicationPid())}, {"ownsNetwork", ownership && ownership->owned()},
                {"signedIn", account && account->signedIn()}, {"connected", network && network->connected()},
                {"containerReady", network && network->containerReady()}, {"synchronizing", network && network->synchronizing()},
                {"nearbyDevices", network ? network->nearbyDevices().size() : 0},
                {"hosting", network && network->hosting()}, {"serverConfigured", network && !network->relayUrl().isEmpty()},
                {"codeRequired", account && account->codeRequired()},
                {"phase", network ? network->localPeer()->phase() : QString()},
                {"status", network ? network->synchronizationStatus() : QString()}};
            iiFileProvider::SaveFile file(parser.value("status-file"));
            if (file.open(QIODevice::WriteOnly) && file.setPermissions(iiFileProvider::FileDevice::ReadOwner | iiFileProvider::FileDevice::WriteOwner)) {
                file.write(QJsonDocument(state).toJson(QJsonDocument::Compact)); file.commit();
            }
        });
        status.start(250);
    }
#ifdef Q_OS_UNIX
    // Signal handlers only set a flag; Qt cleanup runs safely on the event loop.
    std::signal(SIGTERM, [](int) { stopRequested = 1; });
    std::signal(SIGINT, [](int) { stopRequested = 1; });
    QTimer terminationPoll;
    QObject::connect(&terminationPoll, &QTimer::timeout, &app, [&app] { if (stopRequested) app.quit(); });
    terminationPoll.start(200);
#endif
    if (duration > 0) QTimer::singleShot(duration, &app, &QCoreApplication::quit);
    return app.exec();
}

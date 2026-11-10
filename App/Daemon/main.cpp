#include <iiFileProviderDevice.h>
#include "DaemonService.h"
#include "BackendRuntime.h"
#ifdef SOCIETY_DAEMON_MCP
#include "DaemonMcp.h"
#endif
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
#include <QThread>
#include <QStandardPaths>
#include <csignal>
#include <functional>

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
    // Keep existing desktop job history when ownership moves from the window
    // to this executable. Isolated tests may provide their own explicit root.
    if (qEnvironmentVariable("SOCIETY_JOB_STATE_ROOT").isEmpty()) {
        const auto oldName = QCoreApplication::applicationName();
        const auto oldOrganization = QCoreApplication::organizationName();
        QCoreApplication::setApplicationName(QStringLiteral("Society"));
        QCoreApplication::setOrganizationName(QStringLiteral("iisacc"));
        const auto jobRoot = QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath("jobs");
        QCoreApplication::setApplicationName(oldName);
        QCoreApplication::setOrganizationName(oldOrganization);
        if (QDir::isAbsolutePath(jobRoot)) qputenv("SOCIETY_JOB_STATE_ROOT", jobRoot.toUtf8());
    }
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
    parser.addOption({"license-broker", "Start the headless local license broker on demand."});
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
    if (parser.isSet("license-broker") && (parser.isSet("sync") || parser.isSet("directory") || parser.isSet("status-file"))) return 2;
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
    // Local licensing remains responsive even when the selected storage is
    // disconnected: backend I/O runs in a separate event-loop thread.
    std::unique_ptr<AccountController> licenseBroker;
    QTimer licenseBrokerRetry;
    std::function<void()> startBackend;
#if defined(Q_OS_UNIX)
    bool localBrokerReady = false;
    const bool isolatedTest = qEnvironmentVariableIsSet("SOCIETY_TEST_OUTPUT_DIR");
    if (!isolatedTest || qEnvironmentVariableIsSet("SOCIETY_LOCAL_BROKER_DIR")) {
        const QUrl origin(qEnvironmentVariable("SOCIETY_ACCOUNT_URL", "https://iisacc.com"));
        licenseBroker = std::make_unique<AccountController>(origin);
        licenseBroker->setBrokerAccountFactory([origin](QObject *scope) {
            return new AccountController(origin, GroupSessionStore::create(scope), scope);
        });
        const auto startBrokers = [&] {
            localBrokerReady = licenseBroker->startLocalLicenseBroker({}, false);
            const bool legacy = isolatedTest || parser.isSet("license-broker") || licenseBroker->startLicenseBroker();
            if (localBrokerReady && startBackend) startBackend();
            if (localBrokerReady && legacy) licenseBrokerRetry.stop();
        };
        QObject::connect(&licenseBrokerRetry, &QTimer::timeout, &app, startBrokers);
        licenseBrokerRetry.start(3000);
        startBrokers();
    }
    if (parser.isSet("license-broker") && !localBrokerReady) return 1;
    std::signal(SIGTERM, [](int) { stopRequested = 1; });
    std::signal(SIGINT, [](int) { stopRequested = 1; });
    QTimer terminationPoll;
    QObject::connect(&terminationPoll, &QTimer::timeout, &app, [&app] { if (stopRequested) app.quit(); });
    terminationPoll.start(200);
#else
    if (parser.isSet("license-broker")) return 1;
#endif
    if (duration > 0) QTimer::singleShot(duration, &app, &QCoreApplication::quit);
    BackendOptions options;
    options.directory = parser.value("directory");
    options.container = parser.value("container");
    options.statusFile = parser.value("status-file");
    options.loginFile = parser.value("login-file");
    options.accountUrl = accountUrl;
    options.server = QUrl(parser.value("server"));
    options.sync = parser.isSet("sync") || parser.isSet("license-broker");
    options.brokerDirectory = qEnvironmentVariable("SOCIETY_LOCAL_BROKER_DIR", QDir::homePath() + "/.iisacc-society-ipc-v1");

    QThread backendThread;
    SocietyDaemonService delivery;
    BackendRuntime *backend = nullptr;
#ifdef SOCIETY_DAEMON_MCP
    std::unique_ptr<DaemonMcp> daemonMcp;
#endif
    bool backendStartupFailed = false;
    startBackend = [&] {
        if (backend || backendThread.isRunning()) return;
        if (!options.directory.isEmpty() && !delivery.start(options.directory)) {
            QTextStream(stderr) << delivery.errorString() << Qt::endl;
            backendStartupFailed = true;
            app.exit(1);
            return;
        }
        auto runtimeOptions = options;
#if defined(Q_OS_UNIX)
        runtimeOptions.localApi = localBrokerReady;
#elif defined(Q_OS_WIN)
        runtimeOptions.localApi = true;
#endif
        backend = new BackendRuntime(runtimeOptions);
        backend->moveToThread(&backendThread);
#ifdef SOCIETY_DAEMON_MCP
        QObject::connect(backend, &BackendRuntime::localApiAvailable, &app, [&](const QString &directory) {
            if (qEnvironmentVariable("IILOCALLLM_DISABLE_APP_MCP") == "1") return;
            if (qEnvironmentVariableIsSet("SOCIETY_TEST_OUTPUT_DIR")
                && qEnvironmentVariable("SOCIETY_TEST_ENABLE_DAEMON_MCP") != "1") return;
            if (!daemonMcp) daemonMcp = std::make_unique<DaemonMcp>(directory);
            if (!daemonMcp->start()) qWarning() << "Society daemon MCP:" << daemonMcp->errorString();
        });
#endif
        QObject::connect(backend, &BackendRuntime::deliveryDirectorySelected, &delivery, [&delivery](const QString &directory) {
            // iiSocietyHelper requires its observer on the application thread.
            if (delivery.isRunning() && delivery.directory() != directory) delivery.stop();
            if (!delivery.isRunning() && !directory.isEmpty()) delivery.start(directory);
        });
        QObject::connect(&backendThread, &QThread::started, backend, &BackendRuntime::start);
        QObject::connect(backend, &BackendRuntime::fatalError, &app, [&app](const QString &message) {
            QTextStream(stderr) << message << Qt::endl;
            app.exit(1);
        });
        QObject::connect(&backendThread, &QThread::finished, backend, &QObject::deleteLater);
        backendThread.start();
    };
#if defined(Q_OS_UNIX)
    // Unscoped desktop instances share the broker lease. If launchd starts
    // while the on-demand instance is alive it waits; it takes over only after
    // that instance releases its broker. Explicit headless configurations keep
    // their existing independent container-scoped ownership locks.
    const bool scoped = parser.isSet("directory") || parser.isSet("container")
        || parser.isSet("server") || parser.isSet("login-file");
    const bool brokerEnabled = !isolatedTest || qEnvironmentVariableIsSet("SOCIETY_LOCAL_BROKER_DIR");
    if (scoped || !brokerEnabled || localBrokerReady) startBackend();
#else
    startBackend();
#endif
    if (backendStartupFailed) return 1;
    const int result = app.exec();
#ifdef SOCIETY_DAEMON_MCP
    // MCP requests may be waiting on the backend socket. Close the endpoint
    // while its event loop can still finish those requests.
    if (daemonMcp) daemonMcp->stop();
#endif
    if (backendThread.isRunning()) {
        backendThread.quit();
        backendThread.wait();
    }
    return result;
}

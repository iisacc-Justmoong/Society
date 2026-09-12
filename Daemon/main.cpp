#include "DaemonService.h"
#include "App/Services/SyncOwnership.h"
#include "App/State/GroupSessionStore.h"
#include "App/Account/AccountController.h"
#include "App/Network/NetworkDriveController.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QTextStream>
#include <QJsonDocument>
#include <QSaveFile>
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
    parser.setApplicationDescription("Receives iisacc Helper data independently of the Society window.");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({{"d", "directory"}, "Shared Helper observation directory.", "path"});
    parser.addOption({"exit-after-ms", "Exit cleanly after a diagnostic run (zero keeps running).", "ms", "0"});
    parser.addOption({"sync", "Automatically synchronize the selected Society container while the desktop window is closed."});
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
    SocietyDaemonService service;
    if (!service.start(parser.value("directory"))) {
        QTextStream(stderr) << service.errorString() << Qt::endl;
        return 1;
    }
    std::unique_ptr<AccountController> account;
    std::unique_ptr<NetworkDriveController> network;
    std::unique_ptr<SyncOwnership> ownership;
    if (parser.isSet("sync")) {
        const auto directory = parser.isSet("directory") ? parser.value("directory") : GroupSessionStore::defaultDirectory();
        ownership = std::make_unique<SyncOwnership>(directory.isEmpty() ? QString() : QDir(directory).filePath("SyncRuntime"), false);
        QObject::connect(ownership.get(), &SyncOwnership::ownershipChanged, &app, [&](bool owned) {
            network.reset(); account.reset();
            if (!owned) return;
            const auto path = ownership->storedContainer();
            if (path.isEmpty() || !iiSocietyContainer::SharedStorage::open(path, nullptr, true)) return;
            account = std::make_unique<AccountController>();
            network = std::make_unique<NetworkDriveController>();
            network->setContainerPath(path); network->setAccountSession(account.get());
        });
        if (!ownership->start()) qWarning() << "Society background synchronization:" << ownership->errorString();
        QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [&] { ownership->stop(); });
    }
    QTimer status;
    if (parser.isSet("status-file")) {
        QObject::connect(&status, &QTimer::timeout, &app, [&] {
            const QJsonObject state{{"updatedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                {"pid", QString::number(QCoreApplication::applicationPid())}, {"ownsNetwork", ownership && ownership->owned()},
                {"signedIn", account && account->signedIn()}, {"connected", network && network->connected()},
                {"containerReady", network && network->containerReady()}, {"synchronizing", network && network->synchronizing()},
                {"nearbyDevices", network ? network->nearbyDevices().size() : 0},
                {"phase", network ? network->localPeer()->phase() : QString()},
                {"status", network ? network->synchronizationStatus() : QString()}};
            QSaveFile file(parser.value("status-file"));
            if (file.open(QIODevice::WriteOnly) && file.setPermissions(QFile::ReadOwner | QFile::WriteOwner)) {
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

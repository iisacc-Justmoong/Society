#include "MobileNetworkDevice.h"
#include "AuthenticatedNetworkFixture.h"
#include "Society/Network/PairingCredentials.h"
#include "Society/Network/NetworkDriveController.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTest>
#include <QMetaProperty>

using namespace iiWebServer;
class NetworkDriveTests : public QObject {
    Q_OBJECT
private slots:
    void downloadsRequireSelectedContainerFiles_data() {
        QTest::addColumn<QString>("location");
        for (const auto &location : {"no-container", "external-file", "external-folder", "other-container",
                 "package-root", "models", "runtime", "files-root", "files-relative", "non-local", "existing-file"})
            QTest::newRow(location) << QString::fromLatin1(location);
#ifdef Q_OS_UNIX
        QTest::newRow("redirected-file") << QStringLiteral("redirected-file");
        QTest::newRow("redirected-directory") << QStringLiteral("redirected-directory");
#endif
    }
    void downloadsRequireSelectedContainerFiles() {
        QFETCH(QString, location);
        QTemporaryDir container(SOCIETY_TEST_DIRECTORY "/network-boundary-XXXXXX.societycontainer");
        QTemporaryDir other(SOCIETY_TEST_DIRECTORY "/network-other-XXXXXX.societycontainer");
        QTemporaryDir outside(SOCIETY_TEST_DIRECTORY "/network-outside-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(container.path()));
        QVERIFY(iiSocietyContainer::SocietyDrive::create(other.path()));
        QFile original(outside.filePath("original.txt")); QVERIFY(original.open(QIODevice::WriteOnly));
        original.write("original outside content"); original.close();
        QFile existing(container.filePath("Files/existing.txt")); QVERIFY(existing.open(QIODevice::WriteOnly));
        existing.write("existing user file"); existing.close();
        QUrl destination;
        if (location == "external-file") destination = QUrl::fromLocalFile(original.fileName());
        else if (location == "external-folder") destination = QUrl::fromLocalFile(outside.filePath("new/folder/file.txt"));
        else if (location == "other-container") destination = QUrl::fromLocalFile(other.filePath("Files/file.txt"));
        else if (location == "package-root") destination = QUrl::fromLocalFile(container.filePath("file.txt"));
        else if (location == "models") destination = QUrl::fromLocalFile(container.filePath("Models/file.txt"));
        else if (location == "runtime") destination = QUrl::fromLocalFile(container.filePath(".society-runtime/file.txt"));
        else if (location == "files-root") destination = QUrl::fromLocalFile(container.filePath("Files"));
        else if (location == "files-relative") destination = QUrl::fromLocalFile("file.txt");
        else if (location == "non-local") destination = QUrl("https://example.invalid/file.txt");
        else if (location == "existing-file") destination = QUrl::fromLocalFile(existing.fileName());
#ifdef Q_OS_UNIX
        else if (location == "redirected-file") {
            QVERIFY(QFile::link(original.fileName(), container.filePath("Files/link.txt")));
            destination = QUrl::fromLocalFile(container.filePath("Files/link.txt"));
        } else if (location == "redirected-directory") {
            QVERIFY(QFile::link(outside.path(), container.filePath("Files/link")));
            destination = QUrl::fromLocalFile(container.filePath("Files/link/file.txt"));
        }
#endif
        else destination = QUrl::fromLocalFile(container.filePath("Files/file.txt"));
        MobileNetworkDevice client;
        if (location != "no-container") client.setContainerPath(container.path());
        QSignalSpy saved(&client, &NetworkDriveController::downloadFinished);
        client.download("remote.txt", destination);
        QVERIFY(!client.busy()); QCOMPARE(saved.size(), 0);
        QCOMPARE(client.status(), location == "no-container"
            ? QString("Open a Society container before saving files.")
            : location == "existing-file" ? QString("Downloaded files cannot replace an existing file.")
            : QString("Save downloaded files inside this container's Files."));
        QVERIFY(original.open(QIODevice::ReadOnly)); QCOMPARE(original.readAll(), QByteArray("original outside content"));
        QVERIFY(existing.open(QIODevice::ReadOnly)); QCOMPARE(existing.readAll(), QByteArray("existing user file"));
        QVERIFY(!QFileInfo::exists(outside.filePath("new")));
        QVERIFY(!QFileInfo::exists(outside.filePath("file.txt")));
        QVERIFY(!QFileInfo::exists(other.filePath("Files/file.txt")));
        QVERIFY(!QFileInfo::exists(container.filePath("file.txt")));
        QVERIFY(!QFileInfo::exists(container.filePath("Models/file.txt")));
        QVERIFY(!QFileInfo::exists(container.filePath(".society-runtime/file.txt")));
        QVERIFY(!QFileInfo::exists(container.filePath("Files/file.txt")));
    }
    void photosUsesSdkAndKeepsLegacyDisableSwitch() {
        NetworkDriveController network;
        auto *photos = qobject_cast<iiPhotoLibrary::PhotoController *>(network.photos());
        QVERIFY(photos);
        QCOMPARE(photos->parent(), &network);
        QTemporaryDir root(SOCIETY_TEST_DIRECTORY "/photo-sdk-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(root.path()));
        const auto previous = qgetenv("SOCIETY_DISABLE_PHOTOS");
        qputenv("SOCIETY_DISABLE_PHOTOS", "1");
        network.setContainerPath(root.path());
        network.disconnectSession();
        const bool active = photos->active();
        if (previous.isNull()) qunsetenv("SOCIETY_DISABLE_PHOTOS"); else qputenv("SOCIETY_DISABLE_PHOTOS", previous);
        QVERIFY(!active);
        QVERIFY(!photos->handle("untrusted-peer", {{"op", "society.photos"}}).value("ok").toBool());
    }
    void manualPairingDoesNotAuthorizeRemoteImageGeneration() {
        QTemporaryDir root(SOCIETY_TEST_DIRECTORY "/network-local-only-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(root.path()));
        AuthenticatedNetworkFixture identity; QVERIFY(identity.start(root.path(), true));
        NetworkDriveController society(nullptr, QHostAddress::LocalHost);
        society.setContainerPath(root.path()); society.setAccountSession(identity.hostAccount.get());
        QVERIFY(society.startLocalHost());
        LanPeer client;
        const auto proof = identity.clientAccount->pairingCredentials();
        client.setPairingAuthentication([proof](const auto &context) { return SocietyPairingCredentials::sign(proof, context); },
            [proof](const auto &context, const auto &signature) { return SocietyPairingCredentials::verify(proof, context, signature); });
        QVERIFY(client.join(society.localPeer()->createOffer(), QString(64, 'b'), "Society test client"));
        QTRY_VERIFY2(client.connected(), qPrintable(client.errorString()));
        QSignalSpy replies(&client, &LanPeer::completed);
        // The host identity is carried by the established local connection.
        const auto peers = client.peers();
        QVERIFY(!peers.isEmpty());
        const auto id = client.request(peers.first().toObject().value("peerId").toString(),
                                      {{"op", "society.generation"}, {"schema", 1}, {"action", "submit"}});
        QVERIFY(!id.isEmpty());
        QTRY_VERIFY(!replies.isEmpty());
        QVERIFY(!replies.first()[1].toJsonObject().value("ok").toBool());
        QCOMPARE(replies.first()[1].toJsonObject().value("error").toString(), QString("generation_host_not_authorized"));
        client.stop();
        society.disconnectSession();
    }
    void desktopRoleIsFixedAndHostsWithoutASetting_data() {
        QTest::addColumn<bool>("local");
        QTest::newRow("local") << true;
        QTest::newRow("remote") << false;
    }
    void desktopRoleIsFixedAndHostsWithoutASetting() {
        QFETCH(bool, local);
        QTemporaryDir root(SOCIETY_TEST_DIRECTORY "/network-role-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(root.path()));
        QFile source(root.filePath("Files/shared.txt")); QVERIFY(source.open(QIODevice::WriteOnly));
        source.write("shared"); source.close();
        AuthenticatedNetworkFixture identity; QVERIFY(identity.start(root.path()));
        RelayServer relay([&](const auto &, AuthCompletion done) { done({identity.principal(), QDateTime::currentDateTimeUtc().addSecs(60)}); });
        QVERIFY(relay.listen(iiWebServer::Address::loopback()));
        PeerOptions o; o.relayUrl = QUrl(QString("ws://127.0.0.1:%1").arg(relay.port()));
        o.credential = "alice"; o.peerId = QString(64, 'a'); o.name = "Desktop";
        o.localEnabled = local; o.localHostingEnabled = local;
        o.hostFiles = false; // Caller options cannot demote the desktop role.
        o.listenAddress = iiWebServer::Address::loopback();
        NetworkDriveController desktop; MobileNetworkDevice client;
        desktop.setAccountSession(identity.hostAccount.get()); client.setAccountSession(identity.clientAccount.get());
        QCOMPARE(desktop.mode(), NetworkDriveController::HostMode);
        const auto role = desktop.metaObject()->property(desktop.metaObject()->indexOfProperty("mode"));
        QVERIFY(role.isConstant()); QVERIFY(!role.isWritable());
        QVERIFY(!desktop.setProperty("mode", NetworkDriveController::ClientMode));
        QCOMPARE(desktop.mode(), NetworkDriveController::HostMode);
        desktop.setContainerPath(root.path()); QVERIFY(desktop.startSession(o));
        o.peerId = QString(64, 'b'); QVERIFY(client.startSession(o));
        QTRY_VERIFY(desktop.hosting() && client.connected());
        QTRY_COMPARE(client.hosts().size(), 1);
        client.browse(QString(64, 'a')); QTRY_VERIFY(!client.busy());
        QCOMPARE(client.entries().size(), 1);
        QCOMPARE(client.transport(), local ? "local" : "remote");
        desktop.setContainerPath(root.filePath("missing"));
        QVERIFY(!desktop.connected()); QVERIFY(!desktop.hosting());
        QCOMPARE(desktop.mode(), NetworkDriveController::HostMode);
        desktop.setContainerPath(root.path()); QTRY_VERIFY(desktop.hosting());
        desktop.disconnectSession();
        QVERIFY(!desktop.connected()); QCOMPARE(desktop.mode(), NetworkDriveController::HostMode);
    }
    void changedSourceDoesNotPublishDownload() {
        QTemporaryDir root(SOCIETY_TEST_DIRECTORY "/network-cancel-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(root.path()));
        QFile source(root.filePath("Files/source")); QVERIFY(source.open(QIODevice::WriteOnly));
        source.write(QByteArray(700000, 'a')); source.close();
        QFile destination(root.filePath("Files/destination")); QVERIFY(destination.open(QIODevice::WriteOnly));
        destination.write("original"); destination.close();
        const auto target = root.filePath("Files/downloaded");
        AuthenticatedNetworkFixture identity; QVERIFY(identity.start());
        RelayServer relay([&](const auto &, AuthCompletion done) { done({identity.principal(), QDateTime::currentDateTimeUtc().addSecs(60)}); });
        QVERIFY(relay.listen(iiWebServer::Address::loopback()));
        PeerOptions o; o.relayUrl = QUrl(QString("ws://127.0.0.1:%1").arg(relay.port()));
        o.credential = "alice"; o.peerId = QString(64, 'a'); o.name = "host"; o.localEnabled = false; o.service = "com.iisacc.society.files";
        Peer host; FileShare files(root.filePath("Files")); int reads = 0;
        QVERIFY(host.start(o, [&](const auto &, const QJsonObject &request) {
            if (request.value("op") == "read" && ++reads == 2) {
                source.open(QIODevice::Append); source.write("changed"); source.close();
            }
            return files.handle(request);
        }));
        MobileNetworkDevice client; client.setContainerPath(root.path()); client.setAccountSession(identity.clientAccount.get()); o.peerId = QString(64, 'b'); QVERIFY(client.startSession(o));
        QTRY_VERIFY(host.isReady() && client.connected()); QTRY_COMPARE(client.hosts().size(), 1);
        client.browse(QString(64, 'a')); QTRY_VERIFY(!client.busy());
        QSignalSpy saved(&client, &NetworkDriveController::downloadFinished);
        client.download("source", QUrl::fromLocalFile(target)); QTRY_VERIFY(!client.busy());
        QCOMPARE(reads, 2); QCOMPARE(saved.size(), 0);
        QCOMPARE(client.status(), "file_changed");
        QVERIFY(!QFileInfo::exists(target));
        QVERIFY(destination.open(QIODevice::ReadOnly)); QCOMPARE(destination.readAll(), QByteArray("original"));
    }
    void containerChangeAndDisconnectCancelDownload_data() {
        QTest::addColumn<bool>("changeContainer");
        QTest::newRow("disconnect") << false;
        QTest::newRow("change-container") << true;
    }
    void containerChangeAndDisconnectCancelDownload() {
        QFETCH(bool, changeContainer);
        QTemporaryDir root(SOCIETY_TEST_DIRECTORY "/network-mode-cancel-XXXXXX.societycontainer");
        QTemporaryDir next(SOCIETY_TEST_DIRECTORY "/network-next-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(root.path()));
        QVERIFY(iiSocietyContainer::SocietyDrive::create(next.path()));
        QFile source(root.filePath("Files/source")); QVERIFY(source.open(QIODevice::WriteOnly));
        source.write(QByteArray(700000, 'a')); source.close();
        QFile destination(root.filePath("Files/destination")); QVERIFY(destination.open(QIODevice::WriteOnly));
        destination.write("original"); destination.close();
        const auto target = root.filePath("Files/downloaded");
        AuthenticatedNetworkFixture identity; QVERIFY(identity.start(root.path()));
        RelayServer relay([&](const auto &, AuthCompletion done) { done({identity.principal(), QDateTime::currentDateTimeUtc().addSecs(60)}); });
        QVERIFY(relay.listen(iiWebServer::Address::loopback()));
        PeerOptions o; o.relayUrl = QUrl(QString("ws://127.0.0.1:%1").arg(relay.port()));
        o.credential = "alice"; o.peerId = QString(64, 'a'); o.name = "host"; o.localEnabled = false;
        o.service = "com.iisacc.society.files";
        MobileNetworkDevice client; client.setAccountSession(identity.clientAccount.get()); client.setContainerPath(root.path());
        Peer host; FileShare files(root.filePath("Files")); int reads = 0;
        QVERIFY(host.start(o, [&](const auto &, const QJsonObject &request) {
            if (request.value("op") == "read" && ++reads == 2) {
                if (changeContainer) client.setContainerPath(next.path());
                else client.disconnectSession();
            }
            return files.handle(request);
        }));
        o.peerId = QString(64, 'b'); QVERIFY(client.startSession(o));
        QTRY_VERIFY(host.isReady() && client.connected()); QTRY_COMPARE(client.hosts().size(), 1);
        client.browse(QString(64, 'a')); QTRY_VERIFY(!client.busy());
        QSignalSpy saved(&client, &NetworkDriveController::downloadFinished);
        client.download("source", QUrl::fromLocalFile(target));
        QCOMPARE(client.mode(), NetworkDriveController::ClientMode);
        QTRY_COMPARE(reads, 2); QTRY_VERIFY(!client.busy());
        if (changeContainer) { QVERIFY(client.connected()); QCOMPARE(client.containerPath(), next.path()); }
        else QVERIFY(!client.connected());
        QCOMPARE(reads, 2); QCOMPARE(saved.size(), 0);
        QVERIFY(!QFileInfo::exists(target));
        QVERIFY(!QFileInfo::exists(next.filePath("Files/downloaded")));
        QVERIFY(destination.open(QIODevice::ReadOnly)); QCOMPARE(destination.readAll(), QByteArray("original"));
    }
    void societyFilesAcrossLocalAndRemote() {
        for (bool local : {true, false}) {
            AuthenticatedNetworkFixture identity;
            RelayServer relay([&](const auto &credential, AuthCompletion done) { done({credential == "alice" ? identity.principal() : "bob", QDateTime::currentDateTimeUtc().addSecs(60)}); });
            QVERIFY(relay.listen(iiWebServer::Address::loopback()));
            QTemporaryDir root(SOCIETY_TEST_DIRECTORY "/network-drive-XXXXXX.societycontainer"), destination(SOCIETY_TEST_DIRECTORY "/network-download-XXXXXX.societycontainer");
            QVERIFY(iiSocietyContainer::SocietyDrive::create(root.path()));
            QVERIFY(iiSocietyContainer::SocietyDrive::create(destination.path()));
            QVERIFY(identity.start(root.path()));
            const QByteArray bytes(700000, 's'); QFile source(root.filePath("Files/sample.bin"));
            QVERIFY(source.open(QIODevice::WriteOnly)); QCOMPARE(source.write(bytes), bytes.size()); source.close();
            QFile privateFile(root.filePath("Models/private.bin")); QVERIFY(privateFile.open(QIODevice::WriteOnly)); privateFile.write("private"); privateFile.close();
            PeerOptions o; o.relayUrl = QUrl(QString("ws://127.0.0.1:%1").arg(relay.port()));
            o.credential = "alice"; o.peerId = QString(64, 'a'); o.name = "My Society";
            o.localEnabled = local; o.listenAddress = iiWebServer::Address::loopback();
            NetworkDriveController host; MobileNetworkDevice client;
            client.setContainerPath(destination.path());
            host.setAccountSession(identity.hostAccount.get()); client.setAccountSession(identity.clientAccount.get());
            host.setContainerPath(root.path()); QVERIFY(host.startSession(o));
            o.peerId = QString(64, 'b'); QVERIFY(client.startSession(o));
            QTRY_VERIFY(host.connected() && client.connected());
            QTRY_COMPARE(client.hosts().size(), 1);
            QCOMPARE(client.hosts()[0].toMap()["metadata"].toMap()["section"].toString(), "files");
            client.browse(QString(64, 'a')); QTRY_VERIFY(!client.busy());
            QCOMPARE(client.entries().size(), 1); // Only the user-created sample.bin.
            QStringList names;
            for (const auto &entry : client.entries()) names.append(entry.toMap().value("name").toString());
            names.sort();
            QCOMPARE(names, (QStringList{"sample.bin"}));
            QCOMPARE(client.transport(), local ? "local" : "remote");
            QTRY_VERIFY_WITH_TIMEOUT(client.containerReady(), 15000);
            QSignalSpy saved(&client, &NetworkDriveController::downloadFinished);
            const auto target = destination.filePath("Files/sample.bin");
            client.download("sample.bin", QUrl::fromLocalFile(target)); QTRY_COMPARE(saved.size(), 1);
            QFile copy(target); QVERIFY(copy.open(QIODevice::ReadOnly)); QCOMPARE(copy.readAll(), bytes);
            client.download("sample.bin", QUrl::fromLocalFile(destination.filePath("outside/new.bin")));
            QVERIFY(!client.busy()); QCOMPARE(saved.size(), 1);
            QVERIFY(!QFileInfo::exists(destination.filePath("outside")));
            client.downloadToFiles("sample.bin"); QTRY_COMPARE(saved.size(), 2);
            const auto savedPath = saved.last().first().toUrl().toLocalFile();
            QVERIFY(savedPath.startsWith(destination.filePath("Files/")));
            QCOMPARE(savedPath, destination.filePath("Files/sample (2).bin")); // The explicitly downloaded original is preserved.
            QFile extra(savedPath); QVERIFY(extra.open(QIODevice::ReadOnly)); QCOMPARE(extra.readAll(), bytes);
            QFile original(target); QVERIFY(original.open(QIODevice::ReadOnly)); QCOMPARE(original.readAll(), bytes);
            client.browse(QString(64, 'a'), "../Models"); QTRY_VERIFY(!client.busy());
            QVERIFY(client.entries().isEmpty()); QVERIFY(client.status().contains("invalid"));
            host.disconnectSession(); QTRY_VERIFY(client.hosts().isEmpty());
        }
    }
    void replacedContainerAndLogoutStopAccess() {
        QTemporaryDir root(SOCIETY_TEST_DIRECTORY "/network-identity-XXXXXX.societycontainer");
        const auto drive = iiSocietyContainer::SocietyDrive::create(root.path()); QVERIFY(drive);
        AuthenticatedNetworkFixture identity; QVERIFY(identity.start(root.path()));
        RelayServer relay([&](const auto &, AuthCompletion done) { done({identity.principal(), QDateTime::currentDateTimeUtc().addSecs(60)}); });
        QVERIFY(relay.listen(iiWebServer::Address::loopback()));
        PeerOptions o; o.relayUrl = QUrl(QString("ws://127.0.0.1:%1").arg(relay.port()));
        o.credential = "alice"; o.peerId = QString(64, 'a'); o.name = "host"; o.localEnabled = false;
        NetworkDriveController host; MobileNetworkDevice client;
        host.setAccountSession(identity.hostAccount.get()); client.setAccountSession(identity.clientAccount.get());
        host.setContainerPath(root.path()); QVERIFY(host.startSession(o)); o.peerId = QString(64, 'b'); QVERIFY(client.startSession(o));
        QTRY_VERIFY(host.connected() && client.connected()); QTRY_COMPARE(client.hosts().size(), 1);
        QVERIFY(QDir(root.path()).rename("Files", "Files-old")); QVERIFY(QDir(root.path()).mkdir("Files"));
        client.browse(QString(64, 'a')); QTRY_VERIFY(!client.busy()); QVERIFY(client.entries().isEmpty());
        QVERIFY(client.status() != "Files on your device.");
        client.disconnectSession(); QVERIFY(!client.connected()); QVERIFY(client.hosts().isEmpty());
    }
};
QTEST_GUILESS_MAIN(NetworkDriveTests)
#include "tst_networkdrive.moc"

#include "MobileNetworkDevice.h"
#include "App/Network/NetworkDriveController.h"
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTest>
#include <QMetaProperty>

using namespace iiServerHost;
class NetworkDriveTests : public QObject {
    Q_OBJECT
private slots:
    void manualPairingDoesNotAuthorizeRemoteImageGeneration() {
        QTemporaryDir root(SOCIETY_TEST_DIRECTORY "/network-local-only-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(root.path()));
        NetworkDriveController society(nullptr, QHostAddress::LocalHost);
        society.setContainerPath(root.path());
        QVERIFY(society.startLocalHost());
        LanPeer client;
        QVERIFY(client.join(society.localPeer()->createOffer(), "test-client", "Society test client"));
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
        QTemporaryDir root(SOCIETY_TEST_DIRECTORY "/network-role-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(root.path()));
        QFile source(root.filePath("Files/shared.txt")); QVERIFY(source.open(QIODevice::WriteOnly));
        source.write("shared"); source.close();
        RelayServer relay([](const auto &, AuthCompletion done) { done({"alice", QDateTime::currentDateTimeUtc().addSecs(60)}); });
        QVERIFY(relay.listen(QHostAddress::LocalHost));
        PeerOptions o; o.relayUrl = QUrl(QString("ws://127.0.0.1:%1").arg(relay.port()));
        o.credential = "alice"; o.peerId = "desktop"; o.name = "Desktop";
        o.localEnabled = local; o.localHostingEnabled = local;
        o.hostFiles = false; // Caller options cannot demote the desktop role.
        o.listenAddress = QHostAddress::LocalHost;
        NetworkDriveController desktop; MobileNetworkDevice client;
        QCOMPARE(desktop.mode(), NetworkDriveController::HostMode);
        const auto role = desktop.metaObject()->property(desktop.metaObject()->indexOfProperty("mode"));
        QVERIFY(role.isConstant()); QVERIFY(!role.isWritable());
        QVERIFY(!desktop.setProperty("mode", NetworkDriveController::ClientMode));
        QCOMPARE(desktop.mode(), NetworkDriveController::HostMode);
        desktop.setContainerPath(root.path()); QVERIFY(desktop.startSession(o));
        o.peerId = "client"; QVERIFY(client.startSession(o));
        QTRY_VERIFY(desktop.hosting() && client.connected());
        QTRY_COMPARE(client.hosts().size(), 1);
        client.browse("desktop"); QTRY_VERIFY(!client.busy());
        QCOMPARE(client.entries().size(), 1);
        QCOMPARE(client.transport(), local ? "local" : "remote");
        desktop.setContainerPath(root.filePath("missing"));
        QVERIFY(!desktop.connected()); QVERIFY(!desktop.hosting());
        QCOMPARE(desktop.mode(), NetworkDriveController::HostMode);
        desktop.setContainerPath(root.path()); QTRY_VERIFY(desktop.hosting());
        desktop.disconnectSession();
        QVERIFY(!desktop.connected()); QCOMPARE(desktop.mode(), NetworkDriveController::HostMode);
    }
    void failedDownloadPreservesExistingDestination() {
        QTemporaryDir root(SOCIETY_TEST_DIRECTORY "/network-cancel-XXXXXX");
        QFile source(root.filePath("source")); QVERIFY(source.open(QIODevice::WriteOnly));
        source.write(QByteArray(700000, 'a')); source.close();
        QFile destination(root.filePath("destination")); QVERIFY(destination.open(QIODevice::WriteOnly));
        destination.write("original"); destination.close();
        RelayServer relay([](const auto &, AuthCompletion done) { done({"alice", QDateTime::currentDateTimeUtc().addSecs(60)}); });
        QVERIFY(relay.listen(QHostAddress::LocalHost));
        PeerOptions o; o.relayUrl = QUrl(QString("ws://127.0.0.1:%1").arg(relay.port()));
        o.credential = "alice"; o.peerId = "host"; o.name = "host"; o.localEnabled = false; o.service = "com.iisacc.society.files";
        Peer host; FileShare files(root.path()); int reads = 0;
        QVERIFY(host.start(o, [&](const auto &, const QJsonObject &request) {
            if (request.value("op") == "read" && ++reads == 2) {
                source.open(QIODevice::Append); source.write("changed"); source.close();
            }
            return files.handle(request);
        }));
        MobileNetworkDevice client; o.peerId = "client"; QVERIFY(client.startSession(o));
        QTRY_VERIFY(host.isReady() && client.connected()); QTRY_COMPARE(client.hosts().size(), 1);
        client.browse("host"); QTRY_VERIFY(!client.busy());
        QSignalSpy saved(&client, &NetworkDriveController::downloadFinished);
        client.download("source", QUrl::fromLocalFile(destination.fileName())); QTRY_VERIFY(!client.busy());
        QCOMPARE(reads, 2); QCOMPARE(saved.size(), 0);
        QCOMPARE(client.status(), "file_changed");
        QVERIFY(destination.open(QIODevice::ReadOnly)); QCOMPARE(destination.readAll(), QByteArray("original"));
    }
    void disconnectCancelsDownloadWithoutOverwriting() {
        QTemporaryDir root(SOCIETY_TEST_DIRECTORY "/network-mode-cancel-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(root.path()));
        QFile source(root.filePath("Files/source")); QVERIFY(source.open(QIODevice::WriteOnly));
        source.write(QByteArray(700000, 'a')); source.close();
        QFile destination(root.filePath("Files/destination")); QVERIFY(destination.open(QIODevice::WriteOnly));
        destination.write("original"); destination.close();
        RelayServer relay([](const auto &, AuthCompletion done) { done({"alice", QDateTime::currentDateTimeUtc().addSecs(60)}); });
        QVERIFY(relay.listen(QHostAddress::LocalHost));
        PeerOptions o; o.relayUrl = QUrl(QString("ws://127.0.0.1:%1").arg(relay.port()));
        o.credential = "alice"; o.peerId = "host"; o.name = "host"; o.localEnabled = false;
        o.service = "com.iisacc.society.files";
        MobileNetworkDevice client; client.setContainerPath(root.path());
        Peer host; FileShare files(root.filePath("Files")); int reads = 0;
        QVERIFY(host.start(o, [&](const auto &, const QJsonObject &request) {
            if (request.value("op") == "read" && ++reads == 2)
                client.disconnectSession();
            return files.handle(request);
        }));
        o.peerId = "client"; QVERIFY(client.startSession(o));
        QTRY_VERIFY(host.isReady() && client.connected()); QTRY_COMPARE(client.hosts().size(), 1);
        client.browse("host"); QTRY_VERIFY(!client.busy());
        QSignalSpy saved(&client, &NetworkDriveController::downloadFinished);
        client.download("source", QUrl::fromLocalFile(destination.fileName()));
        QCOMPARE(client.mode(), NetworkDriveController::ClientMode);
        QTRY_VERIFY(!client.connected()); QVERIFY(!client.busy());
        QCOMPARE(reads, 2); QCOMPARE(saved.size(), 0);
        QVERIFY(destination.open(QIODevice::ReadOnly)); QCOMPARE(destination.readAll(), QByteArray("original"));
    }
    void societyFilesAcrossLocalAndRemote() {
        RelayServer relay([](const auto &credential, AuthCompletion done) { done({credential == "alice" ? "alice" : "bob", QDateTime::currentDateTimeUtc().addSecs(60)}); });
        QVERIFY(relay.listen(QHostAddress::LocalHost));
        for (bool local : {true, false}) {
            QTemporaryDir root(SOCIETY_TEST_DIRECTORY "/network-drive-XXXXXX"), destination(SOCIETY_TEST_DIRECTORY "/network-download-XXXXXX");
            QVERIFY(iiSocietyContainer::SocietyDrive::create(root.path()));
            const QByteArray bytes(700000, 's'); QFile source(root.filePath("Files/sample.bin"));
            QVERIFY(source.open(QIODevice::WriteOnly)); QCOMPARE(source.write(bytes), bytes.size()); source.close();
            QFile privateFile(root.filePath("Models/private.bin")); QVERIFY(privateFile.open(QIODevice::WriteOnly)); privateFile.write("private"); privateFile.close();
            PeerOptions o; o.relayUrl = QUrl(QString("ws://127.0.0.1:%1").arg(relay.port()));
            o.credential = "alice"; o.peerId = "host"; o.name = "My Society";
            o.localEnabled = local; o.listenAddress = QHostAddress::LocalHost;
            NetworkDriveController host; MobileNetworkDevice client;
            host.setContainerPath(root.path()); QVERIFY(host.startSession(o));
            o.peerId = "client"; QVERIFY(client.startSession(o));
            QTRY_VERIFY(host.connected() && client.connected());
            QTRY_COMPARE(client.hosts().size(), 1);
            QCOMPARE(client.hosts()[0].toMap()["metadata"].toMap()["section"].toString(), "files");
            client.browse("host"); QTRY_VERIFY(!client.busy());
            QCOMPARE(client.entries().size(), 1); // Only the user-created sample.bin.
            QStringList names;
            for (const auto &entry : client.entries()) names.append(entry.toMap().value("name").toString());
            names.sort();
            QCOMPARE(names, (QStringList{"sample.bin"}));
            QCOMPARE(client.transport(), local ? "local" : "remote");
            QSignalSpy saved(&client, &NetworkDriveController::downloadFinished);
            const auto target = destination.filePath("download.bin");
            client.download("sample.bin", QUrl::fromLocalFile(target)); QTRY_COMPARE(saved.size(), 1);
            QFile copy(target); QVERIFY(copy.open(QIODevice::ReadOnly)); QCOMPARE(copy.readAll(), bytes);
            client.browse("host", "../Models"); QTRY_VERIFY(!client.busy());
            QVERIFY(client.entries().isEmpty()); QVERIFY(client.status().contains("invalid"));
            host.disconnectSession(); QTRY_VERIFY(client.hosts().isEmpty());
        }
    }
    void replacedContainerAndLogoutStopAccess() {
        QTemporaryDir root(SOCIETY_TEST_DIRECTORY "/network-identity-XXXXXX");
        const auto drive = iiSocietyContainer::SocietyDrive::create(root.path()); QVERIFY(drive);
        RelayServer relay([](const auto &, AuthCompletion done) { done({"alice", QDateTime::currentDateTimeUtc().addSecs(60)}); });
        QVERIFY(relay.listen(QHostAddress::LocalHost));
        PeerOptions o; o.relayUrl = QUrl(QString("ws://127.0.0.1:%1").arg(relay.port()));
        o.credential = "alice"; o.peerId = "host"; o.name = "host"; o.localEnabled = false;
        NetworkDriveController host; MobileNetworkDevice client;
        host.setContainerPath(root.path()); QVERIFY(host.startSession(o)); o.peerId = "client"; QVERIFY(client.startSession(o));
        QTRY_VERIFY(host.connected() && client.connected()); QTRY_COMPARE(client.hosts().size(), 1);
        QVERIFY(QDir(root.path()).rename("Files", "Files-old")); QVERIFY(QDir(root.path()).mkdir("Files"));
        client.browse("host"); QTRY_VERIFY(!client.busy()); QVERIFY(client.entries().isEmpty());
        QVERIFY(client.status() != "Files on your device.");
        client.disconnectSession(); QVERIFY(!client.connected()); QVERIFY(client.hosts().isEmpty());
    }
};
QTEST_GUILESS_MAIN(NetworkDriveTests)
#include "tst_networkdrive.moc"

#include "App/Network/NetworkDriveController.h"
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTest>

using namespace iiServerHost;
class NetworkDriveTests : public QObject {
    Q_OBJECT
private slots:
    void localSocietyDoesNotProvideRemoteImageGeneration() {
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
                                      {{"op", "generation"}, {"action", "models"}});
        QVERIFY(!id.isEmpty());
        QTRY_VERIFY(!replies.isEmpty());
        QVERIFY(!replies.first()[1].toJsonObject().value("ok").toBool());
        client.stop();
        society.disconnectSession();
    }
    void desktopModeSwitchControlsExposure_data() {
        QTest::addColumn<bool>("local");
        QTest::newRow("local") << true;
        QTest::newRow("remote") << false;
    }
    void desktopModeSwitchControlsExposure() {
        QFETCH(bool, local);
        QTemporaryDir root(SOCIETY_TEST_DIRECTORY "/network-mode-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(root.path()));
        QFile source(root.filePath("Files/shared.txt")); QVERIFY(source.open(QIODevice::WriteOnly));
        source.write("shared"); source.close();
        RelayServer relay([](const auto &, AuthCompletion done) { done({"alice", QDateTime::currentDateTimeUtc().addSecs(60)}); });
        QVERIFY(relay.listen(QHostAddress::LocalHost));
        QTcpServer occupied; QVERIFY(occupied.listen(QHostAddress::LocalHost));
        const auto port = occupied.serverPort();
        PeerOptions o; o.relayUrl = QUrl(QString("ws://127.0.0.1:%1").arg(relay.port()));
        o.credential = "alice"; o.peerId = "desktop"; o.name = "Desktop";
        o.localEnabled = local; o.localHostingEnabled = true; o.hostFiles = true;
        o.listenAddress = QHostAddress::LocalHost; o.localPort = port;
        o.metadata = {{"section", "must-not-leak"}};
        NetworkDriveController desktop, client;
        QVERIFY(desktop.hostModeAvailable());
        QCOMPARE(desktop.mode(), NetworkDriveController::ClientMode);
        desktop.setContainerPath(root.path());
        QVERIFY(desktop.startSession(o)); // Even explicit native host options cannot override client mode.
        o.peerId = "client"; QVERIFY(client.startSession(o));
        QTRY_VERIFY(desktop.connected() && client.connected());
        QVERIFY(!desktop.hosting());
        client.browse("desktop"); QTRY_VERIFY(!client.busy());
        QVERIFY(client.entries().isEmpty());
        QVERIFY(client.status() != "Files on your device.");
        QVERIFY(client.hosts().isEmpty());
        occupied.close();

        for (int pass = 0; pass < 2; ++pass) {
            desktop.setMode(NetworkDriveController::HostMode);
            QTRY_VERIFY(desktop.connected() && desktop.hosting());
            QTRY_COMPARE(client.hosts().size(), 1);
            QCOMPARE(client.hosts()[0].toMap()["metadata"].toMap()["section"].toString(), "files");
            client.browse("desktop"); QTRY_VERIFY(!client.busy());
            QCOMPARE(client.entries().size(), 1);
            QCOMPARE(client.transport(), local ? "local" : "remote");
            desktop.setMode(NetworkDriveController::ClientMode);
            QVERIFY(!desktop.hosting());
            QVERIFY(occupied.listen(QHostAddress::LocalHost, port)); // The old local listener is gone immediately.
            occupied.close();
            QTRY_VERIFY(desktop.connected());
            QTRY_VERIFY(client.hosts().isEmpty());
            client.browse("desktop"); QTRY_VERIFY(!client.busy());
            QVERIFY(client.entries().isEmpty()); // Previously established local/remote paths cannot serve files.
            QVERIFY(client.status() != "Files on your device.");
        }

        QSignalSpy state(&desktop, &NetworkDriveController::stateChanged);
        desktop.setContainerPath(root.filePath("missing"));
        QVERIFY(desktop.connected()); QCOMPARE(state.size(), 0); // Client storage changes do not reconnect.
        desktop.setMode(NetworkDriveController::HostMode);
        QVERIFY(!desktop.connected()); QVERIFY(!desktop.hosting());
        desktop.setMode(NetworkDriveController::ClientMode);
        QTRY_VERIFY(desktop.connected()); // A stale container cannot block client mode.
        desktop.disconnectSession();
        desktop.setMode(NetworkDriveController::HostMode);
        QVERIFY(!desktop.connected()); // Changing modes never restores a signed-out session.
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
        NetworkDriveController client; o.peerId = "client"; QVERIFY(client.startSession(o));
        QTRY_VERIFY(host.isReady() && client.connected()); QTRY_COMPARE(client.hosts().size(), 1);
        client.browse("host"); QTRY_VERIFY(!client.busy());
        QSignalSpy saved(&client, &NetworkDriveController::downloadFinished);
        client.download("source", QUrl::fromLocalFile(destination.fileName())); QTRY_VERIFY(!client.busy());
        QCOMPARE(reads, 2); QCOMPARE(saved.size(), 0);
        QCOMPARE(client.status(), "file_changed");
        QVERIFY(destination.open(QIODevice::ReadOnly)); QCOMPARE(destination.readAll(), QByteArray("original"));
    }
    void modeSwitchCancelsDownloadWithoutOverwriting() {
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
        NetworkDriveController client; client.setContainerPath(root.path());
        Peer host; FileShare files(root.filePath("Files")); int reads = 0;
        QVERIFY(host.start(o, [&](const auto &, const QJsonObject &request) {
            if (request.value("op") == "read" && ++reads == 2)
                client.setMode(NetworkDriveController::HostMode);
            return files.handle(request);
        }));
        o.peerId = "client"; QVERIFY(client.startSession(o));
        QTRY_VERIFY(host.isReady() && client.connected()); QTRY_COMPARE(client.hosts().size(), 1);
        client.browse("host"); QTRY_VERIFY(!client.busy());
        QSignalSpy saved(&client, &NetworkDriveController::downloadFinished);
        client.download("source", QUrl::fromLocalFile(destination.fileName()));
        QTRY_COMPARE(client.mode(), NetworkDriveController::HostMode);
        QTRY_VERIFY(client.hosting()); QVERIFY(!client.busy());
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
            NetworkDriveController host, client;
            host.setMode(NetworkDriveController::HostMode);
            host.setContainerPath(root.path()); QVERIFY(host.startSession(o));
            o.peerId = "client"; QVERIFY(client.startSession(o));
            QTRY_VERIFY(host.connected() && client.connected());
            QTRY_COMPARE(client.hosts().size(), 1);
            QCOMPARE(client.hosts()[0].toMap()["metadata"].toMap()["section"].toString(), "files");
            client.browse("host"); QTRY_VERIFY(!client.busy());
            QCOMPARE(client.entries().size(), 1);
            QCOMPARE(client.entries()[0].toMap()["name"].toString(), "sample.bin");
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
        NetworkDriveController host, client;
        host.setMode(NetworkDriveController::HostMode);
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

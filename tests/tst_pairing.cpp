#include "MobileNetworkDevice.h"
#include "AccountServer.h"
#include <QElapsedTimer>
#include <QLockFile>
#include "App/Network/DevicePairing.h"
#include "PairingCredentialsFixture.h"
#include "App/Network/PairingQr.h"
#include "App/Network/QrScanner.h"
#include "FakeDiscoveryService.h"
#include "MemorySessionStore.h"
#include <QGuiApplication>
#include <QImageReader>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QSettings>
#include <QSignalSpy>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>
#include "backend/runtime/appbootstrap.h"
#include <memory>
#include <vector>

#ifdef Q_OS_MACOS
QString decodePairingQrImage(const QImage &image);
QString decodePairingQrCameraFrame(const QImage &image);
#endif
namespace {
QQuickItem *visualItem(QQuickItem *root, const QString &name) {
    if (root->objectName() == name) return root;
    for (auto *child : root->childItems()) if (auto *found = visualItem(child, name)) return found;
    return nullptr;
}
struct RegisteredAccounts {
    AccountServer authority;
    std::vector<std::unique_ptr<MemorySessionStore>> stores;
    std::vector<std::unique_ptr<AccountController>> accounts;
    bool start(const QString &container, const QString &hostId) {
        const auto drive = iiSocietyContainer::SocietyDrive::open(container);
        if (!drive || !authority.server.listen(QHostAddress::LocalHost)) return false;
        authority.profile.insert("societyContainerDrive", QJsonObject{{"hostDeviceId", hostId},
            {"containerId", drive->identifier()}, {"revision", "1d02e288-9704-4c5a-979e-f9b60d1799ca"},
            {"imagePath", container}});
        qputenv("SOCIETY_TEST_ACCOUNT_DISCOVERY", "1");
        return true;
    }
    AccountController *add(const QString &id, const QString &name, const QString &type, bool automatic) {
        auto store = std::make_unique<MemorySessionStore>();
        auto account = std::make_unique<AccountController>(authority.url(), store.get(), nullptr);
        auto device = account->manager()->deviceInfo();
        device.insert("id", id); device.insert("name", name); device.insert("type", type);
        if (!account->manager()->setDeviceInfo(device) || !account->login("builder@example.com", "FixtureOnly1!")) return nullptr;
        const auto wait = [](auto predicate) {
            QElapsedTimer elapsed; elapsed.start();
            while (!predicate() && elapsed.elapsed() < 5000) QTest::qWait(10);
            return predicate();
        };
        if (!wait([&] { return account->signedIn() && !account->busy(); })) return nullptr;
        account->setAutomaticPairingEnabled(automatic);
        account->requestPairingCredentials();
        if (!wait([&] { return !account->pairingCredentials().isEmpty(); })) return nullptr;
        const auto result = account.get();
        stores.push_back(std::move(store)); accounts.push_back(std::move(account));
        return result;
    }
};
struct Fixture {
    QTemporaryDir container{SOCIETY_TEST_DIRECTORY "/pairing-files-XXXXXX.societycontainer"};
    AccountServer authority;
    std::unique_ptr<AccountController> hostAccount, clientAccount;
    NetworkDriveController host; MobileNetworkDevice client;
    bool start() {
        if (!iiSocietyContainer::SocietyDrive::create(container.path())) return false;
        QFile file(container.filePath("Files/hello.txt"));
        if (!file.open(QIODevice::WriteOnly) || file.write("hello from desktop") < 0) return false;
        file.close();
        if (!authority.server.listen(QHostAddress::LocalHost)) return false;
        authority.profile.insert("societyContainerDrive", QJsonObject{{"hostDeviceId", QString(64, 'a')},
            {"containerId", iiSocietyContainer::SocietyDrive::open(container.path())->identifier()},
            {"revision", "1d02e288-9704-4c5a-979e-f9b60d1799ca"}, {"imagePath", container.path()}});
        hostAccount = std::make_unique<AccountController>(authority.url()); clientAccount = std::make_unique<AccountController>(authority.url());
        const auto wait = [](auto predicate) { QElapsedTimer elapsed; elapsed.start(); while (!predicate() && elapsed.elapsed() < 5000) QTest::qWait(10); return predicate(); };
        char id = 'a';
        for (auto *account : {hostAccount.get(), clientAccount.get()}) {
            auto device = account->manager()->deviceInfo(); device.insert("id", QString(64, id++));
            if (!account->manager()->setDeviceInfo(device) || !account->login("builder@example.com", "FixtureOnly1!")) return false;
            if (!wait([&] { return account->signedIn() && !account->busy(); })) return false;
            account->setAutomaticPairingEnabled(false); account->requestPairingCredentials();
            if (!wait([&] { return !account->pairingCredentials().isEmpty(); })) return false;
        }
        qputenv("SOCIETY_TEST_ACCOUNT_DISCOVERY", "1");
        host.setContainerPath(container.path()); host.setAccountSession(hostAccount.get()); client.setAccountSession(clientAccount.get());
        return true;
    }
};
}
class PairingTests : public QObject {
    Q_OBJECT
private slots:
    void unauthenticatedQrReportsTheAccountRequirement() {
        NetworkDriveController host; DevicePairing panel; panel.setNetwork(&host); panel.showHostQr();
        QVERIFY(!host.hosting()); QVERIFY(panel.qrText().isEmpty()); QCOMPARE(panel.phase(), QString("error"));
    }
    void initTestCase() {
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, QStringLiteral(SOCIETY_TEST_DIRECTORY "/pairing-settings"));
        qmlRegisterType<AccountController>("Society", 1, 0, "AccountController");
        qmlRegisterType<NetworkDriveController>("Society", 1, 0, "NetworkDriveController");
        qmlRegisterType<DevicePairing>("Society", 1, 0, "DevicePairing");
        qmlRegisterType<PairingQr>("Society", 1, 0, "PairingQr");
        qmlRegisterType<QrScanner>("Society", 1, 0, "QrScanner");
    }
    void cleanup() { qunsetenv("SOCIETY_TEST_ACCOUNT_DISCOVERY"); }
    void init() { QSettings(QSettings::IniFormat, QSettings::UserScope, "iisacc", "SocietyPairing").clear(); }
    void fixedRoleDoesNotPauseAutomaticConnections() {
        NetworkDriveController network;
        QVERIFY(network.automaticPairingEnabled());
        QVERIFY(!network.setProperty("mode", NetworkDriveController::ClientMode));
        QCOMPARE(network.mode(), NetworkDriveController::HostMode); QVERIFY(network.automaticPairingEnabled());
        network.setRuntimeEnabled(false); QVERIFY(!network.runtimeEnabled());
        QVERIFY(!network.startLocalHost()); QVERIFY(!network.discovering());
        network.setRuntimeEnabled(true); QVERIFY(network.automaticPairingEnabled());
    }
    void cameraFrameDecoderReadsQrAndIgnoresBlankFrames() {
#ifdef Q_OS_MACOS
        Fixture fixture; QVERIFY(fixture.start());
        DevicePairing pairing; pairing.setNetwork(&fixture.host); pairing.showHostQr();
        const auto payload = pairing.qrText(); QVERIFY(!payload.isEmpty());
        const auto image = PairingQr::encode(payload, 4);
        QCOMPARE(decodePairingQrCameraFrame(image), payload);
        QImage blank(640, 480, QImage::Format_RGB32); blank.fill(Qt::white);
        QVERIFY(decodePairingQrCameraFrame(blank).isEmpty());
#else
        QSKIP("Apple camera decoder requires macOS.");
#endif
    }
    void cameraFrameDecoderReadsUserPhotograph() {
#ifdef Q_OS_MACOS
        const auto path = qEnvironmentVariable("SOCIETY_QR_REGRESSION_IMAGE");
        if (path.isEmpty()) QSKIP("Set SOCIETY_QR_REGRESSION_IMAGE to the supplied photograph; it is not committed.");
        QImageReader reader(path); reader.setAutoTransform(true); const auto image = reader.read();
        QVERIFY2(!image.isNull(), qPrintable(reader.errorString()));
        const auto payload = decodePairingQrCameraFrame(image);
        QVERIFY2(!payload.isEmpty(), "The camera frame decoder could not read the photographed Society QR.");
        iiWebServer::LanLink link; QVERIFY(iiWebServer::LanLink::decode(payload, &link));
        // Inspect the expired photograph only. Never connect using a user's captured offer.
        QVERIFY(link.expiresAt < QDateTime::currentDateTimeUtc());
        qInfo("Photographed Society QR decoded: %lld characters; no connection attempted.", qlonglong(payload.size()));
#else
        QSKIP("Apple camera decoder requires macOS.");
#endif
    }
    void closingOptionalQrResumesAutomaticPairing() {
        Fixture fixture; QVERIFY(fixture.start());
        fixture.host.resumeAutomaticPairing();
        DevicePairing panel; panel.setNetwork(&fixture.host); panel.showHostQr();
        QVERIFY(!panel.qrText().isEmpty()); QVERIFY(!fixture.host.automaticPairingEnabled());
        panel.cancel();
        QVERIFY(fixture.host.automaticPairingEnabled());
        QVERIFY(fixture.hostAccount->automaticPairingEnabled());
    }
    void authenticatedFirstLaunchAutomaticallyOpensTheHostContainer() {
        Fixture fixture; QVERIFY(fixture.start());
        fixture.host.setAccountSession(nullptr); fixture.client.setAccountSession(nullptr);
        FakeDiscoveryService hostDiscovery, phoneDiscovery;
        QTemporaryDir mirror(SOCIETY_TEST_DIRECTORY "/first-launch-XXXXXX.societycontainer");
        NetworkDriveController host(&hostDiscovery, QHostAddress::LocalHost);
        MobileNetworkDevice phone(&phoneDiscovery, QHostAddress::LocalHost);
        QVERIFY(iiSocietyContainer::SocietyDrive::create(mirror.path()));
        fixture.hostAccount->setAutomaticPairingEnabled(true);
        fixture.clientAccount->setAutomaticPairingEnabled(true);
        host.setContainerPath(fixture.container.path()); phone.setContainerPath(mirror.path());
        host.setAccountSession(fixture.hostAccount.get()); phone.setAccountSession(fixture.clientAccount.get());
        QTimer announce; announce.setInterval(100);
        connect(&announce, &QTimer::timeout, this, [&] {
            hostDiscovery.announceTo(phoneDiscovery); phoneDiscovery.announceTo(hostDiscovery);
        });
        announce.start();
        QTRY_VERIFY_WITH_TIMEOUT(host.discovery()->authenticated() && phone.discovery()->authenticated(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(phone.connected(), 15000);
        QTRY_VERIFY(!fixture.clientAccount->savedHostConnection().value("routes").toArray().isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(phone.hostConnectionReady(), 15000);
        QCOMPARE(iiSocietyContainer::SocietyDrive::open(mirror.path())->identifier(),
                 iiSocietyContainer::SocietyDrive::open(fixture.container.path())->identifier());
        QLockFile operation(fixture.container.filePath(".society-sync/operation.lock"));
        QTRY_VERIFY_WITH_TIMEOUT(operation.isLocked() || operation.tryLock(), 5000);
        phone.synchronizeNow();
        QTRY_COMPARE_WITH_TIMEOUT(phone.synchronizationStatus(), QString("Container sync is waiting to retry."), 10000);
        QVERIFY(phone.hostConnectionReady()); // A content synchronization error is not lost host authentication.
        operation.unlock();
        QTRY_VERIFY(!fixture.clientAccount->savedHostConnection().value("routes").toArray().isEmpty());
        announce.stop();
        phone.setRuntimeEnabled(false); QVERIFY(!phone.hostConnectionReady());
        host.pauseAutomaticPairing(false);
        FakeDiscoveryService restartedDiscovery;
        MobileNetworkDevice restarted(&restartedDiscovery, QHostAddress::LocalHost);
        restarted.setContainerPath(mirror.path()); restarted.setAccountSession(fixture.clientAccount.get());
        QTRY_VERIFY(restarted.workspaceReady());
        QVERIFY(!restarted.connected()); // Cached workspace readiness is not fake connectivity.
        QVERIFY(!restarted.hostConnectionReady());
        QVERIFY(restarted.reconnecting());
        restarted.synchronizeNow(); QTest::qWait(100);
        QVERIFY(restarted.reconnecting()); // File sync busy pulses never toggle Retry.
        host.resumeAutomaticPairing();
        // No DNS-SD announcements: the encrypted saved route initiates recovery.
        QTRY_VERIFY_WITH_TIMEOUT(restarted.connected(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(restarted.hostConnectionReady(), 10000);
        QVERIFY(!restarted.reconnecting());
        restarted.disconnectSession(); QVERIFY(!restarted.hostConnectionReady());
        QVERIFY(restarted.workspaceReady());
        QVERIFY(!restarted.reconnecting());
    }
    void automaticQueuePairsSeveralDevicesAndSurvivesPanelClosing() {
        RegisteredAccounts accounts;
        FakeDiscoveryService a, b, c;
        QTemporaryDir root(SOCIETY_TEST_DIRECTORY "/automatic-files-XXXXXX.societycontainer");
        QTemporaryDir receiver(SOCIETY_TEST_DIRECTORY "/automatic-download-XXXXXX.societycontainer");
        NetworkDriveController desktop(&a, QHostAddress::LocalHost); MobileNetworkDevice phone(&b, QHostAddress::LocalHost), tablet(&c, QHostAddress::LocalHost);
        const auto stop = qScopeGuard([&] {
            phone.setRuntimeEnabled(false); tablet.setRuntimeEnabled(false); desktop.setRuntimeEnabled(false);
        });
        QVERIFY(iiSocietyContainer::SocietyDrive::create(root.path())); desktop.setContainerPath(root.path());
        QVERIFY(iiSocietyContainer::SocietyDrive::create(receiver.path())); phone.setContainerPath(receiver.path());
        QFile file(root.filePath("Files/automatic.txt")); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("automatic LAN transfer"); file.close();
        // Two-day-old grants must complete TLS pairing and transfer without a
        // service request or a freshly issued five-minute key.
        accounts.authority.pairingIssuedAt = QDateTime::currentDateTimeUtc().addDays(-2);
        QVERIFY(accounts.start(root.path(), QString(64, 'a')));
        auto *hostAccount = accounts.add(QString(64, 'a'), "Desktop", "pc", true);
        auto *phoneAccount = accounts.add(QString(64, 'b'), "Phone", "phone", true);
        auto *tabletAccount = accounts.add(QString(64, 'c'), "Tablet", "tablet", true);
        QVERIFY(hostAccount && phoneAccount && tabletAccount);
        desktop.setAccountSession(hostAccount); phone.setAccountSession(phoneAccount); tablet.setAccountSession(tabletAccount);
        QTRY_VERIFY(desktop.discovery()->authenticated() && phone.discovery()->authenticated() && tablet.discovery()->authenticated());
        QCOMPARE(hostAccount->pairingCredentials().value("issuedAt").toString(), accounts.authority.pairingIssuedAt.toString(Qt::ISODate));
        const auto requestsBeforePairing = accounts.authority.sessionRequests().size();
        QSignalSpy paired(desktop.localPeer(), &iiWebServer::LanPeer::paired);
        a.announceTo(b); a.announceTo(c); b.announceTo(a); c.announceTo(a);
        QTRY_VERIFY(!desktop.pairingQueue().isEmpty());
        { DevicePairing panel; panel.setNetwork(&desktop); panel.cancel(); }
        const QStringList expected{QString(64, 'b'), QString(64, 'c')};
        const auto notifiedIds = [&] {
            QStringList ids;
            for (const auto &event : paired) ids.append(event.first().toString());
            ids.removeDuplicates(); ids.sort(); return ids;
        };
        const auto connectedIds = [&] { auto ids = desktop.localPeer()->pairedDeviceIds(); ids.sort(); return ids; };
        QTRY_COMPARE_WITH_TIMEOUT(notifiedIds(), expected, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(phone.containerReady() && phone.hostConnectionReady(), 15000);
        QTRY_COMPARE_WITH_TIMEOUT(connectedIds(), expected, 15000); // Exactly two live authenticated sessions.
        QVERIFY(phone.localPeer()->connected() && tablet.localPeer()->connected());
        QCOMPARE(phone.localPeer()->pairedDeviceIds(), (QStringList{QString(64, 'a')}));
        QCOMPARE(tablet.localPeer()->pairedDeviceIds(), (QStringList{QString(64, 'a')}));
        QTRY_COMPARE(phone.entries().size(), 1); QTRY_COMPARE(tablet.entries().size(), 1);
        QStringList names;
        for (const auto &entry : phone.entries()) names.append(entry.toMap().value("name").toString());
        QVERIFY(names.contains("automatic.txt"));
        QCOMPARE(tablet.entries().first().toMap().value("name").toString(), QString("automatic.txt"));
        QTRY_VERIFY(desktop.pairingQueue().isEmpty());
        // Bootstrap can revalidate the adopted UUID and reconnect the same
        // peer. Duplicate discovery is checked only after that lifecycle settles.
        const auto settledPairings = paired.size();
        b.announceTo(a); b.announceTo(a); c.announceTo(a); QTest::qWait(1100);
        QCOMPARE(paired.size(), settledPairings); QCOMPARE(notifiedIds(), expected); QCOMPARE(connectedIds(), expected);
        const auto destination = QUrl::fromLocalFile(receiver.filePath("Files/copied.txt"));
        QSignalSpy downloaded(&phone, &NetworkDriveController::downloadFinished);
        phone.download("automatic.txt", destination); QTRY_COMPARE(downloaded.size(), 1);
        QFile copied(destination.toLocalFile()); QVERIFY(copied.open(QIODevice::ReadOnly)); QCOMPARE(copied.readAll(), QByteArray("automatic LAN transfer"));
        phone.disconnectSession(); QVERIFY(!phone.automaticPairingEnabled());
        QTest::qWait(1100); QVERIFY(!phone.localPeer()->connected());
        phone.resumeAutomaticPairing();
        QTRY_VERIFY_WITH_TIMEOUT(phone.localPeer()->connected(), 15000);
        QCOMPARE(accounts.authority.sessionRequests().size(), requestsBeforePairing);
        desktop.discovery()->clear();
        QTRY_VERIFY(!phone.localPeer()->connected()); QVERIFY(!desktop.hosting()); QVERIFY(desktop.pairingQueue().isEmpty());
    }
    void registeredHostOutranksANewLowerIdDesktop() {
        RegisteredAccounts accounts;
        // The host's indexer must stop before its backing fixture is removed.
        QTemporaryDir root(SOCIETY_TEST_DIRECTORY "/primary-election-XXXXXX.societycontainer"), other(SOCIETY_TEST_DIRECTORY "/newcomer-election-XXXXXX.societycontainer");
        FakeDiscoveryService a, b;
        NetworkDriveController established(&a, QHostAddress::LocalHost), newcomer(&b, QHostAddress::LocalHost);
        QVERIFY(iiSocietyContainer::SocietyDrive::create(root.path())); QVERIFY(iiSocietyContainer::SocietyDrive::create(other.path()));
        established.setContainerPath(root.path()); newcomer.setContainerPath(other.path());
        const QString primaryId(64, 'f'), newcomerId(64, '0');
        QVERIFY(accounts.start(root.path(), primaryId));
        auto *primaryAccount = accounts.add(primaryId, "Primary", "pc", true);
        auto *newcomerAccount = accounts.add(newcomerId, "New desktop", "pc", true);
        QVERIFY(primaryAccount && newcomerAccount);
        established.setAccountSession(primaryAccount); newcomer.setAccountSession(newcomerAccount);
        QTRY_VERIFY(established.discovery()->authenticated() && newcomer.discovery()->authenticated());
        a.announceTo(b); b.announceTo(a);
        QTRY_VERIFY_WITH_TIMEOUT(newcomer.localPeer()->connected(), 10000);
        QVERIFY(established.hosting()); QVERIFY(!newcomer.hosting());
        QCOMPARE(newcomer.mode(), NetworkDriveController::HostMode); // Primary selection is a transport concern.
        // A remembered mirror does not elect itself while its primary is absent.
        established.discovery()->clear(); QTest::qWait(700);
        QVERIFY(!newcomer.hosting());
    }
    void registeredHostAndFailedPeerDoNotBlockOtherDevices() {
        QTemporaryDir root(SOCIETY_TEST_DIRECTORY "/automatic-election-XXXXXX.societycontainer"),
            secondRoot(SOCIETY_TEST_DIRECTORY "/automatic-second-XXXXXX.societycontainer"),
            unavailableRoot(SOCIETY_TEST_DIRECTORY "/automatic-unavailable-XXXXXX.societycontainer");
        RegisteredAccounts accounts;
        FakeDiscoveryService a, b, c;
        NetworkDriveController first(&a, QHostAddress::LocalHost), second(&b, QHostAddress::LocalHost), unavailable(&c, QHostAddress::LocalHost);
        QVERIFY(iiSocietyContainer::SocietyDrive::create(root.path()));
        QVERIFY(iiSocietyContainer::SocietyDrive::create(secondRoot.path()));
        QVERIFY(iiSocietyContainer::SocietyDrive::create(unavailableRoot.path()));
        first.setContainerPath(root.path()); second.setContainerPath(secondRoot.path()); unavailable.setContainerPath(unavailableRoot.path());
        QVERIFY(accounts.start(root.path(), QString(64, 'a')));
        auto *firstAccount = accounts.add(QString(64, 'a'), "First desktop", "pc", true);
        auto *secondAccount = accounts.add(QString(64, 'c'), "Second desktop", "pc", true);
        auto *unavailableAccount = accounts.add(QString(64, 'b'), "Unavailable desktop", "pc", true);
        QVERIFY(firstAccount && secondAccount && unavailableAccount);
        first.setAccountSession(firstAccount); second.setAccountSession(secondAccount); unavailable.setAccountSession(unavailableAccount);
        QTRY_VERIFY(first.discovery()->authenticated() && second.discovery()->authenticated() && unavailable.discovery()->authenticated());
        unavailable.pauseAutomaticPairing();
        a.announceTo(b); a.announceTo(c); b.announceTo(a); b.announceTo(c); c.announceTo(a); c.announceTo(b);
        QTRY_VERIFY_WITH_TIMEOUT(second.localPeer()->connected(), 10000);
        QVERIFY(first.hosting()); QVERIFY(!second.hosting());
        QVERIFY(!unavailable.localPeer()->connected());
        QTRY_VERIFY(!first.pairingQueue().isEmpty());
        unavailable.resumeAutomaticPairing();
        QTRY_VERIFY_WITH_TIMEOUT(unavailable.localPeer()->connected(), 10000);
        QTRY_VERIFY(first.pairingQueue().isEmpty());
    }
    void discoverySelectionAcceptAndCodeConfirmationExposeFiles() {
        QTemporaryDir root(SOCIETY_TEST_DIRECTORY "/discovered-files-XXXXXX.societycontainer");
        RegisteredAccounts accounts;
        FakeDiscoveryService a, b;
        NetworkDriveController desktop(&a, QHostAddress::LocalHost); MobileNetworkDevice phone(&b, QHostAddress::LocalHost);
        QVERIFY(iiSocietyContainer::SocietyDrive::create(root.path())); desktop.setContainerPath(root.path());
        QVERIFY(accounts.start(root.path(), QString(64, 'a')));
        auto *desktopAccount = accounts.add(QString(64, 'a'), "Desktop", "pc", false);
        auto *phoneAccount = accounts.add(QString(64, 'b'), "Phone", "phone", false);
        QVERIFY(desktopAccount && phoneAccount);
        desktop.setAccountSession(desktopAccount); phone.setAccountSession(phoneAccount);
        QTRY_VERIFY(desktop.discovery()->authenticated() && phone.discovery()->authenticated());
        a.announceTo(b); b.announceTo(a);
        DevicePairing host, client; host.setNetwork(&desktop); client.setNetwork(&phone);
        QQmlEngine engine; engine.addImportPath(SOCIETY_LVRS_QML_IMPORT_PATH);
        QQuickWindow hostWindow, clientWindow; hostWindow.resize(1120, 720); clientWindow.resize(390, 844);
        hostWindow.show(); clientWindow.show(); QrScanner hostScanner, clientScanner;
        QQmlComponent component(&engine, QUrl::fromLocalFile(SOCIETY_PAIRING_QML_FILE));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        auto makePanel = [&](DevicePairing *pairing, QrScanner *scanner, QQuickWindow *window) {
            return std::unique_ptr<QObject>(component.createWithInitialProperties({{"pairing", QVariant::fromValue(pairing)},
                {"scanner", QVariant::fromValue(scanner)}, {"appWindow", QVariant::fromValue(window)},
                {"parent", QVariant::fromValue(window->contentItem())}}));
        };
        auto hostPanel = makePanel(&host, &hostScanner, &hostWindow), clientPanel = makePanel(&client, &clientScanner, &clientWindow);
        QVERIFY(hostPanel && clientPanel); QVERIFY(QMetaObject::invokeMethod(hostPanel.get(), "open"));
        QTRY_VERIFY(hostPanel->property("opened").toBool());
        // The manual panel opens its QR tab. Choose the visible Nearby tab
        // through the same interaction as a user before selecting a device.
        auto *nearbyTab = hostPanel->findChild<QQuickItem *>("pairingNearbyTab");
        QVERIFY(nearbyTab); QTRY_VERIFY(nearbyTab->isVisible() && nearbyTab->isEnabled());
        QVERIFY(hostPanel->property("useQr").toBool());
        QVERIFY(QMetaObject::invokeMethod(nearbyTab, "clicked"));
        QTRY_VERIFY(!hostPanel->property("useQr").toBool());
        QQuickItem *pairButton = nullptr;
        QTRY_VERIFY((pairButton = visualItem(hostWindow.contentItem(), "pairingNearbyDevice")) != nullptr);
        QTRY_VERIFY(pairButton->isVisible());
        QVERIFY(!hostPanel->property("useQr").toBool());
        const auto screenshotDir = qEnvironmentVariable("SOCIETY_DISCOVERY_SCREENSHOT_DIR");
        if (!screenshotDir.isEmpty()) {
            QVERIFY(QDir().mkpath(screenshotDir)); QTest::qWait(100);
            QVERIFY(hostWindow.grabWindow().save(screenshotDir + "/nearby-devices.png"));
        }
        QSignalSpy received(&client, &DevicePairing::invitationReceived);
        QVERIFY(QMetaObject::invokeMethod(pairButton, "clicked")); QCOMPARE(host.phase(), "inviting"); QVERIFY(host.qrText().isEmpty());
        QTRY_COMPARE(received.size(), 1); QCOMPARE(client.phase(), "invited");
        QVERIFY(QMetaObject::invokeMethod(clientPanel.get(), "open"));
        QTRY_VERIFY(clientPanel->property("opened").toBool());
        auto *acceptButton = clientPanel->findChild<QQuickItem *>("pairingAcceptInvitation");
        QVERIFY(acceptButton); QTRY_VERIFY(acceptButton->isVisible());
        QVERIFY(!phone.connected()); QVERIFY(QMetaObject::invokeMethod(acceptButton, "clicked"));
        QTRY_COMPARE(host.phase(), "confirming"); QTRY_COMPARE(client.phase(), "confirming");
        QCOMPARE(host.verificationCode(), client.verificationCode()); QVERIFY(host.canConfirm()); QVERIFY(!client.canConfirm());
        auto *allowButton = hostPanel->findChild<QQuickItem *>("pairingConfirmDevice");
        auto *code = clientPanel->findChild<QQuickItem *>("pairingVerificationCode");
        QVERIFY(allowButton && code); QTRY_VERIFY(allowButton->isVisible() && code->isVisible());
        QCOMPARE(code->property("text").toString(), host.verificationCode());
        QVERIFY(clientPanel->property("width").toInt() <= 390);
        if (!screenshotDir.isEmpty()) {
            QTest::qWait(100);
            QVERIFY(hostWindow.grabWindow().save(screenshotDir + "/desktop-confirmation.png"));
            QVERIFY(clientWindow.grabWindow().save(screenshotDir + "/client-confirmation.png"));
        }
        QVERIFY(phone.entries().isEmpty()); QVERIFY(QMetaObject::invokeMethod(allowButton, "clicked"));
        QTRY_COMPARE(host.phase(), "paired"); QTRY_COMPARE(client.phase(), "paired");
        QTRY_VERIFY(!phone.busy()); QCOMPARE(phone.currentHost(), QString(64, 'a')); QCOMPARE(phone.transport(), "local");
        QVERIFY(desktop.nearbyDevices()[0].toMap().value("connected").toBool());
        host.cancel(); QVERIFY(desktop.hosting()); QVERIFY(phone.connected());
    }
    void endingDiscoveryIdentityCancelsUnconfirmedConnection() {
        QTemporaryDir root(SOCIETY_TEST_DIRECTORY "/ending-discovery-XXXXXX.societycontainer");
        RegisteredAccounts accounts;
        FakeDiscoveryService a, b;
        NetworkDriveController desktop(&a, QHostAddress::LocalHost); MobileNetworkDevice phone(&b, QHostAddress::LocalHost);
        QVERIFY(iiSocietyContainer::SocietyDrive::create(root.path())); desktop.setContainerPath(root.path());
        QVERIFY(accounts.start(root.path(), QString(64, 'a')));
        auto *desktopAccount = accounts.add(QString(64, 'a'), "Desktop", "pc", false);
        auto *phoneAccount = accounts.add(QString(64, 'b'), "Phone", "phone", false);
        QVERIFY(desktopAccount && phoneAccount);
        desktop.setAccountSession(desktopAccount); phone.setAccountSession(phoneAccount);
        QTRY_VERIFY(desktop.discovery()->authenticated() && phone.discovery()->authenticated());
        a.announceTo(b); b.announceTo(a);
        DevicePairing host, client; host.setNetwork(&desktop); client.setNetwork(&phone);
        host.inviteDevice(QString(64, 'b')); QTRY_COMPARE(client.phase(), "invited");
        client.acceptInvitation(); QTRY_COMPARE(host.phase(), "confirming");
        desktop.discovery()->clear(); QTRY_COMPARE(client.phase(), "error");
        QVERIFY(!host.canConfirm()); QVERIFY(!phone.connected()); QVERIFY(desktop.nearbyDevices().isEmpty());
    }
    void qrScanPairsBothSidesAndReadsFiles() {
        QTemporaryDir receiver(SOCIETY_TEST_DIRECTORY "/qr-download-XXXXXX.societycontainer");
        Fixture fixture;
        const auto stop = qScopeGuard([&] {
            fixture.client.setRuntimeEnabled(false); fixture.host.setRuntimeEnabled(false);
        });
        QVERIFY(fixture.start());
        QVERIFY(iiSocietyContainer::SocietyDrive::create(receiver.path()));
        fixture.client.setContainerPath(receiver.path());
        QVERIFY(fixture.host.signedIn()); QVERIFY(fixture.host.relayUrl().isEmpty());
        DevicePairing host, client; host.setNetwork(&fixture.host); client.setNetwork(&fixture.client);
        QSignalSpy h(&host, &DevicePairing::paired), c(&client, &DevicePairing::paired);
        host.showHostQr(); QTRY_COMPARE(host.phase(), QString("showing"));
        QVERIFY(host.secondsRemaining() > 0);
        auto qr = host.qrText(); const auto image = PairingQr::encode(qr);
        QVERIFY(!image.isNull());
#ifdef Q_OS_MACOS
        qr = decodePairingQrImage(image); QCOMPARE(qr, host.qrText());
#endif
        client.scanCode(qr);
        QTRY_COMPARE(client.phase(), QString("paired")); QTRY_COMPARE(host.phase(), QString("paired"));
        QCOMPARE(c.size(), 1); QCOMPARE(h.size(), 1);
        QVERIFY(host.qrText().isEmpty());
        iiWebServer::LanLink link; QVERIFY(iiWebServer::LanLink::decode(qr, &link));
        QCOMPARE(client.peerName(), link.name);
        QTRY_VERIFY(!fixture.client.busy()); QCOMPARE(fixture.client.currentHost(), link.hostId);
        QCOMPARE(fixture.client.entries().size(), 1); // Only the public Files root is exposed.
        QCOMPARE(fixture.client.entries().first().toMap().value("name").toString(), QString("hello.txt"));
        QCOMPARE(fixture.client.transport(), QString("local"));
        // Finish the optional QR flow while the first mirror adopts the host
        // UUID. Its local revalidation can reauthenticate the same host, which
        // requires normal automatic recovery rather than an open manual panel.
        host.cancel(); client.cancel();
        QCOMPARE(host.phase(), QString("idle")); QCOMPARE(client.phase(), QString("idle"));
        fixture.host.resumeAutomaticPairing(); fixture.client.resumeAutomaticPairing();
        QTRY_VERIFY_WITH_TIMEOUT(fixture.client.containerReady() && fixture.client.hostConnectionReady(), 15000);
        QVERIFY(fixture.client.localPeer()->connected());
        QCOMPARE(fixture.client.localPeer()->pairedDeviceIds(), QStringList{QString(64, 'a')});
        QCOMPARE(fixture.host.localPeer()->pairedDeviceIds(), QStringList{QString(64, 'b')});
        QCOMPARE(iiSocietyContainer::SocietyDrive::open(receiver.path())->identifier(),
                 iiSocietyContainer::SocietyDrive::open(fixture.container.path())->identifier());
        QTRY_VERIFY(!fixture.client.busy()); QCOMPARE(fixture.client.currentHost(), link.hostId);
        QCOMPARE(fixture.client.entries().size(), 1);
        QCOMPARE(fixture.client.entries().first().toMap().value("name").toString(), QString("hello.txt"));
        QCOMPARE(c.size(), 1); QCOMPARE(h.size(), 1);
        QSignalSpy downloaded(&fixture.client, &NetworkDriveController::downloadFinished);
        fixture.client.download("hello.txt", QUrl::fromLocalFile(receiver.filePath("Files/paired-download.txt")));
        QTRY_COMPARE(downloaded.size(), 1);
        QFile file(receiver.filePath("Files/paired-download.txt")); QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("hello from desktop"));
        QSettings settings(QSettings::IniFormat, QSettings::UserScope, "iisacc", "SocietyPairing");
        QVERIFY(settings.allKeys().isEmpty());
        for (const auto &key : settings.allKeys()) {
            QVERIFY(!settings.value(key).toString().contains(qr));
            QVERIFY(!settings.value(key).toString().contains("alice"));
        }
        client.cancel(); client.scanCode(qr); QTRY_COMPARE(client.phase(), QString("error"));
        QCOMPARE(c.size(), 1); // The captured image cannot be replayed.
    }
    void disconnectStopsLocalHostingWithoutChangingRole() {
        Fixture fixture; QVERIFY(fixture.start());
        DevicePairing host, client; host.setNetwork(&fixture.host); client.setNetwork(&fixture.client);
        host.showHostQr(); QTRY_COMPARE(host.phase(), QString("showing"));
        client.scanCode(host.qrText()); QTRY_COMPARE(client.phase(), QString("paired"));
        fixture.host.disconnectSession();
        QCOMPARE(fixture.host.mode(), NetworkDriveController::HostMode);
        QVERIFY(!fixture.host.hosting()); QTRY_VERIFY(!fixture.client.connected());
    }
    void failedFilesProbeNeverCompletesPairing() {
        Fixture fixture; QVERIFY(fixture.start());
        DevicePairing host, client; host.setNetwork(&fixture.host); client.setNetwork(&fixture.client);
        QSignalSpy complete(&client, &DevicePairing::paired);
        host.showHostQr(); QTRY_COMPARE(host.phase(), QString("showing"));
        const auto qr = host.qrText();
        QVERIFY(QDir().rename(fixture.container.filePath("Files"), fixture.container.filePath("ReplacedFiles")));
        client.scanCode(qr); QTRY_COMPARE(client.phase(), QString("error"));
        QTRY_COMPARE(host.phase(), QString("error"));
        QCOMPARE(complete.size(), 0);
        QVERIFY(QSettings(QSettings::IniFormat, QSettings::UserScope, "iisacc", "SocietyPairing").allKeys().isEmpty());
    }
    void unsafeQrCannotRedirectTheAccountSession() {
        Fixture fixture; QVERIFY(fixture.start());
        DevicePairing client; client.setNetwork(&fixture.client);
        const auto relay = fixture.client.activeRelayUrl();
        iiWebServer::PairingLink link{QUrl("wss://attacker.example/relay"), "desktop", QString(64, 'a')};
        client.scanCode(link.encode()); QCOMPARE(client.phase(), QString("error"));
        QCOMPARE(fixture.client.activeRelayUrl(), relay); QVERIFY(!fixture.client.connected());
        client.scanCode("https://example.com"); QCOMPARE(client.phase(), QString("error"));
        QCOMPARE(fixture.client.activeRelayUrl(), relay);
    }
    void qrPanelFitsSmallAndDesktopWindows_data() {
        QTest::addColumn<QSize>("size");
        QTest::newRow("desktop") << QSize(1120, 720);
        QTest::newRow("phone-width") << QSize(390, 844);
        QTest::newRow("compact-landscape") << QSize(640, 360);
    }
    void qrPanelFitsSmallAndDesktopWindows() {
        QFETCH(QSize, size); Fixture fixture; QVERIFY(fixture.start());
        fixture.host.resumeAutomaticPairing();
        QTRY_VERIFY(fixture.host.discovering());
        DevicePairing pairing; pairing.setNetwork(&fixture.host); QrScanner scanner;
        QQmlEngine engine; engine.addImportPath(SOCIETY_LVRS_QML_IMPORT_PATH);
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&](const QList<QQmlError> &errors) { for (const auto &error : errors) warnings.append(error.toString()); });
        QQuickWindow window; window.resize(size); window.show();
        QQmlComponent component(&engine, QUrl::fromLocalFile(SOCIETY_PAIRING_QML_FILE));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> panel(component.createWithInitialProperties({{"pairing", QVariant::fromValue(&pairing)},
            {"scanner", QVariant::fromValue(&scanner)}, {"appWindow", QVariant::fromValue(&window)},
            {"parent", QVariant::fromValue(window.contentItem())}}));
        QVERIFY2(panel, qPrintable(component.errorString()));
        QVERIFY(QMetaObject::invokeMethod(panel.get(), "open")); QTRY_COMPARE(pairing.phase(), QString("showing"));
        QVERIFY(panel->property("width").toReal() <= size.width()); QVERIFY(panel->property("height").toReal() <= size.height());
        auto *qr = panel->findChild<PairingQr *>("pairingQr"); QVERIFY(qr); QTRY_VERIFY(qr->valid() && qr->isVisible());
        QVERIFY(!qr->smooth());
        if (size.width() == 1120) QTRY_VERIFY(qr->width() >= 400);
        QSignalSpy captured(&scanner, &QrScanner::codeCaptured);
        scanner.captured(pairing.qrText()); QCOMPARE(captured.size(), 0); // No stale camera callback after stop.
        const auto screenshot = qEnvironmentVariable("SOCIETY_PAIRING_SCREENSHOT");
        if (!screenshot.isEmpty() && size.width() == 1120) {
            QTest::qWait(150); const auto frame = window.grabWindow(); QVERIFY(frame.save(screenshot));
#ifdef Q_OS_MACOS
            QCOMPARE(decodePairingQrImage(frame), pairing.qrText()); // Decode the actual rendered window, too.
            QCOMPARE(decodePairingQrCameraFrame(frame), pairing.qrText());
#endif
        }
        DevicePairing client; client.setNetwork(&fixture.client);
        client.scanCode(pairing.qrText()); QTRY_COMPARE(pairing.phase(), QString("paired"));
        auto *done = panel->findChild<QQuickItem *>("pairingDone");
        auto *status = panel->findChild<QQuickItem *>("pairingStatus");
        QVERIFY(done && status); QTRY_VERIFY(done->isVisible()); QVERIFY(!qr->isVisible());
        QVERIFY(status->property("text").toString().contains("local network"));
        QSignalSpy filesRequested(panel.get(), SIGNAL(filesRequested()));
        QVERIFY(QMetaObject::invokeMethod(done, "clicked")); QTRY_COMPARE(pairing.phase(), QString("idle"));
        QCOMPARE(filesRequested.size(), 1); QTRY_VERIFY(!panel->property("visible").toBool());
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    }
};
int main(int argc, char **argv) {
    lvrs::AppBootstrapOptions options; options.applicationName = "SocietyPairingTests";
    options.quickStyleName = "Basic"; options.bootstrapGraphicsBackend = false;
    options.configureRenderQualityDefaults = false; options.logBootstrapDiagnostics = false; options.logGraphicsBackend = false;
    if (!lvrs::preApplicationBootstrap(options).ok) return 1;
    QGuiApplication app(argc, argv); lvrs::postApplicationBootstrap(app, options);
    PairingTests tests; return QTest::qExec(&tests, argc, argv);
}
#include "tst_pairing.moc"

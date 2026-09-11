#include "App/Network/DevicePairing.h"
#include "PairingCredentialsFixture.h"
#include "App/Network/PairingQr.h"
#include "App/Network/QrScanner.h"
#include "FakeDiscoveryService.h"
#include <QGuiApplication>
#include <QImageReader>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include "backend/runtime/appbootstrap.h"

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
struct Fixture {
    QTemporaryDir container{SOCIETY_TEST_DIRECTORY "/pairing-files-XXXXXX"};
    NetworkDriveController host, client;
    bool start() {
        if (!iiSocietyContainer::SocietyDrive::create(container.path())) return false;
        QFile file(container.filePath("Files/hello.txt"));
        if (!file.open(QIODevice::WriteOnly) || file.write("hello from desktop") < 0) return false;
        file.close();
        host.setContainerPath(container.path());
        return true;
    }
};
}
class PairingTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, QStringLiteral(SOCIETY_TEST_DIRECTORY "/pairing-settings"));
        qmlRegisterType<AccountController>("Society", 1, 0, "AccountController");
        qmlRegisterType<NetworkDriveController>("Society", 1, 0, "NetworkDriveController");
        qmlRegisterType<DevicePairing>("Society", 1, 0, "DevicePairing");
        qmlRegisterType<PairingQr>("Society", 1, 0, "PairingQr");
        qmlRegisterType<QrScanner>("Society", 1, 0, "QrScanner");
    }
    void init() { QSettings(QSettings::IniFormat, QSettings::UserScope, "iisacc", "SocietyPairing").clear(); }
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
        iiServerHost::LanLink link; QVERIFY(iiServerHost::LanLink::decode(payload, &link));
        // Inspect the expired photograph only. Never connect using a user's captured offer.
        QVERIFY(link.expiresAt < QDateTime::currentDateTimeUtc());
        qInfo("Photographed Society QR decoded: %lld characters; no connection attempted.", qlonglong(payload.size()));
#else
        QSKIP("Apple camera decoder requires macOS.");
#endif
    }
    void automaticQueuePairsSeveralDevicesAndSurvivesPanelClosing() {
        FakeDiscoveryService a, b, c;
        NetworkDriveController desktop(&a, QHostAddress::LocalHost), phone(&b, QHostAddress::LocalHost), tablet(&c, QHostAddress::LocalHost);
        QTemporaryDir root(SOCIETY_TEST_DIRECTORY "/automatic-files-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(root.path())); desktop.setContainerPath(root.path());
        QFile file(root.filePath("Files/automatic.txt")); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("automatic LAN transfer"); file.close();
        const auto scope = NearbyDevices::accountScope("test", "automatic-fixture");
        desktop.discovery()->setIdentity(scope, "desktop", "Desktop", "pc", true);
        phone.discovery()->setIdentity(scope, "phone", "Phone", "phone", false);
        tablet.discovery()->setIdentity(scope, "tablet", "Tablet", "tablet", false);
        // Two-day-old grants must complete TLS pairing and transfer without a
        // service request or a freshly issued five-minute key.
        for (auto *network : {&desktop, &phone, &tablet})
            network->discovery()->setCredentials(localPairingCredentialsFixture(scope, 'a', QDateTime::currentDateTimeUtc().addDays(-2)));
        a.announceTo(b); a.announceTo(c); b.announceTo(a); c.announceTo(a);
        QSignalSpy paired(desktop.localPeer(), &iiServerHost::LanPeer::paired);
        QTRY_VERIFY(!desktop.pairingQueue().isEmpty());
        { DevicePairing panel; panel.setNetwork(&desktop); panel.cancel(); }
        QTRY_COMPARE_WITH_TIMEOUT(paired.size(), 2, 15000);
        QTRY_COMPARE(phone.entries().size(), 1); QTRY_COMPARE(tablet.entries().size(), 1);
        QCOMPARE(phone.entries()[0].toMap().value("name").toString(), "automatic.txt");
        QTRY_VERIFY(desktop.pairingQueue().isEmpty());
        b.announceTo(a); b.announceTo(a); c.announceTo(a); QTest::qWait(1100); QCOMPARE(paired.size(), 2);
        const auto destination = QUrl::fromLocalFile(root.filePath("copied.txt"));
        QSignalSpy downloaded(&phone, &NetworkDriveController::downloadFinished);
        phone.download("automatic.txt", destination); QTRY_COMPARE(downloaded.size(), 1);
        QFile copied(destination.toLocalFile()); QVERIFY(copied.open(QIODevice::ReadOnly)); QCOMPARE(copied.readAll(), QByteArray("automatic LAN transfer"));
        phone.disconnectSession(); QVERIFY(!phone.automaticPairingEnabled());
        QTest::qWait(1100); QVERIFY(!phone.localPeer()->connected());
        phone.resumeAutomaticPairing();
        QTRY_VERIFY_WITH_TIMEOUT(phone.localPeer()->connected(), 15000);
        desktop.discovery()->clear();
        QTRY_VERIFY(!phone.localPeer()->connected()); QVERIFY(!desktop.hosting()); QVERIFY(desktop.pairingQueue().isEmpty());
    }
    void establishedPrimaryOutranksANewLowerIdDesktop() {
        FakeDiscoveryService a, b;
        NetworkDriveController established(&a, QHostAddress::LocalHost), newcomer(&b, QHostAddress::LocalHost);
        QTemporaryDir root(SOCIETY_TEST_DIRECTORY "/primary-election-XXXXXX"), other(SOCIETY_TEST_DIRECTORY "/newcomer-election-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(root.path())); QVERIFY(iiSocietyContainer::SocietyDrive::create(other.path()));
        established.setContainerPath(root.path()); newcomer.setContainerPath(other.path());
        const auto scope = NearbyDevices::accountScope("test", "primary-election");
        established.discovery()->setIdentity(scope, "z-primary", "Primary", "pc", true);
        newcomer.discovery()->setIdentity(scope, "a-new", "New desktop", "pc", true);
        established.discovery()->setCredentials(localPairingCredentialsFixture(scope), true, "z-primary");
        newcomer.discovery()->setCredentials(localPairingCredentialsFixture(scope));
        a.announceTo(b); b.announceTo(a);
        QTRY_VERIFY_WITH_TIMEOUT(newcomer.localPeer()->connected(), 10000);
        QVERIFY(established.hosting()); QVERIFY(!newcomer.hosting());
        // A remembered mirror does not elect itself while its primary is absent.
        newcomer.discovery()->setCredentials(localPairingCredentialsFixture(scope), false, "z-primary");
        established.discovery()->clear(); QTest::qWait(700);
        QVERIFY(!newcomer.hosting());
    }
    void automaticHostElectionAndFailedPeerDoNotBlockOtherDevices() {
        FakeDiscoveryService a, b, c;
        NetworkDriveController first(&a, QHostAddress::LocalHost), second(&b, QHostAddress::LocalHost), unavailable(&c, QHostAddress::LocalHost);
        QTemporaryDir root(SOCIETY_TEST_DIRECTORY "/automatic-election-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(root.path()));
        first.setContainerPath(root.path()); second.setContainerPath(root.path());
        const auto scope = NearbyDevices::accountScope("test", "election-fixture");
        first.discovery()->setIdentity(scope, "a-desktop", "First desktop", "pc", true);
        second.discovery()->setIdentity(scope, "c-desktop", "Second desktop", "pc", true);
        unavailable.discovery()->setIdentity(scope, "b-offline", "Unavailable phone", "phone", false);
        for (auto *network : {&first, &second, &unavailable}) network->discovery()->setCredentials(localPairingCredentialsFixture(scope));
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
        FakeDiscoveryService a, b;
        NetworkDriveController desktop(&a, QHostAddress::LocalHost), phone(&b, QHostAddress::LocalHost);
        QTemporaryDir root(SOCIETY_TEST_DIRECTORY "/discovered-files-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(root.path())); desktop.setContainerPath(root.path());
        const auto scope = NearbyDevices::accountScope("test", "noncredential-device-discovery");
        desktop.discovery()->setIdentity(scope, "desktop", "Desktop", "pc", true);
        phone.discovery()->setIdentity(scope, "phone", "Phone", "phone", false);
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
        QTRY_VERIFY(!phone.busy()); QCOMPARE(phone.currentHost(), "desktop"); QCOMPARE(phone.transport(), "local");
        QVERIFY(desktop.nearbyDevices()[0].toMap().value("connected").toBool());
        host.cancel(); QVERIFY(desktop.hosting()); QVERIFY(phone.connected());
    }
    void endingDiscoveryIdentityCancelsUnconfirmedConnection() {
        FakeDiscoveryService a, b;
        NetworkDriveController desktop(&a, QHostAddress::LocalHost), phone(&b, QHostAddress::LocalHost);
        QTemporaryDir root(SOCIETY_TEST_DIRECTORY "/ending-discovery-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(root.path())); desktop.setContainerPath(root.path());
        const auto scope = NearbyDevices::accountScope("test", "noncredential-identity");
        desktop.discovery()->setIdentity(scope, "desktop", "Desktop", "pc", true);
        phone.discovery()->setIdentity(scope, "phone", "Phone", "phone", false);
        a.announceTo(b); b.announceTo(a);
        DevicePairing host, client; host.setNetwork(&desktop); client.setNetwork(&phone);
        host.inviteDevice("phone"); QTRY_COMPARE(client.phase(), "invited");
        client.acceptInvitation(); QTRY_COMPARE(host.phase(), "confirming");
        desktop.discovery()->clear(); QTRY_COMPARE(client.phase(), "error");
        QVERIFY(!host.canConfirm()); QVERIFY(!phone.connected()); QVERIFY(desktop.nearbyDevices().isEmpty());
    }
    void qrScanPairsBothSidesAndReadsFiles() {
        Fixture fixture; QVERIFY(fixture.start());
        QVERIFY(!fixture.host.signedIn()); QVERIFY(fixture.host.relayUrl().isEmpty());
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
        iiServerHost::LanLink link; QVERIFY(iiServerHost::LanLink::decode(qr, &link));
        QCOMPARE(client.peerName(), link.name);
        QTRY_VERIFY(!fixture.client.busy()); QCOMPARE(fixture.client.currentHost(), link.hostId);
        QCOMPARE(fixture.client.entries().size(), 1);
        QCOMPARE(fixture.client.transport(), QString("local"));
        QSignalSpy downloaded(&fixture.client, &NetworkDriveController::downloadFinished);
        fixture.client.download("hello.txt", QUrl::fromLocalFile(fixture.container.filePath("paired-download.txt")));
        QTRY_COMPARE(downloaded.size(), 1);
        QFile file(fixture.container.filePath("paired-download.txt")); QVERIFY(file.open(QIODevice::ReadOnly));
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
    void switchingBackToClientStopsLocalHosting() {
        Fixture fixture; QVERIFY(fixture.start());
        DevicePairing host, client; host.setNetwork(&fixture.host); client.setNetwork(&fixture.client);
        host.showHostQr(); QTRY_COMPARE(host.phase(), QString("showing"));
        client.scanCode(host.qrText()); QTRY_COMPARE(client.phase(), QString("paired"));
        fixture.host.setMode(NetworkDriveController::ClientMode);
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
        iiServerHost::PairingLink link{QUrl("wss://attacker.example/relay"), "desktop", QString(64, 'a')};
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

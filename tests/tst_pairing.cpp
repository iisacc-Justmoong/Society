#include "App/Network/DevicePairing.h"
#include "App/Network/PairingQr.h"
#include "App/Network/QrScanner.h"
#include <QGuiApplication>
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
#endif
namespace {
struct Fixture {
    QTemporaryDir container{SOCIETY_TEST_DIRECTORY "/pairing-files-XXXXXX"};
    iiServerHost::RelayServer relay{[](const auto &token, iiServerHost::AuthCompletion done) {
        done({QString::fromUtf8(token), QDateTime::currentDateTimeUtc().addSecs(60)});
    }};
    NetworkDriveController host, client;
    bool start(bool local = false) {
        if (!iiSocietyContainer::SocietyDrive::create(container.path()) || !relay.listen(QHostAddress::LocalHost)) return false;
        QFile file(container.filePath("Files/hello.txt"));
        if (!file.open(QIODevice::WriteOnly) || file.write("hello from desktop") < 0) return false;
        file.close();
        iiServerHost::PeerOptions o;
        o.relayUrl = QUrl(QString("ws://127.0.0.1:%1").arg(relay.port()));
        o.peerId = "desktop"; o.name = "My Desktop"; o.credential = "alice";
        o.localEnabled = local; o.listenAddress = QHostAddress::LocalHost;
        o.localTimeoutMs = 150; o.requestTimeoutMs = 2000;
        host.setContainerPath(container.path()); host.setMode(NetworkDriveController::HostMode);
        if (!host.startSession(o)) return false;
        o.peerId = "iphone"; o.name = "My iPhone";
        return client.startSession(o);
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
    void qrScanPairsBothSidesAndReadsFiles_data() {
        QTest::addColumn<bool>("local");
        QTest::newRow("local") << true; QTest::newRow("remote") << false;
    }
    void qrScanPairsBothSidesAndReadsFiles() {
        QFETCH(bool, local); Fixture fixture; QVERIFY(fixture.start(local));
        QTRY_VERIFY(fixture.host.hosting() && fixture.client.connected());
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
        QVERIFY(host.qrText().isEmpty()); QCOMPARE(client.peerName(), QString("My Desktop"));
        QTRY_VERIFY(!fixture.client.busy()); QCOMPARE(fixture.client.currentHost(), QString("desktop"));
        QCOMPARE(fixture.client.entries().size(), 1);
        QCOMPARE(fixture.client.transport(), local ? QString("local") : QString("remote"));
        QSignalSpy downloaded(&fixture.client, &NetworkDriveController::downloadFinished);
        fixture.client.download("hello.txt", QUrl::fromLocalFile(fixture.container.filePath("paired-download.txt")));
        QTRY_COMPARE(downloaded.size(), 1);
        QFile file(fixture.container.filePath("paired-download.txt")); QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("hello from desktop"));
        QSettings settings(QSettings::IniFormat, QSettings::UserScope, "iisacc", "SocietyPairing");
        QCOMPARE(settings.allKeys().size(), 2);
        for (const auto &key : settings.allKeys()) {
            QVERIFY(!settings.value(key).toString().contains(qr));
            QVERIFY(!settings.value(key).toString().contains("alice"));
        }
        client.cancel(); client.scanCode(qr); QTRY_COMPARE(client.phase(), QString("error"));
        QCOMPARE(c.size(), 1); // The captured image cannot be replayed.
    }
    void rememberedHostReconnectsOnlyInsideItsAccountAndRelay() {
        Fixture fixture; QVERIFY(fixture.start()); QTRY_VERIFY(fixture.host.hosting() && fixture.client.connected());
        DevicePairing host, client; host.setNetwork(&fixture.host); client.setNetwork(&fixture.client);
        host.showHostQr(); QTRY_COMPARE(host.phase(), QString("showing"));
        client.scanCode(host.qrText()); QTRY_COMPARE(client.phase(), QString("paired"));
        client.cancel();
        auto relayUrl = fixture.client.activeRelayUrl(); fixture.client.disconnectSession();
        iiServerHost::PeerOptions options; options.relayUrl = relayUrl;
        options.peerId = "iphone"; options.credential = "alice"; options.localEnabled = false;
        QVERIFY(fixture.client.startSession(options)); QTRY_COMPARE(fixture.client.currentHost(), QString("desktop"));
        QTRY_VERIFY(!fixture.client.busy()); QCOMPARE(fixture.client.entries().size(), 1);

        // The same host identifier in a different account must not match the saved host.
        fixture.client.disconnectSession();
        NetworkDriveController bobHost;
        bobHost.setContainerPath(fixture.container.path()); bobHost.setMode(NetworkDriveController::HostMode);
        options.peerId = "desktop"; options.credential = "bob"; QVERIFY(bobHost.startSession(options));
        QTRY_VERIFY(bobHost.hosting());
        options.peerId = "iphone"; QVERIFY(fixture.client.startSession(options));
        QTRY_COMPARE(fixture.client.hosts().size(), 1); QVERIFY(fixture.client.currentHost().isEmpty());

        fixture.client.disconnectSession();
        Fixture otherRelay; QVERIFY(otherRelay.start()); QTRY_VERIFY(otherRelay.host.hosting());
        options.relayUrl = otherRelay.host.activeRelayUrl(); options.peerId = "second-iphone"; options.credential = "alice";
        QVERIFY(fixture.client.startSession(options));
        QTRY_COMPARE(fixture.client.hosts().size(), 1); QVERIFY(fixture.client.currentHost().isEmpty());
    }
    void failedFilesProbeNeverCompletesPairing() {
        Fixture fixture; QVERIFY(fixture.start()); QTRY_VERIFY(fixture.host.hosting() && fixture.client.connected());
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
        Fixture fixture; QVERIFY(fixture.start()); QTRY_VERIFY(fixture.client.connected());
        DevicePairing client; client.setNetwork(&fixture.client);
        const auto relay = fixture.client.activeRelayUrl();
        iiServerHost::PairingLink link{QUrl("wss://attacker.example/relay"), "desktop", QString(64, 'a')};
        client.scanCode(link.encode()); QCOMPARE(client.phase(), QString("error"));
        QCOMPARE(fixture.client.activeRelayUrl(), relay); QVERIFY(fixture.client.connected());
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
        QFETCH(QSize, size); Fixture fixture; QVERIFY(fixture.start()); QTRY_VERIFY(fixture.host.hosting());
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
        QSignalSpy captured(&scanner, &QrScanner::codeCaptured);
        scanner.captured(pairing.qrText()); QCOMPARE(captured.size(), 0); // No stale camera callback after stop.
        const auto screenshot = qEnvironmentVariable("SOCIETY_PAIRING_SCREENSHOT");
        if (!screenshot.isEmpty() && size.width() == 1120) {
            QTest::qWait(150); const auto frame = window.grabWindow(); QVERIFY(frame.save(screenshot));
#ifdef Q_OS_MACOS
            QCOMPARE(decodePairingQrImage(frame), pairing.qrText()); // Decode the actual rendered window, too.
#endif
        }
        DevicePairing client; client.setNetwork(&fixture.client); QTRY_VERIFY(fixture.client.connected());
        client.scanCode(pairing.qrText()); QTRY_COMPARE(pairing.phase(), QString("paired"));
        auto *done = panel->findChild<QQuickItem *>("pairingDone");
        auto *status = panel->findChild<QQuickItem *>("pairingStatus");
        QVERIFY(done && status); QTRY_VERIFY(done->isVisible()); QVERIFY(!qr->isVisible());
        QVERIFY(status->property("text").toString().contains("My iPhone"));
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

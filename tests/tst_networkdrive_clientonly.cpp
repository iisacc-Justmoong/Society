#include "App/Network/NetworkDriveController.h"
#include "App/Network/DevicePairing.h"
#include "App/Network/PairingQr.h"
#include "App/Network/QrScanner.h"
#include "App/Network/MobileSyncActivity.h"
#include <QFile>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTest>
#include "backend/runtime/appbootstrap.h"

using namespace iiServerHost;
class ClientOnlyNetworkTests : public QObject {
    Q_OBJECT
private slots:
    void screenIsRetainedOnlyForAnActiveConnectedTransfer() {
        QVERIFY(societySyncNeedsScreen(true, true, Qt::ApplicationActive));
        QVERIFY(!societySyncNeedsScreen(false, true, Qt::ApplicationActive)); // Completed batch.
        QVERIFY(!societySyncNeedsScreen(true, false, Qt::ApplicationActive)); // Lost transport.
        for (const auto state : {Qt::ApplicationInactive, Qt::ApplicationHidden, Qt::ApplicationSuspended})
            QVERIFY(!societySyncNeedsScreen(true, true, state)); // Never retain a background screen.
        QVERIFY(societySyncNeedsScreen(true, true, Qt::ApplicationActive)); // Resumed batch.
    }
    void initTestCase() {
        qmlRegisterType<NetworkDriveController>("Society", 1, 0, "NetworkDriveController");
        qmlRegisterType<AccountController>("Society", 1, 0, "AccountController");
        qmlRegisterType<DevicePairing>("Society", 1, 0, "DevicePairing");
        qmlRegisterType<PairingQr>("Society", 1, 0, "PairingQr");
        qmlRegisterType<QrScanner>("Society", 1, 0, "QrScanner");
    }
    void devicePanelOffersOnlyClientMode() {
        QQmlEngine engine;
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&](const QList<QQmlError> &errors) {
            for (const auto &error : errors) warnings.append(error.toString());
        });
        QQuickWindow window; window.resize(390, 844); window.show();
        NetworkDriveController network;
        QQmlComponent component(&engine, QUrl::fromLocalFile(QString::fromUtf8(SOCIETY_NETWORK_QML_FILE)));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> panel(component.createWithInitialProperties({
            {"network", QVariant::fromValue(&network)}, {"parent", QVariant::fromValue(window.contentItem())}}));
        QVERIFY2(panel, qPrintable(component.errorString()));
        QVERIFY(QMetaObject::invokeMethod(panel.get(), "open"));
        QTRY_VERIFY(panel->property("visible").toBool());
        auto *modeLabel = panel->findChild<QQuickItem *>("networkModeLabel");
        auto *settings = panel->findChild<QQuickItem *>("networkPreferences");
        QVERIFY(modeLabel && settings);
        QCOMPARE(modeLabel->property("text").toString(), QString("Client mode"));
        auto *pairButton = panel->findChild<QQuickItem *>("networkPairing");
        QVERIFY(pairButton && pairButton->isVisible());
        QCOMPARE(pairButton->property("text").toString(), QString("Pair desktop"));
        QSignalSpy pairRequested(panel.get(), SIGNAL(pairingRequested()));
        QVERIFY(QMetaObject::invokeMethod(pairButton, "clicked")); QCOMPARE(pairRequested.size(), 1);
        QVERIFY(!settings->isVisible());
        QQmlComponent preferencesComponent(&engine, QUrl::fromLocalFile(QString::fromUtf8(SOCIETY_PREFERENCES_QML_FILE)));
        QVERIFY2(preferencesComponent.isReady(), qPrintable(preferencesComponent.errorString()));
        std::unique_ptr<QObject> preferences(preferencesComponent.createWithInitialProperties({
            {"network", QVariant::fromValue(&network)}, {"transientParent", QVariant::fromValue(&window)}}));
        QVERIFY2(preferences, qPrintable(preferencesComponent.errorString()));
        QVERIFY(QMetaObject::invokeMethod(preferences.get(), "open"));
        QVERIFY(!preferences->property("visible").toBool()); // Desktop preferences cannot be opened on mobile.
        auto *hostButton = preferences->findChild<QQuickItem *>("preferencesHostMode");
        QVERIFY(hostButton); QVERIFY(!hostButton->isEnabled());
        QVERIFY(QMetaObject::invokeMethod(hostButton, "clicked"));
        QCOMPARE(network.mode(), NetworkDriveController::ClientMode);
        QVERIFY(!network.hosting());
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    }
    void phonePairingPanelOffersCameraWithoutLoginOrRelay() {
        QQmlEngine engine; engine.addImportPath(SOCIETY_LVRS_QML_IMPORT_PATH);
        QQuickWindow window; window.resize(390, 844); window.show();
        NetworkDriveController network; DevicePairing pairing; pairing.setNetwork(&network); QrScanner scanner;
        QVERIFY(!network.signedIn()); QVERIFY(network.relayUrl().isEmpty());
        QQmlComponent component(&engine, QUrl::fromLocalFile(SOCIETY_PAIRING_QML_FILE));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> panel(component.createWithInitialProperties({
            {"pairing", QVariant::fromValue(&pairing)}, {"scanner", QVariant::fromValue(&scanner)},
            {"appWindow", QVariant::fromValue(&window)}, {"parent", QVariant::fromValue(window.contentItem())}}));
        QVERIFY2(panel, qPrintable(component.errorString())); QVERIFY(QMetaObject::invokeMethod(panel.get(), "open"));
        QTRY_VERIFY(panel->property("visible").toBool()); QVERIFY(!panel->property("desktop").toBool());
        auto *scan = panel->findChild<QQuickItem *>("pairingScan");
        auto *refresh = panel->findChild<QQuickItem *>("pairingRefresh");
        QVERIFY(scan && refresh); QVERIFY(scan->isVisible() && scan->isEnabled()); QVERIFY(!refresh->isVisible());
        QVERIFY(!panel->findChild<QQuickItem *>("pairingSignIn"));
        QTRY_VERIFY(window.contentItem()->boundingRect().contains(scan->mapRectToScene(scan->boundingRect())));
        pairing.showHostQr(); QCOMPARE(pairing.phase(), QString("error")); QVERIFY(!network.hosting());
        QVERIFY(!network.startLocalHost()); QVERIFY(!network.localPeer()->hosting());
        QVERIFY(QMetaObject::invokeMethod(panel.get(), "close")); QTRY_COMPARE(pairing.phase(), QString("idle"));
    }
    void mobileAlwaysRemainsAClient_data() {
        QTest::addColumn<bool>("local");
        QTest::newRow("local") << true;
        QTest::newRow("remote") << false;
    }
    void mobileAlwaysRemainsAClient() {
        QFETCH(bool, local);
        QTemporaryDir sourceRoot(SOCIETY_TEST_DIRECTORY "/mobile-source-XXXXXX");
        QTemporaryDir container(SOCIETY_TEST_DIRECTORY "/mobile-container-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(container.path()));
        QFile source(sourceRoot.filePath("shared.txt")); QVERIFY(source.open(QIODevice::WriteOnly));
        source.write("from desktop"); source.close();
        RelayServer relay([](const auto &, AuthCompletion done) { done({"alice", QDateTime::currentDateTimeUtc().addSecs(60)}); });
        QVERIFY(relay.listen(QHostAddress::LocalHost));
        PeerOptions options; options.relayUrl = QUrl(QString("ws://127.0.0.1:%1").arg(relay.port()));
        options.credential = "alice"; options.peerId = "desktop"; options.name = "Desktop";
        options.service = "com.iisacc.society.files";
        options.localEnabled = local; options.listenAddress = QHostAddress::LocalHost;
        Peer desktop; FileShare files(sourceRoot.path());
        QVERIFY(desktop.start(options, [&](const auto &, const auto &request) { return files.handle(request); }));
        QTcpServer occupied; QVERIFY(occupied.listen(QHostAddress::LocalHost));
        options.peerId = "mobile"; options.name = "Mobile";
        options.hostFiles = true; options.localHostingEnabled = true; options.localPort = occupied.serverPort();
        options.metadata = {{"section", "must-not-leak"}};
        NetworkDriveController mobile;
        QSignalSpy modeChanged(&mobile, &NetworkDriveController::modeChanged);
        QVERIFY(!mobile.hostModeAvailable());
        QCOMPARE(mobile.mode(), NetworkDriveController::ClientMode);
        mobile.setContainerPath(container.path());
        mobile.setMode(NetworkDriveController::HostMode);
        QCOMPARE(mobile.mode(), NetworkDriveController::ClientMode);
        QVERIFY(mobile.startSession(options)); // Would fail to bind the occupied port if hosting were enabled.
        QTRY_VERIFY(mobile.connected() && desktop.isReady());
        QVERIFY(!mobile.hosting());
        QTRY_COMPARE(mobile.hosts().size(), 1);
        mobile.browse("desktop"); QTRY_VERIFY(!mobile.busy());
        QCOMPARE(mobile.entries().size(), 1);
        QCOMPARE(mobile.transport(), local ? "local" : "remote");
        QSignalSpy saved(&mobile, &NetworkDriveController::downloadFinished);
        const auto target = container.filePath("Files/download.txt");
        mobile.download("shared.txt", QUrl::fromLocalFile(target)); QTRY_COMPARE(saved.size(), 1);
        QFile downloaded(target); QVERIFY(downloaded.open(QIODevice::ReadOnly));
        QCOMPARE(downloaded.readAll(), QByteArray("from desktop"));

        QSignalSpy state(&mobile, &NetworkDriveController::stateChanged);
        mobile.setContainerPath(container.filePath("missing"));
        mobile.setMode(NetworkDriveController::HostMode);
        QVERIFY(mobile.connected()); QCOMPARE(state.size(), 0);
        QVERIFY(QMetaObject::invokeMethod(qGuiApp, "applicationStateChanged", Qt::DirectConnection,
            Q_ARG(Qt::ApplicationState, Qt::ApplicationSuspended)));
        QVERIFY(!mobile.connected());
        mobile.setMode(NetworkDriveController::HostMode);
        QVERIFY(mobile.startSession(options));
        QVERIFY(!mobile.connected()); // A supplied session cannot bypass mobile suspension.
        QVERIFY(QMetaObject::invokeMethod(qGuiApp, "applicationStateChanged", Qt::DirectConnection,
            Q_ARG(Qt::ApplicationState, Qt::ApplicationActive)));
        QTRY_VERIFY(mobile.connected());
        QCOMPARE(mobile.mode(), NetworkDriveController::ClientMode);
        QVERIFY(!mobile.hosting()); QCOMPARE(modeChanged.size(), 0);
        QSignalSpy result(&desktop, &Peer::completed);
        const auto request = desktop.request("mobile", {{"op", "list"}, {"path", ""}});
        QTRY_COMPARE(result.size(), 1);
        QCOMPARE(result[0][0].toString(), request);
        QVERIFY(!result[0][1].toJsonObject().value("ok").toBool());
        QVERIFY(desktop.peers().isEmpty());
        mobile.disconnectSession();
        QVERIFY(QMetaObject::invokeMethod(qGuiApp, "applicationStateChanged", Qt::DirectConnection,
            Q_ARG(Qt::ApplicationState, Qt::ApplicationActive)));
        QVERIFY(!mobile.connected());
    }
};

int main(int argc, char **argv) {
    lvrs::AppBootstrapOptions options;
    options.applicationName = QStringLiteral("SocietyClientOnlyNetworkTests");
    options.quickStyleName = QStringLiteral("Basic");
    options.bootstrapGraphicsBackend = false;
    options.configureRenderQualityDefaults = false;
    options.logBootstrapDiagnostics = false;
    options.logGraphicsBackend = false;
    if (!lvrs::preApplicationBootstrap(options).ok) return 1;
    QGuiApplication app(argc, argv);
    lvrs::postApplicationBootstrap(app, options);
    ClientOnlyNetworkTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "tst_networkdrive_clientonly.moc"

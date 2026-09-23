#include "App/Network/NetworkDriveController.h"
#include "AccountServer.h"
#include "MemorySessionStore.h"
#include <QCryptographicHash>
#include "App/Network/DevicePairing.h"
#include "App/Network/PairingQr.h"
#include "App/Network/QrScanner.h"
#include "App/Network/MobileSyncActivity.h"
#include <QFile>
#include <QJsonDocument>
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
#include <QMetaProperty>
#include "backend/runtime/appbootstrap.h"

using namespace iiServerHost;
class ClientOnlyNetworkTests : public QObject {
    Q_OBJECT
private slots:
    void legacyHostPreferenceCannotPromoteMobileAfterSessionRestore() {
        AccountServer authority; QVERIFY(authority.server.listen(QHostAddress::LocalHost));
        MemorySessionStore store;
        AccountController account(authority.url(), &store, nullptr);
        QSignalSpy restored(&account, &AccountController::pairingStateRestored);
        QVERIFY(account.login("builder@example.com", "FixtureOnly1!"));
        QTRY_VERIFY(account.signedIn()); QTRY_COMPARE(restored.size(), 1);
        const QUrl endpoint("wss://nas.example.test/society");
        QVERIFY(account.setServerConfiguration({{"url", endpoint.toString()}, {"host", true}}));
        AccountController reopened(authority.url(), &store, nullptr);
        NetworkDriveController mobile; mobile.setRuntimeEnabled(false); mobile.setAccountSession(&reopened);
        QTRY_VERIFY(reopened.signedIn()); QTRY_COMPARE(mobile.relayUrl(), endpoint);
        QCOMPARE(mobile.mode(), NetworkDriveController::ClientMode);
        QVERIFY(!mobile.hosting()); QVERIFY(!reopened.serverConfiguration().contains("host"));
        QVERIFY(mobile.configureServer({}));
        QCOMPARE(mobile.mode(), NetworkDriveController::ClientMode);
    }
    void openingAMobileContainerUsesAnAsyncSnapshotAndCachedGetters() {
        QTemporaryDir first(SOCIETY_TEST_DIRECTORY "/mobile-sync-state-XXXXXX");
        QTemporaryDir second(SOCIETY_TEST_DIRECTORY "/mobile-sync-next-XXXXXX");
        const auto drive = iiSocietyContainer::SocietyDrive::create(first.path());
        QVERIFY(drive); QVERIFY(iiSocietyContainer::SocietyDrive::create(second.path()));
        QVERIFY(QDir(first.path()).mkpath(".society-sync"));
        QFile binding(first.filePath(".society-sync/mirror.json")); QVERIFY(binding.open(QIODevice::WriteOnly));
        AccountServer authority; QVERIFY(authority.server.listen(QHostAddress::LocalHost));
        authority.profile.insert("societyContainerDrive", QJsonObject{{"hostDeviceId", QString(64, 'a')}, {"containerId", drive->identifier()},
            {"revision", "1d02e288-9704-4c5a-979e-f9b60d1799ca"}, {"imagePath", "/Volumes/Society.sparsebundle"}});
        AccountController account(authority.url()); QVERIFY(account.login("builder@example.com", "FixtureOnly1!")); QTRY_VERIFY(account.signedIn());
        binding.write(QJsonDocument(QJsonObject{{"schema", 1}, {"host", QString(64, 'a')},
            {"container", drive->identifier()}, {"scope", account.storageScope()}, {"complete", true}}).toJson());
        binding.close();
        NetworkDriveController network;
        network.setAccountSession(&account);
        network.setContainerPath(first.path());
        QVERIFY2(!network.containerReady(), "Mobile container opening must return before reading its sync metadata");
        QTRY_VERIFY(network.containerReady());
        QVERIFY2(!network.hostConnectionReady(), "An offline complete mirror cannot bypass host validation at launch");
        const auto manifest = first.filePath(".society-drive.json");
        QVERIFY(QFile::rename(manifest, manifest + ".held"));
        // A QML getter must never re-open the filesystem. A lifecycle refresh
        // asynchronously detects the changed manifest and publishes new state.
        QVERIFY(network.containerReady());
        network.setApplicationState(Qt::ApplicationActive);
        QTRY_VERIFY(!network.containerReady());
        QVERIFY(QFile::rename(manifest + ".held", manifest));
        network.setContainerPath(first.path() + "/missing");
        network.setContainerPath(first.path());
        network.setContainerPath(second.path());
        QTest::qWait(100);
        QCOMPARE(network.containerPath(), second.path());
        QVERIFY2(!network.containerReady(), "A stale first-container result must not replace the selected container");
    }
    void foregroundOpeningAutomaticallyContinuesAnAuthenticatedSyncAndHonorsCancellation_data() {
        QTest::addColumn<bool>("background");
        QTest::newRow("foreground-expiration") << false;
        QTest::newRow("background-expiration") << true;
    }
    void foregroundOpeningAutomaticallyContinuesAnAuthenticatedSyncAndHonorsCancellation() {
        QFETCH(bool, background);
        AccountServer authority; QVERIFY(authority.server.listen(QHostAddress::LocalHost));
        MemorySessionStore store;
        AccountController account(authority.url(), &store, nullptr);
        QVERIFY(account.login("builder@example.com", "FixtureOnly1!")); QTRY_VERIFY(account.signedIn());
        RelayServer relay([&](const auto &, AuthCompletion done) {
            done({account.manager()->account()->sub(), QDateTime::currentDateTimeUtc().addSecs(60)});
        });
        QVERIFY(relay.listen(QHostAddress::LocalHost));
        QTemporaryDir root(SOCIETY_TEST_DIRECTORY "/automatic-continued-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(root.path()));
        PeerOptions options; options.relayUrl = QUrl(QString("ws://127.0.0.1:%1").arg(relay.port()));
        options.credential = "fixture"; options.peerId = "desktop"; options.name = "Desktop";
        options.service = "com.iisacc.society.files"; options.localEnabled = false;
        Peer desktop;
        // A real authenticated peer holds the first manifest response. The
        // foreground catch-up must obtain a grant before any Sync now click.
        QVERIFY(desktop.start(options, [](const auto &, const auto &) { return QJsonObject{{"ok", true}, {"pending", true}}; }));
        QTRY_VERIFY(desktop.isReady());
        int starts = 0; QList<bool> completions; std::function<void()> expire;
        NetworkDriveController mobile;
        mobile.backgroundActivity()->setBackend([](auto) { return true; }, [] {});
        mobile.backgroundActivity()->setContinuedBackend([&](auto callback) { ++starts; expire = callback; return true; },
            [&](bool success) { completions.append(success); }, [](auto, auto) {});
        mobile.setApplicationState(Qt::ApplicationActive);
        mobile.setContainerPath(root.path()); mobile.setAccountSession(&account);
        QVERIFY(mobile.configureServer(options.relayUrl));
        QTRY_VERIFY(mobile.connected()); QTRY_COMPARE(starts, 1);
        QVERIFY2(!mobile.hostConnectionReady(), "An authenticated socket with a pending manifest is not a usable host connection");
        QVERIFY(mobile.backgroundActivity()->continued());
        if (background) mobile.setApplicationState(Qt::ApplicationHidden);
        QVERIFY(mobile.backgroundActivity()->continued());
        expire(); QTRY_VERIFY(!mobile.backgroundActivity()->active());
        if (background) QTRY_VERIFY(!mobile.connected());
        else QVERIFY(mobile.connected());
        QCOMPARE(completions, QList<bool>{false});
        QTest::qWait(100); QCOMPARE(starts, 1);
        mobile.setApplicationState(Qt::ApplicationActive);
        QTRY_VERIFY(mobile.connected()); QTRY_COMPARE(starts, 2);
    }
    void continuedSyncUpgradesTheGrantAndCompletesOnlyAfterTheBatch() {
        MobileSyncActivity activity;
        int begins = 0, shortEnds = 0;
        QList<bool> completed;
        QList<QPair<qint64, qint64>> progress;
        std::function<void()> expire;
        activity.setBackend([](auto) { return true; }, [&] { ++shortEnds; });
        activity.setContinuedBackend([&](auto callback) { ++begins; expire = callback; return true; },
            [&](bool success) { completed.append(success); },
            [&](qint64 done, qint64 total) { progress.append({done, total}); });
        QVERIFY(activity.retain()); QVERIFY(activity.retainContinued());
        QCOMPARE(shortEnds, 1); QVERIFY(activity.continued());
        QVERIFY(activity.retainContinued()); QVERIFY(activity.retain()); QCOMPARE(begins, 1);
        activity.update("Models/first", 50, 100);
        activity.update("Models/first", 100, 100);
        activity.update("Models/second", 25, 100);
        QCOMPARE(progress, (QList<QPair<qint64, qint64>>{{50, 100}, {100, 101}, {125, 200}}));
        activity.update("Models/second", -1, 100); QCOMPARE(progress.size(), 3);
        const auto old = expire;
        activity.release(true); QCOMPARE(completed, QList<bool>{true});
        QVERIFY(activity.retainContinued()); old(); QCoreApplication::processEvents(); QVERIFY(activity.active());
        QSignalSpy expired(&activity, &MobileSyncActivity::expired);
        expire(); QTRY_COMPARE(expired.size(), 1);
        QCOMPARE(completed, (QList<bool>{true, false})); QVERIFY(!activity.active());
    }
    void interleavedPhotoAndFileProgressCountsEachAcknowledgementOnce() {
        MobileSyncActivity activity; QList<QPair<qint64, qint64>> progress;
        activity.setContinuedBackend([](auto) { return true; }, [](bool) {},
            [&](qint64 done, qint64 total) { progress.append({done, total}); });
        QVERIFY(activity.retainContinued());
        activity.update("Models/model", 50, 100);
        activity.update("Photos/index/photo", 20, 20);
        activity.update("Models/model", 75, 100);
        activity.update("Photos/index/photo", 20, 20); // Replayed completion.
        activity.update("Models/model", 100, 100);
        QCOMPARE(progress, (QList<QPair<qint64, qint64>>{{50,100}, {70,120}, {95,120}, {95,120}, {120,121}}));
        activity.release(true);
    }
    void presentationProgressAndCompletionSurviveExecutionExpiration() {
        MobileSyncActivity activity;
        std::function<void()> expire;
        QList<bool> completions;
        QList<QPair<qint64, qint64>> progress;
        activity.setContinuedBackend([&](auto callback) { expire = callback; return true; },
            [&](bool success) { completions.append(success); },
            [&](qint64 done, qint64 total) { progress.append({done, total}); });
        QVERIFY(activity.retainContinued());
        activity.update("Models/model", 50, 100);
        expire();
        QVERIFY(!activity.active() && activity.batchActive());
        QCOMPARE(completions, QList<bool>{false});
        // Foreground work continues despite losing the background grant.
        activity.update("Models/model", 100, 100);
        QCOMPARE(progress.last(), (QPair<qint64, qint64>{100, 101}));
        activity.release(true);
        QCOMPARE(completions, (QList<bool>{false, true}));
        QVERIFY(!activity.batchActive());
        activity.release(true);
        QCOMPARE(completions.size(), 2);
    }
    void expirationKeepsTheGrantUntilWorkersHaveReleasedTheirLocks() {
        MobileSyncActivity activity; std::function<void()> expire; QStringList order;
        activity.setContinuedBackend([&](auto callback) { expire = callback; return true; },
            [&](bool success) { QVERIFY(!success); order.append("released"); }, [](auto, auto) {});
        connect(&activity, &MobileSyncActivity::expired, this, [&] {
            QVERIFY(activity.active());
            order.append("workers-drained");
        });
        QVERIFY(activity.retainContinued()); expire();
        QTRY_COMPARE(order, (QStringList{"workers-drained", "released"}));
        QVERIFY(!activity.active());
    }
    void backgroundGrantSurvivesHidingAndExpiresWithoutReusingAnOldCallback() {
        MobileSyncActivity activity; std::function<void()> expire; int begins = 0, ends = 0;
        activity.setBackend([&](auto callback) { ++begins; expire = callback; return true; }, [&] { ++ends; });
        QSignalSpy expired(&activity, &MobileSyncActivity::expired);
        QVERIFY(activity.retain()); QVERIFY(activity.retain()); QCOMPARE(begins, 1);
        auto old = expire; activity.release(); QCOMPARE(ends, 1);
        QVERIFY(activity.retain()); old(); QTest::qWait(10); QVERIFY(activity.active()); QCOMPARE(expired.size(), 0);
        expire(); QTRY_COMPARE(expired.size(), 1); QVERIFY(!activity.active()); QCOMPARE(ends, 2);
        activity.release(); QCOMPARE(ends, 2);
        activity.setBackend([](auto) { return false; }, [&] { ++ends; });
        QVERIFY(!activity.retain()); activity.release(); QCOMPARE(ends, 2);
    }
    void grantedBackgroundTimeKeepsTheClientTransportUntilExpiration() {
        RelayServer relay([](const auto &, AuthCompletion done) { done({"alice", QDateTime::currentDateTimeUtc().addSecs(60)}); });
        QVERIFY(relay.listen(QHostAddress::LocalHost));
        PeerOptions options; options.relayUrl = QUrl(QString("ws://127.0.0.1:%1").arg(relay.port()));
        options.credential = "alice"; options.peerId = "mobile"; options.name = "Phone"; options.localEnabled = false;
        NetworkDriveController mobile; std::function<void()> expire;
        mobile.backgroundActivity()->setBackend([&](auto callback) { expire = callback; return true; }, [] {});
        QVERIFY(mobile.startSession(options)); QTRY_VERIFY(mobile.connected());
        QVERIFY(mobile.backgroundActivity()->retain());
        mobile.setApplicationState(Qt::ApplicationSuspended); QVERIFY(mobile.connected()); QVERIFY(!mobile.hosting());
        expire(); QTRY_VERIFY(!mobile.connected());
        mobile.setApplicationState(Qt::ApplicationActive); QTRY_VERIFY(mobile.connected()); QVERIFY(!mobile.hosting());
    }
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
        // This isolated network UI fixture keeps the optional drive property null.
        qmlRegisterUncreatableType<QObject>("Society", 1, 0, "DriveController", "No drive in this network fixture");
        qmlRegisterType<AccountController>("Society", 1, 0, "AccountController");
        qmlRegisterType<DevicePairing>("Society", 1, 0, "DevicePairing");
        qmlRegisterType<PairingQr>("Society", 1, 0, "PairingQr");
        qmlRegisterType<QrScanner>("Society", 1, 0, "QrScanner");
    }
    void mobileSheetsFollowTheFingerAndDismissOnRelease() {
        QQmlEngine engine; engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        QQuickWindow window; window.resize(390, 844); window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        NetworkDriveController network;
        QQmlComponent component(&engine, QUrl::fromLocalFile(QString::fromUtf8(SOCIETY_NETWORK_QML_FILE)));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> panel(component.createWithInitialProperties({
            {"network", QVariant::fromValue(&network)}, {"parent", QVariant::fromValue(window.contentItem())}}));
        QVERIFY2(panel, qPrintable(component.errorString()));
        QVERIFY(panel->setProperty("motionEnabled", false));
        QVERIFY(QMetaObject::invokeMethod(panel.get(), "open")); QTRY_VERIFY(panel->property("opened").toBool());
        auto *frame = qvariant_cast<QQuickItem *>(panel->property("contentItem")); QVERIFY(frame);
        auto *grabber = frame->findChild<QQuickItem *>("sheet_grabber"); QVERIFY(grabber); QVERIFY(grabber->isVisible());
        const auto from = grabber->mapToScene(QPointF(grabber->width() / 2, grabber->height() / 2)).toPoint();
        static auto *touch = QTest::createTouchDevice();
        QTest::touchEvent(&window, touch).press(0, from, &window); QTest::qWait(30);
        QTest::touchEvent(&window, touch).move(0, from + QPoint(0, 30), &window); QTest::qWait(30);
        QTest::touchEvent(&window, touch).move(0, from + QPoint(0, 190), &window); QTest::qWait(30);
        QVERIFY(panel->property("_dragOffset").toReal() > 100);
        QTest::touchEvent(&window, touch).release(0, from + QPoint(0, 190), &window);
        QTRY_VERIFY(!panel->property("visible").toBool());
    }
    void leftEdgeBackGestureIgnoresVerticalScrollingAndModalPages() {
        QQmlEngine engine; engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        QQuickWindow window; window.resize(390, 844); window.show(); window.requestActivate();
        QVERIFY(QTest::qWaitForWindowExposed(&window)); QTRY_VERIFY(window.isActive());
        QQmlComponent component(&engine, QUrl::fromLocalFile(QString::fromUtf8(SOCIETY_NETWORK_QML_FILE)).resolved(QUrl("../MobileGestures.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> gestures(component.createWithInitialProperties({
            {"appWindow", QVariant::fromValue(&window)}, {"backEnabled", true}, {"parent", QVariant::fromValue(window.contentItem())}}));
        QVERIFY2(gestures, qPrintable(component.errorString())); QSignalSpy back(gestures.get(), SIGNAL(backRequested()));
        auto *surface = new QQuickItem(window.contentItem()); surface->setSize(window.size()); surface->setAcceptTouchEvents(true);
        static auto *touch = QTest::createTouchDevice();
        const auto swipe = [&](QPoint from, QPoint to) {
            QTest::touchEvent(&window, touch).press(0, from, &window); QTest::qWait(25);
            QTest::touchEvent(&window, touch).move(0, (from + to) / 2, &window); QTest::qWait(25);
            QTest::touchEvent(&window, touch).move(0, to, &window); QTest::qWait(25);
            QTest::touchEvent(&window, touch).release(0, to, &window); QTest::qWait(25);
        };
        swipe({12, 300}, {160, 315}); QTRY_COMPARE(back.size(), 1);
        swipe({12, 300}, {25, 500}); QCOMPARE(back.size(), 1);
        swipe({120, 300}, {300, 315}); QCOMPARE(back.size(), 1);
        QVERIFY(gestures->setProperty("backEnabled", false)); swipe({12, 300}, {160, 315}); QCOMPARE(back.size(), 1);
    }
    void devicePanelShowsFixedMobileRoleWithoutSelectors() {
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
        QCOMPARE(modeLabel->property("text").toString(), QString("Mobile client"));
        auto *pairButton = panel->findChild<QQuickItem *>("networkPairing");
        QVERIFY(pairButton && pairButton->isVisible());
        QCOMPARE(pairButton->property("text").toString(), QString("Connect manually…"));
        auto *automatic = panel->findChild<QQuickItem *>("networkAutomaticSync");
        QVERIFY(automatic && automatic->isVisible());
        QCOMPARE(automatic->property("text").toString(), QString("Sign in to sync automatically"));
        auto *serverAddress = panel->findChild<QQuickItem *>("networkServerAddress");
        auto *connectServer = panel->findChild<QQuickItem *>("networkConnectServer");
        auto *hostServer = panel->findChild<QQuickItem *>("networkHostServer");
        QVERIFY(serverAddress && connectServer);
        QVERIFY(!hostServer); QVERIFY(!connectServer->isEnabled());
        QVERIFY(serverAddress->width() <= window.width());
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
        QVERIFY(!hostButton);
        QVERIFY(!preferences->findChild<QQuickItem *>("preferencesClientMode"));
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
        QVERIFY(panel->property("useQr").toBool());
        auto *scan = panel->findChild<QQuickItem *>("pairingScan");
        auto *refresh = panel->findChild<QQuickItem *>("pairingRefresh");
        QVERIFY(scan && refresh); QVERIFY(scan->isVisible() && scan->isEnabled()); QVERIFY(!refresh->isVisible());
        QVERIFY(!panel->findChild<QQuickItem *>("pairingSignIn"));
        QTRY_VERIFY(window.contentItem()->boundingRect().contains(scan->mapRectToScene(scan->boundingRect())));
        pairing.showHostQr(); QCOMPARE(pairing.phase(), QString("error")); QVERIFY(!network.hosting());
        QVERIFY(!network.startLocalHost()); QVERIFY(!network.localPeer()->hosting());
        QVERIFY(QMetaObject::invokeMethod(panel.get(), "close")); QTRY_COMPARE(pairing.phase(), QString("idle"));
    }
    void deviceSelectionWaitsForTransfersAndTracksHostAvailability() {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/sidebar-network-XXXXXX");
        QFile file(fixture.filePath("shared.txt")); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("fixture"); file.close();
        RelayServer relay([](const auto &, AuthCompletion done) { done({"alice", QDateTime::currentDateTimeUtc().addSecs(60)}); });
        QVERIFY(relay.listen(QHostAddress::LocalHost));
        PeerOptions options; options.relayUrl = QUrl(QString("ws://127.0.0.1:%1").arg(relay.port()));
        options.credential = "alice"; options.peerId = "first-host"; options.name = "First desktop";
        options.service = "com.iisacc.society.files"; options.localEnabled = false;
        FileShare files(fixture.path()); Peer first, second;
        QVERIFY(first.start(options, [&](const auto &, const auto &request) { return files.handle(request); }));
        options.peerId = "second-host"; options.name = "Second desktop";
        QVERIFY(second.start(options, [&](const auto &, const auto &request) { return files.handle(request); }));
        NetworkDriveController network; options.peerId = "client"; QVERIFY(network.startSession(options));
        QTRY_VERIFY(network.connected() && first.isReady() && second.isReady());
        QTRY_COMPARE(network.hosts().size(), 2);
        QQmlEngine engine; engine.addImportPath(SOCIETY_LVRS_QML_IMPORT_PATH);
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&](const QList<QQmlError> &errors) {
            for (const auto &error : errors) warnings.append(error.toString());
        });
        QQuickWindow window; window.resize(680, 800); window.show();
        QQmlComponent component(&engine, QUrl::fromLocalFile(SOCIETY_NETWORK_QML_FILE));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> panel(component.createWithInitialProperties({
            {"network", QVariant::fromValue(&network)}, {"parent", QVariant::fromValue(window.contentItem())}}));
        QVERIFY2(panel, qPrintable(component.errorString()));
        QVERIFY(QMetaObject::invokeMethod(panel.get(), "open")); QTRY_VERIFY(panel->property("opened").toBool());
        network.browse("first-host"); QVERIFY(network.busy());
        QVERIFY(QMetaObject::invokeMethod(panel.get(), "selectDevice", Q_ARG(QVariant, "second-host"),
            Q_ARG(QVariant, "Second desktop"), Q_ARG(QVariant, "second-host")));
        QVERIFY(!panel->property("selectedFilesVisible").toBool());
        QTRY_COMPARE(network.currentHost(), QString("second-host")); QTRY_VERIFY(!network.busy());
        QCOMPARE(network.entries().size(), 1); QVERIFY(panel->property("selectedFilesVisible").toBool());
        QVERIFY(!panel->property("selectionPending").toBool());
        second.stop(); QTRY_COMPARE(network.hosts().size(), 1);
        QTRY_VERIFY(!panel->property("selectedFilesVisible").toBool());
        // A stale list from the disconnected host stays hidden until a fresh request completes.
        options.peerId = "second-host"; options.name = "Second desktop";
        QVERIFY(second.start(options, [&](const auto &, const auto &request) { return files.handle(request); }));
        QTRY_COMPARE(network.hosts().size(), 2); QTRY_VERIFY(!network.busy());
        QTRY_VERIFY(panel->property("selectedFilesVisible").toBool());
        QVERIFY(QMetaObject::invokeMethod(panel.get(), "selectDevice", Q_ARG(QVariant, "offline"),
            Q_ARG(QVariant, "Offline phone"), Q_ARG(QVariant, "")));
        QVERIFY(!panel->property("selectedFilesVisible").toBool());
        QCOMPARE(network.currentHost(), QString("second-host"));
        QVERIFY(QMetaObject::invokeMethod(panel.get(), "clearDeviceSelection"));
        QVERIFY(panel->property("selectedDeviceId").toString().isEmpty());
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
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
        const auto role = mobile.metaObject()->property(mobile.metaObject()->indexOfProperty("mode"));
        QVERIFY(role.isConstant()); QVERIFY(!role.isWritable());
        QVERIFY(!mobile.hostModeAvailable());
        QCOMPARE(mobile.mode(), NetworkDriveController::ClientMode);
        mobile.setContainerPath(container.path());
        QVERIFY(!mobile.setProperty("mode", NetworkDriveController::HostMode));
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
        QVERIFY(!mobile.setProperty("mode", NetworkDriveController::HostMode));
        QVERIFY(mobile.connected()); QCOMPARE(state.size(), 0);
        QVERIFY(QMetaObject::invokeMethod(qGuiApp, "applicationStateChanged", Qt::DirectConnection,
            Q_ARG(Qt::ApplicationState, Qt::ApplicationSuspended)));
        QVERIFY(!mobile.connected());
        QVERIFY(!mobile.setProperty("mode", NetworkDriveController::HostMode));
        QVERIFY(mobile.startSession(options));
        QVERIFY(!mobile.connected()); // A supplied session cannot bypass mobile suspension.
        QVERIFY(QMetaObject::invokeMethod(qGuiApp, "applicationStateChanged", Qt::DirectConnection,
            Q_ARG(Qt::ApplicationState, Qt::ApplicationActive)));
        QTRY_VERIFY(mobile.connected());
        QCOMPARE(mobile.mode(), NetworkDriveController::ClientMode);
        QVERIFY(!mobile.hosting());
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

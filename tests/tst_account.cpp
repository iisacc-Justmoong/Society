#include "App/Account/AccountController.h"
#include "App/Network/NetworkDriveController.h"
#include "PairingCredentialsFixture.h"
#include "FakeDiscoveryService.h"
#include "MemorySessionStore.h"
#include "App/State/GroupSessionStore.h"
#include <QCryptographicHash>
#include <QScopeGuard>
#include <iiSocietyHelper.h>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkProxy>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include "backend/runtime/appbootstrap.h"

class AccountServer : public QObject {
public:
    QTcpServer server;
    QList<QJsonObject> requests;
    QList<QByteArray> headers;
    int status = 200;
    int pairingVersion = 2;
    bool hang = false;
    QJsonObject profile;
    AccountServer() {
        QFile fixture(SOCIETY_ACCOUNT_FIXTURE);
        if (fixture.open(QIODevice::ReadOnly)) profile = QJsonDocument::fromJson(fixture.readAll()).object().value("account").toObject();
        connect(&server, &QTcpServer::newConnection, this, [this] {
            while (auto *socket = server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                    if (socket->property("handled").toBool()) return;
                    const auto bytes = socket->property("bytes").toByteArray() + socket->readAll();
                    socket->setProperty("bytes", bytes);
                    const auto offset = bytes.indexOf("\r\n\r\n");
                    if (offset < 0) return;
                    int length = 0;
                    for (const auto &line : bytes.left(offset).split('\n'))
                        if (line.toLower().startsWith("content-length:")) length = line.mid(15).trimmed().toInt();
                    if (bytes.size() < offset + 4 + length) return;
                    socket->setProperty("handled", true);
                    const auto input = QJsonDocument::fromJson(bytes.mid(offset + 4, length)).object();
                    requests.append(input); headers.append(bytes.left(offset));
                    if (hang) return;
                    const auto intent = input.value("intent").toString();
                    QJsonObject payload{{"account", QJsonValue::Null}, {"session", QJsonValue::Null}};
                    QByteArray cookies;
                    if (status != 200) payload.insert("error", QJsonObject{{"code", status == 401 ? "invalid_credentials" : "unavailable"}});
                    else if (intent != "logout") {
                        payload.insert("account", profile);
                        payload.insert("session", QJsonObject{{"id", QString(32, 'a')}, {"client", "app"}, {"current", true},
                            {"device", input.value("device")}, {"createdAt", "2026-09-09T00:00:00Z"},
                            {"lastSeenAt", "2026-09-09T00:00:00Z"}, {"expiresAt", "2100-01-01T00:00:00Z"}});
                        cookies = "Set-Cookie: iisacc_auth_id=fixture-id; Path=/; HttpOnly\r\n"
                                  "Set-Cookie: iisacc_auth_refresh=fixture-refresh; Path=/; HttpOnly; Max-Age=3600\r\n"
                                  "Set-Cookie: iisacc_login_session=fixture-session; Path=/; HttpOnly; Max-Age=3600\r\n";
                        if (intent == "pairing") {
                            auto proof = pairingVersion == 2 ? localPairingCredentialsFixture(QString(64, 'b'))
                                : pairingCredentialsFixture(QString(64, 'b'));
                            if (pairingVersion == 1)
                                proof.insert("refreshAt", QDateTime::currentDateTimeUtc().addSecs(200).toString(Qt::ISODate));
                            proof.insert("deviceId", input.value("device").toObject().value("id"));
                            proof.insert("sessionId", QString(32, 'a'));
                            payload = {{"pairing", proof}}; cookies.clear();
                        }
                    }
                    const auto body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
                    socket->write("HTTP/1.1 " + QByteArray::number(status) + " Response\r\nContent-Type: application/json\r\n"
                        "Connection: close\r\n" + cookies + "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body);
                    socket->disconnectFromHost();
                });
            }
        });
    }
    QUrl url() const { return QUrl(QString("http://127.0.0.1:%1").arg(server.serverPort())); }
};

class AccountTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
        qmlRegisterType<AccountController>("Society", 1, 0, "AccountController");
    }
    void loginSharesTheCompleteModelAndRevokesSession() {
        AccountServer server; QVERIFY(server.server.listen(QHostAddress::LocalHost)); QVERIFY(!server.profile.isEmpty());
        AccountController account(server.url());
        NetworkDriveController network; network.setAccountSession(&account);
        iiSocietyHelper::Helper helper; helper.setAccountManager(account.manager());
        QCOMPARE(network.accountManager(), helper.accountManager());
        QCOMPARE(account.manager()->deviceInfo().value("appId").toString(), QString("com.iisacc.society"));
        QVERIFY(account.login("builder@example.com", "FixtureOnly1!"));
        QTRY_VERIFY(account.signedIn()); QVERIFY(network.signedIn());
        QCOMPARE(helper.account()->toVariantMap(), server.profile.toVariantMap());
        QVERIFY(!network.connected()); // Account login does not require a relay address.
        QCOMPARE(server.requests[0].value("device").toObject().value("appId").toString(), QString("com.iisacc.society"));
        QVERIFY(!account.codeRequired());
        QCOMPARE(server.requests.size(), 1);
        QCOMPARE(server.requests[0].value("intent").toString(), QString("login"));
        QVERIFY(!account.relayCredential().isEmpty());
        QVERIFY(account.refresh()); QTRY_VERIFY(!account.busy());
        QVERIFY(account.signedIn());
        QVERIFY(server.headers[1].contains("iisacc_login_session=fixture-session"));
        QVERIFY(account.logout()); QTRY_VERIFY(!account.busy());
        QVERIFY(!account.signedIn()); QVERIFY(!network.signedIn());
        QVERIFY(!helper.account()->isPresent()); QVERIFY(account.relayCredential().isEmpty());
        QCOMPARE(server.requests.last().value("intent").toString(), QString("logout"));
        for (qsizetype i = 1; i < server.requests.size(); ++i) QVERIFY(!server.requests[i].contains("password"));
    }
    void automaticPairingCredentialsUsePrivateSessionAndClearOnLogout() {
        AccountServer server; QVERIFY(server.server.listen(QHostAddress::LocalHost));
        AccountController account(server.url());
        account.requestPairingCredentials(); QVERIFY(server.requests.isEmpty());
        QVERIFY(account.login("builder@example.com", "FixtureOnly1!")); QTRY_VERIFY(account.signedIn());
        QSignalSpy proof(&account, &AccountController::pairingCredentialsChanged);
        account.requestPairingCredentials(); account.requestPairingCredentials();
        QTRY_COMPARE(proof.size(), 1); QVERIFY(!account.pairingCredentials().isEmpty());
        QCOMPARE(server.requests.size(), 2); QCOMPARE(server.requests[1].value("intent"), "pairing");
        QVERIFY(server.headers[1].contains("iisacc_login_session=fixture-session"));
        QVERIFY(!server.requests[1].contains("password"));
        account.requestPairingCredentials(); QTest::qWait(50); QCOMPARE(server.requests.size(), 2);
        QVERIFY(account.logout()); QVERIFY(account.pairingCredentials().isEmpty());
        QTRY_VERIFY(!account.signedIn());
    }
    void groupContainerRestoresLoginPairingAndPauseAcrossLaunches() {
        AccountServer server; QVERIFY(server.server.listen(QHostAddress::LocalHost));
        QTemporaryDir dir(QString(QT_TESTCASE_BUILDDIR) + "/account-group-XXXXXX"); QVERIFY(dir.isValid());
        MemorySessionStore keys, legacy;
        QString pairingKey;
        {
            GroupSessionStore store(dir.path(), &keys, &legacy);
            AccountController account(server.url(), &store, nullptr);
            NetworkDriveController network; network.setAccountSession(&account);
            QSignalSpy loaded(&account, &AccountController::pairingStateRestored);
            QVERIFY(account.login("builder@example.com", "FixtureOnly1!")); QTRY_VERIFY(account.signedIn());
            QTRY_COMPARE(loaded.size(), 1);
            account.requestPairingCredentials(); QTRY_VERIFY(!account.pairingCredentials().isEmpty());
            account.rememberPairedDevice(QString(64, 'c'), "Fixture phone", "phone");
            network.disconnectSession(); QVERIFY(!account.automaticPairingEnabled());
            pairingKey = "pairing-v1-" + QString::fromLatin1(QCryptographicHash::hash(server.url().toEncoded() + '\n'
                + account.manager()->deviceInfo().value("id").toString().toUtf8(), QCryptographicHash::Sha256).toHex());
            bool read = false;
            store.read(pairingKey, [&](auto result) {
                QCOMPARE(result.error, iisacc::accounts::SessionStore::Error::None);
                const auto state = QJsonDocument::fromJson(result.data).object();
                QVERIFY(!state.value("automaticEnabled").toBool()); QCOMPARE(state.value("peers").toArray().size(), 1);
                QVERIFY(!state.value("credentials").toObject().isEmpty());
                QCOMPARE(state.value("schemaVersion").toInt(), 3);
                QVERIFY(!state.contains("snapshot")); QVERIFY(!state.contains("nextAccountCheck"));
                QVERIFY(account.manager()->matchesSessionBinding(state.value("binding").toObject().toVariantMap())); read = true;
            });
            QTRY_VERIFY(read); QVERIFY(account.errorString().isEmpty());
        }
        const auto before = server.requests.size();
        {
            GroupSessionStore store(dir.path(), &keys, &legacy);
            AccountController account(server.url(), &store, nullptr);
            NetworkDriveController network; network.setAccountSession(&account);
            QSignalSpy loaded(&account, &AccountController::pairingStateRestored);
            QTRY_VERIFY(account.signedIn()); QTRY_COMPARE(loaded.size(), 1);
            QVERIFY(!account.pairingCredentials().isEmpty()); QCOMPARE(account.rememberedPeers().size(), 1);
            QVERIFY(!network.automaticPairingEnabled());
            account.requestPairingCredentials();
            QCOMPARE(server.requests.size(), before);
            QCOMPARE(account.pairingCredentials().value("version").toInt(), 2);
            QVERIFY(account.logout()); QTRY_VERIFY(!account.signedIn());
            QVERIFY(account.pairingCredentials().isEmpty()); QVERIFY(account.rememberedPeers().isEmpty());
            bool removed = false;
            store.read(pairingKey, [&](auto result) { QCOMPARE(result.error, iisacc::accounts::SessionStore::Error::Missing); removed = true; });
            QTRY_VERIFY(removed);
        }
        const auto loggedOutRequests = server.requests.size();
        GroupSessionStore store(dir.path(), &keys, &legacy);
        AccountController loggedOut(server.url(), &store, nullptr);
        QSignalSpy restored(loggedOut.manager(), &iisacc::accounts::AccountManager::restoringSessionChanged);
        QTRY_VERIFY(restored.size() >= 2);
        QVERIFY(!loggedOut.signedIn()); QCOMPARE(server.requests.size(), loggedOutRequests);
    }
    void cachedAccountPairsLocallyWhileTheAccountServerIsUnavailable() {
        AccountServer server; QVERIFY(server.server.listen(QHostAddress::LocalHost));
        MemorySessionStore store;
        {
            AccountController account(server.url(), &store, nullptr);
            QSignalSpy restored(&account, &AccountController::pairingStateRestored);
            QVERIFY(account.login("builder@example.com", "FixtureOnly1!")); QTRY_VERIFY(account.signedIn());
            QTRY_COMPARE(restored.size(), 1);
            account.requestPairingCredentials(); QTRY_VERIFY(!account.pairingCredentials().isEmpty());
            QTRY_VERIFY(!store.values.isEmpty());
        }
        const auto before = server.requests.size(); server.status = 503;
        AccountController account(server.url(), &store, nullptr);
        QTRY_VERIFY(account.signedIn());
        QCOMPARE(server.requests.size(), before);
        QTRY_VERIFY(!account.pairingCredentials().isEmpty());
        FakeDiscoveryService a, b;
        NearbyDevices desktop(&a, QHostAddress::LocalHost), mobile(&b, QHostAddress::LocalHost);
        const auto credentials = account.pairingCredentials(); const auto scope = credentials.value("scope").toString();
        desktop.setIdentity(scope, "cached-desktop", "Cached desktop", "pc", true);
        mobile.setIdentity(scope, "cached-mobile", "Cached mobile", "phone", false);
        desktop.setCredentials(credentials); mobile.setCredentials(credentials);
        a.announceTo(b); b.announceTo(a);
        QVERIFY(desktop.devices()[0].toMap().value("verified").toBool());
        QVERIFY(mobile.devices()[0].toMap().value("verified").toBool());
        for (int tick = 0; tick < 100; ++tick) account.requestPairingCredentials();
        QTest::qWait(100); QCOMPARE(server.requests.size(), before);
        QVERIFY(account.refresh()); QTRY_VERIFY(!account.busy());
        QVERIFY(account.signedIn()); QVERIFY(!account.pairingCredentials().isEmpty());
        for (int tick = 0; tick < 100; ++tick) account.requestPairingCredentials();
        QTest::qWait(100); QCOMPARE(server.requests.size(), before + 1);
        server.status = 401; QVERIFY(account.refresh()); QTRY_VERIFY(!account.busy());
        QVERIFY(!account.signedIn()); QVERIFY(account.pairingCredentials().isEmpty());
        AccountController revoked(server.url(), &store, nullptr);
        QSignalSpy restored(revoked.manager(), &iisacc::accounts::AccountManager::restoringSessionChanged);
        QTRY_VERIFY(restored.size() >= 2); QVERIFY(!revoked.signedIn());
    }
    void freshLegacyCredentialsAttemptAnImmediateUpgrade_data() {
        QTest::addColumn<int>("serverVersion");
        QTest::newRow("current-server") << 2;
        QTest::newRow("legacy-server") << 1;
    }
    void freshLegacyCredentialsAttemptAnImmediateUpgrade() {
        QFETCH(int, serverVersion);
        AccountServer server; QVERIFY(server.server.listen(QHostAddress::LocalHost));
        server.pairingVersion = 1;
        MemorySessionStore store; QString pairingKey;
        {
            AccountController account(server.url(), &store, nullptr);
            QSignalSpy loaded(&account, &AccountController::pairingStateRestored);
            QVERIFY(account.login("builder@example.com", "FixtureOnly1!")); QTRY_VERIFY(account.signedIn());
            QTRY_COMPARE(loaded.size(), 1); account.requestPairingCredentials();
            QTRY_VERIFY(!account.pairingCredentials().isEmpty());
            for (const auto &key : store.values.keys()) if (key.startsWith("pairing-v1-")) pairingKey = key;
        }
        QVERIFY(!pairingKey.isEmpty());
        auto record = QJsonDocument::fromJson(store.values.value(pairingKey)).object();
        record.insert("schemaVersion", 1);
        const auto binding = record.take("binding").toObject();
        for (auto it = binding.begin(); it != binding.end(); ++it) record.insert(it.key(), it.value());
        record.remove("requestBlocked");
        for (const auto *field : {"snapshot", "accountVerifiedAt", "nextAccountCheck", "pairingRetryAt", "pairingFailures"}) record.remove(field);
        store.values[pairingKey] = QJsonDocument(record).toJson();
        server.pairingVersion = serverVersion; const auto before = server.requests.size();
        {
            AccountController account(server.url(), &store, nullptr);
            QSignalSpy loaded(&account, &AccountController::pairingStateRestored);
            QTRY_VERIFY(account.signedIn()); QTRY_COMPARE(loaded.size(), 1);
            QCOMPARE(account.pairingCredentials().value("version").toInt(1), 1);
            account.requestPairingCredentials();
            QTRY_COMPARE(server.requests.size(), before + 1);
            QTRY_COMPARE(QJsonDocument::fromJson(store.values.value(pairingKey)).object().value("schemaVersion").toInt(), 3);
            QCOMPARE(account.pairingCredentials().value("version").toInt(1), serverVersion);
            for (int attempt = 0; attempt < 100; ++attempt) account.requestPairingCredentials();
            QTest::qWait(100); QCOMPARE(server.requests.size(), before + 1);
        }
        if (serverVersion == 2) {
            server.status = 503;
            AccountController cached(server.url(), &store, nullptr);
            QTRY_VERIFY(cached.signedIn()); QCOMPARE(server.requests.size(), before + 1);
        }
    }
    void failedSeedAcquisitionNeedsANewTriggerAcrossRestarts() {
        AccountServer server; QVERIFY(server.server.listen(QHostAddress::LocalHost));
        MemorySessionStore store; QString pairingKey;
        {
            AccountController account(server.url(), &store, nullptr);
            QSignalSpy loaded(&account, &AccountController::pairingStateRestored);
            QVERIFY(account.login("builder@example.com", "FixtureOnly1!")); QTRY_VERIFY(account.signedIn());
            QTRY_COMPARE(loaded.size(), 1); account.requestPairingCredentials();
            QTRY_VERIFY(!account.pairingCredentials().isEmpty());
            for (const auto &key : store.values.keys()) if (key.startsWith("pairing-v1-")) pairingKey = key;
        }
        QVERIFY(!pairingKey.isEmpty());
        auto record = QJsonDocument::fromJson(store.values.value(pairingKey)).object();
        auto proof = record.value("credentials").toObject();
        proof.insert("issuedAt", QDateTime::currentDateTimeUtc().addSecs(-60).toString(Qt::ISODate));
        proof.insert("refreshAt", QDateTime::currentDateTimeUtc().addSecs(-1).toString(Qt::ISODate));
        record.insert("credentials", proof); store.values[pairingKey] = QJsonDocument(record).toJson();
        // refreshAt is advisory: a valid offline grant does not cause traffic.
        {
            AccountController cached(server.url(), &store, nullptr);
            QTRY_VERIFY(!cached.pairingCredentials().isEmpty());
            const auto idle = server.requests.size();
            for (int attempt = 0; attempt < 100; ++attempt) cached.requestPairingCredentials();
            QTest::qWait(100); QCOMPARE(server.requests.size(), idle);
        }
        proof.insert("expiresAt", QDateTime::currentDateTimeUtc().addSecs(-1).toString(Qt::ISODate));
        record.insert("credentials", proof); store.values[pairingKey] = QJsonDocument(record).toJson();
        server.status = 503; const auto before = server.requests.size();
        {
            AccountController account(server.url(), &store, nullptr);
            QSignalSpy restored(&account, &AccountController::pairingStateRestored);
            QTRY_VERIFY(account.signedIn()); QTRY_COMPARE(restored.size(), 1);
            for (int attempt = 0; attempt < 100; ++attempt) account.requestPairingCredentials();
            QTRY_COMPARE(server.requests.size(), before + 1);
            QTRY_VERIFY(QJsonDocument::fromJson(store.values.value(pairingKey)).object().value("requestBlocked").toBool());
            QVERIFY(account.pairingCredentials().isEmpty());
        }
        AccountController reopened(server.url(), &store, nullptr);
        QSignalSpy restored(&reopened, &AccountController::pairingStateRestored);
        QTRY_VERIFY(reopened.signedIn()); QTRY_COMPARE(restored.size(), 1);
        for (int attempt = 0; attempt < 100; ++attempt) reopened.requestPairingCredentials();
        QTest::qWait(100); QCOMPARE(server.requests.size(), before + 1);
        server.status = 200;
        QVERIFY(reopened.refresh()); QTRY_VERIFY(!reopened.busy());
        reopened.requestPairingCredentials(); QTRY_VERIFY(!reopened.pairingCredentials().isEmpty());
        QCOMPARE(server.requests.size(), before + 3);
    }
    void restoredPairingRequiresCurrentAccountDeviceSessionAndExpiry_data() {
        QTest::addColumn<QString>("invalidField");
        for (const auto *field : {"expiresAt", "sessionId", "deviceId", "userId"}) QTest::newRow(field) << QString(field);
    }
    void restoredPairingRequiresCurrentAccountDeviceSessionAndExpiry() {
        QFETCH(QString, invalidField);
        AccountServer server; QVERIFY(server.server.listen(QHostAddress::LocalHost));
        MemorySessionStore store;
        AccountController account(server.url(), &store, nullptr);
        const auto device = account.manager()->deviceInfo().value("id").toString();
        const auto key = "pairing-v1-" + QString::fromLatin1(QCryptographicHash::hash(server.url().toEncoded() + '\n'
            + device.toUtf8(), QCryptographicHash::Sha256).toHex());
        auto proof = pairingCredentialsFixture(QString(64, 'b'));
        proof.insert("sessionId", QString(32, 'a')); proof.insert("deviceId", device);
        proof.insert("refreshAt", QDateTime::currentDateTimeUtc().addSecs(200).toString(Qt::ISODate));
        if (invalidField == "expiresAt") proof.insert(invalidField, QDateTime::currentDateTimeUtc().addSecs(-1).toString(Qt::ISODate));
        else if (invalidField != "userId") proof.insert(invalidField, "different-fixture");
        const QJsonObject record{{"schemaVersion", 1}, {"origin", server.url().toString()}, {"deviceId", device},
            {"subject", server.profile.value("sub")}, {"userId", invalidField == "userId" ? QJsonValue("other-account") : server.profile.value("userId")},
            {"credentials", proof}, {"automaticEnabled", true}};
        store.values[key] = QJsonDocument(record).toJson(QJsonDocument::Compact);
        QSignalSpy loaded(&account, &AccountController::pairingStateRestored);
        QVERIFY(account.login("builder@example.com", "FixtureOnly1!")); QTRY_VERIFY(account.signedIn()); QTRY_COMPARE(loaded.size(), 1);
        QVERIFY(account.pairingCredentials().isEmpty());
        account.requestPairingCredentials(); QTRY_VERIFY(!account.pairingCredentials().isEmpty());
        QCOMPARE(server.requests.size(), 2); QCOMPARE(server.requests.last().value("intent"), "pairing");
    }
    void signedInAccountAutomaticallyStartsAuthenticatedDiscovery() {
        qputenv("SOCIETY_TEST_ACCOUNT_DISCOVERY", "1");
        const auto restore = qScopeGuard([] { qunsetenv("SOCIETY_TEST_ACCOUNT_DISCOVERY"); });
        AccountServer server; QVERIFY(server.server.listen(QHostAddress::LocalHost));
        FakeDiscoveryService discovery;
        AccountController account(server.url());
        NetworkDriveController network(&discovery, QHostAddress::LocalHost); network.setAccountSession(&account);
        QVERIFY(account.login("builder@example.com", "FixtureOnly1!"));
        QTRY_VERIFY(network.discovery()->authenticated());
        QCOMPARE(server.requests.size(), 2); QVERIFY(discovery.record.contains("proof"));
        QCOMPARE(discovery.record.value("scope").toString(), QString(64, 'b'));
        QTest::qWait(5300); // Cross the actual LAN discovery timer without HTTP.
        QCOMPARE(server.requests.size(), 2);
        server.profile.insert("displayName", "Changed through account service");
        QVERIFY(account.manager()->notifyAccountChanged("profile-event-2"));
        QVERIFY(!account.manager()->notifyAccountChanged("profile-event-2"));
        QTRY_COMPARE(account.displayName(), QString("Changed through account service"));
        QTest::qWait(100); QCOMPARE(server.requests.size(), 3);
        QVERIFY(network.discovery()->authenticated());
        QVERIFY(account.logout()); QTRY_VERIFY(!account.signedIn());
        QVERIFY(!network.discovery()->active()); QVERIFY(network.pairingQueue().isEmpty());
    }
    void accountVerifiedPairingAutomaticallySynchronizesContainers() {
        qputenv("SOCIETY_TEST_ACCOUNT_DISCOVERY", "1");
        const auto restore = qScopeGuard([] { qunsetenv("SOCIETY_TEST_ACCOUNT_DISCOVERY"); });
        AccountServer server; QVERIFY(server.server.listen(QHostAddress::LocalHost));
        QTemporaryDir a(QString(QT_TESTCASE_BUILDDIR) + "/account-sync-a-XXXXXX"), b(QString(QT_TESTCASE_BUILDDIR) + "/account-sync-b-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(a.path())); QVERIFY(iiSocietyContainer::SocietyDrive::create(b.path()));
        QFile original(a.filePath("Models/from-desktop.bin")); QVERIFY(original.open(QIODevice::WriteOnly)); original.write(QByteArray(700000, 's')); original.close();
        QFile fromClient(b.filePath("Files/client.txt")); QVERIFY(fromClient.open(QIODevice::WriteOnly)); fromClient.write("client bytes"); fromClient.close();
        AccountController first(server.url()), second(server.url());
        auto one = first.manager()->deviceInfo(), two = second.manager()->deviceInfo();
        one.insert("id", QString(64, 'a')); two.insert("id", QString(64, 'b'));
        QVERIFY(first.manager()->setDeviceInfo(one)); QVERIFY(second.manager()->setDeviceInfo(two));
        FakeDiscoveryService firstDiscovery, secondDiscovery;
        NetworkDriveController host(&firstDiscovery, QHostAddress::LocalHost), client(&secondDiscovery, QHostAddress::LocalHost);
        host.setContainerPath(a.path()); client.setContainerPath(b.path()); host.setAccountSession(&first); client.setAccountSession(&second);
        QTimer announce;
        connect(&announce, &QTimer::timeout, this, [&] { firstDiscovery.announceTo(secondDiscovery); secondDiscovery.announceTo(firstDiscovery); });
        announce.start(30);
        QVERIFY(first.login("builder@example.com", "FixtureOnly1!")); QVERIFY(second.login("builder@example.com", "FixtureOnly1!"));
        QSignalSpy synced(&client, &NetworkDriveController::containerSynchronized);
        QTRY_VERIFY_WITH_TIMEOUT(host.hosting() && client.localPeer()->connected(), 15000);
        QTRY_VERIFY2_WITH_TIMEOUT(synced.size() > 0, qPrintable(client.synchronizationStatus()), 30000);
        QFile downloaded(b.filePath("Models/from-desktop.bin")); QVERIFY(downloaded.open(QIODevice::ReadOnly)); QCOMPARE(downloaded.readAll(), QByteArray(700000, 's'));
        QCOMPARE(iiSocietyContainer::SocietyDrive::open(a.path())->identifier(), iiSocietyContainer::SocietyDrive::open(b.path())->identifier());
        QVERIFY(client.containerReady());
        QVERIFY(!QFileInfo::exists(a.filePath("Files/client.txt")));
        QVERIFY(!QFileInfo::exists(b.filePath("Files/client.txt")));
        const auto mirror = iiSocietySync::Replica::binding(b.path());
        QFile retained(b.filePath(mirror.value("recovery").toString() + "/Files/client.txt"));
        QVERIFY(retained.open(QIODevice::ReadOnly)); QCOMPARE(retained.readAll(), QByteArray("client bytes"));
        QVERIFY(fromClient.open(QIODevice::WriteOnly)); fromClient.write("client bytes"); fromClient.close();
        synced.clear(); client.synchronizeNow(); QTRY_VERIFY_WITH_TIMEOUT(synced.size() > 0, 30000);
        QFile uploaded(a.filePath("Files/client.txt")); QVERIFY(uploaded.open(QIODevice::ReadOnly)); QCOMPARE(uploaded.readAll(), QByteArray("client bytes"));
        QCOMPARE(server.requests.size(), 4); // Two logins + two grants; synchronization never contacts iisacc.com.
        QVERIFY(host.synchronizationAvailable());
        client.disconnectSession();
        QVERIFY(client.containerReady()); // The completed mirror remains usable offline.
        QCOMPARE(iiSocietySync::Replica::primaryHost(a.path(), first.pairingCredentials().value("scope").toString()), one.value("id").toString());
        QTRY_VERIFY(host.localPeer()->pairedDeviceIds().isEmpty());
        QTRY_VERIFY(!host.synchronizationAvailable());
        QVERIFY(host.hosting()); // A listener alone grants no replication access.
        QVERIFY(first.logout()); QTRY_VERIFY(!first.signedIn()); QTRY_VERIFY(!host.synchronizationAvailable());
        QVERIFY(!host.hosting());
    }
    void snapshotIsNotAnAuthenticatedSessionAndReferencesCanExpire() {
        AccountServer server;
        NetworkDriveController network;
        iiSocietyHelper::Helper helper;
        {
            AccountController account;
            network.setAccountSession(&account); helper.setAccountManager(account.manager());
            QVERIFY(account.manager()->readAccount(server.profile.toVariantMap()));
            QVERIFY(account.manager()->account()->isPresent());
            QVERIFY(!account.signedIn()); QVERIFY(!network.signedIn());
            QVERIFY(account.relayCredential().isEmpty());
        }
        QVERIFY(!network.accountSession()); QVERIFY(!network.accountManager()); QVERIFY(!helper.accountManager());
    }
    void pairingRejectionUsesTheSdkSessionDecision() {
        qputenv("SOCIETY_TEST_ACCOUNT_DISCOVERY", "1");
        const auto restore = qScopeGuard([] { qunsetenv("SOCIETY_TEST_ACCOUNT_DISCOVERY"); });
        AccountServer server; QVERIFY(server.server.listen(QHostAddress::LocalHost));
        AccountController account(server.url());
        QVERIFY(account.login("builder@example.com", "FixtureOnly1!")); QTRY_VERIFY(account.signedIn());
        server.status = 401;
        FakeDiscoveryService discovery;
        NetworkDriveController network(&discovery, QHostAddress::LocalHost);
        QSignalSpy ending(account.manager(), &iisacc::accounts::AccountManager::sessionEnding);
        network.setAccountSession(&account);
        QTRY_COMPARE(ending.size(), 1); QVERIFY(!account.manager()->isAuthenticated());
        QVERIFY(!account.signedIn()); QVERIFY(!network.discovery()->authenticated());
        QVERIFY(account.pairingCredentials().isEmpty()); QCOMPARE(server.requests.size(), 2);
    }
    void errorsCancellationAndRetry() {
        AccountServer server; QVERIFY(server.server.listen(QHostAddress::LocalHost));
        AccountController account(server.url());
        server.status = 401;
        QVERIFY(account.login("builder@example.com", "FixtureOnly1!"));
        QVERIFY(!account.login("builder@example.com", "FixtureOnly1!"));
        QTRY_VERIFY(!account.busy());
        QVERIFY(!account.signedIn()); QVERIFY(!account.errorString().isEmpty());
        QCOMPARE(account.manager()->error(), iisacc::accounts::AccountManager::Error::InvalidCredentials);
        QVERIFY(!account.codeRequired());
        account.cancelLogin(); QVERIFY(account.errorString().isEmpty());
        server.status = 200; server.hang = true;
        account.manager()->setTimeoutMs(80);
        QVERIFY(account.login("builder@example.com", "FixtureOnly1!"));
        QTRY_VERIFY(!account.busy()); QVERIFY(!account.errorString().isEmpty());
        account.manager()->setTimeoutMs(15000);
        server.hang = false;
        QVERIFY(account.login("builder@example.com", "FixtureOnly1!")); QTRY_VERIFY(account.signedIn());
        QVERIFY(!account.codeRequired());
    }
    void responsiveLoginPanel_data() {
        QTest::addColumn<QSize>("size");
        QTest::newRow("desktop") << QSize(1120, 720);
        QTest::newRow("phone") << QSize(390, 844);
        QTest::newRow("compact-landscape") << QSize(640, 360);
    }
    void responsiveLoginPanel() {
        QFETCH(QSize, size);
        AccountServer server; QVERIFY(server.server.listen(QHostAddress::LocalHost));
        AccountController account(server.url());
        QQmlEngine engine; engine.addImportPath(SOCIETY_LVRS_QML_IMPORT_PATH);
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&](const QList<QQmlError> &errors) {
            for (const auto &error : errors) warnings.append(error.toString());
        });
        QQuickWindow window; window.resize(size); window.show();
        QQmlComponent component(&engine, QUrl(SOCIETY_ACCOUNT_QML_FILE));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> panel(component.createWithInitialProperties({
            {"manager", QVariant::fromValue(account.manager())}, {"parent", QVariant::fromValue(window.contentItem())}}));
        QVERIFY2(panel, qPrintable(component.errorString()));
        account.manager()->showLogin();
        QTRY_VERIFY(panel->property("visible").toBool());
        QVERIFY(panel->property("width").toReal() <= size.width());
        QVERIFY(panel->property("height").toReal() <= size.height());
        auto item = [&](const char *name) { return panel->findChild<QQuickItem *>(name); };
        auto *email = item("accountEmail"), *password = item("accountPassword"), *signIn = item("accountSignIn");
        QVERIFY(email && password && signIn);
        QVERIFY(!signIn->isEnabled());
        email->setProperty("text", "builder@example.com"); password->setProperty("text", "FixtureOnly1!");
        QTRY_VERIFY(signIn->isEnabled());
        QVERIFY(QMetaObject::invokeMethod(signIn, "clicked"));
        QCOMPARE(password->property("text").toString(), QString());
        QTRY_VERIFY(account.signedIn());
        QVERIFY(!item("accountCode")); QVERIFY(!item("accountVerify"));
        QCOMPARE(server.requests.size(), 1);
        auto *profile = item("accountDisplayName"); QVERIFY(profile); QTRY_VERIFY(profile->isVisible());
        QCOMPARE(profile->property("text").toString(), account.displayName());
        const auto screenshot = qEnvironmentVariable("SOCIETY_ACCOUNT_SCREENSHOT");
        if (!screenshot.isEmpty() && size.width() == 390) { QTest::qWait(150); QVERIFY(window.grabWindow().save(screenshot)); }
        auto *signOut = item("accountSignOut"); QVERIFY(signOut);
        QVERIFY(QMetaObject::invokeMethod(signOut, "clicked")); QTRY_VERIFY(!account.signedIn() && !account.busy());
        password = item("accountPassword"); QVERIFY(password);
        password->setProperty("text", "temporary");
        QVERIFY(QMetaObject::invokeMethod(panel.get(), "close")); QTRY_VERIFY(!panel->property("visible").toBool());
        account.manager()->showLogin();
        QTRY_VERIFY(panel->property("visible").toBool());
        QTRY_VERIFY(item("accountPassword"));
        QTRY_COMPARE(item("accountPassword")->property("text").toString(), QString());
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    }
};

int main(int argc, char **argv) {
    lvrs::AppBootstrapOptions options; options.applicationName = "SocietyAccountTests";
    options.quickStyleName = "Basic"; options.bootstrapGraphicsBackend = false;
    options.configureRenderQualityDefaults = false; options.logBootstrapDiagnostics = false; options.logGraphicsBackend = false;
    if (!lvrs::preApplicationBootstrap(options).ok) return 1;
    QGuiApplication app(argc, argv); lvrs::postApplicationBootstrap(app, options);
    AccountTests tests; return QTest::qExec(&tests, argc, argv);
}
#include "tst_account.moc"

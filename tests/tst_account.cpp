#include "App/Account/AccountController.h"
#include "App/Network/NetworkDriveController.h"
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
                                  "Set-Cookie: iisacc_login_session=fixture-session; Path=/; HttpOnly\r\n";
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
        QTRY_VERIFY(item("accountPassword"));
        QCOMPARE(item("accountPassword")->property("text").toString(), QString());
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

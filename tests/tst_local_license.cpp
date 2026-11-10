#include "Society/Account/AccountController.h"
#include "AccountServer.h"
#include "MemorySessionStore.h"
#include <QLocalSocket>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QProcess>
#include <QElapsedTimer>
#include <QSettings>
#include <QScopeGuard>
#include <QUuid>
#include <QTest>
#include <sys/stat.h>

class LocalLicenseTests final : public QObject {
    Q_OBJECT
    static QString brokerTemplate() {
        return QDir::cleanPath(QStringLiteral(SOCIETY_TEST_RESOURCES) + "/../../.license-XXXXXX");
    }
    static QByteArray frame(const QJsonObject &value) {
        const auto body=QJsonDocument(value).toJson(QJsonDocument::Compact);const auto n=quint32(body.size());
        QByteArray bytes;bytes.append(char(n>>24));bytes.append(char(n>>16));bytes.append(char(n>>8));bytes.append(char(n));return bytes+body;
    }
    QJsonObject request(const QString& path,const QJsonObject& input) {
        QLocalSocket socket;socket.connectToServer(path);
        if(!socket.waitForConnected(2000))return {};
        const auto body=QJsonDocument(input).toJson(QJsonDocument::Compact);const auto n=quint32(body.size());
        QByteArray frame;frame.append(char(n>>24));frame.append(char(n>>16));frame.append(char(n>>8));frame.append(char(n));socket.write(frame+body);
        QByteArray bytes;QElapsedTimer elapsed;elapsed.start();quint32 size=0;
        while(elapsed.elapsed()<7000){QCoreApplication::processEvents();bytes+=socket.readAll();
            if(bytes.size()>=4){auto* b=reinterpret_cast<const unsigned char*>(bytes.constData());size=(quint32(b[0])<<24)|(quint32(b[1])<<16)|(quint32(b[2])<<8)|b[3];if(bytes.size()>=int(size+4))break;}
            QTest::qWait(5);
        }
        return size && bytes.size()>=int(size+4)?QJsonDocument::fromJson(bytes.mid(4,size)).object():QJsonObject{};
    }
private slots:
    void headlessBrokerCachesAndReusesVerifiedLicenseOffline() {
        QVERIFY(!QCoreApplication::instance()->inherits("QGuiApplication"));
        QTemporaryDir dir(brokerTemplate()); QVERIFY(dir.isValid());
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
        AccountServer server; QVERIFY(server.server.listen(QHostAddress::LocalHost));
        AccountController account(server.url());
        auto *licenses = account.manager()->licenseManager();
        licenses->setTrustedKeys({{"fixture-owned-license", QByteArray::fromStdString(license_fixture::public_key())}});
        const auto service = QStringLiteral("com.iisacc.license-fixture.") + QUuid::createUuid().toString(QUuid::WithoutBraces);
        auto *store = iisacc::accounts::SessionStore::create(service, &account);
        QVERIFY(licenses->setTokenStore(store));
        QVERIFY(account.login("builder@example.com", "FixtureOnly1!"));
        QTRY_VERIFY(account.signedIn());
        const auto binding = account.manager()->sessionBinding();
        const auto key = QStringLiteral("licenses-v1:") + binding.value("subject").toString()
            + ':' + account.manager()->deviceInfo().value("id").toString();
        bool removed = false;
        const auto cleanup = qScopeGuard([&] {
            if (removed) return;
            bool done = false;
            store->remove(key, [&](auto) { done = true; });
            QElapsedTimer elapsed; elapsed.start();
            while (!done && elapsed.elapsed() < 3000) QTest::qWait(10);
        });
        QVERIFY(licenses->ensure());
        QTRY_COMPARE_WITH_TIMEOUT(licenses->code(), QString("verified"), 10000);
        QVERIFY(licenses->allows("dreamscapes"));
        const auto requests = server.licenseRequests;
        bool readFinished = false;
        iisacc::accounts::SessionStore::Result saved;
        store->read(key, [&](auto value) { saved = std::move(value); readFinished = true; });
        QTRY_VERIFY_WITH_TIMEOUT(readFinished, 10000);
        QCOMPARE(saved.error, iisacc::accounts::SessionStore::Error::None);
        QVERIFY(!saved.data.isEmpty());
        licenses->clear(); server.licenseHang = true;
        QVERIFY(account.startLocalLicenseBroker(dir.path(), false));
        const auto grant=request(account.localLicenseBrokerPath(), {
            {"productId","dreamscapes"},{"appId","com.iisacc.dreamscapes"},
            {"deviceId",account.manager()->deviceInfo().value("id").toString()},
            {"requestId","fa37a985-5c4d-48e7-8343-82cc7254536f"}});
        QVERIFY(grant.value("allowed").toBool());
        QVERIFY(!grant.value("licenseToken").toString().isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(licenses->code(), QString("local_token"), 10000);
        QVERIFY(licenses->allows("dreamscapes"));
        QCOMPARE(server.licenseRequests, requests);
        licenses->clear();
        bool removeFinished = false;
        store->remove(key, [&](auto value) { removed = value.error == iisacc::accounts::SessionStore::Error::None; removeFinished = true; });
        QTRY_VERIFY_WITH_TIMEOUT(removeFinished, 10000);
        QVERIFY(removed);
    }
    void daemonBrokerUsesStoredSessionWithoutGuiAndSeesLogout() {
        AccountServer server;QVERIFY(server.server.listen(QHostAddress::LocalHost));
        MemorySessionStore savedSession, tokenStore;
        {
            AccountController gui(server.url(),&savedSession,nullptr);
            QVERIFY(gui.login("builder@example.com","FixtureOnly1!"));
            QTRY_VERIFY(gui.signedIn());QTRY_VERIFY(!savedSession.values.isEmpty());
        }
        QTemporaryDir dir(brokerTemplate());QVERIFY(dir.isValid());
        AccountController daemon(server.url());
        daemon.setBrokerAccountFactory([&](QObject* parent) {
            auto *account=new AccountController(server.url(),&savedSession,parent);
            account->manager()->licenseManager()->setTrustedKeys({{"fixture-owned-license",QByteArray::fromStdString(license_fixture::public_key())}});
            account->manager()->licenseManager()->setTokenStore(&tokenStore);
            return account;
        });
        QVERIFY(daemon.startLocalLicenseBroker(dir.path(),false));
        QJsonObject input{{"productId","dreamscapes"},{"appId","com.iisacc.dreamscapes"},
            {"deviceId",daemon.manager()->deviceInfo().value("id").toString()},
            {"requestId","fa37a985-5c4d-48e7-8343-82cc7254536f"}};
        auto granted=request(daemon.localLicenseBrokerPath(),input);
        QVERIFY2(granted.value("allowed").toBool(),qPrintable(QString::fromUtf8(QJsonDocument(granted).toJson())));
        QVERIFY(!granted.value("licenseToken").toString().isEmpty());
        savedSession.values.clear();
        const auto afterLogout=request(daemon.localLicenseBrokerPath(),input);
        QVERIFY(!afterLogout.value("allowed").toBool());
        QVERIFY(afterLogout.value("licenseToken").toString().isEmpty());
    }
    void sameUserReceivesOnlySignedDeviceBoundLicense() {
        AccountServer server;QVERIFY(server.server.listen(QHostAddress::LocalHost));MemorySessionStore tokenStore;
        AccountController account(server.url());auto* licenses=account.manager()->licenseManager();
        licenses->setTrustedKeys({{"fixture-owned-license",QByteArray::fromStdString(license_fixture::public_key())}});
        QVERIFY(licenses->setTokenStore(&tokenStore));QVERIFY(account.login("builder@example.com","FixtureOnly1!"));QTRY_VERIFY(account.signedIn());
        QVERIFY(licenses->ensure());QTRY_COMPARE(account.licensesCode(),QString("verified"));
        QTemporaryDir dir(brokerTemplate());QVERIFY(dir.isValid());
        QVERIFY(account.startLocalLicenseBroker(dir.path()));
        QJsonObject input{{"productId","congregation"},{"appId","com.iisacc.congregation.painter"},
            {"deviceId",account.manager()->deviceInfo().value("id").toString()},
            {"requestId","fa37a985-5c4d-48e7-8343-82cc7254536f"}};
        auto result=request(account.localLicenseBrokerPath(),input);
        QVERIFY(result.value("allowed").toBool());QVERIFY(!result.value("licenseToken").toString().isEmpty());
        QCOMPARE(result.value("requestId"),input.value("requestId"));
        auto update=input;update.insert("operation","update");update.insert("action","check");
        update.insert("platformKey","macos-arm64");update.insert("currentVersion","1.0.0");
        const auto updateResult=request(account.localLicenseBrokerPath(),update);
        QCOMPARE(updateResult.value("result").toObject().value("status").toString(),QString("release_unavailable"));
        QCOMPARE(server.updateRequests.size(),1);QCOMPARE(server.updateRequests.first().value("deviceId"),input.value("deviceId"));
        auto dreamscapes=input;dreamscapes.insert("productId","dreamscapes");dreamscapes.insert("appId","com.iisacc.dreamscapes");
        const auto dreamscapesResult=request(account.localLicenseBrokerPath(),dreamscapes);
        QVERIFY(dreamscapesResult.value("allowed").toBool());QVERIFY(!dreamscapesResult.value("licenseToken").toString().isEmpty());
        QLocalSocket helper;helper.connectToServer(account.localHelperBrokerPath());QTRY_COMPARE(helper.state(),QLocalSocket::ConnectedState);
        QVERIFY(helper.write(frame({{"op","hello"},{"productId","dreamscapes"},{"appId","com.iisacc.dreamscapes"},
            {"deviceId",input.value("deviceId")}}))>0);helper.flush();
        QTRY_VERIFY(helper.bytesAvailable()>=4);QByteArray helperBytes=helper.readAll();
        const auto size=[&] {if(helperBytes.size()<4)return quint32(0);const auto* b=reinterpret_cast<const unsigned char*>(helperBytes.constData());
            return (quint32(b[0])<<24)|(quint32(b[1])<<16)|(quint32(b[2])<<8)|b[3];};
        QTRY_VERIFY(([&] {helperBytes+=helper.readAll();return size()>0&&helperBytes.size()>=int(size()+4);})());
        QCOMPARE(QJsonDocument::fromJson(helperBytes.mid(4,size())).object().value("type").toString(),QString("ready"));
        auto mismatched=dreamscapes;mismatched.insert("appId","com.iisacc.congregation.painter");
        QCOMPARE(request(account.localLicenseBrokerPath(),mismatched).value("code").toString(),QString("invalid_app_request"));
#ifdef SOCIETY_LOCAL_CLIENT_PROBE
        QProcess product;product.start(QStringLiteral(SOCIETY_LOCAL_CLIENT_PROBE),{account.localLicenseBrokerPath(),input.value("deviceId").toString()});
        QVERIFY(product.waitForStarted(3000));QTRY_COMPARE_WITH_TIMEOUT(product.state(),QProcess::NotRunning,15000);
        QCOMPARE(product.exitStatus(),QProcess::NormalExit);QCOMPARE(product.exitCode(),0);QVERIFY(product.readAllStandardOutput().contains("licensed"));
#endif
        auto wrong=input;wrong.insert("deviceId",QString(64,'b'));result=request(account.localLicenseBrokerPath(),wrong);
        QVERIFY(!result.value("allowed").toBool());QVERIFY(result.value("licenseToken").isUndefined());
        wrong=input;wrong.insert("appId","foreign");result=request(account.localLicenseBrokerPath(),wrong);
        QCOMPARE(result.value("code").toString(),QString("invalid_app_request"));
        server.licenseOwned=false;QVERIFY(account.refreshLicenses());QTRY_COMPARE(account.licensesCode(),QString("verified"));
        QCOMPARE(request(account.localLicenseBrokerPath(),update).value("code").toString(),QString("update_not_authorized"));
        QCOMPARE(server.updateRequests.size(),1);
        result=request(account.localLicenseBrokerPath(),input);QVERIFY(!result.value("allowed").toBool());QVERIFY(!result.value("licenseToken").toString().isEmpty());
        QVERIFY(helper.write(frame({{"op","sync"}}))>0);helper.flush();QTRY_COMPARE(helper.state(),QLocalSocket::UnconnectedState);
    }
    void rejectsSharedDirectory() {
        QTemporaryDir dir(QStringLiteral(SOCIETY_TEST_RESOURCES)+"/shared-broker-XXXXXX");QVERIFY(dir.isValid());
        QVERIFY(::chmod(QFile::encodeName(dir.path()).constData(),0755)==0);
        AccountController account;QVERIFY(!account.startLocalLicenseBroker(dir.path()));
        QVERIFY(::chmod(QFile::encodeName(dir.path()).constData(),0700)==0);
    }
};
QTEST_GUILESS_MAIN(LocalLicenseTests)
#include "tst_local_license.moc"

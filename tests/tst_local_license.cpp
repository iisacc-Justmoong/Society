#include "Society/Account/AccountController.h"
#include "AccountServer.h"
#include "MemorySessionStore.h"
#include <QLocalSocket>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTemporaryDir>
#include <QProcess>
#include <QElapsedTimer>
#include <QCryptographicHash>
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
    void retiredVincentIsRejectedByBothBrokers() {
        QTemporaryDir dir(brokerTemplate()); QVERIFY(dir.isValid());
        AccountController account;
        QVERIFY(account.startLocalLicenseBroker(dir.path(), false));
        const QJsonObject input{{"productId", "vincent"}, {"appId", "com.iisacc.vincent"},
            {"requestId", "fa37a985-5c4d-48e7-8343-82cc7254536f"}};
        QCOMPARE(request(account.localLicenseBrokerPath(), input).value("code").toString(), QString("invalid_app_request"));
        QVERIFY(account.startLicenseBroker(0));
        QNetworkAccessManager client;
        QNetworkRequest http(account.licenseBrokerUrl());
        http.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        auto *reply = client.post(http, QJsonDocument(input).toJson(QJsonDocument::Compact));
        QTRY_VERIFY(reply->isFinished());
        QCOMPARE(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 400);
        reply->deleteLater();
    }
    void localTokenFastPathReusesSessionAndDetectsLogout() {
        AccountServer server; QVERIFY(server.server.listen(QHostAddress::LocalHost));
        MemorySessionStore savedSession, tokenStore;
        {
            AccountController gui(server.url(),&savedSession,nullptr);
            QVERIFY(gui.login("builder@example.com","FixtureOnly1!"));
            QTRY_VERIFY(gui.signedIn()); QTRY_VERIFY(!savedSession.values.isEmpty());
        }
        QTemporaryDir dir(brokerTemplate());QVERIFY(dir.isValid());
        AccountController daemon(server.url());int constructions=0;
        daemon.setBrokerAccountFactory([&](QObject* parent) {
            ++constructions;auto *account=new AccountController(server.url(),&savedSession,parent);
            account->manager()->licenseManager()->setTrustedKeys({{"fixture-owned-license",QByteArray::fromStdString(license_fixture::public_key())}});
            account->manager()->licenseManager()->setTokenStore(&tokenStore);return account;
        });
        daemon.setBrokerAccountRevision([&](AccountController* account) {
            const auto key=account->manager()->sessionStorageKey();
            if(!savedSession.values.contains(key))return QByteArray{};
            return QCryptographicHash::hash(savedSession.values.value(key),QCryptographicHash::Sha256);
        });
        QVERIFY(daemon.startLocalLicenseBroker(dir.path(),false));
        QJsonObject input{{"operation","local-license-v2"},{"productId","dreamscapes"},{"appId","com.iisacc.dreamscapes"},
            {"deviceId",daemon.manager()->deviceInfo().value("id").toString()},
            {"requestId","fa37a985-5c4d-48e7-8343-82cc7254536f"}};
        auto initial=request(daemon.localLicenseBrokerPath(),input);
        QVERIFY2(!initial.value("localToken").toString().isEmpty(),qPrintable(QString::fromUtf8(QJsonDocument(initial).toJson())));
        QCOMPARE(constructions,1);const auto requests=server.licenseRequests;
        savedSession.holdRead=true;server.licenseHang=true;
        qint64 slowest=0;
        for(int i=0;i<10;++i) {
            QElapsedTimer elapsed;elapsed.start();auto granted=request(daemon.localLicenseBrokerPath(),input);
            slowest=std::max(slowest,elapsed.elapsed());
            QVERIFY(!granted.value("localToken").toString().isEmpty());
            QVERIFY(!granted.contains("allowed"));
        }
        qInfo()<<"LOCAL_BROKER_MAX_MS"<<slowest;
        QVERIFY2(slowest<1000,"the cached local token exceeded one second");
        QCOMPARE(constructions,1);QCOMPARE(server.licenseRequests,requests);QVERIFY(!savedSession.releaseRead);
        savedSession.holdRead=false;savedSession.values.clear();
        auto revoked=request(daemon.localLicenseBrokerPath(),input);
        QVERIFY(revoked.value("localToken").toString().isEmpty());
        QVERIFY(!revoked.value("allowed").toBool());
    }
    void logoutDuringTokenReadDoesNotGrantStaleLicense() {
        AccountServer server; QVERIFY(server.server.listen(QHostAddress::LocalHost));
        MemorySessionStore savedSession, tokenStore;
        {
            AccountController gui(server.url(), &savedSession, nullptr);
            QVERIFY(gui.login("builder@example.com", "FixtureOnly1!"));
            QTRY_VERIFY(gui.signedIn()); QTRY_VERIFY(!savedSession.values.isEmpty());
        }
        QTemporaryDir dir(brokerTemplate()); QVERIFY(dir.isValid());
        AccountController daemon(server.url()); QPointer<AccountController> cached;
        daemon.setBrokerAccountFactory([&](QObject *parent) {
            auto *account = new AccountController(server.url(), &savedSession, parent);
            cached = account;
            account->manager()->licenseManager()->setTrustedKeys({{"fixture-owned-license", QByteArray::fromStdString(license_fixture::public_key())}});
            account->manager()->licenseManager()->setTokenStore(&tokenStore);
            return account;
        });
        daemon.setBrokerAccountRevision([&](AccountController *account) {
            const auto key = account->manager()->sessionStorageKey();
            return savedSession.values.contains(key) ? QCryptographicHash::hash(savedSession.values.value(key), QCryptographicHash::Sha256) : QByteArray{};
        });
        QVERIFY(daemon.startLocalLicenseBroker(dir.path(), false));
        const QJsonObject input{{"operation", "local-license-v2"}, {"productId", "dreamscapes"},
            {"appId", "com.iisacc.dreamscapes"}, {"deviceId", daemon.manager()->deviceInfo().value("id").toString()},
            {"requestId", "fa37a985-5c4d-48e7-8343-82cc7254536f"}};
        QVERIFY(!request(daemon.localLicenseBrokerPath(), input).value("localToken").toString().isEmpty());
        QVERIFY(cached); cached->manager()->licenseManager()->clear(); tokenStore.holdRead = true;
        QLocalSocket socket; socket.connectToServer(daemon.localLicenseBrokerPath());
        QVERIFY(socket.waitForConnected(1000)); socket.write(frame(input)); socket.flush();
        QTRY_VERIFY_WITH_TIMEOUT(bool(tokenStore.releaseRead), 500);
        savedSession.values.clear(); tokenStore.releaseRead();
        QTRY_VERIFY_WITH_TIMEOUT(socket.bytesAvailable() >= 4, 1000);
        const auto header = socket.peek(4);
        const auto *b = reinterpret_cast<const unsigned char *>(header.constData());
        const auto size = (quint32(b[0]) << 24) | (quint32(b[1]) << 16) | (quint32(b[2]) << 8) | quint32(b[3]);
        QVERIFY(size > 0 && size < 65536); QTRY_VERIFY_WITH_TIMEOUT(socket.bytesAvailable() >= size + 4, 1000);
        const auto result = QJsonDocument::fromJson(socket.readAll().mid(4)).object();
        QCOMPARE(result.value("requestId").toString(), input.value("requestId").toString());
        QVERIFY2(result.value("localToken").toString().isEmpty(), "logout during token loading issued a stale grant");
    }
    void timedOutRestoreCanBeDestroyedAndRetried() {
        AccountServer server;QVERIFY(server.server.listen(QHostAddress::LocalHost));
        MemorySessionStore savedSession,tokenStore;
        {
            AccountController gui(server.url(),&savedSession,nullptr);
            QVERIFY(gui.login("builder@example.com","FixtureOnly1!"));
            QTRY_VERIFY(gui.signedIn());QTRY_VERIFY(!savedSession.values.isEmpty());
        }
        savedSession.holdRead=true;
        QTemporaryDir dir(brokerTemplate());QVERIFY(dir.isValid());
        AccountController daemon(server.url());QPointer<AccountController> pending;
        daemon.setBrokerAccountFactory([&](QObject *parent) {
            auto *account=new AccountController(server.url(),&savedSession,parent);pending=account;
            account->manager()->licenseManager()->setTrustedKeys({{"fixture-owned-license",QByteArray::fromStdString(license_fixture::public_key())}});
            account->manager()->licenseManager()->setTokenStore(&tokenStore);return account;
        });
        daemon.setBrokerAccountRevision([&](AccountController *account) {
            const auto key=account->manager()->sessionStorageKey();
            return savedSession.values.contains(key)?QCryptographicHash::hash(savedSession.values.value(key),QCryptographicHash::Sha256):QByteArray{};
        });
        QVERIFY(daemon.startLocalLicenseBroker(dir.path(),false));
        const QJsonObject input{{"operation","local-license-v2"},{"productId","dreamscapes"},{"appId","com.iisacc.dreamscapes"},
            {"deviceId",daemon.manager()->deviceInfo().value("id").toString()},
            {"requestId","fa37a985-5c4d-48e7-8343-82cc7254536f"}};
        QElapsedTimer elapsed;elapsed.start();const auto timeout=request(daemon.localLicenseBrokerPath(),input);
        QVERIFY(elapsed.elapsed()<1000);QCOMPARE(timeout.value("code").toString(),QString("verification_unavailable"));
        QVERIFY(timeout.value("localToken").toString().isEmpty());QVERIFY(pending);
        // A stalled storage read must be retired by the broker itself.
        QTRY_VERIFY_WITH_TIMEOUT(pending.isNull(), 6000);
        savedSession.holdRead=false;
        const auto retry=request(daemon.localLicenseBrokerPath(),input);
        QVERIFY2(!retry.value("localToken").toString().isEmpty(),"timed-out restore left the broker unable to recover");
    }
    void legacyBrokerRechecksLogoutDuringTokenRead() {
        AccountServer server;QVERIFY(server.server.listen(QHostAddress::LocalHost));
        MemorySessionStore savedSession,tokenStore;
        {
            AccountController gui(server.url(),&savedSession,nullptr);
            QVERIFY(gui.login("builder@example.com","FixtureOnly1!"));
            QTRY_VERIFY(gui.signedIn());QTRY_VERIFY(!savedSession.values.isEmpty());
        }
        QTemporaryDir dir(brokerTemplate());QVERIFY(dir.isValid());
        AccountController daemon(server.url());QPointer<AccountController> cached;
        daemon.setBrokerAccountFactory([&](QObject *parent) {
            auto *account=new AccountController(server.url(),&savedSession,parent);cached=account;
            account->manager()->licenseManager()->setTrustedKeys({{"fixture-owned-license",QByteArray::fromStdString(license_fixture::public_key())}});
            account->manager()->licenseManager()->setTokenStore(&tokenStore);return account;
        });
        daemon.setBrokerAccountRevision([&](AccountController *account) {
            const auto key=account->manager()->sessionStorageKey();
            return savedSession.values.contains(key)?QCryptographicHash::hash(savedSession.values.value(key),QCryptographicHash::Sha256):QByteArray{};
        });
        QVERIFY(daemon.startLocalLicenseBroker(dir.path(),false));QVERIFY(daemon.startLicenseBroker(0));
        QJsonObject input{{"operation","local-license-v2"},{"productId","dreamscapes"},{"appId","com.iisacc.dreamscapes"},
            {"deviceId",daemon.manager()->deviceInfo().value("id").toString()},
            {"requestId","fa37a985-5c4d-48e7-8343-82cc7254536f"}};
        QVERIFY(!request(daemon.localLicenseBrokerPath(),input).value("localToken").toString().isEmpty());
        QVERIFY(cached);cached->manager()->licenseManager()->clear();tokenStore.holdRead=true;
        input.remove("operation");input.insert("subject",server.profile.value("sub"));
        const auto body=QJsonDocument(input).toJson(QJsonDocument::Compact);
        QTcpSocket socket;socket.connectToHost(QHostAddress::LocalHost,daemon.licenseBrokerUrl().port());
        QVERIFY(socket.waitForConnected(1000));
        const auto headers=QByteArray("POST /v1/authorize HTTP/1.1\r\nHost: 127.0.0.1:")+
            QByteArray::number(daemon.licenseBrokerUrl().port())+"\r\nContent-Type: application/json\r\nContent-Length: "+
            QByteArray::number(body.size())+"\r\n\r\n";
        socket.write(headers+body);socket.flush();QTRY_VERIFY_WITH_TIMEOUT(bool(tokenStore.releaseRead),500);
        savedSession.values.clear();tokenStore.releaseRead();
        QByteArray response;QTRY_VERIFY_WITH_TIMEOUT(([&] {response+=socket.readAll();const auto split=response.indexOf("\r\n\r\n");
            return split>=0 && QJsonDocument::fromJson(response.mid(split+4)).isObject();})(),1000);
        const auto result=QJsonDocument::fromJson(response.mid(response.indexOf("\r\n\r\n")+4)).object();
        QCOMPARE(result.value("requestId").toString(),input.value("requestId").toString());
        QVERIFY2(!result.value("allowed").toBool() && result.value("licenseToken").toString().isEmpty(),
                 "legacy broker issued a stale grant after logout");
    }
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
        auto vincent=input;vincent.insert("productId","vincent");vincent.insert("appId","com.iisacc.vincent");
        QCOMPARE(request(account.localLicenseBrokerPath(),vincent).value("code").toString(),QString("invalid_app_request"));
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
        const auto probe=qEnvironmentVariable("SOCIETY_TEST_CLIENT_PROBE",QStringLiteral(SOCIETY_LOCAL_CLIENT_PROBE));
        QProcess product;product.start(probe,{account.localLicenseBrokerPath(),input.value("deviceId").toString()});
        QVERIFY(product.waitForStarted(3000));QTRY_COMPARE_WITH_TIMEOUT(product.state(),QProcess::NotRunning,15000);
        const auto output=product.readAllStandardOutput();qInfo().noquote()<<output.trimmed();
        QCOMPARE(product.exitStatus(),QProcess::NormalExit);QCOMPARE(product.exitCode(),0);
        QVERIFY(output.contains("licensed"));QVERIFY(output.contains("LOCAL_LICENSE_CLIENT_US="));
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
    void localIncrementalGrantIsBound() {
        AccountServer server;QVERIFY(server.server.listen(QHostAddress::LocalHost));MemorySessionStore tokenStore;
        AccountController account(server.url());auto* licenses=account.manager()->licenseManager();
        licenses->setTrustedKeys({{"fixture-owned-license",QByteArray::fromStdString(license_fixture::public_key())}});
        QVERIFY(licenses->setTokenStore(&tokenStore));QVERIFY(account.login("builder@example.com","FixtureOnly1!"));QTRY_VERIFY(account.signedIn());
        QVERIFY(licenses->ensure());QTRY_COMPARE(account.licensesCode(),QString("verified"));
        QTemporaryDir dir(brokerTemplate());QVERIFY(dir.isValid());QVERIFY(account.startLocalLicenseBroker(dir.path(),false));
        QJsonObject input{{"operation","update"},{"action","grant"},{"productId","congregation"},
            {"appId","com.iisacc.congregation.painter"},{"deviceId",account.manager()->deviceInfo().value("id").toString()},
            {"requestId","fa37a985-5c4d-48e7-8343-82cc7254536f"},{"platformKey","macos-arm64"},
            {"version","1.3.0"},{"deltaFrom","1.2.3"},{"deltaFormat","iiudlt1"}};
        QCOMPARE(request(account.localLicenseBrokerPath(),input).value("result").toObject().value("status").toString(),QString("release_unavailable"));
        QCOMPARE(server.updateRequests.size(),1);QCOMPARE(server.updateRequests.last().value("deltaFrom").toString(),QString("1.2.3"));
        QCOMPARE(server.updateRequests.last().value("deltaFormat").toString(),QString("iiudlt1"));
        auto invalid=input;invalid.insert("deltaFormat","unknown");
        QCOMPARE(request(account.localLicenseBrokerPath(),invalid).value("code").toString(),QString("invalid_update_delta"));
        invalid=input;invalid.remove("deltaFormat");
        QCOMPARE(request(account.localLicenseBrokerPath(),invalid).value("code").toString(),QString("invalid_update_delta"));
        invalid=input;invalid.insert("deltaFrom","2147483648.2.3");
        QCOMPARE(request(account.localLicenseBrokerPath(),invalid).value("code").toString(),QString("invalid_update_delta"));
        invalid=input;invalid.insert("action","check");
        QCOMPARE(request(account.localLicenseBrokerPath(),invalid).value("code").toString(),QString("invalid_update_delta"));
        QCOMPARE(server.updateRequests.size(),1);
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

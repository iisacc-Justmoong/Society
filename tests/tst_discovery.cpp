#include "App/Network/NearbyDevices.h"
#include "FakeDiscoveryService.h"
#include "PairingCredentialsFixture.h"
#include "App/Network/PairingCredentials.h"
#include <iiServerHost.h>
#include <QSignalSpy>
#include <QJsonDocument>
#include <QTest>
#include <QUuid>

class DiscoveryTests : public QObject {
    Q_OBJECT
    const QString scope = NearbyDevices::accountScope("https://iisacc.com", "noncredential-discovery-fixture");
private slots:
    void localSeedsDeriveRotatingKeysAcrossDaysWithoutServer() {
        const auto issued = QDateTime::fromString("2026-09-10T13:00:00Z", Qt::ISODate);
        const auto grant = localPairingCredentialsFixture(scope, 'a', issued);
        const auto epoch = issued.toSecsSinceEpoch() / 300;
        // Ruby OpenSSL server vector, using a synthetic all-a seed and b scope.
        QCOMPARE(SocietyPairingCredentials::key(localPairingCredentialsFixture(QString(64, 'b'), 'a', issued), epoch, issued).toHex(),
                 QByteArray("6e99fa932cb0cb0ef51e4da192ca893039ef1ee5c586bb0353868e3d6fe4e130"));
        const auto first = SocietyPairingCredentials::key(grant, epoch, issued);
        QCOMPARE(first.size(), 32);
        QVERIFY(first != SocietyPairingCredentials::key(grant, epoch + 1, issued.addSecs(300)));
        const auto tomorrow = issued.addDays(1);
        const auto renewed = localPairingCredentialsFixture(scope, 'a', tomorrow);
        QCOMPARE(SocietyPairingCredentials::key(grant, epoch + 288, tomorrow),
                 SocietyPairingCredentials::key(renewed, epoch + 288, tomorrow));
        QVERIFY(!SocietyPairingCredentials::valid(grant, issued.addDays(7)));
        QVERIFY(!SocietyPairingCredentials::valid(grant, issued.addSecs(-301)));
        auto changed = grant; changed.insert("scope", QString(64, 'f'));
        QVERIFY(first != SocietyPairingCredentials::key(changed, epoch, issued));
        auto malformed = grant; auto seeds = malformed.value("seeds").toObject(); seeds.insert(QString::number(epoch / 288), "public-id");
        malformed.insert("seeds", seeds); QVERIFY(!SocietyPairingCredentials::valid(malformed, issued));
    }
    void localSeedAndCompatibilityKeyAuthenticateTheSameLanPeer() {
        FakeDiscoveryService a, b; NearbyDevices desktop(&a, QHostAddress::LocalHost), phone(&b, QHostAddress::LocalHost);
        desktop.setIdentity(scope, "desktop", "Desktop", "pc", true); phone.setIdentity(scope, "phone", "Phone", "phone", false);
        const auto grant = localPairingCredentialsFixture(scope);
        desktop.setCredentials(grant);
        auto legacy = pairingCredentialsFixture(scope); auto keys = legacy.value("keys").toObject();
        const auto time = QDateTime::currentDateTimeUtc(); const auto epoch = time.toSecsSinceEpoch() / 300;
        for (int offset = 0; offset < 2; ++offset)
            keys.insert(QString::number(epoch + offset), QString::fromLatin1(SocietyPairingCredentials::key(grant, epoch + offset, time).toHex()));
        legacy.insert("keys", keys); phone.setCredentials(legacy);
        a.announceTo(b); b.announceTo(a);
        QVERIFY(desktop.authenticated()); QVERIFY(phone.authenticated());
        QVERIFY(desktop.devices()[0].toMap().value("verified").toBool());
        QVERIFY(phone.devices()[0].toMap().value("verified").toBool());
        QVERIFY(!QJsonDocument(a.record).toJson().contains(QByteArray(64, 'a')));
    }
    void automaticDiscoveryRequiresSecretAndRejectsTamperedRecords() {
        FakeDiscoveryService a, b; NearbyDevices desktop(&a, QHostAddress::LocalHost), phone(&b, QHostAddress::LocalHost);
        desktop.setIdentity(scope, "desktop", "Desktop", "pc", true); phone.setIdentity(scope, "phone", "Phone", "phone", false);
        desktop.setCredentials(pairingCredentialsFixture(scope));
        b.announceTo(a); QVERIFY(!desktop.devices()[0].toMap().value("verified").toBool());
        phone.setCredentials(pairingCredentialsFixture(scope, 'b')); b.announceTo(a);
        QVERIFY(!desktop.devices()[0].toMap().value("verified").toBool());
        phone.setCredentials(pairingCredentialsFixture(scope)); b.announceTo(a);
        QVERIFY(desktop.devices()[0].toMap().value("verified").toBool());
        QVERIFY(!QJsonDocument(b.record).toJson().contains(QByteArray(64, 'a')));
        auto tampered = b.record; tampered.insert("name", "Impersonated phone");
        emit a.found("phone", tampered, QHostAddress::LocalHost, b.port);
        QVERIFY(!desktop.devices()[0].toMap().value("verified").toBool());
        tampered = b.record; tampered.insert("epoch", "1"); emit a.found("phone", tampered, QHostAddress::LocalHost, b.port);
        QVERIFY(!desktop.devices()[0].toMap().value("verified").toBool());
        auto expired = pairingCredentialsFixture(scope); expired.insert("expiresAt", QDateTime::currentDateTimeUtc().addSecs(-1).toString(Qt::ISODate));
        QSignalSpy ending(&desktop, &NearbyDevices::authenticationEnding); desktop.setCredentials(expired);
        QCOMPARE(ending.size(), 1); QVERIFY(!desktop.authenticated());
    }
    void accountFilterUpdatesAndStopsWithoutLoginRequests() {
        FakeDiscoveryService a, b; NearbyDevices desktop(&a, QHostAddress::LocalHost), phone(&b, QHostAddress::LocalHost);
        QVERIFY(!desktop.active());
        desktop.setIdentity(scope, "desktop", "Desktop", "pc", true);
        phone.setIdentity(scope, "phone", "Phone", "phone", false);
        QCOMPARE(a.starts, 1); QVERIFY(desktop.active());
        desktop.setIdentity(scope, "desktop", "Desktop", "pc", true); QCOMPARE(a.starts, 1);
        b.announceTo(a); QCOMPARE(desktop.devices().size(), 1);
        QCOMPARE(desktop.devices()[0].toMap().value("kind").toString(), "phone");
        a.announceTo(a); QCOMPARE(desktop.devices().size(), 1);
        auto record = b.record; record.insert("name", "Renamed phone");
        emit a.found("phone", record, QHostAddress::LocalHost, b.port);
        QCOMPARE(desktop.devices()[0].toMap().value("name").toString(), "Renamed phone");
        emit a.lost("phone"); QVERIFY(desktop.devices().isEmpty());
        record.insert("scope", NearbyDevices::accountScope("https://iisacc.com", "other-account"));
        emit a.found("other", record, QHostAddress::LocalHost, b.port); QVERIFY(desktop.devices().isEmpty());
        QVERIFY(scope != NearbyDevices::accountScope("https://other.example", "noncredential-discovery-fixture"));
        b.announceTo(a); QCOMPARE(desktop.devices().size(), 1);
        desktop.clear(); QVERIFY(!desktop.active()); QVERIFY(desktop.devices().isEmpty()); QVERIFY(a.record.isEmpty());
        b.announceTo(a); QVERIFY(desktop.devices().isEmpty());
    }
    void malformedAndPublicEndpointsNeverBecomeCandidates() {
        FakeDiscoveryService a, b; NearbyDevices desktop(&a, QHostAddress::LocalHost), phone(&b, QHostAddress::LocalHost);
        desktop.setIdentity(scope, "desktop", "Desktop", "pc", true); phone.setIdentity(scope, "phone", "Phone", "phone", false);
        for (const auto *ip : {"8.8.8.8", "169.254.169.254", "224.0.0.251", "::1"})
            emit a.found("phone", b.record, QHostAddress(ip), b.port);
        QVERIFY(desktop.devices().isEmpty());
        auto record = b.record; record.insert("name", "bad\nname"); emit a.found("phone", record, QHostAddress::LocalHost, b.port);
        record = b.record; record.insert("v", "2"); emit a.found("phone", record, QHostAddress::LocalHost, b.port);
        record = b.record; record.insert("id", "../bad"); emit a.found("phone", record, QHostAddress::LocalHost, b.port);
        QVERIFY(desktop.devices().isEmpty());
    }
    void discoveredInvitationPairsOnlyAfterRecipientAndHostConfirm() {
        FakeDiscoveryService a, b; NearbyDevices desktop(&a, QHostAddress::LocalHost), phone(&b, QHostAddress::LocalHost);
        desktop.setIdentity(scope, "desktop", "Desktop", "pc", true); phone.setIdentity(scope, "phone", "Phone", "phone", false);
        a.announceTo(b); b.announceTo(a);
        iiServerHost::LanPeer host, client; int fileReads = 0;
        QVERIFY(host.startHost("desktop", "Desktop", [&](const auto &, const auto &) {
            ++fileReads; return QJsonObject{{"ok", true}, {"entries", QJsonArray()}};
        }, {"127.0.0.1"}, QHostAddress::LocalHost));
        QVERIFY(desktop.invite("phone", host.createDeviceOffer("phone"))); QTRY_VERIFY(phone.hasIncoming());
        QCOMPARE(phone.incomingName(), "Desktop"); QCOMPARE(fileReads, 0); QVERIFY(!client.connected());
        const auto accepted = phone.accept(); QVERIFY(!accepted.isEmpty()); QVERIFY(!phone.hasIncoming());
        QVERIFY(client.join(accepted, "phone", "Phone")); QTRY_COMPARE(client.phase(), "confirming");
        QCOMPARE(fileReads, 0); QCOMPARE(client.verificationCode(), host.verificationCode());
        QVERIFY(host.confirmDevice()); QTRY_VERIFY(client.connected()); QCOMPARE(fileReads, 1);
        desktop.complete(); QVERIFY(desktop.outgoingName().isEmpty());
    }
    void invitationAcceptsAnyObservedEndpointForTheSameDevice() {
        FakeDiscoveryService a, b; NearbyDevices desktop(&a, QHostAddress::LocalHost), phone(&b, QHostAddress::LocalHost);
        desktop.setIdentity(scope, "desktop", "Desktop", "pc", true); phone.setIdentity(scope, "phone", "Phone", "phone", false);
        QUdpSocket first, second;
        QVERIFY(first.bind(QHostAddress::LocalHost, 0)); QVERIFY(second.bind(QHostAddress::LocalHost, 0));
        auto record = a.record; record.insert("name", "First interface");
        emit b.found("first", record, QHostAddress::LocalHost, first.localPort());
        record.insert("name", "Second interface");
        emit b.found("second", record, QHostAddress::LocalHost, second.localPort());
        QCOMPARE(phone.devices().size(), 1);
        auto &sender = phone.devices()[0].toMap().value("name") == "First interface" ? second : first;
        iiServerHost::LanPeer host;
        QVERIFY(host.startHost("desktop", "Desktop", [](const auto &, const auto &) { return QJsonObject(); }, {"127.0.0.1"}, QHostAddress::LocalHost));
        const auto offer = host.createDeviceOffer("phone"); iiServerHost::LanLink link;
        QVERIFY(iiServerHost::LanLink::decode(offer, &link));
        const QJsonObject invitation{{"v", "1"}, {"op", "invite"}, {"scope", scope}, {"from", "desktop"},
            {"to", "phone"}, {"nonce", b.record.value("nonce")}, {"request", QUuid::createUuid().toString(QUuid::WithoutBraces)},
            {"link", offer}, {"expires", QString::number(link.expiresAt.toMSecsSinceEpoch())}};
        const auto bytes = QJsonDocument(invitation).toJson(QJsonDocument::Compact);
        QCOMPARE(sender.writeDatagram(bytes, QHostAddress::LocalHost, b.port), bytes.size());
        QTRY_VERIFY_WITH_TIMEOUT(phone.hasIncoming(), 1000);
    }
    void oneBonjourServiceRetainsAllResolvedAddresses() {
        FakeDiscoveryService a, b; NearbyDevices desktop(&a, QHostAddress::LocalHost), phone(&b, QHostAddress::LocalHost);
        desktop.setIdentity(scope, "desktop", "Desktop", "pc", true); phone.setIdentity(scope, "phone", "Phone", "phone", false);
        a.announceTo(b); b.announceTo(a);
        emit b.found("desktop", a.record, QHostAddress("127.0.0.2"), a.port);
        iiServerHost::LanPeer host;
        QVERIFY(host.startHost("desktop", "Desktop", [](const auto &, const auto &) { return QJsonObject(); }, {"127.0.0.1"}, QHostAddress::LocalHost));
        QVERIFY(desktop.invite("phone", host.createDeviceOffer("phone")));
        QTRY_VERIFY_WITH_TIMEOUT(phone.hasIncoming(), 1000);
        emit b.lost("desktop"); QVERIFY(phone.devices().isEmpty());
    }
    void declineCancelAndExpiredRequests() {
        FakeDiscoveryService a, b; NearbyDevices desktop(&a, QHostAddress::LocalHost), phone(&b, QHostAddress::LocalHost);
        desktop.setIdentity(scope, "desktop", "Desktop", "pc", true); phone.setIdentity(scope, "phone", "Phone", "phone", false);
        a.announceTo(b); b.announceTo(a);
        iiServerHost::LanPeer host;
        QVERIFY(host.startHost("desktop", "Desktop", [](const auto &, const auto &) { return QJsonObject(); }, {"127.0.0.1"}, QHostAddress::LocalHost));
        QSignalSpy ended(&desktop, &NearbyDevices::invitationEnded);
        QVERIFY(desktop.invite("phone", host.createDeviceOffer("phone"))); QTRY_VERIFY(phone.hasIncoming());
        phone.decline(); QTRY_COMPARE(ended.size(), 1); QVERIFY(desktop.outgoingName().isEmpty());
        QVERIFY(desktop.invite("phone", host.createDeviceOffer("phone"))); QTRY_VERIFY(phone.hasIncoming());
        desktop.cancel(); QTRY_VERIFY(!phone.hasIncoming());
        QVERIFY(desktop.invite("phone", host.createDeviceOffer("phone", 1))); QTRY_VERIFY(phone.hasIncoming());
        QTRY_VERIFY(!phone.hasIncoming()); QVERIFY(phone.accept().isEmpty()); QTRY_COMPARE(ended.size(), 2);
        QVERIFY(desktop.invite("phone", host.createDeviceOffer("phone"))); QTRY_VERIFY(phone.hasIncoming());
        desktop.clear(); QTRY_VERIFY(!phone.hasIncoming());
    }
    void staleInstanceCannotReceiveAnInvitation() {
        FakeDiscoveryService a, b; NearbyDevices desktop(&a, QHostAddress::LocalHost), phone(&b, QHostAddress::LocalHost);
        desktop.setIdentity(scope, "desktop", "Desktop", "pc", true); phone.setIdentity(scope, "phone", "Phone", "phone", false);
        a.announceTo(b); b.announceTo(a);
        iiServerHost::LanPeer host;
        QVERIFY(host.startHost("desktop", "Desktop", [](const auto &, const auto &) { return QJsonObject(); }, {"127.0.0.1"}, QHostAddress::LocalHost));
        auto record = b.record; record.insert("nonce", "old-instance"); emit a.found("phone", record, QHostAddress::LocalHost, b.port);
        QSignalSpy invitations(&phone, &NearbyDevices::invitationReceived);
        QVERIFY(desktop.invite("phone", host.createDeviceOffer("phone"))); QTest::qWait(100);
        QCOMPARE(invitations.size(), 0); QVERIFY(!phone.hasIncoming());
    }
    void nativeBonjourFindsNoncredentialFixtures() {
        if (qEnvironmentVariableIntValue("SOCIETY_VERIFY_NATIVE_DISCOVERY") != 1) QSKIP("Native Bonjour is a separate explicit LAN check.");
        NearbyDevices desktop, phone;
        const auto privateScope = NearbyDevices::accountScope("discovery-test", QUuid::createUuid().toString());
        desktop.setIdentity(privateScope, "fixture-desktop", "Society discovery test desktop", "pc", true);
        phone.setIdentity(privateScope, "fixture-phone", "Society discovery test phone", "phone", false);
        desktop.setCredentials(pairingCredentialsFixture(privateScope), true, "fixture-desktop"); phone.setCredentials(pairingCredentialsFixture(privateScope), false, "fixture-desktop");
        QTRY_VERIFY2_WITH_TIMEOUT(!desktop.devices().isEmpty(), qPrintable(desktop.status()), 20000);
        QTRY_VERIFY2_WITH_TIMEOUT(!phone.devices().isEmpty(), qPrintable(phone.status()), 20000);
        QCOMPARE(desktop.devices()[0].toMap().value("id").toString(), "fixture-phone");
        QVERIFY(desktop.devices()[0].toMap().value("verified").toBool());
        QVERIFY(phone.devices()[0].toMap().value("verified").toBool());
        QCOMPARE(phone.devices()[0].toMap().value("primary").toString(), QString("fixture-desktop"));
        iiServerHost::LanPeer host;
        QVERIFY(host.startHost("fixture-desktop", "Discovery fixture", [](const auto &, const auto &) { return QJsonObject(); }));
        QVERIFY(desktop.invite("fixture-phone", host.createDeviceOffer("fixture-phone"), true));
        QTRY_VERIFY2_WITH_TIMEOUT(phone.hasIncoming(), qPrintable(phone.status()), 10000);
        QVERIFY(phone.incomingAutomatic());
        phone.clear(); QTRY_VERIFY_WITH_TIMEOUT(desktop.devices().isEmpty(), 10000);
    }
};
QTEST_GUILESS_MAIN(DiscoveryTests)
#include "tst_discovery.moc"

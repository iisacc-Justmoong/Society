#include "App/Network/NearbyDevices.h"
#include "FakeDiscoveryService.h"
#include <iiServerHost.h>
#include <QSignalSpy>
#include <QTest>
#include <QUuid>

class DiscoveryTests : public QObject {
    Q_OBJECT
    const QString scope = NearbyDevices::accountScope("https://iisacc.com", "noncredential-discovery-fixture");
private slots:
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
        QTRY_VERIFY2_WITH_TIMEOUT(!desktop.devices().isEmpty(), qPrintable(desktop.status()), 20000);
        QTRY_VERIFY2_WITH_TIMEOUT(!phone.devices().isEmpty(), qPrintable(phone.status()), 20000);
        QCOMPARE(desktop.devices()[0].toMap().value("id").toString(), "fixture-phone");
        phone.clear(); QTRY_VERIFY_WITH_TIMEOUT(desktop.devices().isEmpty(), 10000);
    }
};
QTEST_GUILESS_MAIN(DiscoveryTests)
#include "tst_discovery.moc"

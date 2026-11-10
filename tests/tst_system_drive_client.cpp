#include "Society/Drive/DriveController.h"
#include "Society/Files/ModelBackendClient.h"
#include "Shared/LocalBackendAddress.h"
#include <SocietyDrive.h>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>

namespace {
void sendState(QLocalSocket *socket, const QJsonObject &state)
{
    const auto body = QJsonDocument(QJsonObject{{"type", "state"}, {"state", state}}).toJson(QJsonDocument::Compact);
    QByteArray frame(4, '\0');
    qToBigEndian(quint32(body.size()), frame.data());
    socket->write(frame + body);
    socket->flush();
}

QJsonObject driveState(const QString &source, const QString &identifier, const QString &systemPath)
{
    return {{"sourcePath", source}, {"identifier", identifier}, {"ready", true},
            {"connected", !systemPath.isEmpty()}, {"connecting", false}, {"error", ""},
            {"systemPath", systemPath}};
}

QJsonObject receiveOperation(QLocalSocket *socket, const QString &operation)
{
    QByteArray input;
    QElapsedTimer timer; timer.start();
    while (timer.elapsed() < 5000) {
        QTest::qWait(10);
        input += socket->readAll();
        while (input.size() >= 4) {
            const auto size = qFromBigEndian<quint32>(input.constData());
            if (input.size() < qsizetype(size + 4)) break;
            const auto result = QJsonDocument::fromJson(input.mid(4, size)).object();
            input.remove(0, size + 4);
            if (result.value("op").toString() == operation) return result;
        }
    }
    return {};
}
}

class SystemDriveClientTests final : public QObject {
    Q_OBJECT
private slots:
    void anExistingDaemonIsNotStartedAgain()
    {
        QTemporaryDir fixture(QDir::cleanPath(QStringLiteral(SOCIETY_TEST_DIRECTORY) + "/../../.drive-ipc-XXXXXX"));
        QVERIFY(fixture.isValid());
        const auto oldRoot = qgetenv("SOCIETY_LOCAL_BROKER_DIR");
        const auto oldAutostart = qgetenv("SOCIETY_DISABLE_BACKEND_AUTOSTART");
        const auto restore = qScopeGuard([&] {
            if (oldRoot.isNull()) qunsetenv("SOCIETY_LOCAL_BROKER_DIR"); else qputenv("SOCIETY_LOCAL_BROKER_DIR", oldRoot);
            if (oldAutostart.isNull()) qunsetenv("SOCIETY_DISABLE_BACKEND_AUTOSTART"); else qputenv("SOCIETY_DISABLE_BACKEND_AUTOSTART", oldAutostart);
        });
        qputenv("SOCIETY_LOCAL_BROKER_DIR", fixture.path().toUtf8());
        qunsetenv("SOCIETY_DISABLE_BACKEND_AUTOSTART");
        QLocalServer server; QVERIFY(server.listen(societyBackendAddress(fixture.path())));
        int started = 0;
        ModelBackendClient connected(nullptr, 1000, [&] { ++started; });
        connected.start();
        QTRY_VERIFY_WITH_TIMEOUT(connected.connected(), 5000);
        QTest::qWait(400);
        QCOMPARE(started, 0);
        connected.stop();
        server.close();
        ModelBackendClient missing(nullptr, 1000, [&] { ++started; });
        missing.start();
        QTRY_COMPARE_WITH_TIMEOUT(started, 1, 5000);
    }

    void driveStateIsIndependentOfActiveModelOperations()
    {
        QTemporaryDir fixture(QDir::cleanPath(QStringLiteral(SOCIETY_TEST_DIRECTORY) + "/../../.drive-ipc-XXXXXX"));
        QVERIFY(fixture.isValid());
        const auto oldRoot = qgetenv("SOCIETY_LOCAL_BROKER_DIR");
        const auto oldAutostart = qgetenv("SOCIETY_DISABLE_BACKEND_AUTOSTART");
        const auto restore = qScopeGuard([&] {
            if (oldRoot.isNull()) qunsetenv("SOCIETY_LOCAL_BROKER_DIR"); else qputenv("SOCIETY_LOCAL_BROKER_DIR", oldRoot);
            if (oldAutostart.isNull()) qunsetenv("SOCIETY_DISABLE_BACKEND_AUTOSTART"); else qputenv("SOCIETY_DISABLE_BACKEND_AUTOSTART", oldAutostart);
        });
        qputenv("SOCIETY_LOCAL_BROKER_DIR", fixture.path().toUtf8());
        qputenv("SOCIETY_DISABLE_BACKEND_AUTOSTART", "1");
        QLocalServer server;
        QVERIFY(server.listen(societyBackendAddress(fixture.path())));
        ModelBackendClient client;
        QSignalSpy driveStates(&client, &ModelBackendClient::systemDriveStateReceived);
        QSignalSpy modelStates(&client, &ModelBackendClient::stateReceived);
        const auto source = fixture.filePath("selected.societycontainer");
        const QString id = "a43e17fc-a2a1-4b61-8935-79715770e150";
        client.setContainerPath(source);
        client.start();
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 5000);
        auto *peer = server.nextPendingConnection(); QVERIFY(peer);
        QTRY_VERIFY_WITH_TIMEOUT(client.connected(), 5000);
        sendState(peer, {{"selectedContainer", source}, {"containerPath", fixture.filePath("busy-old-container")},
                         {"systemDrive", driveState(source, id, "/test/society")}});
        QTRY_COMPARE_WITH_TIMEOUT(driveStates.size(), 1, 5000);
        QCOMPARE(modelStates.size(), 0);
        sendState(peer, {{"selectedContainer", fixture.filePath("stale")}, {"containerPath", source},
                         {"systemDrive", driveState(source, id, "/test/stale")}});
        QTRY_COMPARE_WITH_TIMEOUT(driveStates.size(), 2, 5000);
        QVERIFY(driveStates.last().first().toJsonObject().isEmpty());
    }

    void controllerUsesOnlyTheDaemonAndRejectsStaleIdentity()
    {
        QTemporaryDir fixture(QDir::cleanPath(QStringLiteral(SOCIETY_TEST_DIRECTORY) + "/../../.drive-ipc-XXXXXX"));
        QVERIFY(fixture.isValid());
        const auto oldRoot = qgetenv("SOCIETY_LOCAL_BROKER_DIR");
        const auto oldSettings = qgetenv("SOCIETY_STORAGE_SETTINGS_PATH");
        const auto oldAutostart = qgetenv("SOCIETY_DISABLE_BACKEND_AUTOSTART");
        const auto restore = qScopeGuard([&] {
            if (oldRoot.isNull()) qunsetenv("SOCIETY_LOCAL_BROKER_DIR"); else qputenv("SOCIETY_LOCAL_BROKER_DIR", oldRoot);
            if (oldSettings.isNull()) qunsetenv("SOCIETY_STORAGE_SETTINGS_PATH"); else qputenv("SOCIETY_STORAGE_SETTINGS_PATH", oldSettings);
            if (oldAutostart.isNull()) qunsetenv("SOCIETY_DISABLE_BACKEND_AUTOSTART"); else qputenv("SOCIETY_DISABLE_BACKEND_AUTOSTART", oldAutostart);
        });
        qputenv("SOCIETY_LOCAL_BROKER_DIR", fixture.path().toUtf8());
        qputenv("SOCIETY_STORAGE_SETTINGS_PATH", fixture.filePath("storage.json").toUtf8());
        qputenv("SOCIETY_DISABLE_BACKEND_AUTOSTART", "1");
        const auto container = iiSocietyContainer::SocietyDrive::createAt(fixture.path()); QVERIFY(container);
        QLocalServer server; QVERIFY(server.listen(societyBackendAddress(fixture.path())));
        DriveController controller;
        QVERIFY(controller.openContainer(container->rootPath()));
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 5000);
        auto *peer = server.nextPendingConnection(); QVERIFY(peer);
        const auto source = container->rootPath();
        const auto id = container->identifier();
        QVERIFY(!receiveOperation(peer, "snapshot").isEmpty());
        const auto send = [&](const QString &identifier, const QString &path) {
            sendState(peer, {{"selectedContainer", source}, {"containerPath", source},
                             {"systemDrive", driveState(source, identifier, path)}});
        };
        send("b43e17fc-a2a1-4b61-8935-79715770e150", "/test/wrong");
        QTest::qWait(50);
        QVERIFY(controller.systemPath().isEmpty());
        send(id, "/test/society");
        QTRY_COMPARE_WITH_TIMEOUT(controller.systemPath(), QStringLiteral("/test/society"), 5000);
        QVERIFY(controller.openContainer(source));
        QVERIFY(controller.systemPath().isEmpty());
        QCOMPARE(receiveOperation(peer, "snapshot").value("container").toString(), source);
        send(id, "/test/society");
        QTRY_COMPARE_WITH_TIMEOUT(controller.systemPath(), QStringLiteral("/test/society"), 5000);
        send("b43e17fc-a2a1-4b61-8935-79715770e150", "/test/wrong");
        QTRY_VERIFY_WITH_TIMEOUT(controller.systemPath().isEmpty(), 5000);
        send(id, "/test/society");
        QTRY_COMPARE_WITH_TIMEOUT(controller.systemPath(), QStringLiteral("/test/society"), 5000);
        sendState(peer, {{"selectedContainer", fixture.filePath("different")}, {"containerPath", source},
                         {"systemDrive", driveState(source, id, "/test/society")}});
        QTRY_VERIFY_WITH_TIMEOUT(controller.systemPath().isEmpty(), 5000);
        send(id, "/test/society");
        QTRY_COMPARE_WITH_TIMEOUT(controller.systemPath(), QStringLiteral("/test/society"), 5000);
        controller.connectToSystem();
        const auto request = receiveOperation(peer, "systemDriveReconnect");
        QCOMPARE(request.value("container").toString(), source);
        QCOMPARE(request.value("identifier").toString(), id);
        QVERIFY(!controller.busy());
        server.close();
        peer->disconnectFromServer();
        QTRY_VERIFY_WITH_TIMEOUT(controller.systemPath().isEmpty(), 5000);
        controller.connectToSystem();
        QVERIFY(controller.systemPath().isEmpty());
        QVERIFY(!controller.busy());
        QVERIFY(controller.systemStatus().contains("background service"));
        const auto manifest = QDir(source).filePath(".society-drive.json");
        QVERIFY(QFile::rename(manifest, manifest + ".test-backup"));
        QVERIFY(!controller.reloadFromDisk());
        QVERIFY(!controller.hasDrive());
        QVERIFY(controller.systemStatus().isEmpty());
    }
};
QTEST_GUILESS_MAIN(SystemDriveClientTests)
#include "tst_system_drive_client.moc"

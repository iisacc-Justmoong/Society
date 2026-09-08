#include "App/Services/SocietyRuntime.h"
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class RuntimeTests : public QObject {
    Q_OBJECT
private slots:
    void suspendedReceiverReleasesLockAndReplaysUnacknowledgedData()
    {
        QTemporaryDir root(SOCIETY_TEST_DIRECTORY "/runtime-XXXXXX");
        SocietyRuntime runtime(true, {root.path(), 50, 400});
        runtime.setApplicationState(Qt::ApplicationActive);
        QVERIFY(runtime.helper()->isRunning());
        QVERIFY(runtime.inbox()->connected());
        iiSocietyHelper::Helper sender;
        QVERIFY(sender.start({"com.iisacc.dreamscapes", "Dreamscapes", "1"}, {root.path(), 50, 400}));
        const auto before = sender.sendData("test.data", {{"phase", "foreground"}});
        auto received = [&](const QString &id) {
            for (const auto &message : runtime.inbox()->messages())
                if (message.toMap().value("id") == id) return true;
            return false;
        };
        QTRY_VERIFY(received(before));
        QVERIFY(runtime.inbox()->acknowledge(runtime.inbox()->lastSequence()));
        runtime.setApplicationState(Qt::ApplicationSuspended);
        QVERIFY(!runtime.helper()->isRunning());
        QVERIFY(!runtime.inbox()->connected());
        SocietyDaemonService otherReceiver;
        QVERIFY(otherReceiver.start(root.path(), 50, 400)); // No lock held by a suspended app.
        otherReceiver.stop();
        const auto offline = sender.sendData("test.data", {{"phase", "suspended"}});
        QVERIFY(!offline.isEmpty());
        runtime.setApplicationState(Qt::ApplicationActive);
        QTRY_VERIFY(received(offline));
        QVERIFY(!received(before)); // Acknowledged data does not replay.
        runtime.setApplicationState(Qt::ApplicationSuspended);
        runtime.setApplicationState(Qt::ApplicationActive);
        QTRY_VERIFY(received(offline)); // Unacknowledged data does replay.
        int copies = 0;
        for (const auto &message : runtime.inbox()->readAfter(0))
            if (message.toMap().value("id") == offline) ++copies;
        QCOMPARE(copies, 1);
    }

    void startupFailureRecoversWithoutRelaunch()
    {
        QTemporaryDir root(SOCIETY_TEST_DIRECTORY "/runtime-retry-XXXXXX");
        const auto path = root.filePath("unavailable");
        QFile blocker(path);
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        blocker.close();
        SocietyRuntime runtime(true, {path, 50, 400});
        runtime.setApplicationState(Qt::ApplicationActive);
        QVERIFY(!runtime.helper()->isRunning());
        QVERIFY(!runtime.inbox()->connected());
        QVERIFY(blocker.remove());
        QTRY_VERIFY_WITH_TIMEOUT(runtime.helper()->isRunning() && runtime.inbox()->connected(), 4000);
        const auto instance = runtime.helper()->self().instanceId;
        runtime.setApplicationState(Qt::ApplicationInactive); // Picker / system sheet is not suspension.
        QCOMPARE(runtime.helper()->self().instanceId, instance);
        QVERIFY(runtime.inbox()->connected());
    }

    void desktopWindowDeactivationKeepsReceiving()
    {
        QTemporaryDir root(SOCIETY_TEST_DIRECTORY "/runtime-desktop-XXXXXX");
        SocietyDaemonService daemon;
        QVERIFY(daemon.start(root.path(), 50, 400));
        SocietyRuntime runtime(false, {root.path(), 50, 400});
        runtime.setApplicationState(Qt::ApplicationActive);
        const auto instance = runtime.helper()->self().instanceId;
        runtime.setApplicationState(Qt::ApplicationHidden);
        QCOMPARE(runtime.helper()->self().instanceId, instance);
        QVERIFY(runtime.helper()->isRunning());
        QVERIFY(runtime.inbox()->connected());
    }
};
QTEST_GUILESS_MAIN(RuntimeTests)
#include "tst_runtime.moc"

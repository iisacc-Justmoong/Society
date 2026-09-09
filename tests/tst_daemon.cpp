#include "Daemon/DaemonService.h"
#include "App/Services/SocietyInbox.h"
#include <iiSocietyHelper.h>
#include <DeliveryStore.h>
#include <QDir>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class DaemonTests : public QObject {
    Q_OBJECT
private slots:
    void offlineQueueAndLateSocietyReceiver()
    {
        QTemporaryDir root(SOCIETY_TEST_DIRECTORY "/daemon-XXXXXX");
        QString message;
        {
            iiSocietyHelper::Helper helper;
            QVERIFY(helper.start({"com.iisacc.dreamscapes", "Dreamscapes", "1"}, {root.path(), 50, 400}));
            message = helper.sendData("generation.queued", {{"job", "persisted-before-society"}});
            QVERIFY(!message.isEmpty());
        }
        SocietyDaemonService daemon;
        QVERIFY(daemon.start(root.path(), 50, 400));
        SocietyDaemonService duplicate;
        QVERIFY(!duplicate.start(root.path(), 50, 400));
        SocietyInbox inbox;
        QSignalSpy received(&inbox, &SocietyInbox::dataReceived);
        QVERIFY(inbox.start(root.path()));
        auto hasMessage = [&] {
            for (const auto &entry : received) if (entry[0].toMap().value("id") == message) return true;
            return false;
        };
        QTRY_VERIFY(hasMessage());
        QVERIFY(inbox.lastSequence() > 0);
        QVERIFY(inbox.acknowledge(inbox.lastSequence()));
        daemon.stop();
        QVERIFY(duplicate.start(root.path(), 50, 400));
        QTest::qWait(500);
        int copies = 0;
        for (const auto &entry : received) if (entry[0].toMap().value("id") == message) ++copies;
        QCOMPARE(copies, 1);
    }

    void processCrashRestartAndSocietyAppReceipt()
    {
        QTemporaryDir root(SOCIETY_TEST_DIRECTORY "/daemon-process-XXXXXX");
        QProcess daemon;
        daemon.setProgram(QStringLiteral(SOCIETY_DAEMON_EXECUTABLE));
        daemon.setArguments({"--directory", root.path()});
        daemon.start();
        QVERIFY(daemon.waitForStarted());
        iiSocietyHelper::Helper helper;
        QVERIFY(helper.start({"com.iisacc.dreamscapes.test", "Dreamscapes test", "1"}, {root.path(), 100, 5000}));
        iiSocietyHelper::DeliveryStore store;
        QVERIFY(store.open(root.path()));
        const auto before = helper.sendData("app.data", {{"value", "before-crash"}});
        auto contains = [&](const QString &id) {
            for (const auto &entry : store.readAfter(0)) if (entry.toMap().value("id") == id) return true;
            return false;
        };
        QTRY_VERIFY(contains(before));
        daemon.kill();
        QVERIFY(daemon.waitForFinished(3000));
        const auto offline = helper.sendData("app.data", {{"value", "while-daemon-offline"}});
        QVERIFY(!offline.isEmpty());
        QVERIFY(!contains(offline));
        daemon.start();
        QVERIFY(daemon.waitForStarted());
        QTRY_VERIFY(contains(offline));

        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("SOCIETY_HELPER_DIRECTORY", root.path());
        environment.insert("SOCIETY_STORAGE_SETTINGS_PATH", root.filePath("storage.json"));
        environment.insert("SOCIETY_DISABLE_SESSION_RESTORE", "1");
        environment.insert("QT_QPA_PLATFORM", "offscreen");
        environment.insert("QT_QUICK_BACKEND", "software");
        environment.insert("QML_DISABLE_DISK_CACHE", "1");
        QProcess society;
        society.setProcessEnvironment(environment);
        society.setProcessChannelMode(QProcess::MergedChannels);
        society.start(QStringLiteral(SOCIETY_EXECUTABLE_PATH), {});
        QVERIFY(society.waitForStarted());
        QByteArray output;
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 5000 && !output.contains(offline.toUtf8())) {
            QTest::qWait(50);
            output += society.readAll();
        }
        society.terminate();
        if (!society.waitForFinished(3000)) { society.kill(); society.waitForFinished(3000); }
        const bool daemonSurvived = daemon.state() == QProcess::Running;
        daemon.kill();
        daemon.waitForFinished(3000);
        QVERIFY(daemonSurvived);
        QVERIFY2(output.contains("Society inbox received") && output.contains(offline.toUtf8()), output.constData());
        int copies = 0;
        for (const auto &entry : store.readAfter(0)) if (entry.toMap().value("id") == before) ++copies;
        QCOMPARE(copies, 1);
    }
};
QTEST_GUILESS_MAIN(DaemonTests)
#include "tst_daemon.moc"

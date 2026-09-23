#include "Daemon/DaemonService.h"
#include "App/Services/SocietyInbox.h"
#include <iiSocietyHelper.h>
#include <DeliveryStore.h>
#include <QDir>
#include <QJsonDocument>
#include <QLockFile>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QScopeGuard>

class DaemonTests : public QObject {
    Q_OBJECT
private slots:
    void headlessServerRejectsUnsafeConfigurationBeforeStartingServices() {
        QTemporaryDir root(SOCIETY_TEST_DIRECTORY "/daemon-server-invalid-XXXXXX");
        for (const auto &arguments : QList<QStringList>{
                {"--server", "wss://nas.example.test"},
                {"--sync", "--host"},
                {"--sync", "--server", "ws://nas.example.test"},
                {"--sync", "--server", "wss://nas.example.test/?token=secret"},
                {"--sync", "--container", root.filePath("missing")},
                {"--sync", "--login-file", root.filePath("missing.json")}}) {
            QProcess daemon; daemon.start(QStringLiteral(SOCIETY_DAEMON_EXECUTABLE), arguments);
            QVERIFY(daemon.waitForStarted()); QVERIFY(daemon.waitForFinished(5000)); QCOMPARE(daemon.exitCode(), 2);
        }
    }
    void daemonIncludesUsableTlsAndSqliteBackends() {
        QProcess daemon; daemon.start(QStringLiteral(SOCIETY_DAEMON_EXECUTABLE), {"--check-runtime"});
        QVERIFY(daemon.waitForStarted()); QVERIFY(daemon.waitForFinished(5000)); QCOMPARE(daemon.exitCode(), 0);
        const auto result = QJsonDocument::fromJson(daemon.readAllStandardOutput()).object();
        QVERIFY(result.value("tls").toBool()); QVERIFY(result.value("sqlite").toBool());
    }
    void isolatedSyncServicePublishesStateWithoutAccessingAnAccount() {
        QTemporaryDir root(SOCIETY_TEST_DIRECTORY "/daemon-sync-XXXXXX");
        QProcess daemon;
        daemon.start(QStringLiteral(SOCIETY_DAEMON_EXECUTABLE), {"--directory", root.path(), "--sync", "--status-file", root.filePath("status.json")});
        const auto stop = qScopeGuard([&] {
            if (daemon.state() != QProcess::NotRunning) { daemon.terminate(); if (!daemon.waitForFinished(4000)) { daemon.kill(); daemon.waitForFinished(); } }
        });
        QVERIFY(daemon.waitForStarted());
        const auto readState = [&] {
            QFile file(root.filePath("status.json"));
            return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject{};
        };
        // Observe ownership while running; shutdown correctly releases it and
        // may publish that final state while draining asynchronous workers.
        QTRY_VERIFY_WITH_TIMEOUT(readState().value("ownsNetwork").toBool(), 5000);
        const auto state = readState();
        QVERIFY(!state.value("signedIn").toBool()); QVERIFY(!state.value("connected").toBool());
        QVERIFY(!state.contains("credentials"));
        daemon.terminate(); QVERIFY(daemon.waitForFinished(4000)); QCOMPARE(daemon.exitCode(), 0);
        QLockFile owner(root.filePath("SyncRuntime/owner.lock")); QVERIFY(owner.tryLock());
    }
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
        // The packaged GUI must load its own Qt/SQLite runtime even when the
        // native test executable uses explicitly selected SDK libraries.
        for (const auto *key : {"DYLD_LIBRARY_PATH", "DYLD_FRAMEWORK_PATH", "DYLD_FALLBACK_LIBRARY_PATH"})
            environment.remove(QString::fromLatin1(key));
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
        // A cold packaged Qt/QML launch may exceed five seconds on external storage.
        // Keep the receipt check bounded while allowing the actual window runtime to load.
        while (timer.elapsed() < 30000 && society.state() != QProcess::NotRunning && !output.contains(offline.toUtf8())) {
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

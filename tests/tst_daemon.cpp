#include "Daemon/DaemonService.h"
#include "Society/Services/SocietyInbox.h"
#include <iiSocietyHelper.h>
#include <DeliveryStore.h>
#include <SocietyDrive.h>
#include <QDir>
#include <SharedStorage.h>
#include <QJsonDocument>
#include <QJsonArray>
#include <QUuid>
#include <QLockFile>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QScopeGuard>

class DaemonTests : public QObject {
    Q_OBJECT
private slots:
    void inboxPollingIsDeferredAndStoppedResultsCannotCrossAConnection()
    {
        QTemporaryDir root(SOCIETY_TEST_DIRECTORY "/inbox-worker-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(root.path()));
        qputenv("SOCIETY_CONTAINER_PATH", root.path().toUtf8());
        const auto directory = root.filePath(".society-runtime/helpers/v1");
        {
            iiSocietyHelper::DeliveryStore sender;
            QVERIFY(sender.open(directory));
            const iiSocietyHelper::Peer peer{{"com.iisacc.test", "Test", "1"},
                QUuid::createUuid().toString(QUuid::WithoutBraces)};
            QVERIFY2(!sender.enqueue(peer, "files.changed", {{"file", "fixture"}}).isEmpty(),
                qPrintable(sender.errorString()));
            QCOMPARE(sender.receivePending(), 1);
        }
        SocietyInbox inbox;
        QSignalSpy received(&inbox, &SocietyInbox::dataReceived);
        QVERIFY(inbox.start(directory));
        QCOMPARE(received.count(), 0); // start/poll does not dispatch disk results inline.
        QCOMPARE(inbox.readAfter(0).size(), 1); // Explicit commands share the same SQL owner.
        inbox.stop();
        QCoreApplication::sendPostedEvents(&inbox);
        QCOMPARE(received.count(), 0);
        QVERIFY(!inbox.connected());
        QVERIFY(inbox.start(directory));
        QTRY_COMPARE(received.count(), 1);
        QVERIFY(inbox.acknowledge(inbox.lastSequence()));
        inbox.stop();
        QVERIFY(inbox.start(directory));
        QTest::qWait(300);
        QCOMPARE(received.count(), 1); // Acknowledged messages remain acknowledged after reopen.
    }
    void explicitPathsOutsideTheSelectedRuntimeAreRejectedBeforeWriting() {
        QTemporaryDir root(SOCIETY_TEST_DIRECTORY "/daemon-scope-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(root.path()));
        qputenv("SOCIETY_CONTAINER_PATH", root.path().toUtf8());
        const auto outside = root.path() + "-outside";
        for (const auto &arguments : QList<QStringList>{{"--directory", outside}, {"--status-file", outside}}) {
            QProcess daemon; daemon.start(QStringLiteral(SOCIETY_DAEMON_EXECUTABLE), arguments);
            // A cold signed bundle on external storage can need more than five
            // seconds to load its own runtime before rejecting the argument.
            QVERIFY(daemon.waitForStarted()); QVERIFY(daemon.waitForFinished(15000)); QCOMPARE(daemon.exitCode(), 2);
            QVERIFY(!QFileInfo::exists(outside));
        }
    }
    void daemonMaintainsSelectedFolderWithoutAnAccount()
    {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/daemon-maintenance-XXXXXX");
        const auto root = fixture.filePath("Society.societycontainer");
        QVERIFY(QDir().mkdir(root));
        // Build the package fixture without opening it in this process;
        // only the child daemon can register its maintenance worker.
        const auto identifier = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QJsonArray sections;
        for (const auto section : iiSocietyContainer::allStoreSections()) {
            const auto name = iiSocietyContainer::storeSectionName(section);
            QVERIFY(QDir(root).mkdir(name));
            sections.append(QJsonObject{{"id", iiSocietyContainer::storeSectionKey(section)}, {"name", name}, {"path", name}});
        }
        const auto manifest = QDir(root).filePath(".society-drive.json");
        const auto temporary = QDir(root).filePath(".society-write-daemon-test");
        QFile orphan(temporary); QVERIFY(orphan.open(QIODevice::WriteOnly));
        QCOMPARE(orphan.write("orphan"), 6); orphan.close();
        QFile savedManifest(manifest); QVERIFY(savedManifest.open(QIODevice::WriteOnly));
        savedManifest.write(QJsonDocument(QJsonObject{{"type", "SocietyDrive"}, {"schemaVersion", 1},
            {"filesLayoutVersion", 1}, {"identifier", identifier}, {"displayName", "Society"}, {"sections", sections}}).toJson());
        savedManifest.close();
        const auto settings = fixture.filePath("storage.json");
        QFile selected(settings); QVERIFY(selected.open(QIODevice::WriteOnly));
        selected.write(QJsonDocument(QJsonObject{{"schemaVersion", 1}, {"path", root},
            {"containerId", identifier}}).toJson()); selected.close();
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.remove("SOCIETY_CONTAINER_PATH");
        environment.insert("SOCIETY_STORAGE_SETTINGS_PATH", settings);
        QProcess daemon; daemon.setProcessEnvironment(environment);
        daemon.start(QStringLiteral(SOCIETY_DAEMON_EXECUTABLE), {"--directory", QDir(root).filePath(".society-runtime/helpers/v1"), "--exit-after-ms", "1500"});
        QVERIFY(daemon.waitForStarted());
        QVERIFY(daemon.waitForFinished(5000));
        QCOMPARE(daemon.exitCode(), 0);
        QVERIFY(!QFileInfo::exists(temporary));
        QVERIFY(QFileInfo::exists(manifest));
    }
    void headlessServerRejectsUnsafeConfigurationBeforeStartingServices() {
        QTemporaryDir root(SOCIETY_TEST_DIRECTORY "/daemon-server-invalid-XXXXXX.societycontainer");
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
        QTemporaryDir root(SOCIETY_TEST_DIRECTORY "/daemon-sync-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(root.path()));
        qputenv("SOCIETY_CONTAINER_PATH", root.path().toUtf8());
        const auto directory = root.filePath(".society-runtime/helpers/v1");
        QProcess daemon;
        daemon.start(QStringLiteral(SOCIETY_DAEMON_EXECUTABLE), {"--directory", directory, "--sync", "--status-file", root.filePath(".society-runtime/status.json"), "--exit-after-ms", "1500"});
        const auto stop = qScopeGuard([&] {
            if (daemon.state() != QProcess::NotRunning) { daemon.terminate(); if (!daemon.waitForFinished(4000)) { daemon.kill(); daemon.waitForFinished(); } }
        });
        QVERIFY(daemon.waitForStarted());
        const auto readState = [&] {
            QFile file(root.filePath(".society-runtime/status.json"));
            return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject{};
        };
        // Observe ownership while running; shutdown correctly releases it and
        // may publish that final state while draining asynchronous workers.
        QTRY_VERIFY_WITH_TIMEOUT(readState().value("ownsNetwork").toBool(), 5000);
        const auto state = readState();
        QVERIFY(!state.value("signedIn").toBool()); QVERIFY(!state.value("connected").toBool());
        QVERIFY(!state.contains("credentials"));
        // The finite service duration provides a native shutdown request on
        // Windows, whose QProcess::terminate() does not deliver POSIX SIGTERM.
        QVERIFY(daemon.waitForFinished(4000)); QCOMPARE(daemon.exitCode(), 0);
        QLockFile owner(root.filePath(".society-runtime/sync/owner.lock")); QVERIFY(owner.tryLock());
    }
    void offlineQueueAndLateSocietyReceiver()
    {
        QTemporaryDir root(SOCIETY_TEST_DIRECTORY "/daemon-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(root.path()));
        qputenv("SOCIETY_CONTAINER_PATH", root.path().toUtf8());
        const auto directory = root.filePath(".society-runtime/helpers/v1");
        QString message;
        {
            iiSocietyHelper::Helper helper;
            QVERIFY(helper.start({"com.iisacc.dreamscapes", "Dreamscapes", "1"}, {directory, 50, 400}));
            message = helper.sendData("generation.queued", {{"job", "persisted-before-society"}});
            QVERIFY(!message.isEmpty());
        }
        SocietyDaemonService daemon;
        QVERIFY(daemon.start(directory, 50, 400));
        SocietyDaemonService duplicate;
        QVERIFY(!duplicate.start(directory, 50, 400));
        SocietyInbox inbox;
        QSignalSpy received(&inbox, &SocietyInbox::dataReceived);
        QVERIFY(inbox.start(directory));
        auto hasMessage = [&] {
            for (const auto &entry : received) if (entry[0].toMap().value("id") == message) return true;
            return false;
        };
        QTRY_VERIFY(hasMessage());
        QVERIFY(inbox.lastSequence() > 0);
        QVERIFY(inbox.acknowledge(inbox.lastSequence()));
        daemon.stop();
        QVERIFY(duplicate.start(directory, 50, 400));
        QTest::qWait(500);
        int copies = 0;
        for (const auto &entry : received) if (entry[0].toMap().value("id") == message) ++copies;
        QCOMPARE(copies, 1);
    }

    void processCrashRestartAndSocietyAppReceipt()
    {
        QTemporaryDir root(SOCIETY_TEST_DIRECTORY "/daemon-process-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(root.path()));
        qputenv("SOCIETY_CONTAINER_PATH", root.path().toUtf8());
        const auto directory = root.filePath(".society-runtime/helpers/v1");
        QProcess daemon;
        daemon.setProgram(QStringLiteral(SOCIETY_DAEMON_EXECUTABLE));
        daemon.setArguments({"--directory", directory});
        daemon.start();
        QVERIFY(daemon.waitForStarted());
        iiSocietyHelper::Helper helper;
        QVERIFY(helper.start({"com.iisacc.dreamscapes.test", "Dreamscapes test", "1"}, {directory, 100, 5000}));
        iiSocietyHelper::DeliveryStore store;
        QVERIFY(store.open(directory));
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
        environment.insert("SOCIETY_HELPER_DIRECTORY", directory);
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

#include "Society/Services/SyncOwnership.h"
#include <QFile>
#include <QDir>
#include <iiSocietyContainer>
#include <QSignalSpy>
#include <QProcess>
#include <QTextStream>
#include <QTemporaryDir>
#include <QTest>
#include <QElapsedTimer>
#include <QSemaphore>
#include <QScopeGuard>
#include <atomic>

class SyncOwnershipTests : public QObject {
    Q_OBJECT
private slots:
    void destroyingAnOwnerDoesNotWaitForAnInFlightValidation() {
        QTemporaryDir root(SOCIETY_TEST_DIRECTORY "/sync-slow-validation-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(root.path()));
        qputenv("SOCIETY_CONTAINER_PATH", root.path().toUtf8());
        auto entered = std::make_shared<QSemaphore>();
        auto release = std::make_shared<QSemaphore>();
        auto finished = std::make_shared<std::atomic_bool>(false);
        const auto unblock = qScopeGuard([release] { release->release(); });
        auto owner = std::make_unique<SyncOwnership>(root.filePath(".society-runtime/sync"), true, nullptr,
            [entered, release, finished](const QString &) {
                entered->release(); release->acquire(); *finished = true; return true;
            });
        QVERIFY(owner->start());
        QTRY_VERIFY_WITH_TIMEOUT(entered->available() > 0, 3000);
        QElapsedTimer elapsed; elapsed.start();
        owner.reset();
        QVERIFY2(elapsed.elapsed() < 500, "Owner destruction waited for storage validation");
        release->release();
        QTRY_VERIFY_WITH_TIMEOUT(finished->load(), 3000);
    }
    void selectingAnotherRuntimeReleasesThePreviousContainerLease() {
        QTemporaryDir parent(SOCIETY_TEST_DIRECTORY "/sync-container-switch-XXXXXX");
        const auto firstRoot = parent.filePath("first.societycontainer");
        const auto secondRoot = parent.filePath("second.societycontainer");
        QVERIFY(QDir().mkdir(firstRoot)); QVERIFY(QDir().mkdir(secondRoot));
        QVERIFY(iiSocietyContainer::SocietyDrive::create(firstRoot)); QVERIFY(iiSocietyContainer::SocietyDrive::create(secondRoot));
        const auto first = firstRoot + "/.society-runtime/sync";
        const auto second = secondRoot + "/.society-runtime/sync";
        qputenv("SOCIETY_CONTAINER_PATH", firstRoot.toUtf8());
        SyncOwnership gui(first, true); QVERIFY(gui.start()); QVERIFY(gui.owned());
        qputenv("SOCIETY_CONTAINER_PATH", secondRoot.toUtf8());
        QVERIFY(gui.setDirectory(second)); QCOMPARE(gui.directory(), second); QVERIFY(gui.owned());
        QTest::qWait(600); QVERIFY(gui.owned()); // Stale validation must not release the new root.
        QLockFile oldLease(first + "/owner.lock"); QVERIFY(oldLease.tryLock());
        QVERIFY(!gui.setDirectory({})); QVERIFY(!gui.owned());
        SyncOwnership secondDaemon(second, false); QVERIFY(secondDaemon.start()); QVERIFY(secondDaemon.owned());
    }
    void daemonRecoversTheNetworkLeaseAfterARealGuiProcessCrash() {
        QTemporaryDir directory(SOCIETY_TEST_DIRECTORY "/sync-owner-crash-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(directory.path()));
        qputenv("SOCIETY_CONTAINER_PATH", directory.path().toUtf8());
        const auto runtime = directory.filePath(".society-runtime/sync");
        QProcess gui;
        gui.start(QCoreApplication::applicationFilePath(), {"--hold-foreground", runtime});
        QVERIFY(gui.waitForStarted()); QVERIFY(gui.waitForReadyRead(3000));
        QVERIFY(gui.readAllStandardOutput().contains("owned"));
        SyncOwnership daemon(runtime, false); QVERIFY(daemon.start()); QVERIFY(!daemon.owned());
        gui.kill(); QVERIFY(gui.waitForFinished(3000));
        QTRY_VERIFY_WITH_TIMEOUT(daemon.owned(), 1500);
    }
    void finalExitRetainsTheLeaseUntilTheProcessHasEnded() {
        QTemporaryDir directory(SOCIETY_TEST_DIRECTORY "/sync-owner-exit-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(directory.path()));
        qputenv("SOCIETY_CONTAINER_PATH", directory.path().toUtf8());
        const auto runtime = directory.filePath(".society-runtime/sync");
        QProcess gui;
        gui.start(QCoreApplication::applicationFilePath(), {"--retire-foreground", runtime});
        QVERIFY(gui.waitForStarted()); QVERIFY(gui.waitForReadyRead(3000));
        QVERIFY(gui.readAllStandardOutput().contains("retired"));
        SyncOwnership daemon(runtime, false); QVERIFY(daemon.start());
        QVERIFY(!daemon.owned());
        QVERIFY(gui.state() != QProcess::NotRunning);
        QVERIFY(gui.waitForFinished(7000)); QCOMPARE(gui.exitCode(), 0);
        QTRY_VERIFY_WITH_TIMEOUT(daemon.owned(), 3000);
    }
    void foregroundTakesOverAndDaemonResumesWithoutOverlappingOwners() {
        QTemporaryDir directory(SOCIETY_TEST_DIRECTORY "/sync-owner-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(directory.path()));
        qputenv("SOCIETY_CONTAINER_PATH", directory.path().toUtf8());
        const auto runtime = directory.filePath(".society-runtime/sync"); QVERIFY(directory.isValid());
        SyncOwnership daemon(runtime, false), gui(runtime, true), secondGui(runtime, true);
        bool daemonRunning = false, guiRunning = false;
        connect(&daemon, &SyncOwnership::ownershipChanged, this, [&](bool owned) {
            if (owned) QVERIFY(!guiRunning); daemonRunning = owned;
        });
        connect(&gui, &SyncOwnership::ownershipChanged, this, [&](bool owned) {
            if (owned) QVERIFY(!daemonRunning); guiRunning = owned;
        });
        QVERIFY(daemon.start()); QVERIFY(daemon.owned()); QVERIFY(gui.start());
        QTRY_VERIFY_WITH_TIMEOUT(gui.owned(), 1000); QVERIFY(!daemon.owned());
        QVERIFY(secondGui.start()); QTest::qWait(300); QVERIFY(!secondGui.owned()); secondGui.stop();
        QVERIFY(gui.rememberContainer(directory.path()));
        QCOMPARE(daemon.storedContainer(), directory.path());
        QVERIFY(!daemon.rememberContainer("/not-the-selected-container"));
        gui.stop(); QTRY_VERIFY_WITH_TIMEOUT(daemon.owned(), 1000); QVERIFY(!guiRunning);
        daemon.stop(); QVERIFY(!daemonRunning);
    }
    void invalidStateDoesNotStartANetworkOwner() {
        SyncOwnership missing({}, false); QVERIFY(!missing.start()); QVERIFY(!missing.owned());
        QTemporaryDir directory(SOCIETY_TEST_DIRECTORY "/sync-owner-state-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(directory.path()));
        qputenv("SOCIETY_CONTAINER_PATH", directory.path().toUtf8());
        const auto runtime = directory.filePath(".society-runtime/sync");
        SyncOwnership outside(directory.path() + "-outside", true); QVERIFY(!outside.start());
        QVERIFY(!QFileInfo::exists(directory.path() + "-outside"));
        SyncOwnership gui(runtime, true); QVERIFY(gui.start());
        QFile bad(QDir(runtime).filePath("container.json")); QVERIFY(bad.open(QIODevice::WriteOnly)); bad.write("{\"version\":1,\"container\":\"../outside\"}"); bad.close();
        QVERIFY(gui.storedContainer().isEmpty()); QVERIFY(!gui.rememberContainer("relative"));
    }
};
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (app.arguments().size() == 3 && (app.arguments()[1] == "--hold-foreground"
                                        || app.arguments()[1] == "--retire-foreground")) {
        SyncOwnership owner(app.arguments()[2], true);
        if (app.arguments()[1] == "--hold-foreground")
            QObject::connect(&owner, &SyncOwnership::ownershipChanged, &app, [](bool active) { if (active) QTextStream(stdout) << "owned" << Qt::endl; });
        if (!owner.start()) return 1;
        if (app.arguments()[1] == "--retire-foreground") {
            owner.retainUntilProcessExit();
            if (owner.start() || owner.setDirectory(app.arguments()[2])) return 3;
            QTextStream(stdout) << "retired" << Qt::endl;
            QTimer::singleShot(4000, &app, &QCoreApplication::quit);
        }
        return app.exec();
    }
    SyncOwnershipTests tests; return QTest::qExec(&tests, argc, argv);
}
#include "tst_syncownership.moc"

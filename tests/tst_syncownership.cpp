#include "Society/Services/SyncOwnership.h"
#include <QFile>
#include <QDir>
#include <SharedStorage.h>
#include <QSignalSpy>
#include <QProcess>
#include <QTextStream>
#include <QTemporaryDir>
#include <QTest>

class SyncOwnershipTests : public QObject {
    Q_OBJECT
private slots:
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
    if (app.arguments().size() == 3 && app.arguments()[1] == "--hold-foreground") {
        SyncOwnership owner(app.arguments()[2], true);
        QObject::connect(&owner, &SyncOwnership::ownershipChanged, &app, [](bool active) { if (active) QTextStream(stdout) << "owned" << Qt::endl; });
        if (!owner.start()) return 1;
        return app.exec();
    }
    SyncOwnershipTests tests; return QTest::qExec(&tests, argc, argv);
}
#include "tst_syncownership.moc"

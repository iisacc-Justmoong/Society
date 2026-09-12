#include "App/Services/SyncOwnership.h"
#include <QFile>
#include <QSignalSpy>
#include <QProcess>
#include <QTextStream>
#include <QTemporaryDir>
#include <QTest>

class SyncOwnershipTests : public QObject {
    Q_OBJECT
private slots:
    void daemonRecoversTheNetworkLeaseAfterARealGuiProcessCrash() {
        QTemporaryDir directory(SOCIETY_TEST_DIRECTORY "/sync-owner-crash-XXXXXX");
        QProcess gui;
        gui.start(QCoreApplication::applicationFilePath(), {"--hold-foreground", directory.path()});
        QVERIFY(gui.waitForStarted()); QVERIFY(gui.waitForReadyRead(3000));
        QVERIFY(gui.readAllStandardOutput().contains("owned"));
        SyncOwnership daemon(directory.path(), false); QVERIFY(daemon.start()); QVERIFY(!daemon.owned());
        gui.kill(); QVERIFY(gui.waitForFinished(3000));
        QTRY_VERIFY_WITH_TIMEOUT(daemon.owned(), 1500);
    }
    void foregroundTakesOverAndDaemonResumesWithoutOverlappingOwners() {
        QTemporaryDir directory(SOCIETY_TEST_DIRECTORY "/sync-owner-XXXXXX"); QVERIFY(directory.isValid());
        SyncOwnership daemon(directory.path(), false), gui(directory.path(), true), secondGui(directory.path(), true);
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
        QVERIFY(gui.rememberContainer(directory.filePath("selected-container")));
        QCOMPARE(daemon.storedContainer(), directory.filePath("selected-container"));
        QVERIFY(!daemon.rememberContainer("/not-the-selected-container"));
        gui.stop(); QTRY_VERIFY_WITH_TIMEOUT(daemon.owned(), 1000); QVERIFY(!guiRunning);
        daemon.stop(); QVERIFY(!daemonRunning);
    }
    void invalidStateDoesNotStartANetworkOwner() {
        SyncOwnership missing({}, false); QVERIFY(!missing.start()); QVERIFY(!missing.owned());
        QTemporaryDir directory(SOCIETY_TEST_DIRECTORY "/sync-owner-state-XXXXXX");
        SyncOwnership gui(directory.path(), true); QVERIFY(gui.start());
        QFile bad(directory.filePath("container.json")); QVERIFY(bad.open(QIODevice::WriteOnly)); bad.write("{\"version\":1,\"container\":\"../outside\"}"); bad.close();
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

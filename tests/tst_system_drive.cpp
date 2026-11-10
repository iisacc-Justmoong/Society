#include "Daemon/SystemDriveCoordinator.h"
#include <QJsonObject>
#include <QTest>
#include <functional>

class SystemDriveCoordinatorTests final : public QObject {
    Q_OBJECT
    using Completion = SystemDriveCoordinator::Completion;
    static QString mountPath() {
#ifdef Q_OS_WIN
        return "S:/";
#else
        return "/Volumes/Society";
#endif
    }
    static QJsonObject mounted(const QString &identifier, const QString &source = "/source/Society.societycontainer") {
        return {{"identifier", identifier}, {"sourcePath", source}, {"isMounted", true}, {"enabled", true},
                {"filesystemType", societySystemFilesystemType()}, {"systemPath", mountPath()}};
    }
private slots:
    void mountsAndChecksTheSelectedContainerWithoutAWindow() {
        QList<QString> requests;
        QList<Completion> completions;
        bool accessible = true;
        SystemDriveCoordinator service(
            [&](const QString &path, Completion done) { requests.append(path); completions.append(std::move(done)); },
            [&](const QString &path) { return accessible && path == mountPath(); });
        const QString id = "a43e17fc-a2a1-4b61-8935-79715770e150";
        service.setSelection("/source/Society.societycontainer", id);
        QCOMPARE(requests.size(), 1);
        service.setSelection("/source/Society.societycontainer", id);
        QCOMPARE(requests.size(), 1);
        completions.takeFirst()(mounted(id), {});
        QVERIFY(service.connected());
        QCOMPARE(service.systemPath(), mountPath());
        service.check();
        QCOMPARE(requests.size(), 1);
        accessible = false;
        service.check();
        QCOMPARE(requests.size(), 2);
        QVERIFY(!service.connected());
    }
    void staleAndInvalidResultsNeverBecomeTheCurrentDrive() {
        QList<QString> requests;
        QList<Completion> completions;
        SystemDriveCoordinator service(
            [&](const QString &path, Completion done) { requests.append(path); completions.append(std::move(done)); },
            [](const QString &) { return true; });
        const QString first = "a43e17fc-a2a1-4b61-8935-79715770e150";
        const QString second = "b43e17fc-a2a1-4b61-8935-79715770e150";
        service.setSelection("/source/first.societycontainer", first);
        service.setSelection("/source/second.societycontainer", second);
        completions.takeFirst()(mounted(first, "/source/first.societycontainer"), {});
        QCOMPARE(requests.size(), 2);
        QVERIFY(!service.connected());
        completions.takeFirst()(mounted(first, "/source/second.societycontainer"), {});
        QVERIFY(!service.connected());
        QVERIFY(!service.errorString().isEmpty());
        service.check();
        QCOMPARE(requests.size(), 3);
        completions.takeFirst()(mounted(second, "/source/second.societycontainer"), {});
        QVERIFY(service.connected());
        service.setSelection({}, {});
        QVERIFY(!service.connected());
    }
    void failuresAreVisibleAndCanBeRetried() {
        QList<Completion> completions;
        SystemDriveCoordinator service(
            [&](const QString &, Completion done) { completions.append(std::move(done)); },
            [](const QString &) { return false; });
        const QString id = "a43e17fc-a2a1-4b61-8935-79715770e150";
        service.setSelection("/source/Society.societycontainer", id);
        completions.takeFirst()({}, "The signed host has no mount entitlement.");
        QVERIFY(service.errorString().contains("entitlement"));
        service.check();
        QCOMPARE(completions.size(), 1);
        completions.takeFirst()(mounted(id), {});
        QVERIFY(!service.connected());
        QVERIFY(!service.errorString().isEmpty());
    }
    void theSameUuidFromAnotherSourceIsNotAccepted() {
        QList<Completion> completions;
        SystemDriveCoordinator service(
            [&](const QString &, Completion done) { completions.append(std::move(done)); },
            [](const QString &) { return true; });
        const QString id = "a43e17fc-a2a1-4b61-8935-79715770e150";
        service.setSelection("/source/Society.societycontainer", id);
        completions.takeFirst()(mounted(id, "/source/other.societycontainer"), {});
        QVERIFY(!service.connected());
        QVERIFY(!service.errorString().isEmpty());
        service.check();
        completions.takeFirst()(mounted(id), {});
        QVERIFY(service.connected());
    }
    void anIncompleteReplicaIsNotMounted() {
        int requests = 0;
        SystemDriveCoordinator service(
            [&](const QString &, Completion) { ++requests; }, [](const QString &) { return true; });
        const QString id = "a43e17fc-a2a1-4b61-8935-79715770e150";
        service.setSelection("/source/replica.societycontainer", id, false);
        service.check();
        QCOMPARE(requests, 0);
        QVERIFY(!service.connected());
        QVERIFY(service.errorString().contains("mirror"));
        service.setSelection("/source/replica.societycontainer", id, true);
        QCOMPARE(requests, 1);
    }
};
QTEST_GUILESS_MAIN(SystemDriveCoordinatorTests)
#include "tst_system_drive.moc"

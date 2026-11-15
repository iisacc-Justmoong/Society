#include "Daemon/RecentBackend.h"
#include <QJsonArray>
#include <QSemaphore>
#include <QTest>
#include <atomic>
#include <mutex>

class RecentBackendTest final : public QObject {
    Q_OBJECT
private slots:
    void switchesWithoutPublishingStaleResultsAndPersistsOldDriveActivity() {
        QSemaphore started,release;
        std::atomic_int aCalls=0;
        std::mutex mutex;
        QVector<society::recent::Activity> recorded;
        RecentBackend backend(nullptr,[&](const QString &root,const QVector<society::recent::Activity> &events,int limit) {
            if (root=="/drive-a" && ++aCalls==1) { started.release(); release.acquire(); }
            { std::lock_guard lock(mutex); for (const auto &event:events) recorded.append(event); }
            society::recent::Snapshot result;
            result.items.append({root+"/Files/file",root,100,1});
            if (limit!=500) result.error="Invalid limit";
            return result;
        });
        backend.selectContainer("/drive-a"); backend.activate();
        bool running=false; QTRY_VERIFY_WITH_TIMEOUT(running || (running=started.tryAcquire()),5000);
        backend.record("/drive-a","/drive-a/Files/edited",200);
        backend.selectContainer("/drive-b"); release.release();
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().value("items").toArray().size(),1,5000);
        QCOMPARE(backend.snapshot().value("items").toArray().at(0).toObject().value("name").toString(),QStringLiteral("/drive-b"));
        QTRY_VERIFY_WITH_TIMEOUT(([&] { std::lock_guard lock(mutex); return !recorded.isEmpty(); })(),5000);
        { std::lock_guard lock(mutex); QCOMPARE(recorded.front().path,QStringLiteral("/drive-a/Files/edited")); }
        QCOMPARE(backend.snapshot().value("items").toArray().at(0).toObject().value("name").toString(),QStringLiteral("/drive-b"));
    }
    void failedWritesAreRetainedForRetry() {
        std::atomic_int calls=0,eventsSeen=0;
        RecentBackend backend(nullptr,[&](const QString &,const QVector<society::recent::Activity> &events,int) {
            society::recent::Snapshot result;
            if (!events.isEmpty()) {
                ++eventsSeen;
                if (eventsSeen==1) result.error="Fixture write failure";
                else result.items.append({events.front().path,"retried",events.front().atMs,1});
            }
            ++calls; return result;
        });
        backend.selectContainer("/drive");
        backend.record("/drive","/drive/Files/file",100);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().value("status").toString(),QStringLiteral("Fixture write failure"),5000);
        QCOMPARE(eventsSeen.load(),1);
        backend.refresh();
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().value("items").toArray().size(),1,5000);
        QCOMPARE(eventsSeen.load(),2);
    }
};
QTEST_GUILESS_MAIN(RecentBackendTest)
#include "tst_recent_backend.moc"

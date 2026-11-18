#include "CTestSupport.h"
#include "QtEventWait.h"
#include <QCoreApplication>
#include <QEvent>
#include <QScopeGuard>
#include "Daemon/RecentBackend.h"
#include <QJsonArray>
#include <QSemaphore>
#include <atomic>
#include <mutex>

class RecentBackendTest final : public QObject {
public:
    void switchesWithoutPublishingStaleResultsAndPersistsOldDriveActivity() {
        QSemaphore started,release;
        std::atomic_int aCalls=0;
        std::mutex mutex;
        QVector<society::recent::Activity> recorded;
        RecentBackend backend(nullptr,[&](const QString &root,const QVector<society::recent::Activity> &events,int limit) {
            if (root=="/drive-a" && ++aCalls==1) { started.release(); release.acquire(); }
            { std::lock_guard lock(mutex); for (const auto &event:events) recorded.append(event); }
            society::recent::Snapshot result;
            result.items.append({root+"/Files/file",root,100,1,42});
            if (limit!=500) result.error="Invalid limit";
            return result;
        });
        bool gateReleased = false;
        const auto releaseOnExit = qScopeGuard([&] { if (!gateReleased) release.release(); });
        backend.selectContainer("/drive-a"); backend.activate();
        bool running=false; CTEST_WAIT_VERIFY(running || (running=started.tryAcquire()),5000);
        backend.record("/drive-a","/drive-a/Files/edited",200);
        backend.selectContainer("/drive-b"); gateReleased = true; release.release();
        CTEST_WAIT_COMPARE_TIMEOUT(backend.snapshot().value("items").toArray().size(),1,5000);
        CTEST_COMPARE(backend.snapshot().value("items").toArray().at(0).toObject().value("name").toString(),QStringLiteral("/drive-b"));
        CTEST_COMPARE(backend.snapshot().value("items").toArray().at(0).toObject().value("modifiedMs").toString(),QStringLiteral("42"));
        CTEST_WAIT_VERIFY(([&] { std::lock_guard lock(mutex); return !recorded.isEmpty(); })(),5000);
        { std::lock_guard lock(mutex); CTEST_COMPARE(recorded.front().path,QStringLiteral("/drive-a/Files/edited")); }
        CTEST_COMPARE(backend.snapshot().value("items").toArray().at(0).toObject().value("name").toString(),QStringLiteral("/drive-b"));
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
        CTEST_WAIT_COMPARE_TIMEOUT(backend.snapshot().value("status").toString(),QStringLiteral("Fixture write failure"),5000);
        CTEST_COMPARE(eventsSeen.load(),1);
        backend.refresh();
        CTEST_WAIT_COMPARE_TIMEOUT(backend.snapshot().value("items").toArray().size(),1,5000);
        CTEST_COMPARE(eventsSeen.load(),2);
    }
};
int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    society_ctest::Runner runner(argc == 2 ? argv[1] : "");
    RecentBackendTest test;
    runner.run("switchesWithoutPublishingStaleResultsAndPersistsOldDriveActivity", [&] { const auto cleanup = qScopeGuard([] { QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); }); test.switchesWithoutPublishingStaleResultsAndPersistsOldDriveActivity(); });
    runner.run("failedWritesAreRetainedForRetry", [&] { const auto cleanup = qScopeGuard([] { QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); }); test.failedWritesAreRetainedForRetry(); });
    return runner.result();
}

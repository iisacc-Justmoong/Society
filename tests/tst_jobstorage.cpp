#include "Society/Jobs/JobStorage.h"

#include <Job.h>
#include <TemporaryLease.h>
#include <iiFileProviderNative.h>
#include <QTemporaryDir>
#include <QTest>

class JobStorageTest : public QObject
{
    Q_OBJECT
private slots:
    void reopensAndProtectsAnInterruptedWorker()
    {
        QTemporaryDir directory(SOCIETY_TEST_DIRECTORY "/job-storage-XXXXXX");
        QVERIFY(directory.isValid());
        const auto root = std::filesystem::path(directory.path().toStdU16String());
        SocietyJobStorage storage(root);
        QVERIFY(!storage.open(false));
        {
            auto store = storage.open(true);
            store->create({"job", "model-import", "local", "drive", "revision", "node", true},
                          {{"one", "source-a", "", "", "First"},
                           {"two", "source-b", "", "", "Second"}});
            auto lease = storage.tryLease("job", true);
            QVERIFY(lease && lease->locked());
            auto contender = storage.tryLease("job", false);
            QVERIFY(contender && !contender->locked());
            store->begin("job", "node", "copying");
            const auto first = store->claim("job", "one");
            store->succeed("job", "one", first.token, "artifact-a");
            const auto second = store->claim("job", "two");
            store->checkpoint("job", "two", second.token, "published:artifact-b");
        }
        auto lease = storage.tryLease("job", false);
        QVERIFY(lease && lease->locked());
        auto reopened = storage.open(false);
        reopened->recoverInterrupted("job");
        const auto job = reopened->job("job");
        QVERIFY(job);
        QCOMPARE(job->succeeded, std::uint64_t(1));
        QCOMPARE(job->unknown, std::uint64_t(1));
        QCOMPARE(reopened->item("job", "two")->checkpoint, std::string("published:artifact-b"));
        QCOMPARE(reopened->retryFailed("job"), std::uint64_t(0));
    }
};

QTEST_GUILESS_MAIN(JobStorageTest)
#include "tst_jobstorage.moc"

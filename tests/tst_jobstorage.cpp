#include "Society/Jobs/JobStorage.h"

#include <Job.h>
#include <iiFileProvider>
#include <QTemporaryDir>
#include "CTestSupport.h"
#include <QCoreApplication>
#include <QEvent>

class JobStorageTest
{
public:
    void reopensAndProtectsAnInterruptedWorker()
    {
        QTemporaryDir directory(SOCIETY_TEST_DIRECTORY "/job-storage-XXXXXX");
        CTEST_CHECK(directory.isValid());
        const auto root = std::filesystem::path(directory.path().toStdU16String());
        SocietyJobStorage storage(root);
        CTEST_CHECK(!storage.open(false));
        {
            auto store = storage.open(true);
            store->create({"job", "model-import", "local", "drive", "revision", "node", true},
                          {{"one", "source-a", "", "", "First"},
                           {"two", "source-b", "", "", "Second"}});
            auto lease = storage.tryLease("job", true);
            CTEST_CHECK(lease && lease->locked());
            auto contender = storage.tryLease("job", false);
            CTEST_CHECK(contender && !contender->locked());
            store->begin("job", "node", "copying");
            const auto first = store->claim("job", "one");
            store->succeed("job", "one", first.token, "artifact-a");
            const auto second = store->claim("job", "two");
            store->checkpoint("job", "two", second.token, "published:artifact-b");
        }
        auto lease = storage.tryLease("job", false);
        CTEST_CHECK(lease && lease->locked());
        auto reopened = storage.open(false);
        reopened->recoverInterrupted("job");
        const auto job = reopened->job("job");
        CTEST_CHECK(job);
        CTEST_COMPARE(job->succeeded, std::uint64_t(1));
        CTEST_COMPARE(job->unknown, std::uint64_t(1));
        CTEST_COMPARE(reopened->item("job", "two")->checkpoint, std::string("published:artifact-b"));
        CTEST_COMPARE(reopened->retryFailed("job"), std::uint64_t(0));
    }
};

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    society_ctest::Runner runner(argc == 2 ? argv[1] : "");
    JobStorageTest tests;
    struct FlushDeferredDeletes {
        ~FlushDeferredDeletes() { QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); }
    };
    runner.run("reopensAndProtectsAnInterruptedWorker", [&] { FlushDeferredDeletes cleanup; tests.reopensAndProtectsAnInterruptedWorker(); });
    return runner.result();
}

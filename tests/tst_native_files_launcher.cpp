#include "Society/Native/NativeFilesLauncher.h"
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include "CTestSupport.h"
#include <QCoreApplication>
#include <QEvent>

class NativeFilesLauncherTests final {
public:
    void startsOnlyTheExpectedExecutable() {
        QTemporaryDir root(QDir::cleanPath(QStringLiteral(SOCIETY_TEST_DIRECTORY)+"/native-launcher-XXXXXX")); CTEST_CHECK(root.isValid());
        const auto program=root.filePath("SocietyNativeFiles"); QString error, seen; int calls=0;
        auto start=[&](const QString &path){++calls; seen=path; return true;};
        CTEST_CHECK(!society::native::launchFiles(program,start,&error)); CTEST_COMPARE(calls,0); CTEST_CHECK(!error.isEmpty());
        QFile file(program); CTEST_CHECK(file.open(QIODevice::WriteOnly)); file.write("fixture"); file.close();
        CTEST_CHECK(file.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner|QFileDevice::ExeOwner));
        CTEST_CHECK_MESSAGE(society::native::launchFiles(program,start,&error),qPrintable(error)); CTEST_COMPARE(calls,1); CTEST_COMPARE(seen,program); CTEST_CHECK(error.isEmpty());
        CTEST_CHECK(!society::native::launchFiles(program,[](const QString &){return false;},&error)); CTEST_CHECK(!error.isEmpty());
    }
};
int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    society_ctest::Runner runner(argc == 2 ? argv[1] : "");
    NativeFilesLauncherTests tests;
    struct FlushDeferredDeletes {
        ~FlushDeferredDeletes() { QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); }
    };
    runner.run("startsOnlyTheExpectedExecutable", [&] { FlushDeferredDeletes cleanup; tests.startsOnlyTheExpectedExecutable(); });
    return runner.result();
}

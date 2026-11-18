#include "Society/ContainerFileOpenHandler.h"
#include <QCoreApplication>
#include "CTestSupport.h"
#include <QCoreApplication>
#include <QEvent>

class ContainerFileOpenTests final {
public:
    void nativeOpenWaitsForTheControllerAndThenOpensOnePackage() {
        QObject receiver; ContainerFileOpenHandler handler;
        receiver.installEventFilter(&handler);
        const auto package = QUrl::fromLocalFile("/Volumes/Storage/Selected.societycontainer");
        QFileOpenEvent opened(package);
        CTEST_CHECK(QCoreApplication::sendEvent(&receiver, &opened));
        QList<QUrl> calls;
        bool ready = false;
        handler.setOpener([&](const QUrl &url) { if (!ready) return false; calls.append(url); return true; });
        CTEST_CHECK(calls.isEmpty()); ready = true; handler.flush();
        CTEST_COMPARE(calls, QList<QUrl>{package}); handler.flush(); CTEST_COMPARE(calls.size(), 1);
    }
    void accountLinkWaitsForWindowAndRejectsUnexpectedParameters() {
        QObject receiver; ContainerFileOpenHandler handler; receiver.installEventFilter(&handler);
        QFileOpenEvent opened(QUrl("society://account"));
        CTEST_CHECK(QCoreApplication::sendEvent(&receiver, &opened));
        int calls=0;handler.setAccountOpener([&]{++calls;});CTEST_COMPARE(calls,1);
        QFileOpenEvent again(QUrl("society://account"));CTEST_CHECK(QCoreApplication::sendEvent(&receiver,&again));CTEST_COMPARE(calls,2);
        QFileOpenEvent bad(QUrl("society://account?next=https://foreign.invalid"));
        QCoreApplication::sendEvent(&receiver,&bad);CTEST_COMPARE(calls,2);
    }
    void ignoresOrdinaryFoldersAndRemoteUrls() {
        QObject receiver; ContainerFileOpenHandler handler; receiver.installEventFilter(&handler);
        QList<QUrl> calls; handler.setOpener([&](const QUrl &url) { calls.append(url); return true; });
        QFileOpenEvent directory(QUrl::fromLocalFile("/Volumes/Storage/Society"));
        QCoreApplication::sendEvent(&receiver, &directory);
        QFileOpenEvent remote(QUrl("https://example.test/Society.societycontainer"));
        QCoreApplication::sendEvent(&receiver, &remote);
        CTEST_CHECK(calls.isEmpty());
    }
};
int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    society_ctest::Runner runner(argc == 2 ? argv[1] : "");
    ContainerFileOpenTests tests;
    struct FlushDeferredDeletes {
        ~FlushDeferredDeletes() { QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); }
    };
    runner.run("nativeOpenWaitsForTheControllerAndThenOpensOnePackage", [&] { FlushDeferredDeletes cleanup; tests.nativeOpenWaitsForTheControllerAndThenOpensOnePackage(); });
    runner.run("accountLinkWaitsForWindowAndRejectsUnexpectedParameters", [&] { FlushDeferredDeletes cleanup; tests.accountLinkWaitsForWindowAndRejectsUnexpectedParameters(); });
    runner.run("ignoresOrdinaryFoldersAndRemoteUrls", [&] { FlushDeferredDeletes cleanup; tests.ignoresOrdinaryFoldersAndRemoteUrls(); });
    return runner.result();
}

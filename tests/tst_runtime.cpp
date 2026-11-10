#include "Society/Services/SocietyRuntime.h"
#include <QDir>
#include <SharedStorage.h>
#include <QFile>
#include <QSignalSpy>
#include <QElapsedTimer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QScopeGuard>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QTest>
#include <atomic>

namespace {
struct SlowInboxState {
    QSemaphore entered, release;
    std::atomic<int> opens{0}, destroyed{0};
};
class SlowInboxStore final : public SocietyInbox::Store {
public:
    explicit SlowInboxStore(std::shared_ptr<SlowInboxState> state) : state_(std::move(state)) {}
    ~SlowInboxStore() override { ++state_->destroyed; }
    bool open(const QString &, QString *) override {
        if (++state_->opens == 1) { state_->entered.release(); state_->release.acquire(); }
        return true;
    }
    void close() override {}
    qint64 acknowledged(const QString &) override { return 0; }
    QVariantList readAfter(qint64, int) override { return {}; }
    QVariantMap daemonSnapshot() override { return {}; }
    bool acknowledge(const QString &, qint64) override { return true; }
    QString errorString() const override { return {}; }
private:
    std::shared_ptr<SlowInboxState> state_;
};
}

class RuntimeTests : public QObject {
    Q_OBJECT
private slots:
    void slowRuntimeValidationDoesNotBlockStartupOrDestruction() {
        QTemporaryDir root(SOCIETY_TEST_DIRECTORY "/deferred-runtime-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(root.path()));
        qputenv("SOCIETY_CONTAINER_PATH", root.path().toUtf8());
        auto entered = std::make_shared<QSemaphore>();
        auto release = std::make_shared<QSemaphore>();
        auto finished = std::make_shared<std::atomic_bool>(false);
        const auto unblock = qScopeGuard([release] { release->release(); });
        auto runtime = std::make_unique<SocietyRuntime>(false, iiSocietyHelper::ObservationOptions{}, nullptr,
            [entered, release, finished](const QString &) {
                entered->release(); release->acquire();
                *finished = true;
                return std::pair{QString(), false};
            });
        // Constructing presence owners must not synchronously discover the
        // selected drive. Main binds the file system when the drive is ready.
        QVERIFY(!runtime->helper()->fileSystem()->isAvailable());
        QElapsedTimer elapsed; elapsed.start();
        runtime->setApplicationState(Qt::ApplicationActive);
        QVERIFY2(elapsed.elapsed() < 500, "Runtime validation blocked startup");
        QTRY_VERIFY_WITH_TIMEOUT(entered->available() > 0, 3000);
        elapsed.restart();
        runtime.reset();
        QVERIFY2(elapsed.elapsed() < 500, "Runtime destruction waited for validation");
        release->release();
        QTRY_VERIFY_WITH_TIMEOUT(finished->load(), 3000);
        QCoreApplication::processEvents();
    }
    void slowHelperLifecycleDoesNotBlockDesktopStartupOrExit() {
        QTemporaryDir root(SOCIETY_TEST_DIRECTORY "/runtime-helper-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(root.path()));
        qputenv("SOCIETY_CONTAINER_PATH", root.path().toUtf8());
        const auto directory = root.filePath(".society-runtime/helpers/v1");
        auto startEntered = std::make_shared<QSemaphore>(), startRelease = std::make_shared<QSemaphore>();
        auto stopEntered = std::make_shared<QSemaphore>(), stopRelease = std::make_shared<QSemaphore>();
        const auto unblock = qScopeGuard([=] { startRelease->release(); stopRelease->release(); });
        iiSocietyHelper::HelperLifecycleHooks hooks;
        hooks.beforeStartIo = [=] { startEntered->release(); startRelease->acquire(); };
        hooks.beforeStopIo = [=] { stopEntered->release(); stopRelease->acquire(); };
        auto runtime = std::make_unique<SocietyRuntime>(false, iiSocietyHelper::ObservationOptions{directory, 50, 400},
            nullptr, [directory](const QString &) { return std::pair{directory, true}; }, hooks);
        QElapsedTimer elapsed; elapsed.start();
        runtime->setApplicationState(Qt::ApplicationActive);
        QVERIFY2(elapsed.elapsed() < 500, "Starting the helper blocked the UI");
        QTRY_VERIFY_WITH_TIMEOUT(startEntered->available() > 0, 3000);
        bool responsive = false; QTimer::singleShot(0, runtime.get(), [&] { responsive = true; });
        QTRY_VERIFY(responsive);
        startRelease->release();
        QTRY_VERIFY_WITH_TIMEOUT(runtime->helper()->isRunning(), 5000);
        const auto presence = directory + "/" + runtime->helper()->self().instanceId + ".json";
        elapsed.restart(); runtime->stop();
        QVERIFY2(elapsed.elapsed() < 500, "Stopping the helper blocked the UI");
        QTRY_VERIFY_WITH_TIMEOUT(stopEntered->available() > 0, 3000);
        elapsed.restart(); runtime.reset();
        QVERIFY2(elapsed.elapsed() < 500, "Destruction waited for the helper worker");
        stopRelease->release();
        QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(presence), 5000);
    }
    void slowInboxOpenDoesNotBlockStopRestartOrDestruction() {
        auto state = std::make_shared<SlowInboxState>();
        const auto unblock = qScopeGuard([state] { state->release.release(); });
        auto inbox = std::make_unique<SocietyInbox>([state] { return std::make_unique<SlowInboxStore>(state); });
        QElapsedTimer elapsed; elapsed.start();
        QVERIFY(inbox->start(QStringLiteral("first")));
        QVERIFY2(elapsed.elapsed() < 500, "Opening the inbox blocked its caller");
        QTRY_VERIFY_WITH_TIMEOUT(state->entered.available() > 0, 3000);
        elapsed.restart();
        QVERIFY(inbox->readAfter(0).isEmpty());
        QVERIFY(!inbox->acknowledge(0));
        QVERIFY2(elapsed.elapsed() < 500, "Commands waited for an unopened inbox");
        elapsed.restart();
        inbox->stop();
        QVERIFY2(elapsed.elapsed() < 500, "Stopping the inbox waited for storage");
        QVERIFY(!inbox->connected());
        QVERIFY(inbox->start(QStringLiteral("second")));
        state->release.release();
        QTRY_VERIFY_WITH_TIMEOUT(inbox->connected() && state->opens == 2, 3000);
        inbox.reset();
        QTRY_COMPARE_WITH_TIMEOUT(state->destroyed.load(), 1, 3000);
    }
    void blockedInboxDoesNotHoldProcessExit() {
        if (qEnvironmentVariableIntValue("SOCIETY_INBOX_EXIT_CHILD") == 1) {
            auto state = std::make_shared<SlowInboxState>();
            auto inbox = std::make_unique<SocietyInbox>([state] { return std::make_unique<SlowInboxStore>(state); });
            QVERIFY(inbox->start(QStringLiteral("blocked")));
            QTRY_VERIFY_WITH_TIMEOUT(state->entered.available() > 0, 3000);
            inbox.reset();
            return; // The simulated I/O remains blocked through process exit.
        }
        QProcess child;
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("SOCIETY_INBOX_EXIT_CHILD"), QStringLiteral("1"));
        child.setProcessEnvironment(environment);
        child.start(QCoreApplication::applicationFilePath(), {QStringLiteral("blockedInboxDoesNotHoldProcessExit")});
        QVERIFY(child.waitForStarted(3000));
        if (!child.waitForFinished(5000)) {
            child.kill(); child.waitForFinished();
            QFAIL("A blocked inbox operation kept the process alive during teardown");
        }
        QCOMPARE(child.exitStatus(), QProcess::NormalExit);
        QVERIFY2(child.exitCode() == 0, child.readAllStandardError().constData());
    }
    void destroyingInboxDuringSlowOpenDoesNotWaitOrDeliverStaleResults() {
        auto state = std::make_shared<SlowInboxState>();
        const auto unblock = qScopeGuard([state] { state->release.release(); });
        auto inbox = std::make_unique<SocietyInbox>([state] { return std::make_unique<SlowInboxStore>(state); });
        QVERIFY(inbox->start(QStringLiteral("first")));
        QTRY_VERIFY_WITH_TIMEOUT(state->entered.available() > 0, 3000);
        QElapsedTimer elapsed; elapsed.start();
        inbox.reset();
        QVERIFY2(elapsed.elapsed() < 500, "Destroying the inbox waited for storage");
        state->release.release();
        QTRY_COMPARE_WITH_TIMEOUT(state->destroyed.load(), 1, 3000);
        QCoreApplication::processEvents();
    }
    void staleBlockedValidationCannotStartThePreviousContainer() {
        QTemporaryDir parent(SOCIETY_TEST_DIRECTORY "/runtime-validation-epoch-XXXXXX");
        const auto a = parent.filePath("a.societycontainer"), b = parent.filePath("b.societycontainer");
        QVERIFY(QDir().mkdir(a)); QVERIFY(QDir().mkdir(b));
        QVERIFY(iiSocietyContainer::SocietyDrive::create(a));
        QVERIFY(iiSocietyContainer::SocietyDrive::create(b));
        qputenv("SOCIETY_CONTAINER_PATH", b.toUtf8());
        auto entered = std::make_shared<QSemaphore>(), release = std::make_shared<QSemaphore>();
        auto calls = std::make_shared<std::atomic<int>>(0);
        const auto unblock = qScopeGuard([release] { release->release(); });
        SocietyRuntime runtime(false, {}, nullptr, [=](const QString &) {
            if (++*calls == 1) { entered->release(); release->acquire(); return std::pair{a + "/.society-runtime/helpers/v1", true}; }
            return std::pair{b + "/.society-runtime/helpers/v1", true};
        });
        QSignalSpy changes(runtime.helper(), &iiSocietyHelper::Helper::runningChanged);
        runtime.setApplicationState(Qt::ApplicationActive);
        QTRY_VERIFY_WITH_TIMEOUT(entered->available() > 0, 3000);
        runtime.stop();
        runtime.setApplicationState(Qt::ApplicationActive);
        release->release();
        QTRY_VERIFY_WITH_TIMEOUT(runtime.helper()->isRunning() && runtime.helper()->directory().startsWith(b), 5000);
        QCOMPARE(changes.count(), 1);
    }
    void changingDefaultContainerRejectsOldValidationAndRebindsTheHelper() {
        QTemporaryDir parent(SOCIETY_TEST_DIRECTORY "/runtime-swap-XXXXXX"); QVERIFY(parent.isValid());
        const auto a=parent.filePath("a.societycontainer"), b=parent.filePath("b.societycontainer");
        QVERIFY(QDir().mkdir(a)); QVERIFY(QDir().mkdir(b));
        QVERIFY(iiSocietyContainer::SocietyDrive::create(a)); QVERIFY(iiSocietyContainer::SocietyDrive::create(b));
        qputenv("SOCIETY_CONTAINER_PATH",a.toUtf8());
        SocietyRuntime runtime(false); runtime.setApplicationState(Qt::ApplicationActive);
        QTRY_VERIFY_WITH_TIMEOUT(runtime.helper()->isRunning() && runtime.helper()->directory().startsWith(a), 4000);
        runtime.stop();
        qputenv("SOCIETY_CONTAINER_PATH",b.toUtf8());
        runtime.setApplicationState(Qt::ApplicationActive);
        QTRY_VERIFY_WITH_TIMEOUT(runtime.helper()->isRunning() && runtime.helper()->directory().startsWith(b),4000);
        QTest::qWait(1200);
        QVERIFY(runtime.helper()->isRunning()); QVERIFY(runtime.helper()->directory().startsWith(b));
    }
    void outsideRuntimeOverridesNeverCreateAppState() {
        QTemporaryDir root(SOCIETY_TEST_DIRECTORY "/runtime-scope-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(root.path()));
        qputenv("SOCIETY_CONTAINER_PATH", root.path().toUtf8());
        const auto outside = root.path() + "-outside";
        SocietyRuntime runtime(true, {outside, 50, 400});
        runtime.setApplicationState(Qt::ApplicationActive);
        QVERIFY(!runtime.helper()->isRunning()); QVERIFY(!runtime.inbox()->connected());
        SocietyDaemonService daemon; QVERIFY(!daemon.start(outside, 50, 400));
        QVERIFY(!QFileInfo::exists(outside));
    }
    void suspendedReceiverReleasesLockAndReplaysUnacknowledgedData()
    {
        QTemporaryDir root(SOCIETY_TEST_DIRECTORY "/runtime-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(root.path()));
        qputenv("SOCIETY_CONTAINER_PATH", root.path().toUtf8());
        const auto directory = root.filePath(".society-runtime/helpers/v1");
        SocietyRuntime runtime(true, {directory, 50, 400});
        runtime.setApplicationState(Qt::ApplicationActive);
        QTRY_VERIFY_WITH_TIMEOUT(runtime.helper()->isRunning(), 4000);
        QTRY_VERIFY_WITH_TIMEOUT(runtime.inbox()->connected(), 4000);
        iiSocietyHelper::Helper sender;
        QVERIFY(sender.start({"com.iisacc.dreamscapes", "Dreamscapes", "1"}, {directory, 50, 400}));
        const auto before = sender.sendData("test.data", {{"phase", "foreground"}});
        auto received = [&](const QString &id) {
            for (const auto &message : runtime.inbox()->messages())
                if (message.toMap().value("id") == id) return true;
            return false;
        };
        QTRY_VERIFY(received(before));
        QVERIFY(runtime.inbox()->acknowledge(runtime.inbox()->lastSequence()));
        runtime.setApplicationState(Qt::ApplicationSuspended);
        QVERIFY(!runtime.helper()->isRunning());
        QVERIFY(!runtime.inbox()->connected());
        SocietyDaemonService otherReceiver;
        QVERIFY(otherReceiver.start(directory, 50, 400)); // No lock held by a suspended app.
        otherReceiver.stop();
        const auto offline = sender.sendData("test.data", {{"phase", "suspended"}});
        QVERIFY(!offline.isEmpty());
        runtime.setApplicationState(Qt::ApplicationActive);
        QTRY_VERIFY(received(offline));
        QVERIFY(!received(before)); // Acknowledged data does not replay.
        runtime.setApplicationState(Qt::ApplicationSuspended);
        runtime.setApplicationState(Qt::ApplicationActive);
        QTRY_VERIFY(received(offline)); // Unacknowledged data does replay.
        int copies = 0;
        for (const auto &message : runtime.inbox()->readAfter(0))
            if (message.toMap().value("id") == offline) ++copies;
        QCOMPARE(copies, 1);
    }

    void startupFailureRecoversWithoutRelaunch()
    {
        QTemporaryDir root(SOCIETY_TEST_DIRECTORY "/runtime-retry-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(root.path()));
        qputenv("SOCIETY_CONTAINER_PATH", root.path().toUtf8());
        const auto directory = root.filePath(".society-runtime/helpers/v1");
        QVERIFY(QDir().mkpath(directory));
        const auto path = directory + "/unavailable";
        QFile blocker(path);
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        blocker.close();
        SocietyRuntime runtime(true, {path, 50, 400});
        runtime.setApplicationState(Qt::ApplicationActive);
        QVERIFY(!runtime.helper()->isRunning());
        QVERIFY(!runtime.inbox()->connected());
        QVERIFY(blocker.remove());
        QTRY_VERIFY_WITH_TIMEOUT(runtime.helper()->isRunning() && runtime.inbox()->connected(), 4000);
        const auto instance = runtime.helper()->self().instanceId;
        runtime.setApplicationState(Qt::ApplicationInactive); // Picker / system sheet is not suspension.
        QCOMPARE(runtime.helper()->self().instanceId, instance);
        QVERIFY(runtime.inbox()->connected());
    }

    void desktopWindowDeactivationKeepsReceiving()
    {
        QTemporaryDir root(SOCIETY_TEST_DIRECTORY "/runtime-desktop-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(root.path()));
        qputenv("SOCIETY_CONTAINER_PATH", root.path().toUtf8());
        const auto directory = root.filePath(".society-runtime/helpers/v1");
        SocietyDaemonService daemon;
        QVERIFY(daemon.start(directory, 50, 400));
        SocietyRuntime runtime(false, {directory, 50, 400});
        runtime.setApplicationState(Qt::ApplicationActive);
        QTRY_VERIFY_WITH_TIMEOUT(runtime.helper()->isRunning(), 4000);
        QTRY_VERIFY_WITH_TIMEOUT(runtime.inbox()->connected(), 4000);
        const auto instance = runtime.helper()->self().instanceId;
        runtime.setApplicationState(Qt::ApplicationHidden);
        QCOMPARE(runtime.helper()->self().instanceId, instance);
        QVERIFY(runtime.helper()->isRunning());
        QVERIFY(runtime.inbox()->connected());
    }
};
QTEST_GUILESS_MAIN(RuntimeTests)
#include "tst_runtime.moc"

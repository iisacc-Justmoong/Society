#include "Society/Tools/MergeModelCatalog.h"
#include <QFile>
#include <QFutureWatcher>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QtTest>
#include <memory>

namespace {
// Hold the real shared pool before the scan starts. Cleanup also runs on an
// assertion failure, so the regression cannot leave application workers stuck.
class PoolGate {
public:
    PoolGate() : pool(QThreadPool::globalInstance()), previous(pool->maxThreadCount()) {
        pool->setMaxThreadCount(1);
        pool->start([this] { entered.release(); release.acquire(); });
        entered.acquire();
    }
    ~PoolGate() { open(); pool->waitForDone(); pool->setMaxThreadCount(previous); }
    void open() { if (!opened) { opened = true; release.release(); } }
private:
    QThreadPool *pool;
    int previous;
    QSemaphore entered, release;
    bool opened = false;
};
bool model(const QString &directory, const QString &name) {
    QFile file(directory + '/' + name);
    return file.open(QIODevice::WriteOnly) && file.write("model fixture") > 0;
}
}

class ModelCatalogTests : public QObject {
    Q_OBJECT
private slots:
    void repeatedRefreshBoundsPendingScansAndPublishesOnlyLatestDirectory() {
        QTemporaryDir first(SOCIETY_TEST_DIRECTORY "/catalog-first-XXXXXX");
        QTemporaryDir latest(SOCIETY_TEST_DIRECTORY "/catalog-latest-XXXXXX");
        QVERIFY(first.isValid()); QVERIFY(latest.isValid());
        QVERIFY(model(first.path(), "old.safetensors"));
        QVERIFY(model(latest.path(), "latest.safetensors"));
        PoolGate gate;
        MergeModelCatalog catalog;
        QSignalSpy changed(&catalog, &MergeModelCatalog::modelsChanged);
        catalog.setDirectory(first.path());
        for (int i = 0; i < 100; ++i) catalog.refresh();
        catalog.setDirectory(latest.path());
        for (int i = 0; i < 100; ++i) catalog.refresh();
        // Each admitted asynchronous scan owns a watcher. A blocked pool must
        // retain one scan, independent of the number of refresh intents.
        QCOMPARE(catalog.findChildren<QFutureWatcherBase *>().size(), 1);
        QVERIFY(catalog.loading());
        changed.clear(); gate.open();
        QTRY_VERIFY_WITH_TIMEOUT(!catalog.loading(), 15000);
        QCOMPARE(catalog.models().size(), 1);
        QCOMPARE(catalog.models().first().toMap().value("name").toString(), QString("latest.safetensors"));
        QCOMPARE(changed.count(), 1);
        QVERIFY(catalog.contains(latest.filePath("latest.safetensors"), true));
        QVERIFY(!catalog.contains(first.filePath("old.safetensors")));
    }
    void clearingOrDestroyingTheOwnerDoesNotWaitForThePool() {
        QTemporaryDir directory(SOCIETY_TEST_DIRECTORY "/catalog-close-XXXXXX");
        QVERIFY(directory.isValid()); QVERIFY(model(directory.path(), "model.safetensors"));
        PoolGate gate;
        auto catalog = std::make_unique<MergeModelCatalog>();
        catalog->setDirectory(directory.path());
        catalog->setDirectory({});
        QVERIFY(!catalog->loading()); QVERIFY(catalog->models().isEmpty());
        QElapsedTimer timer; timer.start(); catalog.reset();
        QVERIFY(timer.elapsed() < 250);
        gate.open();
    }
    void clearingFromBusyNotificationCannotStartAnEmptyDirectoryScan() {
        QTemporaryDir directory(SOCIETY_TEST_DIRECTORY "/catalog-reentrant-XXXXXX");
        QVERIFY(directory.isValid());
        PoolGate gate;
        MergeModelCatalog catalog;
        connect(&catalog, &MergeModelCatalog::loadingChanged, &catalog, [&] {
            if (catalog.loading()) catalog.setDirectory({});
        });
        catalog.setDirectory(directory.path());
        QVERIFY(catalog.directory().isEmpty()); QVERIFY(!catalog.loading());
        QCOMPARE(catalog.findChildren<QFutureWatcherBase *>().size(), 0);
    }
    void notificationsMayDestroyTheirOwner() {
        QTemporaryDir directory(SOCIETY_TEST_DIRECTORY "/catalog-notify-XXXXXX");
        QVERIFY(directory.isValid()); QVERIFY(model(directory.path(), "model.safetensors"));
        auto catalog = std::make_unique<MergeModelCatalog>();
        connect(catalog.get(), &MergeModelCatalog::loadingChanged, this, [&] {
            if (catalog && catalog->loading()) catalog.reset();
        });
        catalog->setDirectory(directory.path());
        QVERIFY(!catalog);
        catalog = std::make_unique<MergeModelCatalog>();
        connect(catalog.get(), &MergeModelCatalog::modelsChanged, this, [&] {
            if (catalog && !catalog->models().isEmpty()) catalog.reset();
        });
        catalog->setDirectory(directory.path());
        QTRY_VERIFY_WITH_TIMEOUT(!catalog, 15000);
    }
};
QTEST_GUILESS_MAIN(ModelCatalogTests)
#include "tst_modelcatalog.moc"

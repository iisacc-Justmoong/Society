#include "Society/ContainerFileOpenHandler.h"
#include <QCoreApplication>
#include <QTest>

class ContainerFileOpenTests final : public QObject {
    Q_OBJECT
private slots:
    void nativeOpenWaitsForTheControllerAndThenOpensOnePackage() {
        QObject receiver; ContainerFileOpenHandler handler;
        receiver.installEventFilter(&handler);
        const auto package = QUrl::fromLocalFile("/Volumes/Storage/Selected.societycontainer");
        QFileOpenEvent opened(package);
        QVERIFY(QCoreApplication::sendEvent(&receiver, &opened));
        QList<QUrl> calls;
        bool ready = false;
        handler.setOpener([&](const QUrl &url) { if (!ready) return false; calls.append(url); return true; });
        QVERIFY(calls.isEmpty()); ready = true; handler.flush();
        QCOMPARE(calls, QList<QUrl>{package}); handler.flush(); QCOMPARE(calls.size(), 1);
    }
    void ignoresOrdinaryFoldersAndRemoteUrls() {
        QObject receiver; ContainerFileOpenHandler handler; receiver.installEventFilter(&handler);
        QList<QUrl> calls; handler.setOpener([&](const QUrl &url) { calls.append(url); return true; });
        QFileOpenEvent directory(QUrl::fromLocalFile("/Volumes/Storage/Society"));
        QCoreApplication::sendEvent(&receiver, &directory);
        QFileOpenEvent remote(QUrl("https://example.test/Society.societycontainer"));
        QCoreApplication::sendEvent(&receiver, &remote);
        QVERIFY(calls.isEmpty());
    }
};
QTEST_GUILESS_MAIN(ContainerFileOpenTests)
#include "tst_containerfileopen.moc"

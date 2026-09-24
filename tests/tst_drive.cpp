#include "App/Drive/DriveController.h"
#include "AccountServer.h"
#include <QNetworkProxy>
#include "App/Drive/StorageNavigation.h"
#include "App/Dashboard/DashboardFiles.h"
#include "App/Dashboard/DashboardCalendar.h"
#include "App/Tools/ModelMergeController.h"
#include "App/Tools/MergeModelCatalog.h"
#include "App/Models/StorageModels.h"
#include "App/Files/DirectoryLocation.h"
#include "App/Files/ModelImporter.h"
#include <StorageMap.h>
#include "backend/runtime/appbootstrap.h"

#include <QDir>
#include <QAbstractItemModel>
#include <QFile>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QMap>
#include <QGuiApplication>
#include <QAccessible>
#include <QImage>
#include <QJSValue>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlError>
#include <QQmlListReference>
#include <QQmlProperty>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QPointer>
#include <QFutureWatcher>
#include <QScopeGuard>
#include <QSemaphore>
#include <QThreadPool>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>
#include <QtEndian>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QWheelEvent>
#include <QStyleHints>
#include "App/Network/NetworkDriveController.h"
#include "MobileNetworkDevice.h"
#include "App/Network/DevicePairing.h"
#include "App/Network/PairingQr.h"
#include "App/Network/QrScanner.h"
#include <SharedStorage.h>
#include <DiskImage.h>
#include <QStorageInfo>
#include <FilesView.h>

static QQuickItem *visualItem(QQuickItem *parent, const QString &name)
{
    if (parent->objectName() == name) return parent;
    for (auto *child : parent->childItems())
        if (auto *found = visualItem(child, name)) return found;
    return nullptr;
}

// Calendar content makes the dashboard taller; exercise controls after real scrolling.
static void revealDashboardItem(QQuickItem *root, QQuickItem *item)
{
    auto *scroll = visualItem(root, "dashboardScroll");
    if (!scroll || !item) return;
    auto *flick = qvariant_cast<QQuickItem *>(scroll->property("contentItem"));
    if (!flick) return;
    const auto y = item->mapToItem(flick, QPointF()).y() + flick->property("contentY").toReal();
    const auto maximum = std::max(0.0, flick->property("contentHeight").toReal() - flick->height());
    flick->setProperty("contentY", std::clamp(y - 16.0, 0.0, maximum));
    QTest::qWait(60);
}

static bool writeCatalogModel(const QString &path, const QJsonObject &metadata)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    const auto header = QJsonDocument(QJsonObject{{"__metadata__", metadata},
        {"weight", QJsonObject{{"dtype", "F16"}, {"shape", QJsonArray{1}}, {"data_offsets", QJsonArray{0, 2}}}}})
        .toJson(QJsonDocument::Compact);
    QByteArray bytes(8, '\0'); qToLittleEndian(quint64(header.size()), bytes.data());
    bytes += header; bytes += QByteArray(2, '\0');
    QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

class SocietyDriveTest final : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        qmlRegisterType<DirectoryLocation>("Society", 1, 0, "DirectoryLocation");
        qmlRegisterType<FileActions>("Society", 1, 0, "FileActions");
        qmlRegisterType<StorageDirectoryModel>("Society", 1, 0, "StorageDirectoryModel");
        qmlRegisterType<DriveController>("Society", 1, 0, "DriveController");
        qmlRegisterType<StorageNavigation>("Society", 1, 0, "StorageNavigation");
        qmlRegisterType<DashboardCalendar>("Society", 1, 0, "DashboardCalendar");
        qmlRegisterType<DashboardFiles>("Society", 1, 0, "DashboardFiles");
        qmlRegisterType<ModelMergeController>("Society", 1, 0, "ModelMergeController");
        qmlRegisterType<MergeModelCatalog>("Society", 1, 0, "MergeModelCatalog");
        qmlRegisterType<StorageModels>("Society", 1, 0, "StorageModels");
        qmlRegisterType<ModelImporter>("Society", 1, 0, "ModelImporter");
        qmlRegisterType<NetworkDriveController>("Society", 1, 0, "NetworkDriveController");
        qmlRegisterType<AccountController>("Society", 1, 0, "AccountController");
        qmlRegisterType<DevicePairing>("Society", 1, 0, "DevicePairing");
        qmlRegisterType<PairingQr>("Society", 1, 0, "PairingQr");
        qmlRegisterType<QrScanner>("Society", 1, 0, "QrScanner");
    }

    void accountOwnsMovedDiskAndRemoteDevicesKeepTheirLocalMounts()
    {
#ifndef Q_OS_MACOS
        QSKIP("Native disk image relocation is available on macOS.");
#else
        QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/account-drive-XXXXXX");
        QVERIFY(fixture.isValid());
        const auto previousSettings = qgetenv("SOCIETY_STORAGE_SETTINGS_PATH");
        const auto reset = qScopeGuard([&] { qputenv("SOCIETY_STORAGE_SETTINGS_PATH", previousSettings); });
        qputenv("SOCIETY_STORAGE_SETTINGS_PATH", fixture.filePath("settings.json").toUtf8());
        const auto volume = iiSocietyContainer::DiskImage::create(fixture.path().toStdString(), 512ULL * 1024 * 1024);
        QVERIFY(volume);
        QString image = QString::fromStdString(volume->imagePath.string());
        const auto eject = qScopeGuard([&] { iiSocietyContainer::DiskImage::detach(image.toStdString()); });
        const auto disk = iiSocietyContainer::SocietyDrive::create(QString::fromStdString(volume->mountPath.string())); QVERIFY(disk);
        AccountServer authority; QVERIFY(authority.server.listen(QHostAddress::LocalHost));
        iisacc::accounts::AccountManager host(authority.url()), peer(authority.url());
        auto device = host.deviceInfo(); device["appId"] = "com.iisacc.society"; device["id"] = QString(64, 'a');
        QVERIFY(host.setDeviceInfo(device)); device["id"] = QString(64, 'b'); QVERIFY(peer.setDeviceInfo(device));
        QVERIFY(host.loginWithPassword("builder@example.com", "fixture")); QTRY_VERIFY(host.isAuthenticated());
        QVERIFY(peer.loginWithPassword("builder@example.com", "fixture")); QTRY_VERIFY(peer.isAuthenticated());
        DriveController local, remote; local.setAccountManager(&host); remote.setAccountManager(&peer);
        QVERIFY(local.openContainer(disk->rootPath()));
        const auto replica = fixture.filePath("local-replica"); QVERIFY(QDir().mkpath(replica));
        QVERIFY(iiSocietyContainer::SocietyDrive::create(replica)); QVERIFY(remote.openContainer(replica));
        QVERIFY(local.saveContainerToAccount());
        QTRY_COMPARE(host.account()->societyContainerDrive().value("imagePath").toString(), image);
        QVERIFY(iiSocietyContainer::DiskImage::detach(image.toStdString()));
        QVERIFY(!local.reloadFromDisk()); QVERIFY(!local.hasDrive());
        QQmlEngine onboardingEngine;
        QQmlComponent onboardingComponent(&onboardingEngine, QUrl::fromLocalFile(QFileInfo(QStringLiteral(SOCIETY_QML_FILE)).dir().filePath("OnboardingView.qml")));
        NetworkDriveController network;
        QScopedPointer<QObject> onboarding(onboardingComponent.createWithInitialProperties({{"drive", QVariant::fromValue(&local)}, {"network", QVariant::fromValue(&network)}}));
        QVERIFY2(onboarding, qPrintable(onboardingComponent.errorString()));
        auto* locate = onboarding->findChild<QQuickItem*>("onboardingChooseDisk");
        QVERIFY(locate); QVERIFY(locate->isVisible());
        const auto moved = fixture.filePath("Moved.sparsebundle"); QVERIFY(QDir().rename(image, moved)); image = moved;
        QVERIFY(local.openContainerImage(QUrl::fromLocalFile(moved)));
        QTRY_VERIFY_WITH_TIMEOUT(!local.busy(), 60000);
        QVERIFY2(local.hasDrive(), qPrintable(local.errorString())); QCOMPARE(local.identifier(), disk->identifier());
        QTRY_COMPARE(host.account()->societyContainerDrive().value("imagePath").toString(), moved);
        QVERIFY(peer.refreshContainerDrive());
        QTRY_COMPARE(peer.account()->societyContainerDrive(), host.account()->societyContainerDrive());
        QCOMPARE(remote.rootPath(), replica); // A remote host path is metadata, never a local mount.
        QVERIFY(!remote.saveContainerToAccount());
        QCOMPARE(peer.account()->societyContainerDrive().value("containerId").toString(), disk->identifier());
#endif
    }

    void onboardingProvidesPlatformRecoveryActions_data()
    {
        QTest::addColumn<bool>("mobile"); QTest::addColumn<QSize>("size");
        QTest::newRow("mobile") << true << QSize(390, 844);
        QTest::newRow("small-mobile") << true << QSize(320, 568);
        QTest::newRow("desktop") << false << QSize(800, 600);
        QTest::newRow("small-desktop") << false << QSize(360, 320);
    }
    void onboardingProvidesPlatformRecoveryActions()
    {
        QFETCH(bool, mobile); QFETCH(QSize, size);
        DriveController drive;
        std::unique_ptr<NetworkDriveController> network = mobile
            ? std::unique_ptr<NetworkDriveController>(new MobileNetworkDevice)
            : std::make_unique<NetworkDriveController>();
        QQmlEngine engine; engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        QQmlComponent component(&engine, QUrl::fromLocalFile(QFileInfo(QStringLiteral(SOCIETY_QML_FILE)).dir().filePath("OnboardingView.qml")));
        QScopedPointer<QObject> object(component.createWithInitialProperties({{"drive", QVariant::fromValue(&drive)},
            {"network", QVariant::fromValue(network.get())}}));
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *view = qobject_cast<QQuickItem *>(object.get()); QVERIFY(view);
        QQuickWindow window; window.setColor(QColor("#1e1e1e"));
        window.resize(size); view->setParentItem(window.contentItem()); view->setSize(size);
        window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *create = view->findChild<QQuickItem *>("onboardingCreateDisk");
        auto *choose = view->findChild<QQuickItem *>("onboardingChooseDisk");
        auto *connectHost = view->findChild<QQuickItem *>("onboardingConnectHost");
        auto *devices = view->findChild<QQuickItem *>("onboardingHostDevices");
        QVERIFY(create && choose && connectHost && devices);
        QCOMPARE(create->isVisible(), !mobile); QCOMPARE(choose->isVisible(), !mobile);
        QCOMPARE(connectHost->isVisible(), mobile); QCOMPARE(devices->isVisible(), mobile);
        QVERIFY(!view->findChild<QObject *>("onboardingContainerPath"));
        auto *scroll = view->findChild<QQuickItem *>("onboardingViewport"); QVERIFY(scroll);
        QTRY_VERIFY(scroll->height() > 0);
        const auto click = [&](QQuickItem *item) {
            const auto top = item->mapToItem(scroll, QPointF()).y() + scroll->property("contentY").toReal();
            scroll->setProperty("contentY", qBound(0.0, top - 8, scroll->property("contentHeight").toReal() - scroll->height()));
            QTest::qWait(50);
            QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, item->mapToScene(item->boundingRect().center()).toPoint());
        };
        if (mobile) {
            QSignalSpy account(view, SIGNAL(accountRequested())), hosts(view, SIGNAL(devicesRequested()));
            click(connectHost); QCOMPARE(account.size(), 1);
            click(devices); QCOMPARE(hosts.size(), 1);
            auto *qr = view->findChild<QQuickItem *>("onboardingQrPairing"); QVERIFY(qr); QVERIFY(qr->isVisible());
            QSignalSpy pairing(view, SIGNAL(pairingRequested())); click(qr); QCOMPARE(pairing.size(), 1);
            QVERIFY(!network->hostConnectionReady()); QVERIFY(!drive.hasDrive());
        } else {
            auto *folder = view->findChild<QObject *>("onboardingFolderDialog");
            auto *disk = view->findChild<QObject *>("onboardingDiskDialog"); QVERIFY(folder && disk);
            click(create); QTRY_VERIFY(folder->property("visible").toBool());
            QVERIFY(QMetaObject::invokeMethod(folder, "reject"));
            click(choose); QTRY_VERIFY(disk->property("visible").toBool());
            QVERIFY(QMetaObject::invokeMethod(disk, "reject"));
            QVERIFY(!drive.hasDrive()); // Canceling either dialog creates nothing.
        }
        const auto captures = qEnvironmentVariable("SOCIETY_ONBOARDING_CAPTURE_DIR");
        if (!captures.isEmpty()) {
            scroll->setProperty("contentY", 0); QTest::qWait(50);
            QVERIFY(QDir().mkpath(captures));
            QVERIFY(window.grabWindow().save(QDir(captures).filePath(QString::fromLatin1(QTest::currentDataTag()) + ".png")));
        }
    }

    void onboardingStartsWithoutAContainer_data()
    {
        QTest::addColumn<QSize>("viewport");
        QTest::newRow("desktop") << QSize(1440, 900);
        QTest::newRow("compact") << QSize(800, 600);
        QTest::newRow("minimum") << QSize(360, 320);
        QTest::newRow("mobile-layout") << QSize(390, 844);
    }

    void onboardingStartsWithoutAContainer()
    {
#ifndef Q_OS_MACOS
        QSKIP("Native disk-image onboarding is currently available on macOS.");
#endif
        QFETCH(QSize, viewport);
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/onboarding-XXXXXX");
        QVERIFY(fixture.isValid());
        const auto previousSettings = qgetenv("SOCIETY_STORAGE_SETTINGS_PATH");
        const auto restoreSettings = qScopeGuard([&] { qputenv("SOCIETY_STORAGE_SETTINGS_PATH", previousSettings); });
        const auto settingsPath = fixture.filePath("settings.json");
        qputenv("SOCIETY_STORAGE_SETTINGS_PATH", settingsPath.toUtf8());
        QQmlApplicationEngine engine;
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        engine.setInitialProperties({{"width", viewport.width()}, {"height", viewport.height()},
            {"mobileLayout", viewport.width() == 390}});
        engine.load(QUrl::fromLocalFile(QString::fromUtf8(SOCIETY_QML_FILE)));
        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *drive = window->findChild<DriveController *>("driveController"); QVERIFY(drive);
        auto *onboarding = window->findChild<QQuickItem *>("containerOnboarding"); QVERIFY(onboarding);
        auto *content = window->findChild<QQuickItem *>("societyContent"); QVERIFY(content);
        auto *proceed = window->findChild<QQuickItem *>("onboardingCreateDisk"); QVERIFY(proceed);
        auto *choose = window->findChild<QQuickItem *>("onboardingChooseDisk"); QVERIFY(choose);
        auto *error = window->findChild<QQuickItem *>("onboardingError"); QVERIFY(error);
        QVERIFY(window->property("onboardingRequired").toBool());
        QVERIFY(onboarding->isVisible()); QVERIFY(!content->isVisible());
        QVERIFY(!drive->hasDrive()); QVERIFY(drive->errorString().isEmpty());
        QVERIFY(!error->isVisible()); QVERIFY(proceed->isEnabled()); QVERIFY(choose->isEnabled());
        QVERIFY(!QFileInfo::exists(settingsPath));
        QVERIFY(!window->findChild<QObject *>("onboardingContainerPath"));

        auto *scroll = window->findChild<QQuickItem *>("onboardingViewport"); QVERIFY(scroll);
        QTRY_VERIFY(proceed->width() > 0 && proceed->height() >= 44);
        const auto inputRect = proceed->mapRectToScene(proceed->boundingRect());
        QVERIFY(inputRect.left() >= 0 && inputRect.right() <= window->width());
        QVERIFY(scroll->property("contentHeight").toReal() >= scroll->height());
        const auto captures = qEnvironmentVariable("SOCIETY_ONBOARDING_CAPTURE_DIR");
        if (!captures.isEmpty()) {
            QVERIFY(QDir().mkpath(captures));
            QTest::qWait(100);
            QVERIFY(window->grabWindow().save(QDir(captures).filePath(QString::fromLatin1(QTest::currentDataTag()) + ".png")));
        }

        // Invalid selections leave the recovery screen usable and settings intact.
        QVERIFY(!drive->openContainerImage(QUrl("https://example.invalid/disk")));
        QVERIFY(onboarding->isVisible()); QVERIFY(!drive->hasDrive());
        QVERIFY(error->isVisible()); QVERIFY(!QFileInfo::exists(settingsPath));
        const auto container = fixture.filePath(QString::fromUtf8("Society 한글 #100%"));
        QVERIFY(QDir().mkpath(container));
        // Exercise the same accepted-folder handler used by the native dialog.
        QVERIFY(QMetaObject::invokeMethod(onboarding, "createDisk", Q_ARG(QVariant, QUrl::fromLocalFile(container))));
        QVERIFY(drive->busy());
        QVERIFY(!proceed->isEnabled());
        QTRY_VERIFY_WITH_TIMEOUT(!drive->busy(), 120000);
        QVERIFY2(drive->hasDrive(), qPrintable(drive->errorString()));
        const auto image = QDir(container).filePath("Society.sparsebundle").toStdString();
        const auto eject = qScopeGuard([&] { iiSocietyContainer::DiskImage::detach(image); });
        QVERIFY(!window->property("onboardingRequired").toBool());
        QVERIFY(!onboarding->isVisible()); QVERIFY(content->isVisible());
        QVERIFY(drive->rootPath() != container);
        QCOMPARE(QStorageInfo(drive->rootPath()).fileSystemType(), QByteArray("apfs"));
        QVERIFY(QStorageInfo(drive->rootPath()).device() != QStorageInfo(container).device());
        const auto publicRoot = iiSocietyContainer::DiskImage::filesRoot(drive->rootPath().toStdString()); QVERIFY(publicRoot);
        QCOMPARE(drive->systemPath(), QString::fromStdString(publicRoot->string()));
        QCOMPARE(QStorageInfo(drive->systemPath()).rootPath(), drive->systemPath());
        QVERIFY(!QFileInfo::exists(drive->systemPath() + "/Models"));
        QVERIFY(!QFileInfo::exists(drive->systemPath() + "/Files"));
        QVERIFY(drive->navigate(drive->systemPath()));
        QCOMPARE(drive->currentSection(), QString("Files"));
        QCOMPARE(drive->breadcrumbs().last().toMap().value("path").toString(), drive->systemPath());
        drive->goUp(); QVERIFY(drive->atRoot());
        QVERIFY(!QFileInfo::exists(container + "/Files"));
        QVERIFY(QFileInfo::exists(container + "/Society.sparsebundle"));
        const auto shared = iiSocietyContainer::SharedStorage::open(); QVERIFY(shared);
        QCOMPARE(shared->drive().identifier(), drive->identifier());

        // The next window goes directly to the dashboard using the saved location.
        QQmlApplicationEngine reopened;
        reopened.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        reopened.load(QUrl::fromLocalFile(QString::fromUtf8(SOCIETY_QML_FILE)));
        QCOMPARE(reopened.rootObjects().size(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(!reopened.rootObjects().first()->property("onboardingRequired").toBool(), 60000);
        QCOMPARE(reopened.rootObjects().first()->findChild<DriveController *>("driveController")->identifier(), drive->identifier());
    }

    void onboardingRecoversUnavailableSavedContainer()
    {
#ifndef Q_OS_MACOS
        QSKIP("Native disk-image onboarding is currently available on macOS.");
#endif
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/onboarding-recovery-XXXXXX");
        QVERIFY(fixture.isValid());
        const auto previousSettings = qgetenv("SOCIETY_STORAGE_SETTINGS_PATH");
        const auto restoreSettings = qScopeGuard([&] { qputenv("SOCIETY_STORAGE_SETTINGS_PATH", previousSettings); });
        qputenv("SOCIETY_STORAGE_SETTINGS_PATH", fixture.filePath("settings.json").toUtf8());
        const auto volume = iiSocietyContainer::DiskImage::create(fixture.path().toStdString(), 512ULL * 1024 * 1024);
        QVERIFY2(volume, volume ? "" : volume.error().c_str());
        const auto eject = qScopeGuard([&] { iiSocietyContainer::DiskImage::detach(volume->imagePath); });
        const auto container = QString::fromStdString(volume->mountPath.string());
        const auto image = QString::fromStdString(volume->imagePath.string()), offline = fixture.filePath("offline.sparsebundle");
        const auto original = iiSocietyContainer::SocietyDrive::create(container); QVERIFY(original);
        QVERIFY(iiSocietyContainer::SharedStorage::setDefaultContainer(container));
        QVERIFY(iiSocietyContainer::DiskImage::detach(volume->imagePath));
        QVERIFY(QDir().rename(image, offline));
        QQmlApplicationEngine engine;
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        engine.load(QUrl::fromLocalFile(QString::fromUtf8(SOCIETY_QML_FILE)));
        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *drive = window->findChild<DriveController *>("driveController"); QVERIFY(drive);
        QVERIFY(window->property("onboardingRequired").toBool());
        QTRY_VERIFY_WITH_TIMEOUT(!drive->busy(), 60000);
        QVERIFY(!drive->errorString().isEmpty()); QVERIFY(!QFileInfo::exists(image));
        QFile saved(fixture.filePath("settings.json")); QVERIFY(saved.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(saved.readAll()).object().value("path").toString(), container);
        saved.close();
        QVERIFY(QDir().rename(offline, image));
        auto *retry = window->findChild<QQuickItem *>("onboardingRetry"); QVERIFY(retry);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, retry->mapToScene(retry->boundingRect().center()).toPoint());
        QTRY_VERIFY_WITH_TIMEOUT(!window->property("onboardingRequired").toBool(), 60000);
        QCOMPARE(drive->identifier(), original->identifier());
        QVERIFY(iiSocietyContainer::DiskImage::detach(volume->imagePath));
        QTRY_VERIFY_WITH_TIMEOUT(window->property("onboardingRequired").toBool(), 10000);
        QVERIFY(drive->systemPath().isEmpty());
        drive->openDefaultContainer();
        QTRY_VERIFY_WITH_TIMEOUT(!drive->busy(), 60000);
        QVERIFY2(drive->hasDrive(), qPrintable(drive->errorString()));
        QCOMPARE(drive->identifier(), original->identifier());
    }

    void onboardingRecoversInvalidInitialPath()
    {
#ifndef Q_OS_MACOS
        QSKIP("Native disk-image onboarding is currently available on macOS.");
#endif
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/onboarding-cli-XXXXXX");
        QVERIFY(fixture.isValid());
        const auto previousSettings = qgetenv("SOCIETY_STORAGE_SETTINGS_PATH");
        const auto restoreSettings = qScopeGuard([&] { qputenv("SOCIETY_STORAGE_SETTINGS_PATH", previousSettings); });
        qputenv("SOCIETY_STORAGE_SETTINGS_PATH", fixture.filePath("settings.json").toUtf8());
        const auto container = fixture.filePath("existing");
        QVERIFY(QDir().mkpath(container));
        const auto volume = iiSocietyContainer::DiskImage::create(container.toStdString(), 512ULL * 1024 * 1024);
        QVERIFY2(volume, volume ? "" : volume.error().c_str());
        const auto eject = qScopeGuard([&] { iiSocietyContainer::DiskImage::detach(volume->imagePath); });
        const auto original = iiSocietyContainer::SocietyDrive::create(QString::fromStdString(volume->mountPath.string())); QVERIFY(original);
        QQmlApplicationEngine engine;
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        engine.setInitialProperties({{"initialContainerPath", "relative-path"}});
        engine.load(QUrl::fromLocalFile(QString::fromUtf8(SOCIETY_QML_FILE)));
        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QVERIFY(window->property("onboardingRequired").toBool());
        auto *drive = window->findChild<DriveController *>("driveController"); QVERIFY(drive);
        QVERIFY(drive->localContainerPath(QUrl("https://example.invalid/container")).isEmpty());
        auto *onboarding = window->findChild<QQuickItem *>("containerOnboarding"); QVERIFY(onboarding);
        const auto image = QUrl::fromLocalFile(QString::fromStdString(volume->imagePath.string()));
        QVERIFY(QMetaObject::invokeMethod(onboarding, "selectDisk", Q_ARG(QVariant, image)));
        QTRY_VERIFY_WITH_TIMEOUT(!window->property("onboardingRequired").toBool(), 60000);
        QCOMPARE(drive->identifier(), original->identifier());
    }

    void ordinaryFoldersDoNotBecomeSystemDisks()
    {
#ifndef Q_OS_MACOS
        QSKIP("Native disk-image onboarding is currently available on macOS.");
#endif
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/legacy-folder-XXXXXX");
        const auto previous = qgetenv("SOCIETY_STORAGE_SETTINGS_PATH");
        const auto restore = qScopeGuard([&] { qputenv("SOCIETY_STORAGE_SETTINGS_PATH", previous); });
        qputenv("SOCIETY_STORAGE_SETTINGS_PATH", fixture.filePath("settings.json").toUtf8());
        DriveController drive;
        QVERIFY(!drive.openContainer(fixture.path()));
        QVERIFY(!QFileInfo::exists(fixture.filePath("Files")));
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        QVERIFY(iiSocietyContainer::SharedStorage::setDefaultContainer(fixture.path()));
        drive.openDefaultContainer();
        QTRY_VERIFY_WITH_TIMEOUT(!drive.busy(), 60000);
        QVERIFY(!drive.hasDrive()); QVERIFY(!drive.errorString().isEmpty());
        QVERIFY(QFileInfo::exists(fixture.filePath("Files")));
    }

    void noticeKeepsFigmaGeometryAndMaterial_data()
    {
        QTest::addColumn<QSize>("viewport");
        QTest::newRow("desktop") << QSize(1440, 900);
        QTest::newRow("compact") << QSize(800, 600);
        QTest::newRow("mobile") << QSize(390, 844);
    }

    void noticeKeepsFigmaGeometryAndMaterial()
    {
        QFETCH(QSize, viewport);
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/alert-figma-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        QQmlApplicationEngine engine;
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        engine.setInitialProperties({{"initialContainerPath", fixture.path()},
            {"width", viewport.width()}, {"height", viewport.height()}});
        engine.load(QUrl::fromLocalFile(QString::fromUtf8(SOCIETY_QML_FILE)));
        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *notice = window->findChild<QQuickItem *>("dashboardNotice"); QVERIFY(notice);
        QVERIFY(notice->setProperty("motionEnabled", false));
        QVERIFY(QMetaObject::invokeMethod(window, "showNotice", Q_ARG(QVariant, "Guild"),
            Q_ARG(QVariant, "Guild is not connected to a workspace service yet.")));
        auto *card = notice->findChild<QQuickItem *>("alertCard");
        auto *title = notice->findChild<QQuickItem *>("alertTitle");
        auto *message = notice->findChild<QQuickItem *>("alertMessage");
        auto *button = notice->findChild<QQuickItem *>("alertPrimarySingle");
        QVERIFY(card && title && message && button);
        QTRY_VERIFY(notice->property("open").toBool());
        QTRY_COMPARE(notice->property("revealProgress").toReal(), 1.0);
        QTRY_VERIFY(title->property("lineCount").toInt() > 0 && message->property("lineCount").toInt() > 0);
        QTRY_COMPARE(card->height(), 46.0 + 26.0 * title->property("lineCount").toInt()
            + 14.0 + 13.0 * message->property("lineCount").toInt() + 36.0 + 1.0 + 28.0 + 56.0 + 32.0);
        const QJsonObject metrics{{"viewportWidth", window->width()}, {"viewportHeight", window->height()},
            {"devicePixelRatio", window->devicePixelRatio()}, {"cardWidth", card->width()}, {"cardHeight", card->height()},
            {"titleSize", title->property("font").value<QFont>().pixelSize()},
            {"bodySize", message->property("font").value<QFont>().pixelSize()}, {"buttonHeight", button->height()},
            {"buttonColor", button->property("backgroundColor").value<QColor>().name()},
            {"glassActive", notice->property("glassActive").toBool()}};
        qInfo().noquote() << QJsonDocument(metrics).toJson(QJsonDocument::Compact);
        const auto captures = qEnvironmentVariable("SOCIETY_ALERT_CAPTURE_DIR");
        if (!captures.isEmpty()) {
            QVERIFY(QDir().mkpath(captures));
            QTest::qWait(200);
            const auto stem = QDir(captures).filePath(QString::fromLatin1(QTest::currentDataTag()));
            QVERIFY(window->grabWindow().save(stem + ".png"));
            QFile report(stem + ".json"); QVERIFY(report.open(QIODevice::WriteOnly));
            report.write(QJsonDocument(metrics).toJson());
        }
        QCOMPARE(card->width(), qMin(500.0, window->width() - 48.0));
        QCOMPARE(card->property("radius").toReal(), 36.0);
        QCOMPARE(card->scale(), 1.0);
        QCOMPARE(title->property("font").value<QFont>().pixelSize(), 26);
        QCOMPARE(message->property("font").value<QFont>().pixelSize(), 13);
        QCOMPARE(button->height(), 56.0);
        QCOMPARE(button->property("cornerRadius").toReal(), 16.0);
        QVERIFY(QRectF(QPointF(), window->size()).contains(card->mapRectToScene(card->boundingRect())));
        const auto primaryColor = window->property("primaryColor").value<QColor>();
        QCOMPARE(primaryColor, QColor("#57965C"));
        QCOMPARE(button->property("backgroundColor").value<QColor>(), primaryColor);
        QVERIFY(notice->property("glassActive").toBool());
        QCOMPARE(notice->property("resolvedBackdropSource").value<QQuickItem *>(),
            window->property("materialBackdropSource").value<QQuickItem *>());
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
            button->mapToScene(button->boundingRect().center()).toPoint());
        QTRY_VERIFY(!notice->property("open").toBool());
    }

    void filesStartEmptyAndPhotosShowsImagesAndVideos()
    {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/fixed-files-ui-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        QImage photo(32, 32, QImage::Format_RGB32); photo.fill(Qt::blue);
        QVERIFY(photo.save(fixture.filePath("Photos/photo.png")));
        QFile video(fixture.filePath("Photos/video.mp4"));
        QVERIFY(video.open(QIODevice::WriteOnly)); video.write("video fixture"); video.close();
        QQmlApplicationEngine engine;
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        engine.setInitialProperties({{"initialContainerPath", fixture.path()}});
        engine.load(QUrl::fromLocalFile(QString::fromUtf8(SOCIETY_QML_FILE)));
        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        window->resize(1120, 720); QVERIFY(window->setProperty("selectedTab", "Storage"));
        auto *drive = window->findChild<DriveController *>("driveController"); QVERIFY(drive);
        auto *files = window->findChild<QQuickItem *>("filesBrowser"); QVERIFY(files);
        QVERIFY(drive->openSection("files"));
        QTRY_COMPARE(files->property("count").toInt(), 0);
        QTRY_VERIFY(!files->property("loading").toBool());
        const auto names = [&] {
            QStringList result;
            auto *model = qvariant_cast<QAbstractItemModel *>(files->property("directoryModel"));
            if (!model) return result;
            const auto role = model->roleNames().key("fileName", -1);
            for (int row = 0; row < model->rowCount(); ++row) result.append(model->data(model->index(row, 0), role).toString());
            result.sort(); return result;
        };
        QCOMPARE(names(), QStringList{});
        auto *emptyTitle = visualItem(files, "filesEmptyState"); QVERIFY(emptyTitle);
        QTRY_COMPARE(emptyTitle->property("text").toString(), QString("This folder is empty"));
        const auto screenshot = qEnvironmentVariable("SOCIETY_FILES_DIRECTORIES_SCREENSHOT_PATH");
        if (!screenshot.isEmpty()) {
            QVERIFY(QTest::qWaitForWindowExposed(window));
            QTest::qWait(150);
            QTRY_COMPARE(emptyTitle->property("text").toString(), QString("This folder is empty"));
            QTest::qWait(50);
            QVERIFY(window->grabWindow().save(screenshot));
        }
        const auto view = iiSocietyContainer::FilesView::open(fixture.path()); QVERIFY(view);
        QVERIFY(view->directories().isEmpty());
        auto *emptyState = visualItem(files, "filesEmptyState"); QVERIFY(emptyState);
        QTRY_VERIFY(emptyState->isVisible());
        QVERIFY(QDir().mkdir(fixture.filePath("Files/Documents")));
        QTRY_COMPARE(files->property("count").toInt(), 1);
        QVERIFY(QMetaObject::invokeMethod(files, "activated", Q_ARG(QString, fixture.filePath("Files/Documents")), Q_ARG(bool, true)));
        QTRY_COMPARE(drive->currentPath(), fixture.filePath("Files/Documents"));
        drive->goUp(); QTRY_COMPARE(drive->currentPath(), fixture.filePath("Files"));
        QVERIFY(QDir().rmdir(fixture.filePath("Files/Documents")));
        QTRY_COMPARE(files->property("count").toInt(), 0);
        QVERIFY(drive->openSection("photos"));
        QCOMPARE(drive->currentPath(), fixture.filePath("Photos"));
        auto *gallery = visualItem(window->contentItem(), "photosView"); QVERIFY(gallery);
        QTRY_VERIFY(gallery->isVisible()); QVERIFY(!files->isVisible());
        QCOMPARE(drive->breadcrumbs().size(), 2);
        drive->goUp(); QTRY_COMPARE(drive->currentPath(), fixture.filePath("Files"));
        window->close();
    }

    void storageNavigationUsesStableIdentitiesAndAccountScopedMemberships()
    {
        StorageNavigation navigation;
        QStringList ids;
        for (const auto &entry : navigation.sections()) ids.append(entry.toMap().value("id").toString());
        QCOMPARE(ids, QStringList({"files", "photos", "asset-library", "generation-history", "models", "thinking-space", "forked", "published", "deleted"}));
        navigation.setCurrentSection("Models"); QCOMPARE(navigation.selectedSection(), QString("models"));
        navigation.setAccountId("first-account"); navigation.setCurrentDeviceId("this-device");
        QVariantList remembered{
            QVariantMap{{"id", "desktop"}, {"name", "Old desktop name"}, {"kind", "desktop"}},
            QVariantMap{{"id", "phone"}, {"name", "My love iPhone"}, {"kind", "phone"}},
            QVariantMap{{"id", "this-device"}, {"name", "This device"}}};
        navigation.setRememberedDevices(remembered);
        navigation.setNearbyDevices({
            QVariantMap{{"id", "desktop"}, {"name", "My other desktop"}, {"kind", "desktop"}, {"verified", true}},
            QVariantMap{{"id", "tablet"}, {"name", "New iPad"}, {"kind", "tablet"}, {"verified", true}},
            QVariantMap{{"id", "unverified"}, {"name", "Unverified device"}}});
        navigation.setHosts({QVariantMap{{"peerId", "desktop"}, {"name", "My other desktop"}},
            QVariantMap{{"peerId", "self-transport"}, {"metadata", QVariantMap{{"deviceId", "this-device"}}}}});
        QCOMPARE(navigation.devices().size(), 3);
        QCOMPARE(navigation.devices().first().toMap().value("id").toString(), QString("desktop"));
        QMap<QString, QVariantMap> devices;
        for (const auto &value : navigation.devices()) devices.insert(value.toMap().value("id").toString(), value.toMap());
        QCOMPARE(devices["desktop"].value("peerId").toString(), QString("desktop"));
        QCOMPARE(devices["desktop"].value("icon").toString(), QString("screens"));
        QCOMPARE(devices["phone"].value("icon").toString(), QString("iPhoneDevice"));
        QCOMPARE(devices["phone"].value("status").toString(), QString("Offline"));
        QCOMPARE(devices["tablet"].value("icon").toString(), QString("nodesdataColumn"));
        QVERIFY(devices["tablet"].value("online").toBool());
        QSignalSpy deviceChanges(&navigation, &StorageNavigation::devicesChanged);
        auto sameDevice = remembered.first().toMap(); sameDevice.insert("lastPairedAt", "new timestamp");
        remembered[0] = sameDevice; navigation.setRememberedDevices(remembered);
        QCOMPARE(deviceChanges.size(), 0); QCOMPARE(navigation.selectedSection(), QString("models"));
        QSignalSpy deviceRequests(&navigation, &StorageNavigation::deviceRequested);
        QVERIFY(navigation.activate("devices", "desktop")); QCOMPARE(deviceRequests.first().last().toString(), QString("desktop"));
        QVERIFY(navigation.activate("devices", "phone")); QVERIFY(deviceRequests.last().last().toString().isEmpty());
        QVERIFY(!navigation.activate("devices", "unverified"));
        navigation.setHosts({}); navigation.setNearbyDevices({});
        QCOMPARE(navigation.devices().size(), 2); // Remembered devices survive disappearance from LAN.
        for (const auto &value : navigation.devices()) QVERIFY(!value.toMap().value("online").toBool());

        const QVariantList guilds{QVariantMap{{"id", "same-id"}, {"name", "Guild 1"}},
            QVariantMap{{"id", "same-id"}, {"name", "Duplicate"}}, QVariantMap{{"name", "Missing ID"}}};
        const QVariantList organizations{QVariantMap{{"id", "same-id"}, {"name", "Organization 1"}}};
        QSignalSpy membershipChanges(&navigation, &StorageNavigation::membershipsChanged);
        QVERIFY(!navigation.replaceMemberships("other-account", guilds, organizations));
        QVERIFY(navigation.replaceMemberships("first-account", guilds, organizations));
        QCOMPARE(navigation.guilds().size(), 1); QCOMPARE(navigation.organizations().size(), 1);
        QVERIFY(navigation.replaceMemberships("first-account", guilds, organizations)); QCOMPARE(membershipChanges.size(), 1);
        QSignalSpy workspaceRequests(&navigation, &StorageNavigation::workspaceRequested);
        QVERIFY(navigation.activate("guild", "same-id")); QVERIFY(navigation.activate("organization", "same-id"));
        QCOMPARE(workspaceRequests.at(0).at(0).toString(), QString("guild"));
        QCOMPARE(workspaceRequests.at(1).at(0).toString(), QString("organization"));
        QVERIFY(!navigation.activate("guild", "missing")); QVERIFY(!navigation.activate("invalid", "same-id"));
        navigation.setAccountId("second-account");
        QVERIFY(navigation.devices().isEmpty()); QVERIFY(navigation.guilds().isEmpty()); QVERIFY(navigation.organizations().isEmpty());
        QVERIFY(!navigation.replaceMemberships("first-account", guilds, organizations)); // Late responses cannot cross sessions.
        navigation.setAccountId(""); QVERIFY(!navigation.replaceMemberships("", guilds, organizations));
        QSignalSpy sectionRequests(&navigation, &StorageNavigation::sectionRequested);
        QVERIFY(navigation.activate("storage", "files")); QCOMPARE(sectionRequests.first().first().toString(), QString("files"));
        QVERIFY(!navigation.activate("storage", "../../Models"));
        deviceChanges.clear();
        navigation.replaceDevices("new-account", "this-device", remembered, {}, {});
        QCOMPARE(navigation.devices().size(), 2); QCOMPARE(deviceChanges.size(), 1);
        navigation.replaceDevices("new-account", "this-device", remembered, {}, {});
        QCOMPARE(deviceChanges.size(), 1); // One complete account snapshot, one visible update.
    }

    void dashboardSidebarMatchesFigmaAndRoutesWorkspaceTargets()
    {
        QQmlEngine engine;
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&](const QList<QQmlError> &errors) {
            for (const auto &error : errors) warnings.append(error.toString());
        });
        const auto directory = QFileInfo(QString::fromUtf8(SOCIETY_QML_FILE)).dir();
        QQmlComponent component(&engine, QUrl::fromLocalFile(directory.filePath("Dashboard/DashboardSidebar.qml")));
        const QVariantList memberships{
            QVariantMap{{"id", "1"}, {"name", "GuildName"}},
            QVariantMap{{"id", "2"}, {"name", "GuildName"}},
            QVariantMap{{"id", "3"}, {"name", "GuildName"}}};
        QScopedPointer<QObject> object(component.createWithInitialProperties({
            {"width", 204}, {"height", 844}, {"guilds", memberships}, {"organizations", memberships}}));
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *sidebar = qobject_cast<QQuickItem *>(object.data()); QVERIFY(sidebar);
        QQuickWindow window;
        window.resize(204, 844);
        sidebar->setParentItem(window.contentItem());
        window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
        const QStringList groups{"workspace", "guild", "organization"};
        const QStringList titles{"Workspace", "Guilds", "Organization"};
        const QList<qreal> positions{12, 200, 324};
        for (int i = 0; i < groups.size(); ++i) {
            auto *heading = visualItem(sidebar, "dashboardHeading" + groups[i]); QVERIFY(heading);
            QCOMPARE(heading->property("text").toString(), titles[i]);
            QTRY_COMPARE(heading->mapToItem(sidebar, QPointF()), QPointF(12, positions[i]));
            QCOMPARE(heading->height(), 12.0);
        }
        const QStringList keys{"home", "projects", "calendar", "activity", "people"};
        const QStringList names{"Home", "Projects", "Calendar", "Activity", "People"};
        for (int i = 0; i < keys.size(); ++i) {
            auto *row = visualItem(sidebar, "dashboardItemworkspace_" + keys[i]); QVERIFY(row);
            QCOMPARE(row->property("label").toString(), names[i]);
            QCOMPARE(row->size(), QSizeF(180, 32));
            QCOMPARE(row->mapToItem(sidebar, QPointF()), QPointF(12, 32 + i * 32));
            QVERIFY(row->property("showTrailingIcon").toBool());
            const auto *accessible = QAccessible::queryAccessibleInterface(row); QVERIFY(accessible);
            QCOMPARE(accessible->text(QAccessible::Name), names[i]);
        }
        QVERIFY(!visualItem(sidebar, "dashboardLocal"));
        QVERIFY(!visualItem(sidebar, "dashboardThisDevice"));
        QSignalSpy homes(sidebar, SIGNAL(homeRequested()));
        QSignalSpy calendars(sidebar, SIGNAL(calendarRequested()));
        QSignalSpy activity(sidebar, SIGNAL(activityRequested()));
        QSignalSpy features(sidebar, SIGNAL(featureRequested(QString)));
        QSignalSpy workspaces(sidebar, SIGNAL(workspaceRequested(QString,QString)));
        for (const auto &key : keys) {
            auto *row = visualItem(sidebar, "dashboardItemworkspace_" + key);
            QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                row->mapToScene(QPointF(row->width()/2, row->height()/2)).toPoint());
        }
        QCOMPARE(homes.size(), 1); QCOMPARE(calendars.size(), 1); QCOMPARE(activity.size(), 1);
        QCOMPARE(features, QList<QList<QVariant>>({{QString("Projects")}, {QString("People")}}));
        auto *organization = visualItem(sidebar, "dashboardItemorganization_3"); QVERIFY(organization);
        organization->forceActiveFocus(); QTest::keyClick(&window, Qt::Key_Space);
        QTRY_COMPARE(workspaces.size(), 1);
        QCOMPARE(workspaces.first(), QVariantList({"organization", "3"}));
        sidebar->forceActiveFocus();
        QTest::mouseMove(&window, QPoint(200, 800));
        const auto capture = qEnvironmentVariable("SOCIETY_DASHBOARD_SIDEBAR_SCREENSHOT_PATH");
        if (!capture.isEmpty()) { QTest::qWait(100); QVERIFY(window.grabWindow().save(capture)); }
        window.resize(204, 240); sidebar->setHeight(240);
        auto *viewport = visualItem(sidebar, "dashboardSidebarScroll"); QVERIFY(viewport);
        QTRY_VERIFY(viewport->property("contentHeight").toReal() > viewport->height());
        organization->forceActiveFocus();
        QTRY_VERIFY(organization->mapToItem(viewport, QPointF(0, 32)).y() <= viewport->height());
        sidebar->setProperty("guilds", QVariantList{});
        sidebar->setProperty("organizations", QVariantList{});
        QTRY_VERIFY(visualItem(sidebar, "dashboardEmptyguild")->isVisible());
        QVERIFY(!visualItem(sidebar, "dashboardItemguild_1"));
        QVERIFY(!visualItem(sidebar, "dashboardItemorganization_3"));
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        sidebar->setParentItem(nullptr);
    }

    void dashboardSidebarOpensCalendarAndAccountMemberships()
    {
        QQmlEngine engine;
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        const auto directory = QFileInfo(QString::fromUtf8(SOCIETY_QML_FILE)).dir();
        QQmlComponent component(&engine, QUrl::fromLocalFile(directory.filePath("Dashboard/Dashboard.qml")));
        DashboardFiles files;
        StorageNavigation navigation;
        navigation.setAccountId("sidebar-fixture");
        QVERIFY(navigation.replaceMemberships("sidebar-fixture", {
            QVariantMap{{"id", "real-id"}, {"name", "My workspace"}}}, {}));
        QScopedPointer<QObject> object(component.createWithInitialProperties({
            {"width", 1024}, {"height", 600}, {"viewModel", QVariant::fromValue(&files)},
            {"navigation", QVariant::fromValue(&navigation)}}));
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *dashboard = qobject_cast<QQuickItem *>(object.data()); QVERIFY(dashboard);
        auto *calendar = visualItem(dashboard, "dashboardCalendarView"); QVERIFY(calendar);
        auto *activity = visualItem(dashboard, "dashboardItemworkspace_activity"); QVERIFY(activity);
        QVERIFY(QMetaObject::invokeMethod(activity, "clicked"));
        QCOMPARE(calendar->property("expandedSection").toString(), QString("activity"));
        QVERIFY(QMetaObject::invokeMethod(visualItem(dashboard, "dashboardItemworkspace_calendar"), "clicked"));
        QCOMPARE(calendar->property("expandedSection").toString(), QString());
        QSignalSpy requested(&navigation, &StorageNavigation::workspaceRequested);
        QVERIFY(QMetaObject::invokeMethod(visualItem(dashboard, "dashboardItemguild_real-id"), "clicked"));
        QCOMPARE(requested.size(), 1);
        QCOMPARE(requested.first(), QVariantList({"guild", "real-id", "My workspace"}));
        navigation.setAccountId("");
        QTRY_VERIFY(!visualItem(dashboard, "dashboardItemguild_real-id"));
    }

    void sidebarMatchesFigmaAndRoutesDynamicTargets()
    {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/sidebar-figma-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        QQmlApplicationEngine engine;
        QStringList warnings;
        connect(&engine, &QQmlApplicationEngine::warnings, this, [&](const QList<QQmlError> &errors) {
            for (const auto &error : errors) warnings.append(error.toString());
        });
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        engine.setInitialProperties({{"initialContainerPath", fixture.path()}, {"width", 1440}, {"height", 900}});
        engine.load(QUrl::fromLocalFile(QString::fromUtf8(SOCIETY_QML_FILE)));
        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *drive = window->findChild<DriveController *>("driveController"); QVERIFY(drive);
        auto *navigation = window->findChild<StorageNavigation *>("storageNavigation"); QVERIFY(navigation);
        QVERIFY(window->setProperty("selectedTab", "Storage")); QVERIFY(drive->openSection("models"));
        navigation->setAccountId("figma-fixture");
        navigation->setRememberedDevices({
            QVariantMap{{"id", "desktop"}, {"name", "My other desktop"}, {"kind", "desktop"}},
            QVariantMap{{"id", "phone"}, {"name", "My love iPhone"}, {"kind", "phone"}},
            QVariantMap{{"id", "tablet"}, {"name", "New iPad"}, {"kind", "tablet"}}});
        const QVariantList guilds{QVariantMap{{"id", "1"}, {"name", "Guild 1"}},
            QVariantMap{{"id", "2"}, {"name", "Guild 2"}}, QVariantMap{{"id", "3"}, {"name", "Guild 3"}}};
        const QVariantList organizations{QVariantMap{{"id", "1"}, {"name", "Organization 1"}},
            QVariantMap{{"id", "2"}, {"name", "Organization 2"}}};
        QVERIFY(navigation->replaceMemberships("figma-fixture", guilds, organizations));
        auto *sidebar = window->findChild<QQuickItem *>("driveSidebar"); QVERIFY(sidebar);
        QTRY_COMPARE(sidebar->width(), 228.0); QTRY_COMPARE(sidebar->mapToScene(QPointF()).y(), 56.0);
        const QStringList groups{"storage", "devices", "guild", "organization"};
        const QStringList titles{"My storage", "Other devices", "Guild", "Organization"};
        const QList<qreal> positions{12, 391, 530, 669};
        for (int i = 0; i < groups.size(); ++i) {
            auto *heading = visualItem(sidebar, "storageHeading" + groups[i]); QVERIFY(heading);
            QCOMPARE(heading->property("text").toString(), titles[i]);
            QTRY_COMPARE(heading->mapToItem(sidebar, QPointF()), QPointF(12, positions[i]));
            QCOMPARE(heading->height(), 11.0);
        }
        auto *models = visualItem(sidebar, "storageSectionmodels"); QVERIFY(models);
        QCOMPARE(models->size(), QSizeF(204, 32)); QVERIFY(models->property("selected").toBool());
        auto *phone = visualItem(sidebar, "storageTargetdevicesphone"); QVERIFY(phone);
        QCOMPARE(phone->property("iconName").toString(), QString("iPhoneDevice"));
        QVERIFY(QMetaObject::invokeMethod(phone, "clicked"));
        auto *panel = window->findChild<QObject *>("networkDevices"); QVERIFY(panel);
        QTRY_VERIFY(panel->property("opened").toBool());
        QCOMPARE(panel->property("selectedDeviceId").toString(), QString("phone"));
        QCOMPARE(panel->property("selectedDeviceName").toString(), QString("My love iPhone"));
        QVERIFY(!panel->property("selectedFilesVisible").toBool());
        QCOMPARE(drive->currentSection(), QString("Models"));
        QVERIFY(QMetaObject::invokeMethod(panel, "close")); QTRY_VERIFY(!panel->property("visible").toBool());
        QSignalSpy workspaces(navigation, &StorageNavigation::workspaceRequested);
        auto *organization = visualItem(sidebar, "storageTargetorganization2"); QVERIFY(organization);
        QVERIFY(QMetaObject::invokeMethod(organization, "clicked")); QCOMPARE(workspaces.size(), 1);
        QCOMPARE(workspaces.first(), QVariantList({"organization", "2", "Organization 2"}));
        auto *notice = window->findChild<QObject *>("dashboardNotice"); QVERIFY(notice);
        QVERIFY(notice->setProperty("open", false));
        const auto screenshot = qEnvironmentVariable("SOCIETY_SIDEBAR_SCREENSHOT_PATH");
        if (!screenshot.isEmpty()) { QTest::qWait(200); QVERIFY(window->grabWindow().save(screenshot)); }
        window->resize(1120, 420);
        auto *scroll = visualItem(sidebar, "storageSidebarScroll"); QVERIFY(scroll);
        QTRY_VERIFY(scroll->property("contentHeight").toReal() > scroll->height());
        const auto position = sidebar->mapToScene(QPointF(100, 180));
        QWheelEvent wheel(position, window->mapToGlobal(position.toPoint()), QPoint(), QPoint(0, -120),
            Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
        QCoreApplication::sendEvent(window, &wheel);
        QTRY_VERIFY(scroll->property("contentY").toReal() > 0);
        organization->forceActiveFocus(Qt::TabFocusReason);
        QTRY_VERIFY(organization->mapToItem(scroll, QPointF()).y() + organization->height() <= scroll->height() + 0.5);
        const auto offset = scroll->property("contentY").toReal();
        QVERIFY(navigation->replaceMemberships("figma-fixture", guilds, organizations));
        QCOMPARE(scroll->property("contentY").toReal(), offset);
        QCOMPARE(visualItem(sidebar, "storageTargetorganization2"), organization);
        navigation->setAccountId("");
        QTRY_VERIFY(visualItem(sidebar, "storageEmptyguild")->isVisible());
        QVERIFY(!visualItem(sidebar, "storageTargetorganization2"));
        QTRY_VERIFY(scroll->property("contentY").toReal() <= scroll->property("contentHeight").toReal() - scroll->height());
        auto *files = visualItem(sidebar, "storageSectionfiles"); QVERIFY(files);
        QVERIFY(QMetaObject::invokeMethod(files, "clicked"));
        QCOMPARE(drive->currentSection(), QString("Files")); QVERIFY(files->property("selected").toBool());
        QVERIFY(!models->property("selected").toBool());
        window->resize(390, 844); QTRY_VERIFY(!sidebar->isVisible());
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        window->close();
    }

    void modelCatalogUsesHostIdentificationBeforeLocalHeaders()
    {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/model-map-XXXXXX");
        const auto drive = iiSocietyContainer::SocietyDrive::create(fixture.path()); QVERIFY(drive);
        const auto modelPath = fixture.filePath("Models/Checkpoint/cached.safetensors");
        QVERIFY(writeCatalogModel(modelPath, {{"modelspec.title", "Payload header title"}}));
        iiSocietyContainer::StorageMap map(*drive);
        QVERIFY(map.publish({QJsonObject{{"path", "models/Checkpoint/cached.safetensors"}, {"kind", "file"},
            {"size", QString::number(QFileInfo(modelPath).size())}, {"resident", true}, {"version", QString(64, 'a')}}}));
        StorageModels catalog; catalog.setDirectory(fixture.filePath("Models"));
        QTRY_VERIFY(!catalog.loading());
        QCOMPARE(catalog.count(), 1);
        const auto row = catalog.groups().value("image").toList().first().toMap();
        QCOMPARE(row.value("name").toString(), "cached.safetensors");
        QVERIFY(map.pendingRequests().isEmpty());
    }

    void modelCatalogGroupsRealMetadataAndWatchesChanges()
    {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/model-catalog-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        for (const auto *modality : {"image", "video", "audio", "language"})
            QVERIFY(writeCatalogModel(fixture.filePath(QString("Models/Other/%1.safetensors").arg(modality)),
                {{"society.modality", modality}, {"modelspec.architecture", "SDXL"}}));
        QVERIFY(writeCatalogModel(fixture.filePath("Models/Checkpoint/known.safetensors"), {}));
        QVERIFY(writeCatalogModel(fixture.filePath("Models/Other/unknown.safetensors"), {}));
        for (const auto &pair : QList<QPair<QString, QString>>{{"video", "StableVideoDiffusionPipeline"},
                {"audio", "WhisperForConditionalGeneration"}, {"language", "LlamaForCausalLM"}})
            QVERIFY(writeCatalogModel(fixture.filePath("Models/Other/" + pair.first + "-inferred.safetensors"),
                {{"modelspec.architecture", pair.second}}));
        const auto outside = fixture.filePath("outside.safetensors");
        QVERIFY(writeCatalogModel(outside, {{"society.modality", "audio"}}));
        QVERIFY(QFile::link(outside, fixture.filePath("Models/escaped.safetensors")));
        StorageModels catalog;
        catalog.setDirectory(fixture.filePath("Models"));
        QTRY_VERIFY(!catalog.loading());
        QCOMPARE(catalog.groups().size(), 4);
        QCOMPARE(catalog.count(), 8); QCOMPARE(catalog.uncategorizedCount(), 1);
        QCOMPARE(catalog.groups().value("image").toList().size(), 2);
        QCOMPARE(catalog.groups().value("video").toList().size(), 2);
        QCOMPARE(catalog.groups().value("language").toList().size(), 2);
        const auto audio = catalog.groups().value("audio").toList().first().toMap();
        QCOMPARE(audio.value("precision").toString(), QString("FP16"));
        QCOMPARE(audio.value("format").toString(), QString("Safetensors"));
        QCOMPARE(audio.value("architecture").toString(), QString("SDXL"));
        QCOMPARE(audio.value("bytes").toLongLong(), QFileInfo(audio.value("path").toString()).size());
        QSignalSpy changes(&catalog, &StorageModels::modelsChanged);
        catalog.refresh(); QTRY_VERIFY(!catalog.loading()); QCOMPARE(changes.size(), 0);
        const auto newcomer = fixture.filePath("Models/Other/new.safetensors");
        QVERIFY(writeCatalogModel(newcomer, {{"society.modality", "audio"}}));
        QTRY_COMPARE(catalog.groups().value("audio").toList().size(), 3);
        QVERIFY(QFile::remove(newcomer));
        QTRY_COMPARE(catalog.groups().value("audio").toList().size(), 2);
        catalog.refresh(); catalog.setDirectory("");
        QTRY_VERIFY(!catalog.loading()); QCOMPARE(catalog.count(), 0);
        QTest::qWait(200); QCOMPARE(catalog.count(), 0);
        catalog.setDirectory("relative"); QTRY_VERIFY(!catalog.loading());
        QVERIFY(!catalog.errorString().isEmpty()); QCOMPARE(catalog.count(), 0);
    }

    void modelsMatchFigmaAndScrollEachCategoryIndependently()
    {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/models-figma-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        const QStringList modalities{"image", "video", "audio", "language"};
        for (const auto &modality : modalities)
            for (int i = 0; i < 9; ++i)
                QVERIFY(writeCatalogModel(fixture.filePath(QString("Models/Checkpoint/%1-%2.safetensors").arg(modality).arg(i)),
                    {{"society.modality", modality}, {"modelspec.architecture", "SDXL"},
                     {"modelspec.title", QString("Studio Portrait %1").arg(i + 1, 2, 10, QChar('0'))}}));
        QQmlApplicationEngine engine;
        QStringList warnings;
        connect(&engine, &QQmlApplicationEngine::warnings, this, [&](const QList<QQmlError> &errors) {
            for (const auto &error : errors) warnings.append(error.toString());
        });
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        engine.setInitialProperties({{"initialContainerPath", fixture.path()}, {"width", 1440}, {"height", 900}});
        engine.load(QUrl::fromLocalFile(QString::fromUtf8(SOCIETY_QML_FILE)));
        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *drive = window->findChild<DriveController *>("driveController"); QVERIFY(drive);
        QVERIFY(window->setProperty("selectedTab", "Storage"));
        auto *navigation = visualItem(window->contentItem(), "storageSectionmodels"); QVERIFY(navigation);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
            navigation->mapToScene(QPointF(navigation->width()/2, navigation->height()/2)).toPoint());
        QTRY_COMPARE(drive->currentSection(), QString("Models"));
        auto *models = window->findChild<QQuickItem *>("modelsView"); QVERIFY(models);
        QTRY_VERIFY(models->isVisible());
        auto *catalog = window->findChild<StorageModels *>("modelCatalog"); QVERIFY(catalog);
        QTRY_COMPARE(catalog->count(), 36); QTRY_VERIFY(!catalog->loading());
        auto *sidebar = window->findChild<QQuickItem *>("driveSidebar"); QVERIFY(sidebar);
        QCOMPARE(sidebar->width(), 228.0); QCOMPARE(sidebar->mapToScene(QPointF()).y(), 56.0);
        QCOMPARE(navigation->height(), 32.0);
        QCOMPARE(navigation->width(), 204.0);
        QCOMPARE(navigation->mapToScene(QPointF()), QPointF(12, 247));
        QList<QQuickItem *> lists;
        for (int index = 0; index < modalities.size(); ++index) {
            const auto &modality = modalities[index];
            auto *list = visualItem(models, "modelCards" + modality); QVERIFY(list); lists.append(list);
            QTRY_COMPARE(list->property("count").toInt(), 9);
            QCOMPARE(list->size(), QSizeF(1164, 280));
            QCOMPARE(list->mapToScene(QPointF()), QPointF(252, 164 + index * 388));
            auto *card = visualItem(list, "modelCard" + modality + "0"); QVERIFY(card);
            QCOMPARE(card->size(), QSizeF(256, 280));
            QCOMPARE(card->property("rows").toList().size(), 3);
            QVERIFY(!card->property("showAction").toBool());
            auto *next = visualItem(list, "modelCard" + modality + "1"); QVERIFY(next);
            QCOMPARE(next->x() - card->x(), 264.0);
            auto *import = visualItem(models, "importModels" + modality); QVERIFY(import);
            QSignalSpy requests(models, SIGNAL(importRequested())); QVERIFY(requests.isValid());
            QVERIFY(QMetaObject::invokeMethod(import, "clicked")); QCOMPARE(requests.size(), 1);
            auto *dialog = window->findChild<QObject *>("modelFileDialog"); QVERIFY(dialog);
            QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
        }
        auto *card = visualItem(lists.first(), "modelCardimage0"); QVERIFY(card);
        auto *menu = window->findChild<QObject *>("modelCardMenu"); QVERIFY(menu);
        QSignalSpy menuOpened(menu, SIGNAL(opened())); QVERIFY(menuOpened.isValid());
        auto *touch = QTest::createTouchDevice();
        const auto touchPoint = card->mapToScene(QPointF(60, 80)).toPoint();
        QTest::touchEvent(window, touch).press(0, touchPoint, window);
        QTest::touchEvent(window, touch).release(0, touchPoint, window);
        QTest::qWait(100);
        QCOMPARE(menuOpened.size(), 0); // A touch tap is selection, not a mouse context click.
        QVERIFY(!menu->property("visible").toBool());
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, card->mapToScene(QPointF(60, 80)).toPoint());
        const auto selected = models->property("selectedPath").toString(); QVERIFY(!selected.isEmpty());
        QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier, card->mapToScene(QPointF(60, 80)).toPoint());
        QTRY_VERIFY(menu->property("opened").toBool());
        QCOMPARE(menu->property("filePath").toString(), selected);
        const auto actions = menu->property("items").value<QJSValue>().toVariant().toList();
        QCOMPARE(actions.size(), 9);
        QVERIFY(actions.at(8).toMap().value("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(menu, "close"));
        QTRY_VERIFY(!menu->property("visible").toBool());
        const auto position = lists.first()->mapToScene(QPointF(300, 180));
        QWheelEvent wheel(position, window->mapToGlobal(position.toPoint()), QPoint(), QPoint(-120, 0),
            Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
        QCoreApplication::sendEvent(window, &wheel);
        QTRY_VERIFY(lists.first()->property("contentX").toReal() > 0);
        const auto scroll = lists.first()->property("contentX").toReal();
        for (int i = 1; i < lists.size(); ++i) QCOMPARE(lists[i]->property("contentX").toReal(), 0.0);
        QVERIFY(writeCatalogModel(fixture.filePath("Models/Checkpoint/extra.safetensors"),
            {{"society.modality", "audio"}, {"modelspec.title", "Newest model"}}));
        QTRY_COMPARE(catalog->count(), 37);
        QTRY_COMPARE(models->property("selectedPath").toString(), selected);
        QTRY_COMPARE(lists.first()->property("contentX").toReal(), scroll);
        const auto screenshot = qEnvironmentVariable("SOCIETY_MODELS_SCREENSHOT_PATH");
        if (!screenshot.isEmpty()) { QTest::qWait(200); QVERIFY(window->grabWindow().save(screenshot)); }
        auto *scrollView = window->findChild<QQuickItem *>("modelsScroll"); QVERIFY(scrollView);
        auto *flickable = qvariant_cast<QQuickItem *>(scrollView->property("contentItem")); QVERIFY(flickable);
        QVERIFY(QMetaObject::invokeMethod(flickable, "flick", Q_ARG(qreal, 0), Q_ARG(qreal, -2000)));
        QTRY_VERIFY(flickable->property("contentY").toReal() > 0);
        if (!screenshot.isEmpty()) {
            QVERIFY(QMetaObject::invokeMethod(flickable, "cancelFlick"));
            QVERIFY(flickable->setProperty("contentY", flickable->property("contentHeight").toReal() - flickable->height()));
            QTest::qWait(200); QVERIFY(window->grabWindow().save(screenshot + "-lower.png"));
        }
        QVERIFY(QMetaObject::invokeMethod(flickable, "cancelFlick"));
        QVERIFY(flickable->setProperty("contentY", 300.0));
        QVERIFY(lists.first()->setProperty("contentX", scroll));
        QTest::qWait(100);
        QSignalSpy catalogChanges(catalog, &StorageModels::modelsChanged);
        catalog->refresh(); QTRY_VERIFY(!catalog->loading());
        QCOMPARE(catalogChanges.size(), 0);
        QCOMPARE(flickable->property("contentY").toReal(), 300.0);
        QCOMPARE(lists.first()->property("contentX").toReal(), scroll);
        // Inserting ahead of the focused card must retain its keyboard selection and both axes.
        const auto prepended = fixture.filePath("Models/Checkpoint/aaa-first.safetensors");
        QVERIFY(writeCatalogModel(prepended, {{"society.modality", "image"}, {"modelspec.title", "A first model"}}));
        catalog->refresh(); QTRY_COMPARE(catalog->count(), 38); QTRY_VERIFY(!catalog->loading());
        QTRY_COMPARE(lists.first()->property("currentIndex").toInt(), 1);
        QTRY_COMPARE(models->property("selectedPath").toString(), selected);
        QTRY_COMPARE(lists.first()->property("contentX").toReal(), scroll);
        QTRY_COMPARE(flickable->property("contentY").toReal(), 300.0);
        QVERIFY(QFile::remove(prepended)); catalog->refresh();
        QTRY_COMPARE(catalog->count(), 37); QTRY_VERIFY(!catalog->loading());
        QTRY_COMPARE(lists.first()->property("currentIndex").toInt(), 0);
        QTRY_COMPARE(lists.first()->property("contentX").toReal(), scroll);
        QTRY_COMPARE(flickable->property("contentY").toReal(), 300.0);
        window->resize(390, 844);
        QTRY_VERIFY(!sidebar->isVisible());
        for (auto *list : lists) QTRY_COMPARE(list->width(), 342.0);
        window->resize(1440, 900);
        QVERIFY(QMetaObject::invokeMethod(models, "browseFoldersRequested"));
        auto *files = window->findChild<QQuickItem *>("fileGridView"); QVERIFY(files);
        QTRY_VERIFY(files->isVisible()); QTRY_COMPARE(files->property("count").toInt(), 23);
        QVERIFY(drive->openSection("models")); QTRY_VERIFY(models->isVisible());
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        window->close();
    }
    void reloadAdoptedIdentityAndHideAnIncompleteMirror()
    {
        QTemporaryDir root(QStringLiteral(SOCIETY_TEST_DIRECTORY "/drive-mirror-XXXXXX"));
        QVERIFY(iiSocietyContainer::SocietyDrive::create(root.path()));
        DriveController drive; QVERIFY(drive.openContainer(root.path()));
        const auto old = drive.identifier(); const auto host = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QVERIFY(drive.openSection("files")); drive.setMirrorPending(true);
        QVERIFY(drive.hasDrive()); QVERIFY(!drive.contentsAvailable()); QVERIFY(!drive.openSection("models"));
        QVERIFY(iiSocietyContainer::SocietyDrive::adoptReplicaIdentity(root.path(), old, host));
        QVERIFY(drive.reloadFromDisk()); QCOMPARE(drive.identifier(), host); QVERIFY(drive.atRoot());
        QVERIFY(!drive.contentsAvailable()); QVERIFY(iiSocietyContainer::SocietyDrive::completeReplica(root.path(), host));
        drive.setMirrorPending(false); QVERIFY(drive.contentsAvailable());
        QVERIFY(drive.openSection("files"));
        QCOMPARE(iiSocietyContainer::SharedStorage::open()->drive().identifier(), host);
    }
    void synchronizationPreservesFileModelSelectionAndScroll()
    {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/drive-sync-view-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        for (int i = 0; i < 80; ++i) {
            QFile file(fixture.filePath(QString("Files/file-%1.txt").arg(i, 3, 10, QChar('0'))));
            QVERIFY(file.open(QIODevice::WriteOnly)); QVERIFY(file.write("unchanged") > 0);
        }
        QQmlApplicationEngine engine;
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        engine.setInitialProperties({{"initialContainerPath", fixture.path()}});
        engine.load(QUrl::fromLocalFile(QString::fromUtf8(SOCIETY_QML_FILE)));
        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        window->resize(390, 844); QVERIFY(window->setProperty("selectedTab", "Storage"));
        auto *drive = window->findChild<DriveController *>("driveController");
        auto *network = window->findChild<NetworkDriveController *>("networkDriveController");
        auto *files = window->findChild<QQuickItem *>("filesBrowser");
        auto *grid = files ? files->findChild<QQuickItem *>("filesTableScroll") : nullptr;
        QVERIFY(drive && network && files && grid); QVERIFY(drive->openSection("files"));
        QTRY_COMPARE(files->property("count").toInt(), 80);
        QTRY_VERIFY(!files->property("loading").toBool());
        QPointer<QAbstractItemModel> model = qvariant_cast<QAbstractItemModel *>(files->property("directoryModel")); QVERIFY(model);
        QVERIFY(QMetaObject::invokeMethod(files, "selectEntry", Q_ARG(QVariant, 30))); QVERIFY(grid->setProperty("contentY", 880.0));
        QTest::qWait(50);
        const auto selected = files->property("selectedPath").toString();
        const auto scroll = grid->property("contentY").toReal(); QVERIFY(scroll > 0); QVERIFY(!selected.isEmpty());
        QSignalSpy reset(model, &QAbstractItemModel::modelReset);
        for (int i = 0; i < 3; ++i) {
            QVERIFY(QMetaObject::invokeMethod(network, "mirrorChanged"));
            QVERIFY(QMetaObject::invokeMethod(network, "containerSynchronized", Q_ARG(QString, "host")));
            QTest::qWait(1050);
            QVERIFY2(model, "Periodic synchronization destroyed the active folder model.");
            QCOMPARE(qvariant_cast<QAbstractItemModel *>(files->property("directoryModel")), model.data());
            QCOMPARE(files->property("selectedPath").toString(), selected);
            QCOMPARE(grid->property("contentY").toReal(), scroll);
            QCOMPARE(drive->currentPath(), fixture.filePath("Files"));
        }
        QCOMPARE(reset.size(), 0);
        // Native directory watching must still publish real changes without replacing the model.
        QFile added(fixture.filePath("Files/zz-new.txt")); QVERIFY(added.open(QIODevice::WriteOnly));
        QVERIFY(added.write("new file") > 0); added.close();
        QTRY_COMPARE(files->property("count").toInt(), 81);
        QCOMPARE(qvariant_cast<QAbstractItemModel *>(files->property("directoryModel")), model.data());
        QTRY_COMPARE(files->property("selectedPath").toString(), selected);
        QTRY_COMPARE(grid->property("contentY").toReal(), scroll);
        QVERIFY(added.remove()); QTRY_COMPARE(files->property("count").toInt(), 80);
        QTRY_COMPARE(files->property("selectedPath").toString(), selected);
        QTRY_COMPARE(grid->property("contentY").toReal(), scroll);
        window->close();
    }
    void backgroundRefreshRejectsStaleRootsAndNavigation()
    {
        QTemporaryDir first(SOCIETY_TEST_DIRECTORY "/refresh-first-XXXXXX"), second(SOCIETY_TEST_DIRECTORY "/refresh-second-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(first.path()));
        QVERIFY(iiSocietyContainer::SocietyDrive::create(second.path()));
        DriveController drive; QVERIFY(drive.openContainer(first.path()));
        const auto original = drive.identifier();
        const auto adopted = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QVERIFY(iiSocietyContainer::SocietyDrive::adoptReplicaIdentity(first.path(), original, adopted));
        auto *pool = QThreadPool::globalInstance(); const auto maximum = pool->maxThreadCount();
        QSemaphore entered, release;
        pool->setMaxThreadCount(1);
        const auto cleanup = qScopeGuard([&] { release.release(); pool->waitForDone(); pool->setMaxThreadCount(maximum); });
        const auto blockWorker = [&] { pool->start([&] { entered.release(); release.acquire(); }); return entered.tryAcquire(1, 3000); };
        QVERIFY(blockWorker());
        drive.refreshFromDisk();
        auto *watcher = drive.findChild<QFutureWatcherBase *>(); QVERIFY(watcher);
        QSignalSpy finished(watcher, &QFutureWatcherBase::finished);
        for (int i = 0; i < 4; ++i) drive.refreshFromDisk();
        QCOMPARE(drive.findChildren<QFutureWatcherBase *>().size(), 1); // Busy requests coalesce.
        bool responsive = false; QTimer::singleShot(0, &drive, [&] { responsive = true; });
        QTRY_VERIFY(responsive); QCOMPARE(drive.identifier(), original); // The UI did not perform the blocked read.
        QVERIFY(drive.openContainer(second.path())); const auto selected = drive.identifier();
        release.release(); QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(drive.rootPath(), second.path()); QCOMPARE(drive.identifier(), selected);
        QCOMPARE(iiSocietyContainer::SharedStorage::open()->drive().rootPath(), second.path());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

        const auto oldPath = second.filePath("Files/Old"), newPath = second.filePath("Files/New");
        QVERIFY(QDir().mkpath(oldPath)); QVERIFY(QDir().mkpath(newPath)); QVERIFY(drive.navigate(oldPath));
        QVERIFY(blockWorker()); drive.refreshFromDisk();
        watcher = drive.findChild<QFutureWatcherBase *>(); QVERIFY(watcher);
        QSignalSpy navigatedRead(watcher, &QFutureWatcherBase::finished);
        QVERIFY(QDir().rmdir(oldPath)); QVERIFY(drive.navigate(newPath));
        release.release(); QTRY_COMPARE(navigatedRead.size(), 1);
        QCOMPARE(drive.currentPath(), newPath);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

        QVERIFY(!drive.navigate(second.filePath("Files/Missing")));
        const auto navigationError = drive.errorString(); QVERIFY(!navigationError.isEmpty());
        QSignalSpy contents(&drive, &DriveController::contentsChanged), location(&drive, &DriveController::locationChanged);
        QSignalSpy errors(&drive, &DriveController::errorChanged);
        drive.refreshFromDisk(); watcher = drive.findChild<QFutureWatcherBase *>(); QVERIFY(watcher);
        QSignalSpy unchangedRead(watcher, &QFutureWatcherBase::finished); QTRY_COMPARE(unchangedRead.size(), 1);
        QCOMPARE(contents.size(), 0); QCOMPARE(location.size(), 0);
        QCOMPARE(drive.errorString(), navigationError); QCOMPARE(errors.size(), 0);
    }
    void unchangedDashboardSnapshotDoesNotReplaceRows()
    {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/dashboard-stable-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        QFile file(fixture.filePath("Files/item.txt")); QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.write("stable") > 0); file.close();
        DashboardFiles files; files.setContainerPath(fixture.path()); QTRY_VERIFY(!files.loading());
        const auto rows = files.recentFiles(); QSignalSpy changed(&files, &DashboardFiles::filesChanged);
        files.refresh(); QTRY_VERIFY(!files.loading()); QCOMPARE(changed.size(), 0); QCOMPARE(files.recentFiles(), rows);
        QVERIFY(file.remove()); files.refresh(); QTRY_VERIFY(!files.loading());
        QCOMPARE(changed.size(), 1); QVERIFY(files.recentFiles().isEmpty());
    }
    void controllerLayoutAndNavigation()
    {
        QTemporaryDir fixture(QStringLiteral(SOCIETY_TEST_DIRECTORY "/drive-core-XXXXXX"));
        QVERIFY(fixture.isValid());
        DriveController controller;
        QVERIFY(!controller.hasDrive());
        controller.openDefaultContainer();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 60000);
        QVERIFY(!controller.hasDrive());
        QVERIFY(controller.sections().isEmpty());
        QVERIFY(!controller.openContainer(""));
        QVERIFY(!controller.openContainer("relative"));
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        QVERIFY(controller.openContainer(fixture.path()));
        const auto shared = iiSocietyContainer::SharedStorage::open();
        QVERIFY(shared);
        QCOMPARE(shared->drive().identifier(), controller.identifier());
        DriveController reopened;
        QVERIFY(reopened.openContainer(fixture.path()));
        QCOMPARE(reopened.identifier(), controller.identifier());
        QCOMPARE(controller.sections().size(), 9);
        const auto identifier = controller.identifier();
        QVERIFY(!identifier.isEmpty());
        QCOMPARE(controller.breadcrumbs().first().toMap().value("name").toString(), QString("Society"));
        QVERIFY(controller.atRoot());
        for (const auto section : iiSocietyContainer::allStoreSections()) {
            QVERIFY(controller.openSection(iiSocietyContainer::storeSectionKey(section)));
            QCOMPARE(controller.currentSection(), iiSocietyContainer::storeSectionName(section));
            QCOMPARE(controller.breadcrumbs().size(), 2);
            const auto nested = fixture.filePath(iiSocietyContainer::storeSectionName(section) + "/App Access");
            QVERIFY(QDir().mkpath(nested));
            if (section == iiSocietyContainer::StoreSection::GenerationHistory) {
                QVERIFY(!controller.navigate(nested));
                QCOMPARE(controller.currentPath(), fixture.filePath("Generation History"));
            } else {
                QVERIFY(controller.navigate(nested));
                QCOMPARE(controller.currentPath(), nested);
            }
            QCOMPARE(controller.currentSection(), iiSocietyContainer::storeSectionName(section));
            controller.goHome();
            QVERIFY(controller.atRoot());
        }
        QVERIFY(QDir().mkpath(fixture.filePath("Files/Nested/Deep")));
        QVERIFY(controller.navigate(fixture.filePath("Files/Nested/Deep")));
        QCOMPARE(controller.breadcrumbs().size(), 4);
        controller.goUp();
        QCOMPARE(controller.currentPath(), fixture.filePath("Files/Nested"));
        QVERIFY(!controller.navigate(QFileInfo(fixture.path()).dir().path()));
        QCOMPARE(controller.currentPath(), fixture.filePath("Files/Nested"));
        QVERIFY(QDir().mkdir(fixture.filePath("Unassigned")));
        QVERIFY(!controller.navigate(fixture.filePath("Unassigned")));
        QVERIFY(!controller.openSection("unknown"));
        QVERIFY(!controller.openFile(fixture.filePath("Unassigned")));
        QVERIFY(controller.openContainer(fixture.path()));
        QCOMPARE(controller.identifier(), identifier);
        QVERIFY(controller.atRoot());
        QVERIFY(QDir(fixture.filePath("Models")).removeRecursively());
        QVERIFY(!controller.openSection("files"));
        QVERIFY(!controller.errorString().isEmpty());
    }

    void rejectsConflictsWithoutChangingTheCurrentDrive()
    {
        QTemporaryDir first(QStringLiteral(SOCIETY_TEST_DIRECTORY "/drive-first-XXXXXX"));
        QTemporaryDir second(QStringLiteral(SOCIETY_TEST_DIRECTORY "/drive-conflict-XXXXXX"));
        QVERIFY(first.isValid() && second.isValid());
        QFile conflict(second.filePath("Models"));
        QVERIFY(conflict.open(QIODevice::WriteOnly));
        conflict.write("preserve");
        conflict.close();
        DriveController controller;
        QVERIFY(iiSocietyContainer::SocietyDrive::create(first.path()));
        QVERIFY(controller.openContainer(first.path()));
        const auto id = controller.identifier();
        QVERIFY(!controller.openContainer(second.path()));
        QCOMPARE(controller.identifier(), id);
        QVERIFY(!QFileInfo::exists(second.filePath("Files")));
        QVERIFY(conflict.open(QIODevice::ReadOnly));
        QCOMPARE(conflict.readAll(), QByteArray("preserve"));
    }

    void rejectsPublicFilesAsANewContainer()
    {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/drive-public-source-XXXXXX");
        QVERIFY(fixture.isValid());
        DriveController controller;
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        QVERIFY(controller.openContainer(fixture.path()));
        const auto identifier = controller.identifier();
        QVERIFY(!controller.openContainer(fixture.filePath("Files")));
        QVERIFY(!controller.errorString().isEmpty());
        QCOMPARE(controller.identifier(), identifier);
        QCOMPARE(controller.rootPath(), fixture.path());
        QCOMPARE(QDir(fixture.filePath("Files")).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot, QDir::Name),
            QStringList{});
        const auto shared = iiSocietyContainer::SharedStorage::open();
        QVERIFY(shared);
        QCOMPARE(shared->drive().rootPath(), fixture.path());
    }

    void storageTabOpensFilesDirectly_data()
    {
        QTest::addColumn<bool>("mobile");
        QTest::newRow("desktop") << false;
        QTest::newRow("mobile") << true;
    }

    void storageTabOpensFilesDirectly()
    {
        QFETCH(bool, mobile);
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/storage-entry-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        QVERIFY(QDir().mkpath(fixture.filePath("Files/Nested")));
        QQmlApplicationEngine engine;
        QStringList warnings;
        connect(&engine, &QQmlApplicationEngine::warnings, this, [&](const QList<QQmlError> &errors) {
            for (const auto &error : errors) warnings.append(error.toString());
        });
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        engine.setInitialProperties({{"initialContainerPath", fixture.path()}, {"mobileLayout", mobile},
            {"width", mobile ? 390 : 1440}, {"height", mobile ? 844 : 900}});
        engine.load(QUrl::fromLocalFile(QString::fromUtf8(SOCIETY_QML_FILE)));
        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *drive = window->findChild<DriveController *>("driveController"); QVERIFY(drive);
        auto *files = window->findChild<QQuickItem *>("filesBrowser"); QVERIFY(files);
        auto *navigation = window->findChild<StorageNavigation *>("storageNavigation"); QVERIFY(navigation);
        auto *storageTab = visualItem(window->contentItem(), mobile ? "mobileStorageTab" : "storageTab");
        auto *dashboardTab = visualItem(window->contentItem(), mobile ? "mobileDashboardTab" : "dashboardTab");
        QVERIFY(storageTab && dashboardTab);
        const auto click = [&](QQuickItem *item) {
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                item->mapToScene(item->boundingRect().center()).toPoint());
        };

        click(storageTab);
        QTRY_COMPARE(drive->currentPath(), fixture.filePath("Files"));
        QTRY_VERIFY(files->isVisible());
        QCOMPARE(navigation->selectedSection(), QString("files"));
        QVERIFY(!window->findChild<QQuickItem *>("sectionsGrid"));

        // Both a repeated click and returning from another tab start at Files.
        QVERIFY(drive->openSection("models"));
        click(storageTab);
        QTRY_COMPARE(drive->currentPath(), fixture.filePath("Files"));
        QVERIFY(drive->navigate(fixture.filePath("Files/Nested")));
        click(dashboardTab);
        click(storageTab);
        QTRY_COMPARE(drive->currentPath(), fixture.filePath("Files"));

        // Explicit destinations still open their requested section.
        click(dashboardTab);
        QVERIFY(QMetaObject::invokeMethod(window, "showStorage", Q_ARG(QVariant, "photos")));
        QTRY_COMPARE(drive->currentPath(), fixture.filePath("Photos"));
        auto *photos = window->findChild<QQuickItem *>("photosView"); QVERIFY(photos);
        QTRY_VERIFY(photos->isVisible());
        drive->goUp();
        QTRY_COMPARE(drive->currentPath(), fixture.filePath("Files"));
        QTRY_VERIFY(files->isVisible());

        // A mirror completing after entry must also land on Files.
        drive->setMirrorPending(true);
        drive->goHome();
        QVERIFY(!files->isVisible());
        drive->setMirrorPending(false);
        QTRY_COMPARE(drive->currentPath(), fixture.filePath("Files"));
        QTRY_VERIFY(files->isVisible());
        QCOMPARE(navigation->selectedSection(), QString("files"));
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        window->close();
    }

    void driveWindowOpensFilesAndNavigatesSections()
    {
        QTemporaryDir fixture(QStringLiteral(SOCIETY_TEST_DIRECTORY "/drive-gui-XXXXXX"));
        QVERIFY(fixture.isValid());
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        QVERIFY(QDir().mkpath(fixture.filePath("Files/Nested")));
        QFile file(fixture.filePath("Files/Example.txt"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("example");
        file.close();
        QFile privateFile(fixture.filePath("Models/App only.txt"));
        QVERIFY(privateFile.open(QIODevice::WriteOnly));
        privateFile.write("Available through Society");
        privateFile.close();

        QQmlApplicationEngine engine;
        QStringList warnings;
        connect(&engine, &QQmlApplicationEngine::warnings, this, [&](const QList<QQmlError> &errors) {
            for (const auto &error : errors)
                warnings.append(error.toString());
        });
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        engine.setInitialProperties({{"initialContainerPath", fixture.path()}});
        engine.load(QUrl::fromLocalFile(QString::fromUtf8(SOCIETY_QML_FILE)));
        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        QCOMPARE(window->title(), QString("Society"));
        auto *driveTitle = window->findChild<QQuickItem *>("societyDriveTitle");
        auto *driveHome = visualItem(window->contentItem(), "storageSectionfiles");
        QVERIFY(driveTitle && driveHome);
        QCOMPARE(driveTitle->property("text").toString(), QString("Society"));
        QCOMPARE(driveHome->property("label").toString(), QString("Files"));
        auto *controller = window->findChild<DriveController *>("driveController");
        auto *files = window->findChild<QQuickItem *>("filesBrowser");
        auto *content = window->findChild<QQuickItem *>("driveContent");
        QVERIFY(controller && files && content);
        QVERIFY(!window->findChild<QQuickItem *>("sectionsGrid"));
        QTRY_VERIFY(window->isVisible());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QCOMPARE(controller->sections().size(), 9);
        auto *storageTab = window->findChild<QQuickItem *>("storageTab");
        QVERIFY(storageTab);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
            storageTab->mapToScene(QPointF(storageTab->width()/2, storageTab->height()/2)).toPoint());
        QTRY_COMPARE(window->property("selectedTab").toString(), QString("Storage"));
        QTRY_COMPARE(controller->currentPath(), fixture.filePath("Files"));
        QTRY_VERIFY(files->isVisible());
        auto *storage = window->findChild<QQuickItem *>("storageView");
        auto *progress = window->findChild<QQuickItem *>("mirrorProgress");
        QVERIFY(storage && progress);
        QVERIFY(storage->setProperty("synchronizationStatus", "Syncing test-model.safetensors (42%)…"));
        controller->setMirrorPending(true);
        QTRY_VERIFY(progress->isVisible()); QVERIFY(!files->isVisible());
        QCOMPARE(progress->property("text").toString(), QString("Syncing test-model.safetensors (42%)…"));
        controller->setMirrorPending(false); QTRY_VERIFY(files->isVisible()); QVERIFY(!progress->isVisible());
        QTRY_VERIFY(!files->property("loading").toBool());
        QVERIFY(QDir().mkpath(QStringLiteral(SOCIETY_TEST_DIRECTORY "/storage-files-entry")));
        QVERIFY(window->grabWindow().save(QStringLiteral(SOCIETY_TEST_DIRECTORY "/storage-files-entry/files.png")));
        QTRY_COMPARE(files->property("count").toInt(), 2);
        QVERIFY(QMetaObject::invokeMethod(files, "activated", Q_ARG(QString, fixture.filePath("Files/Nested")), Q_ARG(bool, true)));
        QTRY_COMPARE(controller->currentPath(), fixture.filePath("Files/Nested"));
        QTest::keySequence(window, QKeySequence(QKeySequence::Back));
        QTRY_COMPARE(controller->currentPath(), fixture.filePath("Files"));
        QVERIFY(controller->openSection("models"));
        QTRY_COMPARE(files->property("path").toString(), QString());
        QTRY_COMPARE(files->property("count").toInt(), 0);
        QTRY_VERIFY(QFileInfo::exists(fixture.filePath("Models/Wildcards/App only.txt")));
        auto *models = window->findChild<QQuickItem *>("modelsView"); QVERIFY(models);
        QTRY_VERIFY(models->isVisible());
        QVERIFY(!files->isVisible());
        QVERIFY(QMetaObject::invokeMethod(models, "browseFoldersRequested"));
        auto *modelGrid = window->findChild<QQuickItem *>("fileGridView"); QVERIFY(modelGrid);
        QVERIFY(modelGrid->isVisible()); QVERIFY(!files->isVisible());
        QTRY_COMPARE(modelGrid->property("path").toString(), fixture.filePath("Models"));
        QTRY_COMPARE(modelGrid->property("count").toInt(), 23);
        const auto modelsScreenshot = qEnvironmentVariable("SOCIETY_MODEL_TYPES_SCREENSHOT_PATH");
        if (!modelsScreenshot.isEmpty()) { QTest::qWait(200); QVERIFY(window->grabWindow().save(modelsScreenshot)); }
        controller->goHome();
        QTRY_VERIFY(files->isVisible());
        QTRY_COMPARE(controller->currentPath(), fixture.filePath("Files"));
        for (const auto size : {QSize(390, 844), QSize(844, 390), QSize(360, 320), QSize(1120, 720)}) {
            window->resize(size);
            QTRY_COMPARE(window->size(), size);
            QTRY_VERIFY2(files->width() > 0 && files->height() > 0,
                qPrintable(QString("Window %1x%2 leaves a %3x%4 Files view")
                    .arg(size.width()).arg(size.height()).arg(files->width()).arg(files->height())));
            // Keep header and bottom actions inside the platform's usable area.
            const auto top = window->property("contentTopInset").toReal();
            const auto bottom = window->property("mobileSystemSafeBottomInset").toReal();
            const auto left = window->property("mobileSystemSafeLeftInset").toReal();
            const auto right = window->property("mobileSystemSafeRightInset").toReal();
            const auto toolbarHeight = window->findChild<QQuickItem *>("dashboardToolbar")->height();
            QTRY_COMPARE(content->mapToScene(QPointF()).y(), top + toolbarHeight);
            QTRY_COMPARE(content->mapToScene(QPointF()).x(), left);
            QTRY_COMPARE(content->width(), size.width() - left - right);
            QTRY_COMPARE(content->mapToScene(QPointF(0, content->height())).y(), size.height() - bottom);
        }
        // History shows completed images directly, including at phone width.
        QVERIFY(QDir().mkpath(fixture.filePath("Generation History/Legacy App")));
        QFile historyRecord(fixture.filePath("Generation History/request.json"));
        QVERIFY(historyRecord.open(QIODevice::WriteOnly));
        historyRecord.write("{}");
        historyRecord.close();
        QImage result(32, 32, QImage::Format_RGB32);
        result.fill(Qt::blue);
        QVERIFY(result.save(fixture.filePath("Generation History/result.PNG")));
        for (const auto size : {QSize(1120, 720), QSize(390, 844)}) {
            window->resize(size);
            QVERIFY(controller->openSection("generation-history"));
            QTRY_COMPARE(modelGrid->property("count").toInt(), 1);
            QVERIFY(modelGrid->isVisible()); QVERIFY(!files->isVisible());
            QVERIFY(!controller->navigate(fixture.filePath("Generation History/Legacy App")));
            QCOMPARE(controller->currentPath(), fixture.filePath("Generation History"));
            QVERIFY(controller->openSection("files"));
            QTRY_COMPARE(files->property("count").toInt(), 2);
            controller->goHome();
        }
        // Phone width retains every logical area even without the desktop sidebar.
        window->resize(QSize(390, 844));
        QTRY_VERIFY(!window->findChild<QQuickItem *>("driveSidebar")->isVisible());
        for (const auto section : iiSocietyContainer::allStoreSections()) {
            QVERIFY(controller->openSection(iiSocietyContainer::storeSectionKey(section)));
            QVERIFY(!visualItem(window->contentItem(), "driveUp"));
            auto *photos = visualItem(window->contentItem(), "photosView"); QVERIFY(photos);
            QTRY_VERIFY(section == iiSocietyContainer::StoreSection::Models ? models->isVisible()
                : section == iiSocietyContainer::StoreSection::Photos ? photos->isVisible()
                : section == iiSocietyContainer::StoreSection::Files ? files->isVisible() : modelGrid->isVisible());
            QCOMPARE(controller->currentSection(), iiSocietyContainer::storeSectionName(section));
            controller->goHome();
            QTRY_VERIFY(files->isVisible());
            QCOMPARE(controller->currentPath(), fixture.filePath("Files"));
        }
        const auto *connectButton = window->findChild<QQuickItem *>("connectToSystem");
        QVERIFY(connectButton);
        QCOMPARE(connectButton->property("text").toString(), QString("Connect to %1").arg(controller->systemName()));

        // OS URL drops target Models from Files and a phone-size header.
        QTemporaryDir dropped(SOCIETY_TEST_DIRECTORY "/model-drop-XXXXXX");
        QVERIFY(dropped.isValid());
        auto *importer = window->findChild<ModelImporter *>("modelImporter");
        auto *overlay = window->findChild<QQuickItem *>("modelDropOverlay");
        QVERIFY(importer && overlay);
        QSignalSpy imported(importer, &ModelImporter::finished);
        for (int index = 0; index < 3; ++index) {
            window->resize(index == 2 ? QSize(390, 844) : QSize(1120, 720));
            if (index == 1)
                QVERIFY(controller->openSection("files"));
            else
                controller->goHome();
            const auto name = QString("model-%1.safetensor").arg(index);
            QFile model(dropped.filePath(name));
            QVERIFY(model.open(QIODevice::WriteOnly));
            model.write("dragged model");
            model.close();
            QMimeData mime;
            mime.setUrls({QUrl::fromLocalFile(model.fileName())});
            const QPoint point = index == 2 ? QPoint(20, 30) : QPoint(360, 290);
            QDragEnterEvent enter(point, Qt::CopyAction | Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(window, &enter);
            QVERIFY(enter.isAccepted());
            QTRY_VERIFY(overlay->isVisible());
            QDropEvent drop(point, Qt::CopyAction | Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(window, &drop);
            QVERIFY(drop.isAccepted());
            QCOMPARE(drop.dropAction(), Qt::CopyAction);
            QTRY_COMPARE(imported.size(), index + 1);
            QTRY_COMPARE(controller->currentSection(), QString("Models"));
            QTRY_VERIFY(models->isVisible());
            QTRY_COMPARE(files->property("count").toInt(), 0);
            QTRY_VERIFY(!files->property("loading").toBool());
            QVERIFY(model.exists());
            QVERIFY(QFileInfo::exists(fixture.filePath("Models/Other/" + name)));
            QVERIFY(!QFileInfo::exists(fixture.filePath("Files/" + name)));
        }
        QMimeData unsupported;
        unsupported.setUrls({QUrl("https://example.com/weights.safetensor")});
        QDragEnterEvent rejected(QPoint(100, 100), Qt::CopyAction, &unsupported, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &rejected);
        QVERIFY(!rejected.isAccepted());
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        window->close();
    }

    void viewAllRecentFilesOpensStorageList()
    {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/recent-storage-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        const auto epoch = QDateTime::fromString("2026-01-01T00:00:00Z", Qt::ISODate);
        for (int i = 0; i < 80; ++i) {
            QFile file(fixture.filePath(QString("Files/photo-%1.txt").arg(79 - i, 2, 10, QChar('0'))));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QVERIFY(file.write("fixture") > 0); QVERIFY(file.flush());
            QVERIFY(file.setFileTime(epoch.addSecs(i), QFileDevice::FileModificationTime));
        }
        QQmlApplicationEngine engine;
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        engine.setInitialProperties({{"initialContainerPath", fixture.path()}});
        engine.load(QUrl::fromLocalFile(QString::fromUtf8(SOCIETY_QML_FILE)));
        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *all = visualItem(window->contentItem(), "viewAllRecentFiles"); QVERIFY(all);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
            all->mapToScene(QPointF(all->width()/2, all->height()/2)).toPoint());
        QTRY_COMPARE(window->property("selectedTab").toString(), QString("Storage"));
        auto *files = window->findChild<QQuickItem *>("filesBrowser"); QVERIFY(files);
        auto *grid = files->findChild<QQuickItem *>("filesTableScroll"); QVERIFY(grid);
        QTRY_COMPARE(files->property("path").toString(), fixture.filePath("Files"));
        QVERIFY(files->property("listMode").toBool());
        QTRY_COMPARE(files->property("count").toInt(), 80);
        QTRY_VERIFY(!files->property("loading").toBool());
        QTRY_VERIFY(grid->property("atYBeginning").toBool());
        QCOMPARE(grid->property("contentY").toReal(), 0.0);
        auto *model = qvariant_cast<QAbstractItemModel *>(files->property("directoryModel")); QVERIFY(model);
        const auto nameRole = model->roleNames().key("fileName", -1); QVERIFY(nameRole >= 0);
        QCOMPARE(model->data(model->index(0, 0), nameRole).toString(), QString("photo-00.txt"));
        QCOMPARE(model->data(model->index(79, 0), nameRole).toString(), QString("photo-79.txt"));
        const auto screenshot = qEnvironmentVariable("SOCIETY_RECENT_STORAGE_SCREENSHOT_PATH");
        if (!screenshot.isEmpty()) { QTest::qWait(200); QVERIFY(window->grabWindow().save(screenshot)); }
        window->close();
    }

    void dashboardFilesUseRealDriveDataAndRejectStaleRoots()
    {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/dashboard-files-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        QVERIFY(QDir().mkpath(fixture.filePath("Files/Work")));
        const auto write = [&](const QString &name, int age) {
            QFile file(fixture.filePath(name));
            if (!file.open(QIODevice::WriteOnly)) return false;
            if (file.write("fixture") < 0) return false;
            if (!file.flush()) return false;
            return file.setFileTime(QDateTime::currentDateTimeUtc().addSecs(-age), QFileDevice::FileModificationTime);
        };
        QVERIFY(write("Files/Work/earliest.txt", 900));
        QVERIFY(write("Files/latest.txt", 0));
        QVERIFY(write("Files/second.iisc", 60));
        QVERIFY(write("Files/third.txt", 120));
        QVERIFY(write("Files/Work/finished.PNG", 180));
        QVERIFY(write("Photos/photo-only.png", -60));
        QVERIFY(write("Models/model-only.safetensors", -60));
        QVERIFY(write("Deleted/deleted-only.txt", -60));
        QVERIFY(write("Asset Library/asset-only.png", -60));
        QVERIFY(write("Generation History/finished.PNG", 500));
        for (int i = 0; i < 14; ++i)
            QVERIFY(write(QString("Generation History/history-%1.png").arg(i), 560 + i * 60));
        QVERIFY(write("Generation History/request.json", 1));
        QVERIFY(QDir().mkpath(fixture.filePath("Generation History/Legacy")));
        QVERIFY(write("Generation History/Legacy/nested.png", 0));
        QTemporaryDir outside(SOCIETY_TEST_DIRECTORY "/dashboard-outside-XXXXXX");
        QFile secret(outside.filePath("outside.txt"));
        QVERIFY(secret.open(QIODevice::WriteOnly)); secret.write("outside"); secret.close();
        QVERIFY(QFile::link(secret.fileName(), fixture.filePath("Files/linked.txt")));
        QVERIFY(QFile::link(outside.path(), fixture.filePath("Files/Linked folder")));

        DashboardFiles files;
        files.refresh();
        QVERIFY(!files.loading()); QVERIFY(files.recentFiles().isEmpty());
        files.setContainerPath(fixture.path());
        QTRY_VERIFY(!files.loading());
        QVERIFY2(files.errorString().isEmpty(), qPrintable(files.errorString()));
        QCOMPARE(files.recentFiles().size(), 5);
        for (const auto &entry : files.recentFiles())
            QVERIFY(entry.toMap().value("path").toString().startsWith(fixture.filePath("Files/")));
        QCOMPARE(files.recentFiles().first().toMap().value("name").toString(), QString("latest.txt"));
        QVERIFY(files.recentFiles().first().toMap().value("previewSource").toUrl().isEmpty());
        QCOMPARE(files.generationHistory().size(), 15);
        QCOMPARE(files.generationHistory().first().toMap().value("name").toString(), QString("finished.PNG"));
        const auto image = files.generationHistory().first().toMap();
        QCOMPARE(image.value("previewSource").toUrl().toLocalFile(), fixture.filePath("Generation History/finished.PNG"));
        QCOMPARE(image.value("dateText").toString(), image.value("modified").toDateTime().toLocalTime().date().toString(Qt::ISODate));
        files.setQuery("finished.PNG");
        QCOMPARE(files.recentFiles().size(), 1);
        QCOMPARE(files.recentFiles().first().toMap().value("path").toString(), fixture.filePath("Files/Work/finished.PNG"));
        QCOMPARE(files.generationHistory().size(), 1);
        QCOMPARE(files.generationHistory().first().toMap().value("path").toString(), fixture.filePath("Generation History/finished.PNG"));
        for (const auto &query : {"photo-only", "model-only", "deleted-only", "asset-only"}) {
            files.setQuery(query);
            QVERIFY(files.recentFiles().isEmpty());
            QVERIFY(files.generationHistory().isEmpty());
        }
        files.setQuery("EARLIEST");
        QCOMPARE(files.recentFiles().size(), 1); // Search the snapshot, not just the visible recent cards.
        QCOMPARE(files.recentFiles().first().toMap().value("folderPath").toString(), fixture.filePath("Files/Work"));
        QVERIFY(files.generationHistory().isEmpty());
        files.setQuery("linked"); QVERIFY(files.recentFiles().isEmpty());
        files.setQuery("outside"); QVERIFY(files.recentFiles().isEmpty());
        files.setQuery("request.json"); QVERIFY(files.recentFiles().isEmpty());
        files.setQuery("nested.png"); QVERIFY(files.recentFiles().isEmpty());
        files.setQuery("");
        QVERIFY(QFile::remove(fixture.filePath("Files/latest.txt")));
        files.refresh(); QTRY_VERIFY(!files.loading());
        QCOMPARE(files.recentFiles().first().toMap().value("name").toString(), QString("second.iisc"));
        files.refresh();
        files.setContainerPath("");
        QTest::qWait(100);
        QVERIFY(!files.loading()); QVERIFY(files.recentFiles().isEmpty());
        files.setContainerPath("Files");
        QVERIFY(!files.errorString().isEmpty()); QVERIFY(files.recentFiles().isEmpty());
        files.setContainerPath(outside.path());
        QTRY_VERIFY(!files.loading());
        QVERIFY(!files.errorString().isEmpty()); QVERIFY(files.recentFiles().isEmpty());

        QTemporaryDir historyOnly(SOCIETY_TEST_DIRECTORY "/dashboard-history-only-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(historyOnly.path()));
        QFile generated(historyOnly.filePath("Generation History/generated.png"));
        QVERIFY(generated.open(QIODevice::WriteOnly)); generated.write("fixture"); generated.close();
        files.setContainerPath(historyOnly.path()); QTRY_VERIFY(!files.loading());
        QVERIFY(files.recentFiles().isEmpty());
        QCOMPARE(files.generationHistory().size(), 1);
    }

    void dashboardLoadsLocalFilesWhileNetworkMirrorIsPending()
    {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/dashboard-startup-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        QFile document(fixture.filePath("Files/Existing.txt"));
        QVERIFY(document.open(QIODevice::WriteOnly)); document.write("existing"); document.close();
        QImage image(16, 16, QImage::Format_RGB32); image.fill(Qt::cyan);
        QVERIFY(image.save(fixture.filePath("Generation History/Existing.png")));
        QQmlApplicationEngine engine;
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        engine.setInitialProperties({{"initialContainerPath", fixture.path()}, {"mobileLayout", true}});
        engine.load(QUrl::fromLocalFile(QString::fromUtf8(SOCIETY_QML_FILE)));
        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        auto *drive = window->findChild<DriveController *>("driveController"); QVERIFY(drive);
        auto *files = window->findChild<DashboardFiles *>("dashboardFiles"); QVERIFY(files);
        drive->setMirrorPending(true);
        QVERIFY(!drive->contentsAvailable());
        QTRY_VERIFY(!files->loading());
        QCOMPARE(files->containerPath(), drive->rootPath());
        QCOMPARE(files->recentFiles().size(), 1);
        QCOMPARE(files->generationHistory().size(), 1);
        auto *dashboard = window->findChild<QQuickItem *>("dashboardView"); QVERIFY(dashboard);
        QCOMPARE(qvariant_cast<DashboardFiles *>(dashboard->property("viewModel")), files);
        auto *recent = visualItem(dashboard, "dashboardRecentFilesCards"); QVERIFY(recent);
        auto *history = visualItem(dashboard, "dashboardGenerationHistoryCards"); QVERIFY(history);
        QTRY_COMPARE(recent->property("count").toInt(), 1);
        QTRY_COMPARE(history->property("count").toInt(), 1);
        auto *recentTitle = visualItem(dashboard, "dashboardRecentFilesTitle"); QVERIFY(recentTitle);
        auto *historyTitle = visualItem(dashboard, "dashboardGenerationHistoryTitle"); QVERIFY(historyTitle);
        QCOMPARE(recentTitle->property("text").toString(), QString("Recent files"));
        QCOMPARE(historyTitle->property("text").toString(), QString("Generate history"));
        window->close();
    }

    void dashboardCreationRefreshesTheInjectedViewModel()
    {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/dashboard-creation-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        DashboardFiles files;
        files.setContainerPath(fixture.path()); QTRY_VERIFY(!files.loading());
        QVERIFY(files.recentFiles().isEmpty());
        QQmlEngine engine;
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        QQmlComponent component(&engine, QUrl::fromLocalFile(
            QFileInfo(QString::fromUtf8(SOCIETY_QML_FILE)).dir().filePath("Dashboard/Dashboard.qml")));
        for (int i = 1; i <= 2; ++i) {
            QFile document(fixture.filePath(QString("Files/Launch-%1.txt").arg(i)));
            QVERIFY(document.open(QIODevice::WriteOnly)); document.write("existing at view creation"); document.close();
            QSignalSpy refreshed(&files, &DashboardFiles::loadingChanged);
            QScopedPointer<QObject> dashboard(component.createWithInitialProperties({
                {"width", 430}, {"height", 780}, {"viewModel", QVariant::fromValue(&files)}}));
            QVERIFY2(dashboard, qPrintable(component.errorString()));
            QVERIFY2(!refreshed.isEmpty(), "View creation must read the current store even when the model path has not changed");
            QTRY_VERIFY(!files.loading());
            QCOMPARE(files.recentFiles().size(), i);
            auto *list = visualItem(qobject_cast<QQuickItem *>(dashboard.data()), "dashboardRecentFilesCards");
            QVERIFY(list); QTRY_COMPARE(list->property("count").toInt(), i);
        }
    }

    void dashboardObservesLocalAdditionsEditsAndRemovals()
    {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/dashboard-watch-XXXXXX");
        const auto drive = iiSocietyContainer::SocietyDrive::create(fixture.path()); QVERIFY(drive);
        DashboardFiles files;
        files.setContainerPath(fixture.path()); QTRY_VERIFY(!files.loading());
        QImage image(16, 16, QImage::Format_RGB32); image.fill(Qt::cyan);
        const auto generated = fixture.filePath("Generation History/Generated.png");
        QVERIFY(image.save(generated));
        QVERIFY(QDir().mkpath(fixture.filePath("Files/Documents/New/Nested")));
        const auto path = fixture.filePath("Files/Documents/New/Nested/Added.txt");
        QFile document(path); QVERIFY(document.open(QIODevice::WriteOnly)); document.write("first"); document.close();
        QTRY_COMPARE(files.recentFiles().size(), 1);
        QTRY_COMPARE(files.generationHistory().size(), 1);
        const auto before = files.recentFiles().first().toMap();
        const auto beforePreview = files.generationHistory().first().toMap().value("previewSource");
        QVERIFY(document.open(QIODevice::WriteOnly)); document.write("a longer second revision");
        QVERIFY(document.setFileTime(QDateTime::currentDateTimeUtc().addSecs(10), QFileDevice::FileModificationTime));
        document.close();
        QTRY_VERIFY(files.recentFiles().first().toMap().value("metadata1") != before.value("metadata1"));
        QTRY_VERIFY(files.recentFiles().first().toMap().value("modified") != before.value("modified"));
        // Atomic replacement drops native file watches; subsequent edits must still arrive.
        QVERIFY(QFile::remove(generated)); image.fill(Qt::yellow); QVERIFY(image.save(generated));
        QFile replacement(generated); QVERIFY(replacement.open(QIODevice::ReadWrite));
        QVERIFY(replacement.setFileTime(QDateTime::currentDateTimeUtc().addSecs(20), QFileDevice::FileModificationTime));
        replacement.close();
        QTRY_VERIFY(files.generationHistory().size() == 1
            && files.generationHistory().first().toMap().value("previewSource") != beforePreview);
        const auto replacedPreview = files.generationHistory().first().toMap().value("previewSource");
        QVERIFY(replacement.open(QIODevice::ReadWrite));
        QVERIFY(replacement.setFileTime(QDateTime::currentDateTimeUtc().addSecs(30), QFileDevice::FileModificationTime));
        replacement.close();
        QTRY_VERIFY(files.generationHistory().first().toMap().value("previewSource") != replacedPreview);
        QVERIFY(QFile::remove(path)); QVERIFY(QFile::remove(generated));
        QTRY_VERIFY(files.recentFiles().isEmpty() && files.generationHistory().isEmpty());
        // Local replica readiness remains a boundary even though network readiness is not.
        const auto host = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QVERIFY(iiSocietyContainer::SocietyDrive::adoptReplicaIdentity(fixture.path(), drive->identifier(), host));
        QVERIFY(image.save(generated));
        files.refresh(); QTRY_VERIFY(!files.loading());
        QVERIFY(files.generationHistory().isEmpty());
        QVERIFY(iiSocietyContainer::SocietyDrive::completeReplica(fixture.path(), host));
        QTRY_COMPARE(files.generationHistory().size(), 1);
        files.setContainerPath("");
        QVERIFY(QFile::remove(generated)); QTest::qWait(300);
        QVERIFY(files.recentFiles().isEmpty() && files.generationHistory().isEmpty());
    }

    void dashboardListsShowAtMostTwentyFiles_data()
    {
        QTest::addColumn<bool>("mobile");
        QTest::newRow("desktop") << false;
        QTest::newRow("mobile") << true;
    }

    void dashboardListsShowAtMostTwentyFiles()
    {
        QFETCH(bool, mobile);
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/dashboard-limit-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        QImage preview(16, 16, QImage::Format_RGB32); preview.fill(Qt::darkCyan);
        const auto newest = QDateTime::currentDateTimeUtc();
        for (int i = 0; i < 25; ++i) {
            const auto suffix = QString("%1").arg(i, 2, 10, QLatin1Char('0'));
            QFile document(fixture.filePath("Files/file-" + suffix + ".txt"));
            QVERIFY(document.open(QIODevice::WriteOnly));
            QCOMPARE(document.write("fixture"), qint64(7)); QVERIFY(document.flush());
            QVERIFY(document.setFileTime(newest.addSecs(-100 - i), QFileDevice::FileModificationTime));
            const auto path = fixture.filePath("Generation History/image-" + suffix + ".png");
            QVERIFY(preview.save(path));
            QFile image(path); QVERIFY(image.open(QIODevice::ReadWrite));
            QVERIFY(image.setFileTime(newest.addSecs(-i), QFileDevice::FileModificationTime));
        }
        QQmlApplicationEngine engine;
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        engine.setInitialProperties({{"initialContainerPath", fixture.path()}, {"mobileLayout", mobile},
            {"desktopMinWidth", 320}, {"width", mobile ? 390 : 1440}, {"height", 844}});
        engine.load(QUrl::fromLocalFile(QString::fromUtf8(SOCIETY_QML_FILE)));
        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *files = window->findChild<DashboardFiles *>("dashboardFiles"); QVERIFY(files);
        QTRY_VERIFY(!files->loading());
        QCOMPARE(files->recentFiles().size(), 20);
        QCOMPARE(files->generationHistory().size(), 20);
        QCOMPARE(files->recentFiles().first().toMap().value("name").toString(), QString("file-00.txt"));
        QCOMPARE(files->recentFiles().last().toMap().value("name").toString(), QString("file-19.txt"));
        QCOMPARE(files->generationHistory().first().toMap().value("name").toString(), QString("image-00.png"));
        QCOMPARE(files->generationHistory().last().toMap().value("name").toString(), QString("image-19.png"));
        auto *recent = visualItem(window->contentItem(), "dashboardRecentFilesCards");
        auto *history = visualItem(window->contentItem(), "dashboardGenerationHistoryCards");
        QVERIFY(recent && history);
        for (auto *list : {recent, history}) {
            QTRY_COMPARE(list->property("count").toInt(), 20);
            list->forceActiveFocus(); QTest::keyClick(window, Qt::Key_End);
            QTRY_COMPARE(list->property("currentIndex").toInt(), 19);
            QTRY_VERIFY(list->property("contentX").toReal() > 0);
        }
        // Search the entire snapshot, including files outside the first 20 cards.
        files->setQuery("24");
        QTRY_COMPARE(recent->property("count").toInt(), 1);
        QTRY_COMPARE(history->property("count").toInt(), 1);
        QCOMPARE(files->recentFiles().first().toMap().value("name").toString(), QString("file-24.txt"));
        QCOMPARE(files->generationHistory().first().toMap().value("name").toString(), QString("image-24.png"));
        files->setQuery("image");
        QTRY_COMPARE(recent->property("count").toInt(), 0);
        QTRY_COMPARE(history->property("count").toInt(), 20);
        files->setQuery("file");
        QTRY_COMPARE(recent->property("count").toInt(), 20);
        QTRY_COMPARE(history->property("count").toInt(), 0);
        files->setQuery("");
        QTRY_COMPARE(recent->property("count").toInt(), 20);
        QTRY_COMPARE(history->property("count").toInt(), 20);
        window->close();
    }

    void dashboardCalendarMatchesFigmaAndNavigates()
    {
        using namespace iiCalendar;
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/calendar-dashboard-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        QImage preview(240, 160, QImage::Format_RGB32); preview.fill(QColor("#526757"));
        const auto reference = qEnvironmentVariable("SOCIETY_DASHBOARD_CARDS_PREVIEW_PATH");
        if (!reference.isEmpty()) QVERIFY(preview.load(reference));
        for (int i = 0; i < 6; ++i) QVERIFY(preview.save(fixture.filePath(QString("Files/Canvas %1.png").arg(i))));
        for (int i = 0; i < 11; ++i) QVERIFY(preview.save(fixture.filePath(QString("Generation History/Generated %1.png").arg(i))));
        QFile pdf(fixture.filePath("Files/Launch brief.pdf")); QVERIFY(pdf.open(QIODevice::WriteOnly)); pdf.write("fixture"); pdf.close();
        DashboardFiles files; files.setContainerPath(fixture.path()); QTRY_VERIFY(!files.loading());
        DashboardCalendar calendar;
        calendar.setTimeZone("Asia/Seoul"); calendar.selectDate("2026-09-19"); calendar.setContainerPath(fixture.path());
        QTRY_VERIFY(!calendar.loading());
        {
            Store store(calendar.databasePath().toStdString());
            const TimeZone zone("Asia/Seoul");
            for (int i = 0; i < 3; ++i) {
                Event event; event.id = "meeting-" + std::to_string(i);
                event.title = i == 0 ? "Product review" : i == 1 ? "Launch planning" : "Weekly wrap-up";
                event.location.name = i == 0 ? "Society" : "Workspace";
                const auto start = zone.resolve({Date(2026, 9, 19), Time(i == 0 ? 9 : i == 1 ? 14 : 18, i == 0 ? 30 : 0)});
                event.schedule = TimedSchedule{{start, start + Duration(i == 0 ? 2700 : 1800)}, "Asia/Seoul"};
                if (i == 0) for (int j = 0; j < 6; ++j)
                    event.attachments.push_back({"file-" + std::to_string(j), j == 0 ? "Launch brief.pdf" : "Research notes.pdf", "Files/Launch brief.pdf", "PDF", 248000});
                store.put(event);
            }
            for (int i = 0; i < 4; ++i) {
                Event task; task.id = "task-" + std::to_string(i); task.kind = EventKind::Task;
                task.title = i == 0 ? "Finalize launch checklist" : i == 1 ? "Review onboarding copy" : "Prepare release notes";
                task.due = zone.resolve({Date(2026, 9, 19), Time(17, i)});
                if (i % 2) { task.status = EventStatus::Completed; task.percentComplete = 100; }
                store.put(task);
            }
            for (int i = 0; i < 8; ++i) {
                Event event; event.id = "activity-" + std::to_string(i); event.kind = EventKind::Activity;
                event.title = "Updated launch notes"; event.description = "10:42 · Society / Planning";
                auto start = zone.resolve({Date(2026, 9, 19), Time(10, 42 + i)});
                event.schedule = TimedSchedule{{start, start}, "Asia/Seoul"}; store.put(event);
            }
        }
        calendar.refresh(); QTRY_VERIFY(!calendar.loading());
        QCOMPARE(calendar.summary(), "3 events · 2 open tasks · 6 files");
        QQmlEngine engine; engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&](const QList<QQmlError> &errors) {
            for (const auto &error : errors) warnings.append(error.toString());
        });
        QQmlComponent component(&engine, QUrl::fromLocalFile(QFileInfo(QStringLiteral(SOCIETY_QML_FILE)).dir().filePath("Dashboard/Dashboard.qml")));
        QQuickWindow window; window.setColor(QColor("#1e1e1e")); window.resize(1440, 1000);
        QScopedPointer<QObject> object(component.createWithInitialProperties({{"width", 1440}, {"height", 1000},
            {"viewModel", QVariant::fromValue(&files)}, {"calendarModel", QVariant::fromValue(&calendar)}}));
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *dashboard = qobject_cast<QQuickItem *>(object.get()); QVERIFY(dashboard);
        dashboard->setParentItem(window.contentItem()); window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
        const auto item = [&](const QString &name) { return visualItem(dashboard, name); };
        auto *month = item("calendarMonthPanel"), *detail = item("calendarDetailPanel"), *view = item("dashboardCalendarView");
        QVERIFY(month && detail && view);
        QTRY_COMPARE(month->size(), QSizeF(803, 370)); QTRY_COMPARE(detail->size(), QSizeF(355, 370));
        QCOMPARE(month->mapToItem(dashboard, QPointF()), QPointF(238, 34));
        QCOMPARE(detail->mapToItem(dashboard, QPointF()), QPointF(1051, 34));
        QVERIFY(item("calendarDot_2026-09-19")->isVisible());
        QCOMPARE(item("calendarDayTitle")->property("text").toString(), "Sat, Sep 19");
        for (int i = 1; i < 7; ++i) {
            auto *card = item("dashboardRecentFilesCard" + QString::number(i));
            QVERIFY(card); QTRY_COMPARE(card->property("previewStatus").toInt(), 1);
        }
        for (int i = 0; i < 8; ++i) {
            auto *card = item("dashboardGenerationHistoryCard" + QString::number(i));
            if (card) QTRY_COMPARE(card->property("previewStatus").toInt(), 1);
        }
        QTest::qWait(200); QVERIFY(window.grabWindow().save(QStringLiteral(SOCIETY_TEST_DIRECTORY "/calendar-dashboard.png")));
        const auto click = [&](const QString &name) {
            // Native rendering lays out recreated summary rows on the next frame.
            QTest::qWait(60);
            auto *button = item(name); if (!button) return false;
            QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, button->mapToScene(button->boundingRect().center()).toPoint());
            return true;
        };
        for (const QString section : {"events", "tasks", "activity", "files"}) {
            QVERIFY(click("calendarViewAll_" + section));
            QTRY_COMPARE(view->property("expandedSection").toString(), section);
            QVERIFY(click("calendarDetailBack")); QTRY_COMPARE(view->property("expandedSection").toString(), "");
        }
        QVERIFY(click("calendarTask_task-0")); QTRY_VERIFY(!calendar.loading());
        QTRY_COMPARE(calendar.completedTasks(), 3);
        QSignalSpy opened(dashboard, SIGNAL(fileRequested(QString)));
        QVERIFY(click("calendarEntry_file-0"));
        QCOMPARE(opened.size(), 1);
        QCOMPARE(opened.first().first().toString(), fixture.filePath("Files/Launch brief.pdf"));
        QCOMPARE(calendar.attachmentPath("Files/Launch brief.pdf"), fixture.filePath("Files/Launch brief.pdf"));
        QVERIFY(calendar.attachmentPath("../../outside.pdf").isEmpty());
        QVERIFY(calendar.attachmentPath("https://example.com/file.pdf").isEmpty());
        QVERIFY(click("calendarNextDay")); QTRY_VERIFY(!calendar.loading()); QCOMPARE(calendar.selectedDate(), "2026-09-20");
        QVERIFY(calendar.events().isEmpty());
        QVERIFY(click("calendarDay_2026-09-19")); QTRY_VERIFY(!calendar.loading()); QCOMPARE(calendar.events().size(), 3);
        QTest::qWait(50);
        QTest::keyClick(&window, Qt::Key_Right); QTRY_VERIFY(!calendar.loading()); QCOMPARE(calendar.selectedDate(), "2026-09-20");
        QTest::qWait(50);
        QTest::keyClick(&window, Qt::Key_Left); QTRY_VERIFY(!calendar.loading()); QCOMPARE(calendar.selectedDate(), "2026-09-19");
        QVERIFY(click("calendarNextMonth")); QTRY_VERIFY(!calendar.loading()); QCOMPARE(calendar.monthTitle(), "October 2026");
        QVERIFY(click("calendarPreviousMonth")); QTRY_VERIFY(!calendar.loading()); QCOMPARE(calendar.selectedDate(), "2026-09-19");
        QVERIFY(click("calendarToday")); QTRY_VERIFY(!calendar.loading());
        QCOMPARE(calendar.selectedDate(), QString::fromStdString(TimeZone("Asia/Seoul").at(Clock::sample().wallTime).local.date.toString()));
        calendar.selectDate("2026-09-19"); QTRY_VERIFY(!calendar.loading());
        for (int width : {390, 760}) {
            window.resize(width, 1000); dashboard->setWidth(width);
            QTRY_VERIFY(view->property("stacked").toBool());
            QTRY_COMPARE(month->width(), width - (width >= 760 ? 204 : 0) - 68.0);
            QCOMPARE(detail->width(), month->width());
            QVERIFY(detail->mapToItem(view, QPointF()).y() >= 380);
            QVERIFY(detail->mapToScene(QPointF(detail->width(), 0)).x() <= width);
        }
        window.resize(390, 1000); dashboard->setWidth(390); QTest::qWait(200);
        QVERIFY(window.grabWindow().save(QStringLiteral(SOCIETY_TEST_DIRECTORY "/calendar-dashboard-mobile.png")));
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        dashboard->setParentItem(nullptr);
    }

    void dashboardCardRowsMatchFigmaAndRemainInteractive()
    {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/dashboard-cards-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        QImage preview(96, 64, QImage::Format_RGB32);
        preview.fill(QColor("#316c98"));
        const auto previewPath = qEnvironmentVariable("SOCIETY_DASHBOARD_CARDS_PREVIEW_PATH");
        if (!previewPath.isEmpty()) QVERIFY(preview.load(previewPath));
        const auto seedRows = [&](const QString &directory, const QString &prefix, int count) {
            for (int i = 0; i < count; ++i) {
                const auto path = fixture.filePath(QString("%1/%2 # %3.png").arg(directory, prefix).arg(i));
                if (!preview.save(path)) return false;
                QFile file(path);
                if (!file.open(QIODevice::ReadWrite)
                    || !file.setFileTime(QDateTime(QDate(2026, 9, 13), QTime(12, 0)).addSecs(-i),
                        QFileDevice::FileModificationTime)) return false;
            }
            return true;
        };
        QVERIFY(seedRows("Files", "Recent", 6));
        QVERIFY(seedRows("Generation History", "Generated", 11));
        DashboardFiles files;
        files.setContainerPath(fixture.path()); QTRY_VERIFY(!files.loading());
        const auto recentFiles = files.recentFiles(), historyFiles = files.generationHistory();
        QCOMPARE(recentFiles.size(), 6); QCOMPARE(historyFiles.size(), 11);
        QQmlEngine engine;
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&](const QList<QQmlError> &errors) {
            for (const auto &error : errors) warnings.append(error.toString());
        });
        const auto dashboardPath = QFileInfo(QString::fromUtf8(SOCIETY_QML_FILE)).dir().filePath("Dashboard/Dashboard.qml");
        QQmlComponent component(&engine, QUrl::fromLocalFile(dashboardPath));
        QQuickWindow window;
        window.setColor(QColor("#1e1e1e"));
        window.resize(1440, 844);
        QScopedPointer<QObject> object(component.createWithInitialProperties({
            {"width", 1440}, {"height", 844}, {"viewModel", QVariant::fromValue(&files)}}));
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *dashboard = qobject_cast<QQuickItem *>(object.data());
        QVERIFY(dashboard);
        dashboard->setParentItem(window.contentItem());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *recent = visualItem(dashboard, "dashboardRecentFilesCards");
        auto *history = visualItem(dashboard, "dashboardGenerationHistoryCards");
        QTRY_VERIFY(recent && history);
        QTRY_COMPARE(recent->property("count").toInt(), 6);
        QTRY_COMPARE(history->property("count").toInt(), 11);
        QTRY_COMPARE(recent->size(), QSizeF(1188, 160));
        QTRY_COMPARE(recent->mapToItem(dashboard, QPointF()), QPointF(228, 462));
        QTRY_COMPARE(history->mapToItem(dashboard, QPointF()), QPointF(228, 680));
        QCOMPARE(recent->property("spacing").toReal(), 8.0);
        QVERIFY(recent->clip() && history->clip());
        auto *first = visualItem(recent, "dashboardRecentFilesCard0");
        auto *second = visualItem(recent, "dashboardRecentFilesCard1");
        QTRY_VERIFY(first && second);
        QCOMPARE(first->size(), QSizeF(140, 160));
        QCOMPARE(second->x() - first->x(), 148.0);
        QCOMPARE(first->property("variantName").toString(), QString("File"));
        QCOMPARE(first->property("size").toInt(), 0); // LV.Card.Small
        QCOMPARE(first->property("detail").toInt(), 0); // LV.Card.Brief
        QVERIFY(!first->property("selectable").toBool());
        QCOMPARE(first->property("metadata").toString(), QString("2026-09-13"));
        QCOMPARE(first->property("previewSource").toUrl(), recentFiles.first().toMap().value("previewSource").toUrl());
        QTRY_COMPARE(first->property("previewStatus").toInt(), 1); // Image.Ready
        auto *image = first->findChild<QQuickItem *>("card_previewImage");
        QVERIFY(image);
        QCOMPARE(image->size(), QSizeF(140, 160));
        QCOMPARE(image->property("fillMode").toInt(), 2); // PreserveAspectCrop
        const auto screenshot = qEnvironmentVariable("SOCIETY_DASHBOARD_CARDS_SCREENSHOT_PATH");
        if (!screenshot.isEmpty()) { QTest::qWait(200); QVERIFY(window.grabWindow().save(screenshot)); }

        QSignalSpy opened(dashboard, SIGNAL(fileRequested(QString)));
        QSignalSpy revealed(dashboard, SIGNAL(revealRequested(QString)));
        QVERIFY(opened.isValid() && revealed.isValid());
        const auto click = [&](QQuickItem *item, Qt::MouseButton button = Qt::LeftButton) {
            QTest::mouseClick(&window, button, Qt::NoModifier,
                item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
        };
        click(first);
        QCOMPARE(opened.size(), 1);
        QCOMPARE(opened.last().first().toString(), recentFiles.first().toMap().value("path").toString());
        QVERIFY(!first->property("selected").toBool());
        first->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(&window, Qt::Key_Space);
        QCOMPARE(opened.size(), 2);
        click(first, Qt::RightButton);
        auto *menu = dashboard->findChild<QObject *>("dashboardFileMenu");
        QVERIFY(menu);
        QTRY_VERIFY(menu->property("opened").toBool());
        QVERIFY(QMetaObject::invokeMethod(menu, "triggerEntry", Q_ARG(QVariant, 1)));
        QCOMPARE(revealed.size(), 1);
        QCOMPARE(revealed.first().first().toString(), fixture.filePath("Files"));
        QCOMPARE(opened.size(), 2);
        QTRY_VERIFY(!menu->property("visible").toBool());
        first->forceActiveFocus(Qt::TabFocusReason);
        auto *menuButton = first->findChild<QQuickItem *>("card_menu");
        QVERIFY(menuButton);
        QTRY_VERIFY(menuButton->isVisible());
        click(menuButton);
        QTRY_VERIFY(menu->property("opened").toBool());
        QVERIFY(QMetaObject::invokeMethod(menu, "triggerEntry", Q_ARG(QVariant, 0)));
        QCOMPARE(opened.size(), 3);
        QCOMPARE(opened.last().first().toString(), recentFiles.first().toMap().value("path").toString());
        QTRY_VERIFY(!menu->property("visible").toBool());

        // Touch keeps the desktop click action and exposes its context menu by holding.
        QVERIFY(dashboard->setProperty("touchNavigation", true));
        auto *touch = QTest::createTouchDevice();
        const auto cardPoint = first->mapToScene(QPointF(first->width()/2, first->height()/2)).toPoint();
        const auto priorOpened = opened.count();
        QTest::touchEvent(&window, touch).press(0, cardPoint, &window);
        QTest::touchEvent(&window, touch).release(0, cardPoint, &window);
        QTRY_COMPARE(opened.count(), priorOpened + 1);
        QTest::touchEvent(&window, touch).press(0, cardPoint, &window);
        QTest::qWait(QGuiApplication::styleHints()->mousePressAndHoldInterval() + 100);
        QTRY_VERIFY(menu->property("opened").toBool());
        QTest::touchEvent(&window, touch).release(0, cardPoint, &window);
        QCOMPARE(opened.count(), priorOpened + 1);
        QVERIFY(QMetaObject::invokeMethod(menu, "close"));
        QTRY_VERIFY(!menu->property("visible").toBool());
        QVERIFY(dashboard->setProperty("touchNavigation", false));
        opened.clear();

        history->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(&window, Qt::Key_End);
        QTRY_COMPARE(history->property("currentIndex").toInt(), 10);
        QTRY_VERIFY(history->property("contentX").toReal() > 0);
        QTest::keyClick(&window, Qt::Key_Return);
        QCOMPARE(opened.size(), 1);
        QCOMPARE(opened.last().first().toString(), historyFiles.last().toMap().value("path").toString());
        QTest::keyClick(&window, Qt::Key_Home);
        QTRY_COMPARE(history->property("contentX").toReal(), 0.0);
        QTest::keyClick(&window, Qt::Key_Right);
        QTRY_COMPARE(history->property("currentIndex").toInt(), 1);
        for (int width : {760, 390, 1440}) {
            window.resize(width, 844);
            dashboard->setWidth(width);
            const qreal contentWidth = width - (width >= 760 ? 204 : 0) - 48;
            QTRY_COMPARE(recent->size(), QSizeF(contentWidth, 160));
            QTRY_COMPARE(history->size(), QSizeF(contentWidth, 160));
            QVERIFY(recent->mapToItem(dashboard, QPointF(recent->width(), 0)).x() <= width - 24);
        }
        files.setContainerPath("");
        QTRY_COMPARE(recent->property("count").toInt(), 0);
        QTRY_VERIFY(!recent->isVisible() && !history->isVisible());
        auto *empty = visualItem(dashboard, "emptyRecentFiles");
        QVERIFY(empty && empty->isVisible());
        files.setQuery("missing");
        QCOMPARE(empty->property("label").toString(), QString("No matching files"));
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        window.close();
    }

    void mobileDashboardKeepsLogicalControlSizes()
    {
        QQmlEngine engine;
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        const auto path = QFileInfo(QString::fromUtf8(SOCIETY_QML_FILE)).dir().filePath("Dashboard/Dashboard.qml");
        QQmlComponent component(&engine, QUrl::fromLocalFile(path));
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/dashboard-mobile-size-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        QImage image(16, 16, QImage::Format_RGB32); image.fill(Qt::cyan);
        QVERIFY(image.save(fixture.filePath("Files/Image.png")));
        QVERIFY(image.save(fixture.filePath("Generation History/Image.png")));
        DashboardFiles files;
        files.setContainerPath(fixture.path()); QTRY_VERIFY(!files.loading());
        QScopedPointer<QObject> object(component.createWithInitialProperties({
            {"width", 430}, {"height", 780}, {"viewModel", QVariant::fromValue(&files)}}));
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *dashboard = qobject_cast<QQuickItem *>(object.data()); QVERIFY(dashboard);
        QQuickWindow window;
        window.resize(430, 780);
        dashboard->setParentItem(window.contentItem());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        const QStringList controls{"promptField", "mediaTypeButton", "aspectRatioButton",
            "generationCountButton", "generateButton", "viewAllRecentFiles"};
        QMap<QString, QSizeF> desktopSizes;
        for (const auto &name : controls) {
            auto *item = visualItem(dashboard, name); QVERIFY(item);
            QTRY_COMPARE(item->height(), 22.0);
            desktopSizes.insert(name, item->size());
        }
        auto *quick = visualItem(dashboard, "quickGenerate"); QVERIFY(quick);
        auto *recent = visualItem(dashboard, "dashboardRecentFilesCards"); QVERIFY(recent);
        auto *card = visualItem(dashboard, "dashboardRecentFilesCard0"); QTRY_VERIFY(card);
        const auto sectionGap = [&] {
            return recent->mapToScene(QPointF()).y() - quick->mapToScene(QPointF(0, quick->height())).y();
        };
        const qreal desktopSectionGap = sectionGap();
        auto *theme = engine.singletonInstance<QObject *>("LVRS", "Theme"); QVERIFY(theme);
        QVERIFY(theme->setProperty("targetOverride", "ios"));
        QVERIFY(dashboard->setProperty("touchNavigation", true));
        for (const QSize size : {QSize(430, 780), QSize(320, 440), QSize(932, 320)}) {
            window.resize(size);
            dashboard->setSize(size);
            const qreal inset = size.width() < 760 ? 16 : 24;
            QTRY_COMPARE(recent->width(), size.width() - (size.width() >= 760 ? 204 : 0) - inset * 2);
            QTest::qWait(50); // Let nested layouts settle after the src/platform/viewport change.
            for (const auto &name : controls) {
                auto *item = visualItem(dashboard, name); QVERIFY(item);
                QTRY_COMPARE(item->height(), desktopSizes.value(name).height());
                if (name != "promptField") QCOMPARE(item->width(), desktopSizes.value(name).width());
                const auto bounds = item->mapRectToScene(item->boundingRect());
                QCOMPARE(bounds.size(), item->size());
                QVERIFY2(bounds.left() >= 0 && bounds.right() <= size.width(), qPrintable(name));
            }
            QTRY_COMPARE(sectionGap(), desktopSectionGap);
            QCOMPARE(card->mapRectToScene(card->boundingRect()).size(), QSizeF(140, 160));
        }
        // Native-sized controls must still accept touch input and submit the same request.
        auto *prompt = visualItem(dashboard, "promptField");
        QVERIFY(prompt->setProperty("text", "A quiet landscape"));
        auto *generate = visualItem(dashboard, "generateButton");
        revealDashboardItem(dashboard, generate);
        QSignalSpy submitted(dashboard, SIGNAL(generateRequested(QString,QString,QString,int)));
        QVERIFY(submitted.isValid());
        auto *touch = QTest::createTouchDevice();
        const auto point = generate->mapToScene(QPointF(generate->width()/2, generate->height()/2)).toPoint();
        QTest::touchEvent(&window, touch).press(0, point, &window);
        QTest::touchEvent(&window, touch).release(0, point, &window);
        QTRY_COMPARE(submitted.count(), 1);
        QCOMPARE(submitted.first().first().toString(), QString("A quiet landscape"));
        dashboard->setParentItem(nullptr);
    }

    void mobileViewsShareDesktopContentAndKeepState_data()
    {
        QTest::addColumn<int>("tabPlatformStyle");
        QTest::newRow("ios") << 1;
        QTest::newRow("android") << 2;
    }

    void mobileViewsShareDesktopContentAndKeepState()
    {
        QFETCH(int, tabPlatformStyle);
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/mobile-views-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        QVERIFY(QDir().mkpath(fixture.filePath("Files/Work")));
        QImage sample(240, 160, QImage::Format_RGB32); sample.fill(Qt::darkCyan);
        QVERIFY(sample.save(fixture.filePath("Files/Mobile landscape.png")));
        QVERIFY(sample.save(fixture.filePath("Generation History/Mobile study.png")));
        QQmlApplicationEngine engine;
        QStringList warnings;
        connect(&engine, &QQmlApplicationEngine::warnings, this, [&](const QList<QQmlError> &errors) {
            for (const auto &error : errors) warnings.append(error.toString());
        });
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        engine.setInitialProperties({{"initialContainerPath", fixture.path()}, {"mobileLayout", true},
            {"desktopMinWidth", 320}, {"width", 390}, {"height", 844}});
        engine.load(QUrl::fromLocalFile(QString::fromUtf8(SOCIETY_QML_FILE)));
        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *view = window->findChild<QQuickItem *>("societyContent");
        auto *dashboard = window->findChild<QQuickItem *>("dashboardView");
        auto *tools = window->findChild<QQuickItem *>("toolsView");
        auto *storage = window->findChild<QQuickItem *>("storageView");
        auto *drive = window->findChild<DriveController *>("driveController");
        auto *files = window->findChild<DashboardFiles *>("dashboardFiles");
        auto *prompt = window->findChild<QQuickItem *>("promptField");
        auto *tabs = window->findChild<QQuickItem *>("mobileTabBar");
        QVERIFY(view && dashboard && tools && storage && drive && files && prompt && tabs);
        QVERIFY(tabs->setProperty("platformStyle", tabPlatformStyle));
        QTRY_COMPARE(tabs->property("count").toInt(), 5);
        QCOMPARE(tabs->property("autoSelect").toBool(), false);
        QCOMPARE(tabs->property("bottomSafeInset").toReal(), 0.0); // Main already owns the system safe area.
        auto *tabList = QAccessible::queryAccessibleInterface(tabs); QVERIFY(tabList);
        QCOMPARE(tabList->role(), QAccessible::PageTabList);
        for (const QString name : {"mobileNavigationToggle", "mobileSearchToggle", "mobileAccount"}) {
            auto *button = visualItem(window->contentItem(), name); QVERIFY(button);
            QTRY_COMPARE(button->size(), QSizeF(22, 22));
            QCOMPARE(button->mapRectToScene(button->boundingRect()).size(), QSizeF(22, 22));
            auto *icon = button->findChild<QQuickItem *>("iconButton_icon"); QVERIFY(icon);
            QTRY_COMPARE(icon->size(), QSizeF(18, 18));
        }
        QTRY_VERIFY(dashboard->isVisible());
        QCOMPARE(window->property("selectedTab").toString(), QString("Dashboard"));
        QTRY_COMPARE(files->containerPath(), fixture.path());
        QTRY_VERIFY(!files->loading() && !files->recentFiles().isEmpty() && !files->generationHistory().isEmpty());
        QCOMPARE(window->findChildren<QQuickItem *>("dashboardView").size(), 1);
        QCOMPARE(window->findChildren<QQuickItem *>("toolsView").size(), 1);
        QCOMPARE(window->findChildren<QQuickItem *>("storageView").size(), 1);
        auto *touch = QTest::createTouchDevice();
        const auto tap = [&](const QString &name) {
            // Settle the previous page's layout before hit-testing the next tab.
            QTest::qWait(60);
            auto *item = visualItem(window->contentItem(), name);
            if (!item || !item->isVisible()) return false;
            const auto p = item->mapToScene(QPointF(item->width()/2, item->height()/2)).toPoint();
            QTest::touchEvent(window, touch).press(0, p, window);
            QTest::qWait(60);
            QTest::touchEvent(window, touch).release(0, p, window);
            QCoreApplication::processEvents();
            return true;
        };
        QVERIFY(prompt->setProperty("text", "Keep this mobile prompt"));
        QVERIFY(tap("mobileToolsTab")); QTRY_VERIFY(tools->isVisible());
        QTRY_COMPARE(tabs->property("currentIndex").toInt(), 1);
        auto *toolsTab = visualItem(window->contentItem(), "mobileToolsTab"); QVERIFY(toolsTab);
        auto *accessibleTab = QAccessible::queryAccessibleInterface(toolsTab); QVERIFY(accessibleTab);
        QCOMPARE(accessibleTab->role(), QAccessible::PageTab);
        QTRY_VERIFY(accessibleTab->state().selected);
        auto *merge = window->findChild<QQuickItem *>("modelMergeTool"); QVERIFY(merge);
        QVERIFY(!merge->isVisible());
        QVERIFY(tap("toolCard-model-merge"));
        QTRY_VERIFY(merge->isVisible());
        QVERIFY(merge->setProperty("sharedWeight", "0.75"));
        QVERIFY(tap("mobileStorageTab")); QTRY_VERIFY(storage->isVisible());
        QVERIFY(drive->navigate(fixture.filePath("Files/Work")));
        QVERIFY(tap("mobileDashboardTab")); QTRY_VERIFY(dashboard->isVisible());
        QCOMPARE(prompt->property("text").toString(), QString("Keep this mobile prompt"));
        QVERIFY(tap("mobileToolsTab")); QTRY_VERIFY(tools->isVisible());
        QCOMPARE(merge->property("sharedWeight").toString(), QString("0.75"));
        QVERIFY(tap("mobileStorageTab")); QTRY_VERIFY(storage->isVisible());
        QCOMPARE(drive->currentPath(), fixture.filePath("Files"));
        QVERIFY(tap("mobileSearchToggle"));
        auto *search = window->findChild<QQuickItem *>("mobileSearch"); QVERIFY(search);
        QTRY_VERIFY(search->isVisible() && dashboard->isVisible());
        QTRY_COMPARE(search->height(), 22.0);
        QSignalSpy searchNavigation(view, SIGNAL(tabRequested(QString))); QVERIFY(searchNavigation.isValid());
        QVERIFY(search->setProperty("text", "Mobile"));
        QVERIFY(search->setProperty("text", "Mobile landscape"));
        QTRY_COMPARE(files->recentFiles().size(), 1);
        QCOMPARE(searchNavigation.count(), 0); // Typing must not navigate again and dismiss the keyboard.
        QCOMPARE(view->property("query").toString(), QString("Mobile landscape"));
        QVERIFY(search->setProperty("text", ""));
        QVERIFY(tap("mobileSearchToggle")); QTRY_VERIFY(!search->isVisible());
        QVERIFY(tap("mobileBrowseTab"));
        QCOMPARE(tabs->property("currentIndex").toInt(), 0); // A panel must not replace the selected screen.
        auto *devices = window->findChild<QObject *>("networkDevices"); QVERIFY(devices);
        QTRY_VERIFY(devices->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(devices, "close"));
        QTRY_VERIFY(!devices->property("visible").toBool());
        QVERIFY(tap("mobileEnvironmentTab"));
        QCOMPARE(tabs->property("currentIndex").toInt(), 0);
        auto *environment = window->findChild<QObject *>("mobileEnvironment"); QVERIFY(environment);
        QTRY_VERIFY(environment->property("visible").toBool());
        QVERIFY(!window->findChild<QQuickWindow *>("preferencesWindow"));
        QVERIFY(QMetaObject::invokeMethod(environment, "close"));
        QTRY_VERIFY(!environment->property("visible").toBool());
        QVERIFY(tap("mobileNavigationToggle"));
        auto *navigation = window->findChild<QObject *>("mobileNavigation"); QVERIFY(navigation);
        QTRY_VERIFY(navigation->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(navigation, "close"));
        QTRY_VERIFY(!navigation->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(tools, "goBack"));
        for (const QSize size : {QSize(320, 568), QSize(390, 844), QSize(844, 390), QSize(1024, 768)}) {
            window->resize(size);
            QTRY_COMPARE(window->size(), size);
            QTRY_COMPARE(view->width(), qreal(size.width()));
            QCOMPARE(tabs->isVisible(), size.width() < 760);
            QTest::qWait(60); // Let text and row layout settle before checking elision.
            if (tabs->isVisible()) {
                for (const QString name : {"mobileNavigationToggle", "mobileSearchToggle", "mobileAccount"}) {
                    auto *button = visualItem(window->contentItem(), name); QVERIFY(button);
                    QTRY_COMPARE(button->mapRectToScene(button->boundingRect()).size(), QSizeF(22, 22));
                }
                for (const QString label : {"Dashboard", "Tools", "Storage", "Browse", "Environment"}) {
                    auto *item = visualItem(window->contentItem(), "mobile" + label + "Label"); QVERIFY(item);
                    QTRY_VERIFY(!item->property("truncated").toBool());
                }
            } else {
                for (const QString name : {"dashboardTab", "toolsTab", "storageTab", "browseTab", "environmentTab",
                                           "dashboardAccount", "dashboardSearch"}) {
                    auto *control = visualItem(window->contentItem(), name); QVERIFY(control);
                    QTRY_COMPARE(control->mapRectToScene(control->boundingRect()).height(), 22.0);
                }
                auto *account = visualItem(window->contentItem(), "dashboardAccount");
                QCOMPARE(account->width(), 22.0);
            }
            for (const QString tab : {"Dashboard", "Tools", "Storage"}) {
                QVERIFY(window->setProperty("selectedTab", tab));
                QTest::qWait(40);
                const QStringList names = tab == "Dashboard"
                    ? QStringList{"viewAllRecentFiles"}
                    : tab == "Tools" ? QStringList{"promptField", "generateButton", "toolsCatalog", "toolsSearch", "toolCard-model-merge"}
                    : QStringList{"filesSearch", "filesBrowser"};
                for (const QString &name : names) {
                    auto *item = visualItem(window->contentItem(), name); QVERIFY2(item, qPrintable(name));
                    if (tab == "Dashboard") revealDashboardItem(dashboard, item);
                    if (tab == "Tools") {
                        auto *catalog = visualItem(tools, "toolsCatalog"); QVERIFY(catalog);
                        auto *flick = qvariant_cast<QQuickItem *>(catalog->property("contentItem")); QVERIFY(flick);
                        if (item != catalog) {
                            const auto y = item->mapToItem(flick, QPointF()).y() + flick->property("contentY").toReal();
                            const auto maximum = std::max(0.0, flick->property("contentHeight").toReal() - flick->height());
                            flick->setProperty("contentY", std::clamp(y - 16.0, 0.0, maximum));
                            QTest::qWait(60);
                        }
                    }
                    const auto bounds = item->mapRectToScene(item->boundingRect());
                    QVERIFY2(item->isVisible() && bounds.width() > 0 && bounds.height() > 0
                        && bounds.left() >= -0.5 && bounds.right() <= size.width() + 0.5
                        && bounds.top() >= 0 && bounds.bottom() <= size.height() + 0.5,
                        qPrintable(QString("%1 at %2x%3: %4,%5 %6x%7").arg(name).arg(size.width()).arg(size.height())
                            .arg(bounds.x()).arg(bounds.y()).arg(bounds.width()).arg(bounds.height())));
                }
                const auto output = qEnvironmentVariable("SOCIETY_MOBILE_SCREENSHOT_DIRECTORY");
                if (!output.isEmpty()) {
                    QVERIFY(QDir().mkpath(output));
                    QTest::mouseMove(window, QPoint(8, 8));
                    QTest::qWait(200);
                    QVERIFY(window->grabWindow().save(output + QString("/%1-%2-%3x%4.png")
                        .arg(QTest::currentDataTag(), tab).arg(size.width()).arg(size.height())));
                }
            }
        }
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        window->close();
    }

    void mobileStorageKeepsSectionsActionsAndGalleryReachable()
    {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/mobile-storage-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        QImage photo(90, 160, QImage::Format_RGB32); photo.fill(Qt::darkMagenta);
        QVERIFY(photo.save(fixture.filePath("Generation History/Portrait.png")));
        QQmlApplicationEngine engine;
        QStringList warnings;
        connect(&engine, &QQmlApplicationEngine::warnings, this, [&](const QList<QQmlError> &errors) {
            for (const auto &error : errors) warnings.append(error.toString());
        });
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        engine.setInitialProperties({{"initialContainerPath", fixture.path()}, {"mobileLayout", true},
            {"selectedTab", "Storage"}, {"desktopMinWidth", 320}, {"width", 320}, {"height", 568}});
        engine.load(QUrl::fromLocalFile(QString::fromUtf8(SOCIETY_QML_FILE)));
        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *drive = window->findChild<DriveController *>("driveController"); QVERIFY(drive);
        QVERIFY(!window->findChild<QQuickItem *>("sectionsGrid"));
        auto *browser = window->findChild<QQuickItem *>("filesBrowser"); QVERIFY(browser);
        QTRY_VERIFY(browser->isVisible());
        QTRY_COMPARE(drive->currentPath(), fixture.filePath("Files"));
        QCOMPARE(drive->sections().size(), 9);
        auto *actions = window->findChild<QObject *>("storageActions"); QVERIFY(actions);
        auto *toggle = visualItem(window->contentItem(), "storageActionsToggle"); QVERIFY(toggle);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
            toggle->mapToScene(QPointF(toggle->width()/2, toggle->height()/2)).toPoint());
        QTRY_VERIFY(actions->property("visible").toBool());
        auto *import = visualItem(window->contentItem(), "mobileImportModels"); QVERIFY(import);
        QTRY_VERIFY(import->isVisible() && import->height() >= 44);
        QVERIFY(QMetaObject::invokeMethod(actions, "close"));
        QTRY_VERIFY(!actions->property("visible").toBool());
        auto *navigationToggle = visualItem(window->contentItem(), "mobileNavigationToggle"); QVERIFY(navigationToggle);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
            navigationToggle->mapToScene(QPointF(navigationToggle->width()/2, navigationToggle->height()/2)).toPoint());
        auto *navigation = window->findChild<QObject *>("mobileNavigation"); QVERIFY(navigation);
        QTRY_VERIFY(navigation->property("opened").toBool());
        auto *photoSection = visualItem(window->contentItem(), "mobile_storageSectionphotos"); QVERIFY(photoSection);
        QVERIFY(photoSection->isVisible() && photoSection->height() >= 44);
        auto *touch = QTest::createTouchDevice();
        const auto photoPoint = photoSection->mapToScene(QPointF(photoSection->width()/2, photoSection->height()/2)).toPoint();
        QTest::touchEvent(window, touch).press(0, photoPoint, window);
        QTest::touchEvent(window, touch).release(0, photoPoint, window);
        QTRY_COMPARE(drive->currentPath(), fixture.filePath("Photos"));
        QTRY_VERIFY(!navigation->property("visible").toBool());
        QVERIFY(drive->openSection("generation-history"));
        auto *files = window->findChild<QQuickItem *>("fileGridView"); QVERIFY(files);
        auto *grid = files->findChild<QQuickItem *>("fileGrid"); QVERIFY(grid);
        QTRY_COMPARE(grid->property("count").toInt(), 1);
        QTRY_VERIFY(!files->property("loading").toBool() && !files->property("initialPositionPending").toBool());
        QVERIFY(files->property("imagesOnly").toBool());
        QTRY_VERIFY(files->height() > 350);
        QCOMPARE(grid->property("cellWidth").toReal(), grid->property("cellHeight").toReal());
        QVERIFY(drive->openSection("photos"));
        auto *photos = window->findChild<QQuickItem *>("photosView"); QVERIFY(photos);
        QTRY_VERIFY(photos->isVisible());
        auto *addPhotos = photos->findChild<QQuickItem *>("addPhotos"); QVERIFY(addPhotos);
        QVERIFY(addPhotos->height() >= 44);
        QCOMPARE(drive->currentPath(), fixture.filePath("Photos"));
        drive->goUp(); QTRY_VERIFY(browser->isVisible());
        QTRY_COMPARE(drive->currentPath(), fixture.filePath("Files"));
        QVERIFY(drive->openSection("models"));
        auto *models = window->findChild<QQuickItem *>("modelsView"); QVERIFY(models);
        QTRY_VERIFY(models->isVisible());
        QVERIFY(!visualItem(window->contentItem(), "driveUp"));
        QTest::keySequence(window, QKeySequence(QKeySequence::Back));
        QTRY_COMPARE(drive->currentPath(), fixture.filePath("Files"));
        window->resize(844, 390);
        auto *wideActions = visualItem(window->contentItem(), "wideStorageActionsToggle"); QVERIFY(wideActions);
        QTRY_VERIFY(wideActions->isVisible());
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
            wideActions->mapToScene(wideActions->boundingRect().center()).toPoint());
        QTRY_VERIFY(actions->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(actions, "close"));
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        window->close();
    }

    void windowUsesFrameworkChromeAndContent()
    {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/window-content-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        QQmlApplicationEngine engine;
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        engine.setInitialProperties({{"initialContainerPath", fixture.path()}});
        engine.load(QUrl::fromLocalFile(QString::fromUtf8(SOCIETY_QML_FILE)));
        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        QTRY_VERIFY(window->isVisible());
        QCOMPARE(window->property("primaryColor").value<QColor>(), QColor("#57965C"));
        auto *material = window->findChild<QQuickItem *>("applicationWindowMaterial");
        QVERIFY(material);
        QCOMPARE(material->property("primaryColor").value<QColor>(), QColor("#57965C"));
        QCOMPARE(material->property("color").value<QColor>(), QColor("#0B0B0B"));
        QCOMPARE(material->property("tintOpacity").toReal(), 0.5);
        QCOMPARE(material->property("intenseOpacity").toReal(), 0.0);
        QCOMPARE(material->property("faintOpacity").toReal(), 0.0);
        QCOMPARE(material->property("blurRadius").toReal(), 64.0);
        QVERIFY2(!window->flags().testFlag(Qt::FramelessWindowHint),
                 "The design must be app content inside a standard LVRS window.");
        QVERIFY(window->property("solidChrome").toBool());
        QVERIFY(window->property("windowChromeInteractionsEnabled").toBool());
        auto *view = window->findChild<QQuickItem *>("societyContent");
        QVERIFY(view);
        QQmlListReference content(window, "content");
        QVERIFY(content.isValid());
        bool viewMounted = false;
        for (qsizetype i = 0; i < content.count(); ++i)
            viewMounted |= content.at(i) == view;
        QVERIFY2(viewMounted, "SocietyView must be mounted in LV.ApplicationWindow.content.");
        QVERIFY(view->findChild<QQuickItem *>("dashboardView"));
        QVERIFY(view->findChild<QQuickItem *>("storageView"));
        for (int i = 0; i < 3; ++i)
            QVERIFY(!visualItem(window->contentItem(), QString("windowControl%1").arg(i)));
        QTRY_COMPARE(view->mapToScene(QPointF()).y(), 0.0);
        QTRY_COMPARE(view->mapToScene(QPointF(0, view->height())).y(),
                     window->height() - window->property("mobileSystemSafeBottomInset").toReal());
        window->close();
    }

    void toolbarSharesWindowControlsRow()
    {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/toolbar-row-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        QQmlApplicationEngine engine;
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        engine.setInitialProperties({{"initialContainerPath", fixture.path()}});
        engine.load(QUrl::fromLocalFile(QString::fromUtf8(SOCIETY_QML_FILE)));
        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        auto *toolbar = window->findChild<QQuickItem *>("dashboardToolbar");
        auto *tabs = window->findChild<QQuickItem *>("dashboardTabs");
        auto *toolsTab = window->findChild<QQuickItem *>("toolsTab");
        auto *storageTab = window->findChild<QQuickItem *>("storageTab");
        auto *search = window->findChild<QQuickItem *>("dashboardSearch");
        auto *account = window->findChild<QQuickItem *>("dashboardAccount");
        QVERIFY(toolbar && tabs && toolsTab && storageTab && search && account);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTRY_COMPARE(toolbar->mapToScene(QPointF()).y(), 0.0);
        QCOMPARE(toolbar->height(), 56.0);
        QCOMPARE(window->property("windowDragHandleHeight").toReal(), toolbar->height());
        QSignalSpy moves(window, SIGNAL(windowMoveAttempted(bool)));
        QVERIFY(moves.isValid());
        const auto center = [](QQuickItem *item) {
            return item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
        };
        for (const QSize size : {QSize(1440, 900), QSize(760, 540), QSize(360, 640)}) {
            window->resize(size);
            QTRY_COMPARE(window->size(), size);
            QTRY_COMPARE(toolbar->width(), qreal(size.width()));
            // Qt layouts round odd-height controls to the device pixel grid.
            QTRY_VERIFY(qAbs(center(tabs).y() - 28.0) <= 0.5);
            QTRY_VERIFY(qAbs(center(account).y() - 28.0) <= 0.5);
            QTRY_VERIFY(tabs->mapToScene(QPointF()).x()
                        >= window->property("nativeTitleBarControlsRect").toRectF().right());
            QTRY_VERIFY(account->mapToScene(QPointF(account->width(), 0)).x() <= size.width() - 12);
            if (size.width() >= 700) {
                QVERIFY(search->isVisible());
                QVERIFY(qAbs(center(search).y() - 28.0) <= 0.5);
                QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, center(search).toPoint());
                QTest::keyClick(window, Qt::Key_Q);
                QCOMPARE(search->property("text").toString(), QString("q"));
                QVERIFY(search->setProperty("text", ""));
            } else {
                QVERIFY(!search->isVisible());
            }
            QVERIFY(window->setProperty("selectedTab", "Dashboard"));
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, center(toolsTab).toPoint());
            QTRY_COMPARE(window->property("selectedTab").toString(), QString("Tools"));
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, center(storageTab).toPoint());
            QTRY_COMPARE(window->property("selectedTab").toString(), QString("Storage"));
        }
        QCOMPARE(moves.count(), 0);
        window->resize(1440, 900);
        QTRY_COMPARE(toolbar->width(), 1440.0);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, QPoint(600, 28));
        QCOMPARE(moves.count(), 1);
        window->close();
    }

    void dashboardKeepsStorageAndPromptState()
    {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/dashboard-gui-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        QVERIFY(QDir().mkpath(fixture.filePath("Files/Work")));
        QVERIFY(QDir().mkpath(fixture.filePath("Files/Folder.png")));
        QImage sample(48, 48, QImage::Format_RGB32); sample.fill(Qt::darkBlue);
        for (const auto &name : {"Lunar studies.png", "Quiet landscape.png", "Paper forms.png"}) {
            const auto path = fixture.filePath(QString("Generation History/") + name);
            QVERIFY(sample.save(path));
            QFile file(path); QVERIFY(file.open(QIODevice::ReadWrite));
            QVERIFY(file.setFileTime(QDateTime::currentDateTimeUtc().addSecs(-3600), QFileDevice::FileModificationTime));
        }
        QVERIFY(sample.save(fixture.filePath("Files/Lunar studies.png")));
        for (const auto &name : {"Brand launch.iisc", "Chapter 01.txt"}) {
            QFile file(fixture.filePath(QString("Files/") + name));
            QVERIFY(file.open(QIODevice::WriteOnly)); file.write("fixture");
        }
        QQmlApplicationEngine engine;
        QStringList warnings;
        connect(&engine, &QQmlApplicationEngine::warnings, this, [&](const QList<QQmlError> &errors) {
            for (const auto &error : errors) warnings.append(error.toString());
        });
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        engine.setInitialProperties({{"initialContainerPath", fixture.path()}});
        engine.load(QUrl::fromLocalFile(QString::fromUtf8(SOCIETY_QML_FILE)));
        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        auto *dashboard = window->findChild<QQuickItem *>("dashboardView");
        auto *tools = window->findChild<QQuickItem *>("toolsView");
        auto *storage = window->findChild<QQuickItem *>("storageView");
        auto *dashboardTab = window->findChild<QQuickItem *>("dashboardTab");
        auto *toolsTab = window->findChild<QQuickItem *>("toolsTab");
        auto *storageTab = window->findChild<QQuickItem *>("storageTab");
        auto *prompt = window->findChild<QQuickItem *>("promptField");
        auto *drive = window->findChild<DriveController *>("driveController");
        QVERIFY(dashboard && tools && storage && dashboardTab && toolsTab && storageTab && prompt && drive);
        QTRY_VERIFY(window->isVisible() && dashboard->isVisible());
        QVERIFY(!tools->isVisible());
        QVERIFY(!storage->isVisible());
        QTRY_VERIFY(dashboardTab->mapToScene(QPointF(dashboardTab->width(), 0)).x()
                    <= toolsTab->mapToScene(QPointF()).x());
        QTRY_VERIFY(toolsTab->mapToScene(QPointF(toolsTab->width(), 0)).x()
                    <= storageTab->mapToScene(QPointF()).x());
        const auto *toolsAccessibility = QAccessible::queryAccessibleInterface(toolsTab);
        QVERIFY(toolsAccessibility);
        QCOMPARE(toolsAccessibility->text(QAccessible::Name), QString("Tools"));
        QVERIFY(!toolsAccessibility->state().selected);
        const auto *localNavigation = QAccessible::queryAccessibleInterface(visualItem(dashboard, "dashboardItemworkspace_home"));
        QVERIFY(localNavigation);
        QCOMPARE(localNavigation->text(QAccessible::Name), QString("Home"));
        QVERIFY(localNavigation->text(QAccessible::Description).isEmpty());
        auto *files = window->findChild<DashboardFiles *>("dashboardFiles");
        QVERIFY(files); QTRY_VERIFY(!files->loading());
        QCOMPARE(files->recentFiles().size(), 3);
        QCOMPARE(files->generationHistory().size(), 3);
        const auto screenshot = qEnvironmentVariable("SOCIETY_DASHBOARD_SCREENSHOT_PATH");
        if (!screenshot.isEmpty()) { QTest::qWait(300); QVERIFY(window->grabWindow().save(screenshot)); }
        const auto click = [&](QQuickItem *item) {
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                item->mapToScene(QPointF(item->width()/2, item->height()/2)).toPoint());
        };
        click(toolsTab);
        QTRY_VERIFY(tools->isVisible());
        QVERIFY(prompt->setProperty("text", "A quiet lunar landscape"));
        auto *quickGenerate = window->findChild<QQuickItem *>("quickGenerate");
        QVERIFY(quickGenerate); quickGenerate->setProperty("aspectRatio", "16:9");
        auto *quantity = window->findChild<QQuickItem *>("generationCountButton");
        QVERIFY(quantity);
        QCOMPARE(quantity->property("text").toString(), QString("1"));
        const QVariantList counts{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 15, 20, 25, 30, 40, 50, 100, 200, 500, 1000};
        QCOMPARE(quickGenerate->property("generationCounts").value<QJSValue>().toVariant().toList(), counts);
        auto *countMenu = window->findChild<QObject *>("generationCountMenu");
        QVERIFY(countMenu); click(quantity);
        QTRY_VERIFY(countMenu->property("opened").toBool());
        auto *countList = window->findChild<QQuickItem *>("generationCountList");
        QVERIFY(countList);
        QTest::keyClick(window, Qt::Key_End);
        QTRY_COMPARE(countList->property("currentIndex").toInt(), 19);
        QTRY_VERIFY(visualItem(countList, "generationCountOption19"));
        auto *lastCount = visualItem(countList, "generationCountOption19");
        QTRY_VERIFY(countList->boundingRect().contains(lastCount->mapRectToItem(countList, lastCount->boundingRect())));
        click(lastCount);
        QTRY_VERIFY(!countMenu->property("visible").toBool());
        QCOMPARE(quickGenerate->property("generationCount").toInt(), 1000);
        click(dashboardTab);
        auto *search = window->findChild<QQuickItem *>("dashboardSearch");
        QVERIFY(search); search->setProperty("text", "CHAPTER");
        QTRY_COMPARE(files->recentFiles().size(), 1);
        QVERIFY(files->generationHistory().isEmpty());
        search->setProperty("text", "");
        QTRY_COMPARE(files->recentFiles().size(), 3);
        click(toolsTab);
        QTRY_COMPARE(window->property("selectedTab").toString(), QString("Tools"));
        QTRY_VERIFY(tools->isVisible() && !dashboard->isVisible() && !storage->isVisible());
        QVERIFY(toolsAccessibility->state().selected);
        const auto toolsScreenshot = qEnvironmentVariable("SOCIETY_TOOLS_SCREENSHOT_PATH");
        if (!toolsScreenshot.isEmpty()) { QTest::qWait(200); QVERIFY(window->grabWindow().save(toolsScreenshot)); }
        auto *merge = window->findChild<QQuickItem *>("modelMergeTool"); QVERIFY(merge);
        QVERIFY(!merge->isVisible());
        const auto catalog = tools->property("tools").value<QJSValue>().toVariant().toList();
        QCOMPARE(catalog.size(), 1);
        QCOMPARE(catalog.first().toMap().value("key").toString(), QString("model-merge"));
        auto *mergeCard = visualItem(tools, "toolCard-model-merge"); QVERIFY(mergeCard);
        click(mergeCard);
        QTRY_VERIFY(merge->isVisible());
        QVERIFY(merge->setProperty("sharedWeight", "0.375"));
        QVERIFY(merge->setProperty("mode", "weighted-difference"));
        auto *toolsBack = visualItem(tools, "toolsBack"); QVERIFY(toolsBack);
        QTest::qWait(60);
        click(toolsBack);
        QTRY_VERIFY(!merge->isVisible() && mergeCard->isVisible());
        click(mergeCard);
        QTRY_VERIFY(merge->isVisible());
        QCOMPARE(merge->property("sharedWeight").toString(), QString("0.375"));
        QCOMPARE(merge->property("mode").toString(), QString("weighted-difference"));
        click(storageTab);
        QTRY_VERIFY(storage->isVisible() && !dashboard->isVisible() && !tools->isVisible());
        QVERIFY(!toolsAccessibility->state().selected);
        QVERIFY(drive->navigate(fixture.filePath("Files/Work")));
        click(toolsTab);
        QTRY_VERIFY(tools->isVisible() && !storage->isVisible());
        QCOMPARE(merge->property("sharedWeight").toString(), QString("0.375"));
        QCOMPARE(merge->property("mode").toString(), QString("weighted-difference"));
        click(dashboardTab);
        QTRY_VERIFY(dashboard->isVisible() && !storage->isVisible() && !tools->isVisible());
        QCOMPARE(prompt->property("text").toString(), QString("A quiet lunar landscape"));
        QCOMPARE(quickGenerate->property("aspectRatio").toString(), QString("16:9"));
        QCOMPARE(quickGenerate->property("generationCount").toInt(), 1000);
        click(storageTab);
        QTRY_VERIFY(storage->isVisible());
        QCOMPARE(drive->currentPath(), fixture.filePath("Files"));
        click(dashboardTab);
        QTRY_VERIFY(visualItem(window->contentItem(), "viewAllRecentFiles"));
        auto *allFiles = visualItem(window->contentItem(), "viewAllRecentFiles");
        QVERIFY(allFiles); click(allFiles);
        QTRY_VERIFY(storage->isVisible());
        QCOMPARE(drive->currentSection(), QString("Files"));
        auto *fileGrid = window->findChild<QQuickItem *>("filesBrowser"); QVERIFY(fileGrid);
        // Files is a table; switching to image-only history must keep its grid.
        QTRY_VERIFY(!fileGrid->property("loading").toBool());
        QTRY_COMPARE(fileGrid->property("count").toInt(), 5);
        click(dashboardTab);
        QTRY_VERIFY(visualItem(window->contentItem(), "viewAllGenerationHistory"));
        auto *history = visualItem(window->contentItem(), "viewAllGenerationHistory");
        QVERIFY(history); click(history);
        QTRY_VERIFY(storage->isVisible());
        QCOMPARE(drive->currentSection(), QString("Generation History"));
        const auto storageScreenshot = qEnvironmentVariable("SOCIETY_STORAGE_SCREENSHOT_PATH");
        if (!storageScreenshot.isEmpty()) { QTest::qWait(200); QVERIFY(window->grabWindow().save(storageScreenshot)); }
        click(dashboardTab);
        QVERIFY(!window->findChild<QQuickItem *>("dashboardDeleted"));
        click(storageTab);
        auto *deleted = visualItem(storage, "storageSectiondeleted");
        QVERIFY(deleted); click(deleted);
        QTRY_VERIFY(storage->isVisible());
        QCOMPARE(drive->currentSection(), QString("Deleted"));
        click(dashboardTab);
        click(toolsTab);
        QVERIFY(QMetaObject::invokeMethod(tools, "goBack"));
        QSignalSpy submitted(window, SIGNAL(generateRequested(QString,QString,QString,int)));
        auto *generate = window->findChild<QQuickItem *>("generateButton");
        QVERIFY(generate); click(generate);
        QCOMPARE(submitted.size(), 1);
        QCOMPARE(submitted.first(), QVariantList({QString("A quiet lunar landscape"), QString("Image"), QString("16:9"), 1000}));
        auto *notice = window->findChild<QQuickItem *>("dashboardNotice");
        QVERIFY(notice); QTRY_VERIFY(notice->property("open").toBool());
        notice->setProperty("open", false);
        auto *accountButton = window->findChild<QQuickItem *>("dashboardAccount");
        auto *accountViews = window->findChild<QObject *>("accountViews");
        QVERIFY(accountViews);
        auto *accountPanel = accountViews->property("dialog").value<QObject *>();
        QVERIFY(accountButton && accountPanel); click(accountButton);
        QTRY_VERIFY(accountPanel->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(accountPanel, "close"));
        QTRY_VERIFY(!accountPanel->property("visible").toBool());
        auto *environmentTab = window->findChild<QQuickItem *>("environmentTab");
        QVERIFY(environmentTab); click(environmentTab);
        auto *preferences = window->findChild<QQuickWindow *>("preferencesWindow");
        QVERIFY(preferences); QTRY_VERIFY(preferences->isVisible());
        preferences->close();
        QTRY_VERIFY(!preferences->isVisible());
        window->requestActivate();
        auto *browseTab = window->findChild<QQuickItem *>("browseTab");
        auto *devices = window->findChild<QObject *>("networkDevices");
        QVERIFY(browseTab && devices); click(browseTab);
        QTRY_VERIFY(devices->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(devices, "close"));
        QTRY_VERIFY(!devices->property("visible").toBool());
        for (const auto size : {QSize(1024, 768), QSize(760, 640), QSize(390, 844), QSize(1440, 900)}) {
            window->resize(size); QTRY_COMPARE(window->size(), size);
            const auto *scroll = window->findChild<QQuickItem *>("dashboardScroll");
            QVERIFY(scroll && scroll->width() > 0 && scroll->height() > 0);
            QTRY_VERIFY(scroll->width() <= size.width());
        }
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        window->close();
    }

    void preferencesDriveLocationAndWindowLifecycle()
    {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/preferences-gui-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        QQmlApplicationEngine engine;
        QStringList warnings;
        connect(&engine, &QQmlApplicationEngine::warnings, this, [&](const QList<QQmlError> &errors) {
            for (const auto &error : errors) warnings.append(error.toString());
        });
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        engine.setInitialProperties({{"initialContainerPath", fixture.path()}});
        engine.load(QUrl::fromLocalFile(QString::fromUtf8(SOCIETY_QML_FILE)));
        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        auto *network = window->findChild<NetworkDriveController *>("networkDriveController");
        auto *open = window->findChild<QQuickItem *>("openPreferences");
        QVERIFY(network && open);
        window->setProperty("selectedTab", "Storage");
        QVERIFY(!window->findChild<QQuickWindow *>("preferencesWindow")); // Created only when requested.
        auto *preferencesAction = window->findChild<QObject *>("globalPreferencesAction");
        QVERIFY(preferencesAction);
        QVERIFY(QMetaObject::invokeMethod(preferencesAction, "triggered"));
        auto *preferences = window->findChild<QQuickWindow *>("preferencesWindow");
        QVERIFY(preferences && preferences != window);
        QTRY_VERIFY(preferences->isVisible());
        QCOMPARE(preferences->property("primaryColor").value<QColor>(), QColor("#57965C"));
        auto *preferencesMaterial = preferences->findChild<QQuickItem *>("applicationWindowMaterial");
        QVERIFY(preferencesMaterial);
        QCOMPARE(preferencesMaterial->property("color").value<QColor>(), QColor("#0B0B0B"));
        QCOMPARE(preferencesMaterial->property("tintOpacity").toReal(), 0.5);
        QCOMPARE(preferencesMaterial->property("intenseOpacity").toReal(), 0.0);
        QCOMPARE(preferencesMaterial->property("faintOpacity").toReal(), 0.0);
        QCOMPARE(preferencesMaterial->property("primaryColor").value<QColor>(), QColor("#57965C"));
        QCOMPARE(preferences->transientParent(), window);
        QCOMPARE(preferences->modality(), Qt::NonModal);
        auto *host = preferences->findChild<QQuickItem *>("preferencesHostMode");
        auto *client = preferences->findChild<QQuickItem *>("preferencesClientMode");
        QVERIFY(!host && !client);
        QVERIFY(preferences->findChild<QQuickItem *>("preferencesDriveCategory"));
        QVERIFY(preferences->findChild<QQuickItem *>("preferencesDriveDetails"));
        auto *location = preferences->findChild<QQuickItem *>("preferencesDriveLocation");
        auto *apply = preferences->findChild<QQuickItem *>("applySocietyDrive");
        auto *current = preferences->findChild<QQuickItem *>("preferencesCurrentDrive");
        QVERIFY(location && apply && current);
        QTemporaryDir other(SOCIETY_TEST_DIRECTORY "/preferences-other-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(other.path()));
        QVERIFY(location->setProperty("text", other.path()));
        QVERIFY(QMetaObject::invokeMethod(apply, "clicked"));
        QTRY_COMPARE(current->property("text").toString(), other.path());
        QCOMPARE(iiSocietyContainer::SharedStorage::open()->drive().rootPath(), other.path());
        QVERIFY(location->setProperty("text", other.filePath("missing")));
        QVERIFY(QMetaObject::invokeMethod(apply, "clicked"));
        QVERIFY(preferences->property("locationFailed").toBool());
        QCOMPARE(current->property("text").toString(), other.path());
        location->setProperty("text", other.path());
        QVERIFY(QMetaObject::invokeMethod(apply, "clicked"));
        QVERIFY(!preferences->findChild<QQuickItem *>("closePreferences"));
        QVERIFY(window->findChild<QObject *>("globalMenuBar"));
        QCOMPARE(network->mode(), NetworkDriveController::HostMode);
        QVERIFY(!network->setProperty("mode", NetworkDriveController::ClientMode));

        for (const auto size : {QSize(360, 320), QSize(760, 540), QSize(720, 440)}) {
            preferences->resize(size); QTRY_COMPARE(preferences->size(), size);
        }
        const auto screenshot = qEnvironmentVariable("SOCIETY_PREFERENCES_SCREENSHOT_PATH");
        if (!screenshot.isEmpty()) { QTest::qWait(150); QVERIFY(preferences->grabWindow().save(screenshot)); }
        preferences->close(); QTRY_VERIFY(!preferences->isVisible());
        QVERIFY(window->isVisible());
        window->requestActivate();
        QTRY_VERIFY(window->isActive());
        QTest::keySequence(window, QKeySequence(QStringLiteral("Ctrl+,")));
        QTRY_VERIFY(preferences->isVisible());
        QCOMPARE(window->findChildren<QQuickWindow *>("preferencesWindow").size(), 1);
        QCOMPARE(network->mode(), NetworkDriveController::HostMode);
        for (const auto &key : {QKeySequence(Qt::Key_Escape), QKeySequence(QKeySequence::Close)}) {
            preferences->requestActivate(); QTRY_VERIFY(preferences->isActive());
            QTest::keySequence(preferences, key);
            QTRY_VERIFY(!preferences->isVisible());
            QVERIFY(window->isVisible());
            QVERIFY(QMetaObject::invokeMethod(window, "openPreferences"));
            QTRY_VERIFY(preferences->isVisible());
        }
        QVERIFY(QMetaObject::invokeMethod(window, "openPreferences"));
        QCOMPARE(window->findChild<QQuickWindow *>("preferencesWindow"), preferences);
        QVERIFY(!preferences->findChild<QQuickItem *>("preferencesDevices"));
        QVERIFY(QMetaObject::invokeMethod(window, "openDevices"));
        QTRY_VERIFY(!preferences->isVisible());
        auto *panel = window->findChild<QObject *>("networkDevices");
        QVERIFY(panel); QTRY_VERIFY(panel->property("visible").toBool());
        auto *settings = panel->findChild<QQuickItem *>("networkPreferences");
        QVERIFY(settings);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
            settings->mapToScene(QPointF(settings->width()/2, settings->height()/2)).toPoint());
        QTRY_VERIFY(preferences->isVisible()); QTRY_VERIFY(!panel->property("visible").toBool());
        window->close();
        QTRY_VERIFY(!preferences->isVisible());
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    }
};

int main(int argc, char *argv[])
{
    lvrs::AppBootstrapOptions options;
    options.applicationName = QStringLiteral("SocietyDriveTests");
    options.quickStyleName = QStringLiteral("Basic");
    options.bootstrapGraphicsBackend = false;
    options.configureRenderQualityDefaults = false;
    options.logBootstrapDiagnostics = false;
    options.logGraphicsBackend = false;
    if (!lvrs::preApplicationBootstrap(options).ok)
        return 1;
    QGuiApplication app(argc, argv);
    QTemporaryDir settings(SOCIETY_TEST_DIRECTORY "/drive-settings-XXXXXX");
    if (!settings.isValid()) return 1;
    qputenv("SOCIETY_STORAGE_SETTINGS_PATH", settings.filePath("storage.json").toUtf8());
    qputenv("SOCIETY_CALENDAR_DIRECTORY", settings.filePath("calendar").toUtf8());
    qunsetenv("SOCIETY_CONTAINER_PATH");
    lvrs::postApplicationBootstrap(app, options);
    SocietyDriveTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_drive.moc"

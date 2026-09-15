#include "App/Drive/DriveController.h"
#include "App/Drive/StorageNavigation.h"
#include "App/Dashboard/DashboardFiles.h"
#include "App/Tools/ModelMergeController.h"
#include "App/Tools/MergeModelCatalog.h"
#include "App/Models/StorageModels.h"
#include "App/Files/DirectoryLocation.h"
#include "App/Files/ModelImporter.h"
#include "backend/runtime/appbootstrap.h"

#include <QDir>
#include <QAbstractItemModel>
#include <QFile>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QGuiApplication>
#include <QAccessible>
#include <QImage>
#include <QJSValue>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlError>
#include <QQmlListReference>
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
#include "App/Network/DevicePairing.h"
#include "App/Network/PairingQr.h"
#include "App/Network/QrScanner.h"
#include <SharedStorage.h>
#include <FilesView.h>

static QQuickItem *visualItem(QQuickItem *parent, const QString &name)
{
    if (parent->objectName() == name) return parent;
    for (auto *child : parent->childItems())
        if (auto *found = visualItem(child, name)) return found;
    return nullptr;
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
        qmlRegisterType<DriveController>("Society", 1, 0, "DriveController");
        qmlRegisterType<StorageNavigation>("Society", 1, 0, "StorageNavigation");
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
    void fixedFilesDirectoriesAppearAndPhotosShowsImagesAndVideos()
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
        auto *files = window->findChild<QQuickItem *>("fileGridView"); QVERIFY(files);
        auto *grid = files->findChild<QQuickItem *>("fileGrid"); QVERIFY(grid);
        QVERIFY(drive->openSection("files"));
        QTRY_COMPARE(files->property("count").toInt(), 3);
        QTRY_VERIFY(!files->property("loading").toBool());
        const auto names = [&] {
            QStringList result;
            auto *model = qvariant_cast<QAbstractItemModel *>(grid->property("model"));
            if (!model) return result;
            const auto role = model->roleNames().key("fileName", -1);
            for (int row = 0; row < model->rowCount(); ++row) result.append(model->data(model->index(row, 0), role).toString());
            result.sort(); return result;
        };
        QCOMPARE(names(), (QStringList{"3D objects", "Audios", "Documents"}));
        const auto screenshot = qEnvironmentVariable("SOCIETY_FILES_DIRECTORIES_SCREENSHOT_PATH");
        if (!screenshot.isEmpty()) { QVERIFY(QTest::qWaitForWindowExposed(window)); QTest::qWait(150); QVERIFY(window->grabWindow().save(screenshot)); }
        const auto view = iiSocietyContainer::FilesView::open(fixture.path()); QVERIFY(view);
        for (const auto &directory : view->directories()) {
            QVERIFY(QMetaObject::invokeMethod(files, "activated", Q_ARG(QString, directory.path()), Q_ARG(bool, true)));
            QTRY_COMPARE(drive->currentPath(), directory.path());
            QTRY_VERIFY(!files->property("loading").toBool());
            QTRY_COMPARE(files->property("count").toInt(), 0);
            drive->goUp(); QTRY_COMPARE(drive->currentPath(), fixture.filePath("Files"));
        }
        QVERIFY(drive->openSection("photos"));
        QCOMPARE(drive->currentPath(), fixture.filePath("Photos"));
        auto *gallery = visualItem(window->contentItem(), "photosView"); QVERIFY(gallery);
        QTRY_VERIFY(gallery->isVisible()); QVERIFY(!files->isVisible());
        QCOMPARE(drive->breadcrumbs().size(), 2);
        drive->goUp(); QVERIFY(drive->atRoot());
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
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, card->mapToScene(QPointF(60, 80)).toPoint());
        const auto selected = models->property("selectedPath").toString(); QVERIFY(!selected.isEmpty());
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
        auto *files = window->findChild<QQuickItem *>("fileGridView");
        auto *grid = files ? files->findChild<QQuickItem *>("fileGrid") : nullptr;
        QVERIFY(drive && network && files && grid); QVERIFY(drive->openSection("files"));
        QTRY_COMPARE(files->property("count").toInt(), 83);
        QTRY_VERIFY(!files->property("loading").toBool());
        QTRY_VERIFY(!files->property("initialPositionPending").toBool());
        QPointer<QAbstractItemModel> model = qvariant_cast<QAbstractItemModel *>(grid->property("model")); QVERIFY(model);
        QVERIFY(grid->setProperty("currentIndex", 30)); QVERIFY(grid->setProperty("contentY", 880.0));
        QTest::qWait(50);
        const auto selected = files->property("selectedPath").toString();
        const auto scroll = grid->property("contentY").toReal(); QVERIFY(scroll > 0); QVERIFY(!selected.isEmpty());
        QSignalSpy reset(model, &QAbstractItemModel::modelReset);
        for (int i = 0; i < 3; ++i) {
            QVERIFY(QMetaObject::invokeMethod(network, "mirrorChanged"));
            QVERIFY(QMetaObject::invokeMethod(network, "containerSynchronized", Q_ARG(QString, "host")));
            QTest::qWait(1050);
            QVERIFY2(model, "Periodic synchronization destroyed the active folder model.");
            QCOMPARE(qvariant_cast<QAbstractItemModel *>(grid->property("model")), model.data());
            QCOMPARE(files->property("selectedPath").toString(), selected);
            QCOMPARE(grid->property("contentY").toReal(), scroll);
            QCOMPARE(drive->currentPath(), fixture.filePath("Files"));
        }
        QCOMPARE(reset.size(), 0);
        // Native directory watching must still publish real changes without replacing the model.
        QFile added(fixture.filePath("Files/zz-new.txt")); QVERIFY(added.open(QIODevice::WriteOnly));
        QVERIFY(added.write("new file") > 0); added.close();
        QTRY_COMPARE(files->property("count").toInt(), 84);
        QCOMPARE(qvariant_cast<QAbstractItemModel *>(grid->property("model")), model.data());
        QTRY_COMPARE(files->property("selectedPath").toString(), selected);
        QTRY_COMPARE(grid->property("contentY").toReal(), scroll);
        QVERIFY(added.remove()); QTRY_COMPARE(files->property("count").toInt(), 83);
        QTRY_COMPARE(files->property("selectedPath").toString(), selected);
        QTRY_COMPARE(grid->property("contentY").toReal(), scroll);
        window->close();
    }
    void backgroundRefreshRejectsStaleRootsAndNavigation()
    {
        QTemporaryDir first(SOCIETY_TEST_DIRECTORY "/refresh-first-XXXXXX"), second(SOCIETY_TEST_DIRECTORY "/refresh-second-XXXXXX");
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
        QVERIFY(!controller.hasDrive());
        QVERIFY(!controller.busy());
        QVERIFY(controller.sections().isEmpty());
        QVERIFY(!controller.openContainer(""));
        QVERIFY(!controller.openContainer("relative"));
        QVERIFY(controller.openContainer(fixture.path()));
        const auto shared = iiSocietyContainer::SharedStorage::open();
        QVERIFY(shared);
        QCOMPARE(shared->drive().identifier(), controller.identifier());
        DriveController reopened;
        reopened.openDefaultContainer();
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
        QVERIFY(controller.openContainer(fixture.path()));
        const auto identifier = controller.identifier();
        QVERIFY(!controller.openContainer(fixture.filePath("Files")));
        QVERIFY(!controller.errorString().isEmpty());
        QCOMPARE(controller.identifier(), identifier);
        QCOMPARE(controller.rootPath(), fixture.path());
        QCOMPARE(QDir(fixture.filePath("Files")).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot, QDir::Name),
            (QStringList{"3D objects", "Audios", "Documents"}));
        const auto shared = iiSocietyContainer::SharedStorage::open();
        QVERIFY(shared);
        QCOMPARE(shared->drive().rootPath(), fixture.path());
    }

    void driveWindowShowsNineSectionsAndNavigates()
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
        auto *grid = window->findChild<QQuickItem *>("sectionsGrid");
        auto *files = window->findChild<QQuickItem *>("fileGridView");
        auto *content = window->findChild<QQuickItem *>("driveContent");
        QVERIFY(controller && grid && files && content);
        QTRY_VERIFY(window->isVisible());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTRY_COMPARE(grid->property("count").toInt(), 9);
        auto *storageTab = window->findChild<QQuickItem *>("storageTab");
        QVERIFY(storageTab);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
            storageTab->mapToScene(QPointF(storageTab->width()/2, storageTab->height()/2)).toPoint());
        QTRY_VERIFY(window->property("selectedTab").toString() == "Storage");
        QVERIFY(grid->isVisible());
        QVERIFY(!files->isVisible());
        auto *storage = window->findChild<QQuickItem *>("storageView");
        auto *progress = window->findChild<QQuickItem *>("mirrorProgress");
        QVERIFY(storage && progress);
        QVERIFY(storage->setProperty("synchronizationStatus", "Syncing test-model.safetensors (42%)…"));
        controller->setMirrorPending(true);
        QTRY_VERIFY(progress->isVisible()); QVERIFY(!grid->isVisible());
        QCOMPARE(progress->property("text").toString(), QString("Syncing test-model.safetensors (42%)…"));
        controller->setMirrorPending(false); QTRY_VERIFY(grid->isVisible()); QVERIFY(!progress->isVisible());
        const auto findTile = [&](auto &&self, QQuickItem *item) -> QQuickItem * {
            if (item->objectName() == "sectionTile" && item->property("text").toString() == "Files")
                return item;
            for (auto *child : item->childItems())
                if (auto *found = self(self, child))
                    return found;
            return nullptr;
        };
        QTRY_VERIFY(findTile(findTile, grid));
        auto *tile = findTile(findTile, grid);
        const auto point = tile->mapToScene(QPointF(tile->width()/2, tile->height()/2)).toPoint();
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
        QTRY_COMPARE(controller->currentSection(), QString("Files"));
        QTRY_VERIFY(files->isVisible());
        QTRY_COMPARE(files->property("count").toInt(), 5);
        QVERIFY(QMetaObject::invokeMethod(files, "activated", Q_ARG(QString, fixture.filePath("Files/Nested")), Q_ARG(bool, true)));
        QTRY_COMPARE(controller->currentPath(), fixture.filePath("Files/Nested"));
        auto *up = window->findChild<QQuickItem *>("driveUp");
        QVERIFY(up);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          up->mapToScene(QPointF(up->width()/2, up->height()/2)).toPoint());
        QTRY_COMPARE(controller->currentPath(), fixture.filePath("Files"));
        QVERIFY(controller->openSection("models"));
        QTRY_COMPARE(files->property("path").toString(), fixture.filePath("Models"));
        QTRY_COMPARE(files->property("count").toInt(), 23);
        QTRY_VERIFY(QFileInfo::exists(fixture.filePath("Models/Wildcards/App only.txt")));
        auto *models = window->findChild<QQuickItem *>("modelsView"); QVERIFY(models);
        QTRY_VERIFY(models->isVisible());
        QVERIFY(!files->isVisible());
        QVERIFY(QMetaObject::invokeMethod(models, "browseFoldersRequested"));
        QVERIFY(files->isVisible());
        const auto modelsScreenshot = qEnvironmentVariable("SOCIETY_MODEL_TYPES_SCREENSHOT_PATH");
        if (!modelsScreenshot.isEmpty()) { QTest::qWait(200); QVERIFY(window->grabWindow().save(modelsScreenshot)); }
        controller->goHome();
        QTRY_VERIFY(grid->isVisible());
        for (const auto size : {QSize(390, 844), QSize(844, 390), QSize(360, 320), QSize(1120, 720)}) {
            window->resize(size);
            QTRY_COMPARE(window->size(), size);
            QTRY_VERIFY2(grid->width() > 0 && grid->height() > 0,
                qPrintable(QString("Window %1x%2 leaves a %3x%4 section grid")
                    .arg(size.width()).arg(size.height()).arg(grid->width()).arg(grid->height())));
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
            auto *devicesButton = window->findChild<QQuickItem *>("openNetworkDevices");
            auto *devices = window->findChild<QObject *>("networkDevices");
            QVERIFY(devicesButton && devices);
            QTRY_VERIFY(QRectF(QPointF(0, 0), window->size()).contains(
                devicesButton->mapToScene(QPointF(devicesButton->width()/2, devicesButton->height()/2))));
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                devicesButton->mapToScene(QPointF(devicesButton->width()/2, devicesButton->height()/2)).toPoint());
            QTRY_VERIFY(devices->property("visible").toBool());
            QVERIFY(devices->property("width").toReal() <= size.width());
            QVERIFY(devices->property("height").toReal() <= size.height());
            auto *network = window->findChild<NetworkDriveController *>("networkDriveController");
            auto *modeLabel = window->findChild<QQuickItem *>("networkModeLabel");
            auto *settings = window->findChild<QQuickItem *>("networkPreferences");
            auto *preferencesButton = window->findChild<QQuickItem *>("openPreferences");
            QVERIFY(network && modeLabel && settings && preferencesButton);
            QCOMPARE(modeLabel->property("text").toString(), QString("Client mode"));
            QCOMPARE(settings->isVisible(), network->hostModeAvailable());
            QCOMPARE(preferencesButton->isVisible(), network->hostModeAvailable());
            QTRY_VERIFY(QRectF(QPointF(0, 0), window->size()).contains(
                preferencesButton->mapToScene(QPointF(preferencesButton->width()/2, preferencesButton->height()/2))));
            const auto screenshot = qEnvironmentVariable("SOCIETY_NETWORK_SCREENSHOT_PATH");
            if (size == QSize(1120, 720) && !screenshot.isEmpty()) {
                QTest::qWait(150);
                QVERIFY(window->grabWindow().save(screenshot));
            }
            QVERIFY(QMetaObject::invokeMethod(devices, "close"));
            QTRY_VERIFY(!devices->property("visible").toBool());
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
            QTRY_COMPARE(files->property("count").toInt(), 1);
            QVERIFY(!controller->navigate(fixture.filePath("Generation History/Legacy App")));
            QCOMPARE(controller->currentPath(), fixture.filePath("Generation History"));
            QVERIFY(controller->openSection("files"));
            QTRY_COMPARE(files->property("count").toInt(), 5);
            controller->goHome();
        }
        // Phone width retains every logical area even without the desktop sidebar.
        window->resize(QSize(390, 844));
        QTRY_VERIFY(!window->findChild<QQuickItem *>("driveSidebar")->isVisible());
        for (const auto section : iiSocietyContainer::allStoreSections()) {
            QVERIFY(controller->openSection(iiSocietyContainer::storeSectionKey(section)));
            auto *photos = visualItem(window->contentItem(), "photosView"); QVERIFY(photos);
            QTRY_VERIFY(section == iiSocietyContainer::StoreSection::Models ? models->isVisible()
                : section == iiSocietyContainer::StoreSection::Photos ? photos->isVisible() : files->isVisible());
            QCOMPARE(controller->currentSection(), iiSocietyContainer::storeSectionName(section));
            controller->goHome();
            QTRY_VERIFY(grid->isVisible());
            QCOMPARE(grid->property("count").toInt(), 9);
        }
        const auto *connectButton = window->findChild<QQuickItem *>("connectToSystem");
        QVERIFY(connectButton);
        QCOMPARE(connectButton->property("text").toString(), QString("Connect to %1").arg(controller->systemName()));

        // OS URL drops target Models from the home page, Files and a phone-size header.
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
            QTRY_COMPARE(files->property("count").toInt(), 23);
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

    void viewAllRecentFilesOpensStorageAtTheNewest()
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
        auto *files = window->findChild<QQuickItem *>("fileGridView"); QVERIFY(files);
        auto *grid = files->findChild<QQuickItem *>("fileGrid"); QVERIFY(grid);
        QTRY_COMPARE(files->property("path").toString(), fixture.filePath("Files"));
        QVERIFY(files->property("chronological").toBool());
        QTRY_COMPARE(files->property("count").toInt(), 83);
        QTRY_VERIFY(!files->property("loading").toBool());
        QTRY_VERIFY(grid->property("atYEnd").toBool());
        QVERIFY(grid->property("contentY").toReal() > 0);
        auto *model = qvariant_cast<QAbstractItemModel *>(grid->property("model")); QVERIFY(model);
        const auto nameRole = model->roleNames().key("fileName", -1); QVERIFY(nameRole >= 0);
        QCOMPARE(model->data(model->index(3, 0), nameRole).toString(), QString("photo-79.txt"));
        QCOMPARE(model->data(model->index(82, 0), nameRole).toString(), QString("photo-00.txt"));
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
        QCOMPARE(files.recentFiles().size(), 6);
        QCOMPARE(files.recentFiles().first().toMap().value("name").toString(), QString("latest.txt"));
        QVERIFY(files.recentFiles().first().toMap().value("previewSource").toUrl().isEmpty());
        QCOMPARE(files.generationHistory().size(), 11);
        QCOMPARE(files.generationHistory().first().toMap().value("name").toString(), QString("finished.PNG"));
        const auto image = files.generationHistory().first().toMap();
        QCOMPARE(image.value("previewSource").toUrl(), QUrl::fromLocalFile(fixture.filePath("Generation History/finished.PNG")));
        QCOMPARE(image.value("dateText").toString(), image.value("modified").toDateTime().toLocalTime().date().toString(Qt::ISODate));
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
    }

    void dashboardCardRowsMatchFigmaAndRemainInteractive()
    {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/dashboard-cards-XXXXXX");
        QVERIFY(fixture.isValid());
        QImage preview(96, 64, QImage::Format_RGB32);
        preview.fill(QColor("#316c98"));
        const auto previewPath = qEnvironmentVariable("SOCIETY_DASHBOARD_CARDS_PREVIEW_PATH");
        if (!previewPath.isEmpty()) QVERIFY(preview.load(previewPath));
        const auto rows = [&](const QString &prefix, int count) {
            QVariantList result;
            for (int i = 0; i < count; ++i) {
                const auto name = QString("%1 # %2.png").arg(prefix).arg(i);
                const auto path = fixture.filePath(name);
                if (!preview.save(path)) return QVariantList();
                result.append(QVariantMap{{"name", name}, {"path", path},
                    {"folderPath", fixture.path()}, {"description", "Image"},
                    {"previewSource", QUrl::fromLocalFile(path)}, {"dateText", "2026-09-13"}});
            }
            return result;
        };
        const auto recentFiles = rows("Recent", 6);
        const auto historyFiles = rows("Generated", 11);
        QCOMPARE(recentFiles.size(), 6);
        QCOMPARE(historyFiles.size(), 11);
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
            {"width", 1440}, {"height", 844}, {"recentFiles", recentFiles}, {"historyFiles", historyFiles}}));
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
        QTRY_COMPARE(recent->size(), QSizeF(1172, 160));
        QTRY_COMPARE(recent->mapToItem(dashboard, QPointF()), QPointF(244, 154));
        QTRY_COMPARE(history->mapToItem(dashboard, QPointF()), QPointF(244, 372));
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
        QCOMPARE(revealed.first().first().toString(), fixture.path());
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
            const qreal contentWidth = width - (width >= 760 ? 220 : 0) - 48;
            QTRY_COMPARE(recent->size(), QSizeF(contentWidth, 160));
            QTRY_COMPARE(history->size(), QSizeF(contentWidth, 160));
            QVERIFY(recent->mapToItem(dashboard, QPointF(recent->width(), 0)).x() <= width - 24);
        }
        QVERIFY(dashboard->setProperty("recentFiles", QVariantList()));
        QVERIFY(dashboard->setProperty("historyFiles", QVariantList()));
        QTRY_COMPARE(recent->property("count").toInt(), 0);
        QTRY_VERIFY(!recent->isVisible() && !history->isVisible());
        auto *empty = visualItem(dashboard, "emptyRecentFiles");
        QVERIFY(empty && empty->isVisible());
        QVERIFY(dashboard->setProperty("query", "missing"));
        QCOMPARE(empty->property("label").toString(), QString("No matching files"));
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        window.close();
    }

    void mobileViewsShareDesktopContentAndKeepState()
    {
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
        QTRY_VERIFY(dashboard->isVisible());
        QCOMPARE(window->property("selectedTab").toString(), QString("Dashboard"));
        QTRY_COMPARE(files->containerPath(), fixture.path());
        QTRY_VERIFY(!files->loading() && !files->recentFiles().isEmpty() && !files->generationHistory().isEmpty());
        QCOMPARE(window->findChildren<QQuickItem *>("dashboardView").size(), 1);
        QCOMPARE(window->findChildren<QQuickItem *>("toolsView").size(), 1);
        QCOMPARE(window->findChildren<QQuickItem *>("storageView").size(), 1);
        auto *touch = QTest::createTouchDevice();
        const auto tap = [&](const QString &name) {
            auto *item = visualItem(window->contentItem(), name);
            if (!item || !item->isVisible()) return false;
            const auto p = item->mapToScene(QPointF(item->width()/2, item->height()/2)).toPoint();
            QTest::touchEvent(window, touch).press(0, p, window);
            QTest::touchEvent(window, touch).release(0, p, window);
            QCoreApplication::processEvents();
            return true;
        };
        QVERIFY(prompt->setProperty("text", "Keep this mobile prompt"));
        QVERIFY(tap("mobileToolsTab")); QTRY_VERIFY(tools->isVisible());
        QVERIFY(tools->setProperty("sharedWeight", "0.75"));
        QVERIFY(tap("mobileStorageTab")); QTRY_VERIFY(storage->isVisible());
        QVERIFY(drive->navigate(fixture.filePath("Files/Work")));
        QVERIFY(tap("mobileDashboardTab")); QTRY_VERIFY(dashboard->isVisible());
        QCOMPARE(prompt->property("text").toString(), QString("Keep this mobile prompt"));
        QVERIFY(tap("mobileToolsTab")); QTRY_COMPARE(tools->property("sharedWeight").toString(), QString("0.75"));
        QVERIFY(tap("mobileStorageTab")); QCOMPARE(drive->currentPath(), fixture.filePath("Files/Work"));
        QVERIFY(tap("mobileSearchToggle"));
        auto *search = window->findChild<QQuickItem *>("mobileSearch"); QVERIFY(search);
        QTRY_VERIFY(search->isVisible() && dashboard->isVisible());
        QSignalSpy searchNavigation(view, SIGNAL(tabRequested(QString))); QVERIFY(searchNavigation.isValid());
        QVERIFY(search->setProperty("text", "Mobile"));
        QVERIFY(search->setProperty("text", "Mobile landscape"));
        QTRY_COMPARE(files->recentFiles().size(), 1);
        QCOMPARE(searchNavigation.count(), 0); // Typing must not navigate again and dismiss the keyboard.
        QCOMPARE(view->property("query").toString(), QString("Mobile landscape"));
        QVERIFY(search->setProperty("text", ""));
        QVERIFY(tap("mobileSearchToggle")); QTRY_VERIFY(!search->isVisible());
        QVERIFY(tap("mobileBrowseTab"));
        auto *devices = window->findChild<QObject *>("networkDevices"); QVERIFY(devices);
        QTRY_VERIFY(devices->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(devices, "close"));
        QTRY_VERIFY(!devices->property("visible").toBool());
        QVERIFY(tap("mobileEnvironmentTab"));
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
        for (const QSize size : {QSize(320, 568), QSize(390, 844), QSize(844, 390), QSize(1024, 768)}) {
            window->resize(size);
            QTRY_COMPARE(window->size(), size);
            QTRY_COMPARE(view->width(), qreal(size.width()));
            QCOMPARE(tabs->isVisible(), size.width() < 760);
            if (tabs->isVisible()) {
                for (const QString label : {"Dashboard", "Tools", "Storage", "Browse", "Environment"}) {
                    auto *item = visualItem(window->contentItem(), "mobile" + label + "Label"); QVERIFY(item);
                    QTRY_VERIFY(!item->property("truncated").toBool());
                }
            }
            for (const QString tab : {"Dashboard", "Tools", "Storage"}) {
                QVERIFY(window->setProperty("selectedTab", tab));
                QTest::qWait(40);
                const QStringList names = tab == "Dashboard"
                    ? QStringList{"promptField", "generateButton", "viewAllRecentFiles"}
                    : tab == "Tools" ? QStringList{"mergeScroll", "mergeRun", "mergeStatus"}
                    : QStringList{"driveUp", "fileGridView"};
                for (const QString &name : names) {
                    auto *item = visualItem(window->contentItem(), name); QVERIFY2(item, qPrintable(name));
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
                    QVERIFY(window->grabWindow().save(output + QString("/%1-%2x%3.png").arg(tab).arg(size.width()).arg(size.height())));
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
        auto *sections = window->findChild<QQuickItem *>("sectionsGrid"); QVERIFY(sections);
        QTRY_COMPARE(sections->property("count").toInt(), 9);
        QTRY_VERIFY(sections->width() / sections->property("cellWidth").toReal() >= 2);
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
        drive->goUp(); QTRY_VERIFY(sections->isVisible());
        QVERIFY(drive->openSection("models"));
        auto *models = window->findChild<QQuickItem *>("modelsView"); QVERIFY(models);
        QTRY_VERIFY(models->isVisible());
        auto *up = visualItem(window->contentItem(), "driveUp"); QVERIFY(up);
        QTRY_VERIFY(up->isVisible() && up->isEnabled() && up->height() >= 44);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
            up->mapToScene(QPointF(up->width()/2, up->height()/2)).toPoint());
        QTRY_VERIFY(drive->atRoot());
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
        const auto *localNavigation = QAccessible::queryAccessibleInterface(window->findChild<QQuickItem *>("dashboardLocal"));
        QVERIFY(localNavigation);
        QCOMPARE(localNavigation->text(QAccessible::Name), QString("Local"));
        QVERIFY(localNavigation->text(QAccessible::Description).isEmpty());
        auto *files = window->findChild<DashboardFiles *>("dashboardFiles");
        QVERIFY(files); QTRY_VERIFY(!files->loading());
        QCOMPARE(files->recentFiles().size(), 6);
        QCOMPARE(files->generationHistory().size(), 3);
        const auto screenshot = qEnvironmentVariable("SOCIETY_DASHBOARD_SCREENSHOT_PATH");
        if (!screenshot.isEmpty()) { QTest::qWait(300); QVERIFY(window->grabWindow().save(screenshot)); }
        const auto click = [&](QQuickItem *item) {
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                item->mapToScene(QPointF(item->width()/2, item->height()/2)).toPoint());
        };
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
        auto *search = window->findChild<QQuickItem *>("dashboardSearch");
        QVERIFY(search); search->setProperty("text", "CHAPTER");
        QTRY_COMPARE(files->recentFiles().size(), 1);
        QVERIFY(files->generationHistory().isEmpty());
        search->setProperty("text", "");
        QTRY_COMPARE(files->recentFiles().size(), 6);
        click(toolsTab);
        QTRY_COMPARE(window->property("selectedTab").toString(), QString("Tools"));
        QTRY_VERIFY(tools->isVisible() && !dashboard->isVisible() && !storage->isVisible());
        QVERIFY(tools->setProperty("sharedWeight", "0.375"));
        QVERIFY(tools->setProperty("mode", "weighted-difference"));
        QVERIFY(toolsAccessibility->state().selected);
        const auto toolsScreenshot = qEnvironmentVariable("SOCIETY_TOOLS_SCREENSHOT_PATH");
        if (!toolsScreenshot.isEmpty()) { QTest::qWait(200); QVERIFY(window->grabWindow().save(toolsScreenshot)); }
        click(storageTab);
        QTRY_VERIFY(storage->isVisible() && !dashboard->isVisible() && !tools->isVisible());
        QVERIFY(!toolsAccessibility->state().selected);
        QVERIFY(drive->navigate(fixture.filePath("Files/Work")));
        click(toolsTab);
        QTRY_VERIFY(tools->isVisible() && !storage->isVisible());
        QCOMPARE(tools->property("sharedWeight").toString(), QString("0.375"));
        QCOMPARE(tools->property("mode").toString(), QString("weighted-difference"));
        click(dashboardTab);
        QTRY_VERIFY(dashboard->isVisible() && !storage->isVisible() && !tools->isVisible());
        QCOMPARE(prompt->property("text").toString(), QString("A quiet lunar landscape"));
        QCOMPARE(quickGenerate->property("aspectRatio").toString(), QString("16:9"));
        QCOMPARE(quickGenerate->property("generationCount").toInt(), 1000);
        click(storageTab);
        QTRY_VERIFY(storage->isVisible());
        QCOMPARE(drive->currentPath(), fixture.filePath("Files/Work"));
        click(dashboardTab);
        QTRY_VERIFY(visualItem(window->contentItem(), "viewAllRecentFiles"));
        auto *allFiles = visualItem(window->contentItem(), "viewAllRecentFiles");
        QVERIFY(allFiles); click(allFiles);
        QTRY_VERIFY(storage->isVisible());
        QCOMPARE(drive->currentSection(), QString("Files"));
        click(dashboardTab);
        QTRY_VERIFY(visualItem(window->contentItem(), "viewAllGenerationHistory"));
        auto *history = visualItem(window->contentItem(), "viewAllGenerationHistory");
        QVERIFY(history); click(history);
        QTRY_VERIFY(storage->isVisible());
        QCOMPARE(drive->currentSection(), QString("Generation History"));
        const auto storageScreenshot = qEnvironmentVariable("SOCIETY_STORAGE_SCREENSHOT_PATH");
        if (!storageScreenshot.isEmpty()) { QTest::qWait(200); QVERIFY(window->grabWindow().save(storageScreenshot)); }
        click(dashboardTab);
        auto *deleted = window->findChild<QQuickItem *>("dashboardDeleted");
        QVERIFY(deleted); click(deleted);
        QTRY_VERIFY(storage->isVisible());
        QCOMPARE(drive->currentSection(), QString("Deleted"));
        click(dashboardTab);
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

    void preferencesControlsHostingAndReusesItsWindow()
    {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/preferences-gui-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        iiServerHost::RelayServer relay([](const auto &, iiServerHost::AuthCompletion done) {
            done({"alice", QDateTime::currentDateTimeUtc().addSecs(60)});
        });
        QVERIFY(relay.listen(QHostAddress::LocalHost));
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
        iiServerHost::PeerOptions options;
        options.relayUrl = QUrl(QString("ws://127.0.0.1:%1").arg(relay.port()));
        options.credential = "alice"; options.peerId = "desktop"; options.name = "Desktop";
        options.localEnabled = false;
        QVERIFY(network->startSession(options));
        iiServerHost::Peer observer;
        options.peerId = "observer"; options.hostFiles = false; options.service = "com.iisacc.society.files";
        QVERIFY(observer.start(options));
        QTRY_VERIFY(network->connected() && observer.isReady());
        QTRY_VERIFY(window->isVisible() && open->isVisible());
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
            open->mapToScene(QPointF(open->width()/2, open->height()/2)).toPoint());
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
        auto *done = preferences->findChild<QQuickItem *>("closePreferences");
        QVERIFY(host && client && done);
        const auto click = [&](QQuickItem *item) {
            QTest::mouseClick(preferences, Qt::LeftButton, Qt::NoModifier,
                item->mapToScene(QPointF(item->width()/2, item->height()/2)).toPoint());
        };
        QVERIFY(client->property("checked").toBool());
        click(host);
        QTRY_VERIFY(network->hosting()); QTRY_COMPARE(observer.peers().size(), 1);
        QVERIFY(host->property("checked").toBool()); QVERIFY(!client->property("checked").toBool());
        click(host); // An already selected radio option stays selected.
        QVERIFY(host->property("checked").toBool());
        click(client);
        QTRY_COMPARE(network->mode(), NetworkDriveController::ClientMode);
        QTRY_VERIFY(network->connected() && observer.peers().isEmpty());
        QVERIFY(client->property("checked").toBool()); QVERIFY(!host->property("checked").toBool());
        network->setMode(NetworkDriveController::HostMode);
        QTRY_VERIFY(network->hosting()); QTRY_VERIFY(host->property("checked").toBool());

        for (const auto size : {QSize(360, 320), QSize(760, 540), QSize(560, 440)}) {
            preferences->resize(size); QTRY_COMPARE(preferences->size(), size);
            QTRY_VERIFY(QRectF(QPointF(), size).contains(done->mapToScene(QPointF(done->width()/2, done->height()/2))));
        }
        const auto screenshot = qEnvironmentVariable("SOCIETY_PREFERENCES_SCREENSHOT_PATH");
        if (!screenshot.isEmpty()) { QTest::qWait(150); QVERIFY(preferences->grabWindow().save(screenshot)); }
        click(done); QTRY_VERIFY(!preferences->isVisible());
        QVERIFY(window->isVisible()); QVERIFY(network->hosting());
        window->requestActivate();
        QTRY_VERIFY(window->isActive());
        QTest::keySequence(window, QKeySequence(QStringLiteral("Ctrl+,")));
        QTRY_VERIFY(preferences->isVisible());
        QCOMPARE(window->findChildren<QQuickWindow *>("preferencesWindow").size(), 1);
        QVERIFY(host->property("checked").toBool());
        for (const auto &key : {QKeySequence(Qt::Key_Escape), QKeySequence(QKeySequence::Close)}) {
            preferences->requestActivate(); QTRY_VERIFY(preferences->isActive());
            QTest::keySequence(preferences, key);
            QTRY_VERIFY(!preferences->isVisible());
            QVERIFY(window->isVisible()); QVERIFY(network->hosting());
            QVERIFY(QMetaObject::invokeMethod(window, "openPreferences"));
            QTRY_VERIFY(preferences->isVisible());
        }
        QVERIFY(QMetaObject::invokeMethod(window, "openPreferences"));
        QCOMPARE(window->findChild<QQuickWindow *>("preferencesWindow"), preferences);
        auto *devices = preferences->findChild<QQuickItem *>("preferencesDevices");
        QVERIFY(devices); click(devices);
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
    qunsetenv("SOCIETY_CONTAINER_PATH");
    lvrs::postApplicationBootstrap(app, options);
    SocietyDriveTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_drive.moc"

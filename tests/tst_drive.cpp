#include "Society/Dashboard/CalendarDatabase.h"
#include "native_link.h"
#include "Society/Drive/DriveController.h"
#include "Society/Drive/CloudFilesController.h"
#include "Society/Environment/EnvironmentAppsModel.h"
#include "Society/Account/AccountController.h"
#include "AccountServer.h"
#include "MemorySessionStore.h"
#include <QBuffer>
#include <QNetworkProxy>
#include "Society/Drive/StorageNavigation.h"
#include "Society/Native/NativeNavigationSurface.h"
#include "Society/Native/LegacyNavigationBridge.h"
#include "Society/Dashboard/DashboardFiles.h"
#include "Society/Dashboard/DashboardCalendar.h"
#include "Society/Dashboard/CalendarWorkspace.h"
#include "Society/Dashboard/ProjectWorkspace.h"
#include "Society/Tools/ModelMergeController.h"
#include "Society/Tools/ModelPackagingController.h"
#include "Society/Tools/ModelCalibratorLauncher.h"
#include "Society/Tools/MergeModelCatalog.h"
#include "Society/Models/StorageModels.h"
#include "Society/Files/DirectoryLocation.h"
#include "Society/Files/ModelImporter.h"
#include <StorageMap.h>
#include <iiSocietyContainer/PreviewCache.h>
#include <ModelType.h>
#include <ModelStore.h>
#include <FileRepository.h>
#include <TemporaryLease.h>
#include "backend/runtime/appbootstrap.h"

#include <QDir>
#include <QAbstractItemModel>
#include <QColor>
#include <QFile>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QMap>
#include <QGuiApplication>
#include <QFontMetricsF>
#include <QAccessible>
#include <QImage>
#include <QJSValue>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlError>
#include <QQmlListReference>
#include <QQmlProperty>
#include <QQuickItem>
#include <QQuickItemGrabResult>
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
#include "Society/Network/NetworkDriveController.h"
#include "MobileNetworkDevice.h"
#include "Society/Network/DevicePairing.h"
#include "Society/Network/PairingQr.h"
#include "Society/Network/QrScanner.h"
#include <SharedStorage.h>
#include <ApplicationStorage.h>
#include <QStorageInfo>
#include <FilesView.h>
#include <filesystem>
#include <optional>

#ifdef Q_OS_MACOS
QRectF societyNativeTitleBarControlsRect(QQuickWindow *window);
bool societyNativeWindowHasTranslucentBackdrop(QQuickWindow *window);
#endif

static QQuickItem *visualItem(QQuickItem *parent, const QString &name)
{
    if (parent->objectName() == name) return parent;
    for (auto *child : parent->childItems())
        if (auto *found = visualItem(child, name)) return found;
    return nullptr;
}

static bool clickNavigationSegment(QQuickWindow *window, int index)
{
    auto *segments = window->findChild<NativeNavigationSurface *>("dashboardTabs");
    if (!segments || !segments->isVisible() || index < 0 || index >= 7) return false;
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
        segments->mapToScene(QPointF(11 + 24 * index, 11)).toPoint());
    return true;
}

static void loadSociety(QQmlApplicationEngine &engine)
{
    engine.load(QUrl::fromLocalFile(QString::fromUtf8(SOCIETY_QML_FILE)));
    if (auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0)))
        LegacyNavigationBridge::install(window);
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
    void modelCardRenameUpdatesFileCompanionsAndView() {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/model-rename-view-XXXXXX.societycontainer");
        QVERIFY(fixture.isValid());
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        const auto source = fixture.filePath("Models/Checkpoint/old.safetensors");
        const auto renamed = fixture.filePath("Models/Checkpoint/new.safetensors");
        const auto taken = fixture.filePath("Models/Checkpoint/taken.safetensors");
        const auto other = fixture.filePath("Models/Other/other.safetensors");
        QVERIFY(writeCatalogModel(source, {})); QVERIFY(writeCatalogModel(taken, {}));
        QVERIFY(writeCatalogModel(other, {}));
        QFile companion(source + ".model.json"); QVERIFY(companion.open(QIODevice::WriteOnly));
        QVERIFY(companion.write("{\"type\":\"Checkpoint\"}") > 0); companion.close();
        QFile preview(fixture.filePath("Models/Checkpoint/old.preview.png"));
        QVERIFY(preview.open(QIODevice::WriteOnly)); QVERIFY(preview.write("preview") > 0); preview.close();
        const auto storage = iiSocietyContainer::SharedStorage::open(fixture.path()); QVERIFY(storage);
        QString error;
        QVERIFY2(storage->recordFile(iiSocietyContainer::StoreSection::Models, "Checkpoint/old.safetensors", &error), qPrintable(error));
        QVERIFY2(storage->recordFile(iiSocietyContainer::StoreSection::Models, "Other/other.safetensors", &error), qPrintable(error));
        QVERIFY(!QFileInfo::exists(other + ".iisacc.json"));
        QVERIFY(QFileInfo::exists(fixture.filePath(".society-runtime/classifications/models/Other/other.safetensors.iisacc.json")));
        const auto before = storage->fileTracking(iiSocietyContainer::StoreSection::Models, "Checkpoint/old.safetensors");
        QVERIFY(!before.value("objectKey").toString().isEmpty());

        QQmlApplicationEngine engine;
        engine.addImageProvider(QStringLiteral("society-preview"), new iiSocietyContainer::PreviewProvider);
        QStringList warnings;
        connect(&engine, &QQmlApplicationEngine::warnings, this, [&](const QList<QQmlError> &errors) {
            for (const auto &entry : errors) warnings.append(entry.toString());
        });
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        engine.setInitialProperties({{"initialContainerPath", fixture.path()}, {"width", 1440}, {"height", 900}});
        loadSociety(engine);
        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *drive = window->findChild<DriveController *>("driveController"); QVERIFY(drive);
        auto *importer = window->findChild<ModelImporter *>("modelImporter"); QVERIFY(importer);
        QTRY_COMPARE(importer->containerPath(), fixture.path());
        QTest::qWait(50);
        QTRY_VERIFY_WITH_TIMEOUT(!importer->busy(), 30000);
        QVERIFY(window->setProperty("selectedTab", "Storage"));
        QQuickItem *storageView = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((storageView = window->findChild<QQuickItem *>("storageView")), 30000);
        QVERIFY(drive->openSection("models"));
        QQuickItem *models = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((models = window->findChild<QQuickItem *>("modelsView")) && models->isVisible(), 30000);
        auto *catalog = window->findChild<StorageModels *>("modelCatalog"); QVERIFY(catalog);
        QTRY_COMPARE_WITH_TIMEOUT(catalog->count(), 3, 30000);
        const auto otherRows = catalog->groups().value("Other").toList();
        QCOMPARE(otherRows.size(), 1);
        QCOMPARE(otherRows.first().toMap().value("path").toString(), other);
        for (const auto &group : catalog->groups()) for (const auto &row : group.toList())
            QVERIFY(!row.toMap().value("path").toString().endsWith(".iisacc.json", Qt::CaseInsensitive));
        auto *browser = models->findChild<QQuickItem *>("modelFilesBrowser"); QVERIFY(browser);
        QVERIFY(drive->navigate(fixture.filePath("Models/Checkpoint")));
        QTRY_COMPARE(browser->property("path").toString(), fixture.filePath("Models/Checkpoint"));
        QTRY_COMPARE_WITH_TIMEOUT(browser->property("count").toInt(), 2, 30000);
        QQuickItem *cell = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((cell = visualItem(browser, "filesCell_0_0")), 30000);
        QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier, cell->mapToScene(cell->boundingRect().center()).toPoint());
        auto *menu = browser->findChild<QObject *>("filesContextMenu"); QVERIFY(menu);
        QTRY_VERIFY(menu->property("opened").toBool());
        QCOMPARE(menu->property("filePath").toString(), source);
        auto *actions = menu->property("handler").value<QObject *>(); QVERIFY(actions);
        QSignalSpy completed(actions, SIGNAL(completed(QString))), failed(actions, SIGNAL(failed(QString)));
        QVERIFY(completed.isValid() && failed.isValid());
        QVERIFY(QMetaObject::invokeMethod(menu, "triggerEntry", Q_ARG(QVariant, 6)));
        auto *sheet = menu->findChild<QObject *>("renameFileSheet"); QVERIFY(sheet);
        QTRY_VERIFY(sheet->property("opened").toBool());
        auto *input = sheet->findChild<QObject *>("fileNameInput"); QVERIFY(input);
        auto *confirm = sheet->findChild<QObject *>("confirmFileRename"); QVERIFY(confirm);
        auto *dismiss = sheet->findChild<QObject *>("dismissFileRename"); QVERIFY(dismiss);
        QVERIFY(input->setProperty("text", "taken.safetensors"));
        QVERIFY(QMetaObject::invokeMethod(confirm, "clicked"));
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 30000);
        QVERIFY(sheet->property("visible").toBool());
        QVERIFY(QFileInfo::exists(source)); QVERIFY(QFileInfo::exists(taken));
        const auto nativeDrive = iiSocietyContainer::SocietyDrive::open(fixture.path()); QVERIFY(nativeDrive);
        const auto repositoryId = nativeDrive->storageKind() == iiSocietyContainer::SocietyDrive::StorageKind::LogicalDisk
            ? nativeDrive->storageIdentifier() : nativeDrive->identifier();
        const auto leasePath = iiSocietyContainer::FileRepository::packagePath(
            std::filesystem::path(fixture.path().toStdU16String()), repositoryId.toStdString()) / "operations";
        std::optional<iiFileProvider::TemporaryLease> lease;
        lease.emplace(leasePath, iiFileProvider::TemporaryLease::Mode::TryExclusive); QVERIFY(lease->locked());
        QVERIFY(input->setProperty("text", "new.safetensors"));
        QVERIFY(QMetaObject::invokeMethod(confirm, "clicked"));
        QTRY_VERIFY(actions->property("busy").toBool());
        QTRY_COMPARE(dismiss->property("text").toString(), QStringLiteral("Close"));
        QVERIFY(QFileInfo::exists(source)); QVERIFY(!QFileInfo::exists(renamed));
        QVERIFY(QMetaObject::invokeMethod(dismiss, "clicked"));
        QTRY_VERIFY(!sheet->property("visible").toBool());
        QFile raced(renamed); QVERIFY(raced.open(QIODevice::WriteOnly));
        QCOMPARE(raced.write("other model"), 11); raced.close();
        lease.reset();
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 2, 30000);
        QTRY_VERIFY(sheet->property("visible").toBool());
        QVERIFY(QFileInfo::exists(source));
        QVERIFY(raced.open(QIODevice::ReadOnly)); QCOMPARE(raced.readAll(), QByteArray("other model")); raced.close();
        auto *status = sheet->findChild<QObject *>("renameStatus"); QVERIFY(status);
        QTRY_VERIFY(status->property("visible").toBool());
        QVERIFY(QFile::remove(renamed));
        QVERIFY(QMetaObject::invokeMethod(confirm, "clicked"));
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 30000);
        QTRY_VERIFY(!sheet->property("visible").toBool());
        QVERIFY(!QFileInfo::exists(source)); QVERIFY(QFileInfo::exists(renamed));
        QVERIFY(QFileInfo::exists(renamed + ".model.json"));
        QVERIFY(!QFileInfo::exists(renamed + ".iisacc.json"));
        QVERIFY(QFileInfo::exists(fixture.filePath(".society-runtime/classifications/models/Checkpoint/new.safetensors.iisacc.json")));
        QVERIFY(QFileInfo::exists(fixture.filePath("Models/Checkpoint/new.preview.png")));
        QTRY_VERIFY_WITH_TIMEOUT(catalog->groups().value("Checkpoint").toList().first().toMap().value("path").toString() == renamed, 30000);
        const auto after = storage->fileTracking(iiSocietyContainer::StoreSection::Models, "Checkpoint/new.safetensors", &error);
        QVERIFY2(!after.isEmpty(), qPrintable(error));
        QCOMPARE(after.value("objectKey"), before.value("objectKey"));
        const auto store = iiSocietyContainer::ModelStore::open(fixture.path()); QVERIFY(store);
        QCOMPARE(store->resolve("Checkpoint/old.safetensors", &error), renamed);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        window->close();
    }

    void preloadedModelsAreSharedAndHeavyToolViewsAreDeferred() {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/preloaded-viewmodels-XXXXXX.societycontainer");
        QVERIFY(fixture.isValid());
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        QVERIFY(writeCatalogModel(fixture.filePath("Models/Checkpoint/example.safetensors"), {}));
        QFile file(fixture.filePath("Files/preload.txt"));
        QVERIFY(file.open(QIODevice::WriteOnly)); QVERIFY(file.write("content") > 0); file.close();

        QQmlApplicationEngine engine;
        engine.addImageProvider(QStringLiteral("society-preview"), new iiSocietyContainer::PreviewProvider);
        QStringList warnings;
        connect(&engine, &QQmlApplicationEngine::warnings, this, [&](const QList<QQmlError> &errors) {
            for (const auto &error : errors) warnings.append(error.toString());
        });
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        engine.setInitialProperties({{"initialContainerPath", fixture.path()}, {"mobileLayout", true}});
        loadSociety(engine);
        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *drive = window->findChild<DriveController *>("driveController"); QVERIFY(drive);
        QTRY_VERIFY_WITH_TIMEOUT(drive->contentsAvailable(), 30000);
        auto *dashboard = window->findChild<QQuickItem *>("dashboardView"); QVERIFY(dashboard);
        auto *browser = window->findChild<QQuickItem *>("dashboardFilesBrowser");
        QTRY_VERIFY_WITH_TIMEOUT((browser = window->findChild<QQuickItem *>("dashboardFilesBrowser")), 30000);
        auto *directory = qvariant_cast<QAbstractItemModel *>(browser->property("directoryModel"));
        QTRY_VERIFY_WITH_TIMEOUT((directory = qvariant_cast<QAbstractItemModel *>(browser->property("directoryModel"))), 30000);
        QTRY_VERIFY_WITH_TIMEOUT(!browser->property("loading").toBool(), 30000);
        auto *catalog = window->findChild<StorageModels *>("modelCatalog"); QVERIFY(catalog);
        QTRY_COMPARE_WITH_TIMEOUT(catalog->directory(), fixture.filePath("Models"), 30000);
        QTRY_VERIFY_WITH_TIMEOUT(!catalog->loading(), 30000);
        QCOMPARE(catalog->count(), 1);
        QVERIFY(!window->findChild<QQuickItem *>("storageView"));
        QVERIFY(!window->findChild<QQuickItem *>("modelsView"));
        QVERIFY(!window->findChild<QQuickItem *>("toolsView"));

        // A rapid detour must not keep constructing an unvisited page.
        for (const auto &tab : {QStringLiteral("Tools"), QStringLiteral("Storage"), QStringLiteral("Environment")}) {
            auto *loader = window->findChild<QObject *>(tab.toLower() + "Loader"); QVERIFY(loader);
            QVERIFY(window->setProperty("selectedTab", tab));
            QVERIFY(window->setProperty("selectedTab", "Dashboard"));
            QTRY_COMPARE(loader->property("status").toInt(), 0);
        }
        auto *projectsLoader = dashboard->findChild<QObject *>("dashboardProjectsLoader"); QVERIFY(projectsLoader);
        QVERIFY(QMetaObject::invokeMethod(dashboard, "showProjects"));
        QVERIFY(QMetaObject::invokeMethod(dashboard, "goHome"));
        QTRY_COMPARE(projectsLoader->property("status").toInt(), 0);
        auto *calendarLoader = dashboard->findChild<QObject *>("calendarWorkspaceLoader"); QVERIFY(calendarLoader);
        QVERIFY(QMetaObject::invokeMethod(dashboard, "showCalendar", Q_ARG(QString, "")));
        QVERIFY(QMetaObject::invokeMethod(dashboard, "goHome"));
        QTRY_COMPARE(calendarLoader->property("status").toInt(), 0);

        QSignalSpy folderChanges(directory, SIGNAL(folderChanged()));
        QVERIFY(window->setProperty("selectedTab", "Storage"));
        auto *content = window->findChild<QObject *>("societyContent"); QVERIFY(content);
        auto *storageLoader = window->findChild<QObject *>("storageLoader"); QVERIFY(storageLoader);
        const bool storageWasReady = qvariant_cast<QQuickItem *>(storageLoader->property("item")) != nullptr;
        QVERIFY(QMetaObject::invokeMethod(content, "openStorageActions"));
        if (!storageWasReady) QVERIFY(content->property("pendingStorageActions").toBool());
        auto *storage = window->findChild<QQuickItem *>("storageView");
        QTRY_VERIFY_WITH_TIMEOUT((storage = window->findChild<QQuickItem *>("storageView")), 30000);
        QTRY_VERIFY(storage->isVisible());
        QObject *actions = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((actions = window->findChild<QObject *>("storageActions")), 30000);
        QTRY_VERIFY_WITH_TIMEOUT(actions->property("visible").toBool(), 30000);
        QVERIFY(QMetaObject::invokeMethod(actions, "close"));
        QTRY_VERIFY(!actions->property("visible").toBool());
        QTRY_COMPARE(drive->currentSection(), QString("Files"));
        auto *storageBrowser = storage->findChild<QQuickItem *>("filesBrowser"); QVERIFY(storageBrowser);
        QTRY_COMPARE(qvariant_cast<QAbstractItemModel *>(storageBrowser->property("directoryModel")), directory);
        QTRY_VERIFY(!storageBrowser->property("loading").toBool());
        QCOMPARE(folderChanges.count(), 0);
        QCOMPARE(window->findChild<StorageModels *>("modelCatalog"), catalog);
        QVERIFY(!window->findChild<QQuickItem *>("modelsView"));

        QSignalSpy catalogLoading(catalog, SIGNAL(loadingChanged()));
        QVERIFY(drive->openSection("models"));
        QVERIFY(drive->openSection("files"));
        auto *modelsLoader = storage->findChild<QObject *>("storageModelsLoader"); QVERIFY(modelsLoader);
        QTRY_COMPARE(modelsLoader->property("status").toInt(), 0);
        QVERIFY(!window->findChild<QQuickItem *>("modelsView"));
        QVERIFY(drive->openSection("models"));
        QQuickItem *models = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((models = window->findChild<QQuickItem *>("modelsView")), 30000);
        QTRY_VERIFY(models->isVisible());
        QTest::qWait(100);
        QCOMPARE(catalogLoading.count(), 0);
        QCOMPARE(catalog->count(), 1);

        QVERIFY(window->setProperty("selectedTab", "Tools"));
        auto *tools = window->findChild<QQuickItem *>("toolsView");
        QTRY_VERIFY_WITH_TIMEOUT((tools = window->findChild<QQuickItem *>("toolsView")), 30000);
        QVERIFY(!window->findChild<QQuickItem *>("modelMergeTool"));
        QVERIFY(!window->findChild<QQuickItem *>("modelPackagingTool"));
        auto *mergeCatalog = window->findChild<MergeModelCatalog *>("mergeModelCatalog"); QVERIFY(mergeCatalog);
        QTRY_COMPARE_WITH_TIMEOUT(mergeCatalog->directory(), fixture.filePath("Models"), 30000);
        QTRY_COMPARE_WITH_TIMEOUT(mergeCatalog->models().size(), 1, 30000);
        QTRY_VERIFY_WITH_TIMEOUT(!mergeCatalog->loading(), 30000);
        QSignalSpy mergeLoading(mergeCatalog, &MergeModelCatalog::loadingChanged);
        QVERIFY(QMetaObject::invokeMethod(tools, "openTool", Q_ARG(QString, "model-packaging")));
        QVERIFY(QMetaObject::invokeMethod(tools, "openTool", Q_ARG(QString, "model-merge")));
        auto *packagingLoader = tools->findChild<QObject *>("packagingToolLoader"); QVERIFY(packagingLoader);
        QTRY_COMPARE(packagingLoader->property("status").toInt(), 0); // The abandoned incubation was cancelled.
        QQuickItem *merge = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((merge = window->findChild<QQuickItem *>("modelMergeTool")), 30000);
        QVERIFY(merge->isVisible());
        QCOMPARE(window->findChild<MergeModelCatalog *>("mergeModelCatalog"), mergeCatalog);
        QVERIFY(!window->findChild<QQuickItem *>("modelPackagingTool"));
        QTest::qWait(200);
        QCOMPARE(mergeLoading.count(), 0);
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    }

    void unvisitedTabsAreDeferredAndKeepTheirStateAfterNavigation() {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/lazy-tabs-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        QQmlApplicationEngine engine;
        engine.addImageProvider(QStringLiteral("society-preview"), new iiSocietyContainer::PreviewProvider);
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        engine.setInitialProperties({{"initialContainerPath", fixture.path()}});
        loadSociety(engine);
        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        QVERIFY(window->findChild<QQuickItem *>("dashboardView"));
        QVERIFY(!window->findChild<QQuickItem *>("toolsView"));
        QVERIFY(!window->findChild<QQuickItem *>("storageView"));
        QVERIFY(!window->findChild<QQuickItem *>("environmentView"));
        auto *calendar = window->findChild<DashboardCalendar *>("dashboardCalendarModel"); QVERIFY(calendar);
        QVERIFY(calendar->databasePath().isEmpty());
        QVERIFY(window->setProperty("selectedTab", "Tools"));
        auto *toolsLoader = window->findChild<QObject *>("toolsLoader"); QVERIFY(toolsLoader);
        QTRY_COMPARE_WITH_TIMEOUT(toolsLoader->property("status").toInt(), 1, 15000); // Loader.Ready
        auto *tools = window->findChild<QQuickItem *>("toolsView");
        QVERIFY(tools->setProperty("query", "retained query"));
        QVERIFY(window->setProperty("selectedTab", "Storage"));
        auto *storageLoader = window->findChild<QObject *>("storageLoader"); QVERIFY(storageLoader);
        QTRY_COMPARE_WITH_TIMEOUT(storageLoader->property("status").toInt(), 1, 15000);
        auto *content = window->findChild<QObject *>("societyContent"); QVERIFY(content);
        QVERIFY(QMetaObject::invokeMethod(content, "showEnvironmentDevices"));
        QVERIFY(window->setProperty("selectedTab", "Environment"));
        auto *environmentLoader = window->findChild<QObject *>("environmentLoader"); QVERIFY(environmentLoader);
        QTRY_COMPARE_WITH_TIMEOUT(environmentLoader->property("status").toInt(), 1, 15000);
        auto *environment = window->findChild<QQuickItem *>("environmentView"); QVERIFY(environment);
        QCOMPARE(environment->property("page").toString(), QString("Devices"));
        QVERIFY(window->setProperty("selectedTab", "Tools"));
        QTRY_VERIFY(tools->isVisible());
        QCOMPARE(window->findChild<QQuickItem *>("toolsView"), tools);
        QCOMPARE(tools->property("query").toString(), QString("retained query"));
        QVERIFY(window->setProperty("selectedTab", "Dashboard"));
        auto *dashboard = window->findChild<QObject *>("dashboardView"); QVERIFY(dashboard);
        QVERIFY(QMetaObject::invokeMethod(dashboard, "showCalendar", Q_ARG(QString, "")));
        QTRY_VERIFY_WITH_TIMEOUT(!calendar->databasePath().isEmpty(), 10000);
    }
    void suiteStorageIsDiscoveredAndAdoptedOnFirstLaunch() {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/suite-default-drive-XXXXXX"); QVERIFY(fixture.isValid()); QString error;
        const auto shared = iiSocietyContainer::ApplicationStorage::openInGroup(fixture.path(), "com.iisacc.dreamscapes", &error);
        QVERIFY2(shared, qPrintable(error));
        const auto settings = qgetenv("SOCIETY_STORAGE_SETTINGS_PATH"), selected = qgetenv("SOCIETY_CONTAINER_PATH"), suite = qgetenv("IISACC_SHARED_STORAGE_ROOT");
        const auto restore = qScopeGuard([&] {
            settings.isNull() ? qunsetenv("SOCIETY_STORAGE_SETTINGS_PATH") : qputenv("SOCIETY_STORAGE_SETTINGS_PATH", settings);
            selected.isNull() ? qunsetenv("SOCIETY_CONTAINER_PATH") : qputenv("SOCIETY_CONTAINER_PATH", selected);
            suite.isNull() ? qunsetenv("IISACC_SHARED_STORAGE_ROOT") : qputenv("IISACC_SHARED_STORAGE_ROOT", suite);
        });
        qputenv("SOCIETY_STORAGE_SETTINGS_PATH", fixture.filePath("selection.json").toUtf8()); qunsetenv("SOCIETY_CONTAINER_PATH");
        qputenv("IISACC_SHARED_STORAGE_ROOT", fixture.path().toUtf8());
        DriveController controller; controller.openDefaultContainer(); QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 60000);
        QVERIFY2(controller.hasDrive(), qPrintable(controller.errorString()));
        QCOMPARE(controller.rootPath(), shared->drive().rootPath()); QCOMPARE(controller.identifier(), shared->drive().identifier());
        QCOMPARE(iiSocietyContainer::ApplicationStorage::registration(fixture.path(), &error).value("managingApplication"), "com.iisacc.society");
    }
    void appOwnedLogicalStorageOpensAndRestoresWithoutCopyingFiles() {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/app-logical-drive-XXXXXX"); QVERIFY(fixture.isValid()); QString error;
        const auto shared = iiSocietyContainer::ApplicationStorage::openInGroup(fixture.path(), "com.iisacc.dreamscapes", &error);
        QVERIFY2(shared, qPrintable(error)); const auto root = shared->drive().rootPath(), id = shared->drive().identifier();
        QFile original(shared->filePath(iiSocietyContainer::StoreSection::GenerationHistory, "before-society.png"));
        QVERIFY(original.open(QIODevice::WriteOnly)); QCOMPARE(original.write("original image"), 14); original.close();
        const auto settings = qgetenv("SOCIETY_STORAGE_SETTINGS_PATH"), selected = qgetenv("SOCIETY_CONTAINER_PATH");
        const auto restore = qScopeGuard([&] {
            settings.isNull() ? qunsetenv("SOCIETY_STORAGE_SETTINGS_PATH") : qputenv("SOCIETY_STORAGE_SETTINGS_PATH", settings);
            selected.isNull() ? qunsetenv("SOCIETY_CONTAINER_PATH") : qputenv("SOCIETY_CONTAINER_PATH", selected);
        });
        qputenv("SOCIETY_STORAGE_SETTINGS_PATH", fixture.filePath("desktop-settings.json").toUtf8()); qunsetenv("SOCIETY_CONTAINER_PATH");
        DriveController first; QVERIFY2(first.openContainer(root), qPrintable(first.errorString())); QCOMPARE(first.identifier(), id);
        QVERIFY2(iiSocietyContainer::ApplicationStorage::adoptInGroup(fixture.path(), &error), qPrintable(error));
        DriveController reopened; reopened.openDefaultContainer(); QTRY_VERIFY_WITH_TIMEOUT(!reopened.busy(), 60000);
        QVERIFY2(reopened.hasDrive(), qPrintable(reopened.errorString())); QCOMPARE(reopened.rootPath(), root); QCOMPARE(reopened.identifier(), id);
        QVERIFY(original.open(QIODevice::ReadOnly)); QCOMPARE(original.readAll(), QByteArray("original image"));
        QVERIFY(!QFileInfo::exists(fixture.filePath("Society.societycontainer")));
    }
    void initTestCase()
    {
        qmlRegisterType<EnvironmentAppsModel>("Society", 1, 0, "EnvironmentAppsModel");
        qmlRegisterType<DirectoryLocation>("Society", 1, 0, "DirectoryLocation");
        qmlRegisterType<FileActions>("Society", 1, 0, "FileActions");
        qmlRegisterType<StorageDirectoryModel>("Society", 1, 0, "StorageDirectoryModel");
        qmlRegisterType<DriveController>("Society", 1, 0, "DriveController");
        qmlRegisterType<CloudFilesController>("Society", 1, 0, "CloudFilesController");
        qmlRegisterType<StorageNavigation>("Society", 1, 0, "StorageNavigation");
        qmlRegisterType<DashboardCalendar>("Society", 1, 0, "DashboardCalendar");
        qmlRegisterType<CalendarWorkspace>("Society", 1, 0, "CalendarWorkspace");
        qmlRegisterType<ProjectWorkspace>("Society", 1, 0, "ProjectWorkspace");
        qmlRegisterType<DashboardFiles>("Society", 1, 0, "DashboardFiles");
        qmlRegisterType<ModelMergeController>("Society", 1, 0, "ModelMergeController");
        qmlRegisterType<ModelPackagingController>("Society", 1, 0, "ModelPackagingController");
        qmlRegisterType<ModelCalibratorLauncher>("Society", 1, 0, "ModelCalibratorLauncher");
        qmlRegisterType<MergeModelCatalog>("Society", 1, 0, "MergeModelCatalog");
        qmlRegisterType<StorageModels>("Society", 1, 0, "StorageModels");
        qmlRegisterType<ModelImporter>("Society", 1, 0, "ModelImporter");
        qmlRegisterType<NetworkDriveController>("Society", 1, 0, "NetworkDriveController");
        qmlRegisterType<AccountController>("Society", 1, 0, "AccountController");
        qmlRegisterType<DevicePairing>("Society", 1, 0, "DevicePairing");
        qmlRegisterType<PairingQr>("Society", 1, 0, "PairingQr");
        qmlRegisterType<QrScanner>("Society", 1, 0, "QrScanner");
    }

    void cloudFilesControllerReachesStorage()
    {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/cloud-wiring-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        QQmlApplicationEngine engine;
        engine.addImageProvider(QStringLiteral("society-preview"), new iiSocietyContainer::PreviewProvider);
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        engine.setInitialProperties({{"initialContainerPath", fixture.path()}});
        loadSociety(engine);
        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        auto *controller = window->findChild<CloudFilesController *>("cloudFilesController"); QVERIFY(controller);
        QVERIFY(window->setProperty("selectedTab", "Storage"));
        QTRY_COMPARE(window->findChild<QObject *>("storageLoader")->property("status").toInt(), 1);
        QTRY_VERIFY(window->findChild<QQuickItem *>("storageView"));
        auto *storage = window->findChild<QQuickItem *>("storageView"); QVERIFY(storage);
        QCOMPARE(storage->property("cloudFiles").value<CloudFilesController *>(), controller);
        QVERIFY(!controller->busy());
    }

    void accountOwnsMovedDiskAndRemoteDevicesKeepTheirLocalMounts()
    {
        QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/account-drive-XXXXXX");
        QVERIFY(fixture.isValid());
        const auto previousSettings = qgetenv("SOCIETY_STORAGE_SETTINGS_PATH");
        const auto reset = qScopeGuard([&] { qputenv("SOCIETY_STORAGE_SETTINGS_PATH", previousSettings); });
        qputenv("SOCIETY_STORAGE_SETTINGS_PATH", fixture.filePath("settings.json").toUtf8());
        const auto disk = iiSocietyContainer::SocietyDrive::createAt(fixture.path());
        QVERIFY(disk);
        QString image = disk->rootPath();
        AccountServer authority; QVERIFY(authority.server.listen(QHostAddress::LocalHost));
        iisacc::accounts::AccountManager host(authority.url()), peer(authority.url());
        auto device = host.deviceInfo(); device["appId"] = "com.iisacc.society"; device["id"] = QString(64, 'a');
        QVERIFY(host.setDeviceInfo(device)); device["id"] = QString(64, 'b'); QVERIFY(peer.setDeviceInfo(device));
        QVERIFY(host.loginWithPassword("builder@example.com", "fixture")); QTRY_VERIFY(host.isAuthenticated());
        QVERIFY(peer.loginWithPassword("builder@example.com", "fixture")); QTRY_VERIFY(peer.isAuthenticated());
        DriveController local, remote; local.setAccountManager(&host); remote.setAccountManager(&peer);
        QVERIFY(local.openContainer(disk->rootPath()));
        const auto replica = fixture.filePath("local-replica.societycontainer"); QVERIFY(QDir().mkpath(replica));
        QVERIFY(iiSocietyContainer::SocietyDrive::create(replica)); QVERIFY(remote.openContainer(replica));
        // Opening the existing host disk while signed in must register it automatically.
        QTRY_COMPARE(host.account()->societyContainerDrive().value("imagePath").toString(), image);
        const auto offline = fixture.filePath("offline-folder");
        QVERIFY(QDir().rename(image, offline)); image = offline;
        QVERIFY(!local.reloadFromDisk()); QVERIFY(!local.hasDrive());
        QQmlEngine onboardingEngine;
        onboardingEngine.addImageProvider(QStringLiteral("society-preview"), new iiSocietyContainer::PreviewProvider);
        QQmlComponent onboardingComponent(&onboardingEngine, QUrl::fromLocalFile(QFileInfo(QStringLiteral(SOCIETY_QML_FILE)).dir().filePath("OnboardingView.qml")));
        NetworkDriveController network;
        QScopedPointer<QObject> onboarding(onboardingComponent.createWithInitialProperties({{"drive", QVariant::fromValue(&local)}, {"network", QVariant::fromValue(&network)}}));
        QVERIFY2(onboarding, qPrintable(onboardingComponent.errorString()));
        auto* locate = onboarding->findChild<QQuickItem*>("onboardingChooseDisk");
        QVERIFY(locate); QVERIFY(locate->isVisible());
        const auto moved = fixture.filePath("MovedSociety.societycontainer"); QVERIFY(QDir().rename(image, moved)); image = moved;
        QVERIFY(local.openContainerImage(QUrl::fromLocalFile(moved)));
        QTRY_VERIFY_WITH_TIMEOUT(!local.busy(), 60000);
        QVERIFY2(local.hasDrive(), qPrintable(local.errorString())); QCOMPARE(local.identifier(), disk->identifier());
        QTRY_COMPARE(host.account()->societyContainerDrive().value("imagePath").toString(), moved);
        QVERIFY(peer.refreshContainerDrive());
        QTRY_COMPARE(peer.account()->societyContainerDrive(), host.account()->societyContainerDrive());
        QCOMPARE(remote.rootPath(), replica); // A remote host path is metadata, never a local mount.
        QVERIFY(!remote.saveContainerToAccount());
        QCOMPARE(peer.account()->societyContainerDrive().value("containerId").toString(), disk->identifier());
    }

    void movedPackageRepairsTheSameHostAccountLocation()
    {
        QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/account-package-moved-XXXXXX");
        const auto previousSettings = qgetenv("SOCIETY_STORAGE_SETTINGS_PATH");
        const auto reset = qScopeGuard([&] { qputenv("SOCIETY_STORAGE_SETTINGS_PATH", previousSettings); });
        qputenv("SOCIETY_STORAGE_SETTINGS_PATH", fixture.filePath("settings.json").toUtf8());
        const auto package = iiSocietyContainer::SocietyDrive::createAt(fixture.path()); QVERIFY(package);
        QVERIFY(iiSocietyContainer::SharedStorage::setDefaultContainer(package->rootPath()));
        AccountServer authority; QVERIFY(authority.server.listen(QHostAddress::LocalHost));
        const auto hostId = QString(64, 'a');
        authority.profile.insert("societyContainerDrive", QJsonObject{
            {"containerId", package->identifier()}, {"imagePath", fixture.filePath("Society")},
            {"hostDeviceId", hostId}, {"revision", QUuid::createUuid().toString(QUuid::WithoutBraces)}});
        iisacc::accounts::AccountManager host(authority.url());
        auto device = host.deviceInfo(); device["appId"] = "com.iisacc.society"; device["id"] = hostId;
        QVERIFY(host.setDeviceInfo(device));
        QVERIFY(host.loginWithPassword("builder@example.com", "fixture")); QTRY_VERIFY(host.isAuthenticated());
        DriveController controller; QVERIFY(controller.openContainer(package->rootPath()));
        controller.setAccountManager(&host); controller.openDefaultContainer();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 60000);
        QVERIFY(controller.hasDrive()); QCOMPARE(controller.identifier(), package->identifier());
        QTRY_COMPARE(host.account()->societyContainerDrive().value("imagePath").toString(), package->rootPath());
        QVERIFY(!QFileInfo::exists(fixture.filePath("Society")));
    }

    void accountLegacyImageDoesNotReplaceTheCurrentOrdinaryFolder()
    {
        QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/account-legacy-image-XXXXXX");
        QVERIFY(fixture.isValid());
        const auto previousSettings = qgetenv("SOCIETY_STORAGE_SETTINGS_PATH");
        const auto previousContainer = qgetenv("SOCIETY_CONTAINER_PATH");
        const auto restore = qScopeGuard([&] {
            qputenv("SOCIETY_STORAGE_SETTINGS_PATH", previousSettings);
            qputenv("SOCIETY_CONTAINER_PATH", previousContainer);
        });
        const auto settingsPath = fixture.filePath("settings.json");
        qputenv("SOCIETY_STORAGE_SETTINGS_PATH", settingsPath.toUtf8());
        qunsetenv("SOCIETY_CONTAINER_PATH");
        const auto ordinary = iiSocietyContainer::SocietyDrive::createAt(fixture.path()); QVERIFY(ordinary);
        DriveController drive; QVERIFY(drive.openContainer(ordinary->rootPath()));
        QFile settings(settingsPath); QVERIFY(settings.open(QIODevice::ReadOnly));
        const auto settingsBytes = settings.readAll(); settings.close();
        const auto image = fixture.filePath("Old.sparsebundle");
        QVERIFY(QDir().mkpath(image + "/bands"));
        QFile payload(image + "/bands/preserved"); QVERIFY(payload.open(QIODevice::WriteOnly));
        payload.write("old image bytes"); payload.close();
        AccountServer authority; QVERIFY(authority.server.listen(QHostAddress::LocalHost));
        const auto hostId = QString(64, 'a');
        authority.profile.insert("societyContainerDrive", QJsonObject{
            {"containerId", ordinary->identifier()}, {"imagePath", image},
            {"hostDeviceId", hostId}, {"revision", QUuid::createUuid().toString(QUuid::WithoutBraces)}});
        iisacc::accounts::AccountManager host(authority.url());
        auto device = host.deviceInfo(); device["appId"] = "com.iisacc.society"; device["id"] = hostId;
        QVERIFY(host.setDeviceInfo(device));
        QVERIFY(host.loginWithPassword("builder@example.com", "fixture")); QTRY_VERIFY(host.isAuthenticated());
        drive.setAccountManager(&host);
        QTRY_VERIFY(drive.errorString().contains("not restored"));
        QVERIFY(drive.hasDrive()); QCOMPARE(drive.rootPath(), ordinary->rootPath());
        drive.openDefaultContainer(); QVERIFY(!drive.busy());
        QVERIFY(drive.hasDrive()); QCOMPARE(drive.identifier(), ordinary->identifier());
        QVERIFY(drive.openContainerImage(QUrl::fromLocalFile(image)));
        QTRY_VERIFY_WITH_TIMEOUT(!drive.busy(), 60000);
        QVERIFY(drive.errorString().contains("not restored"));
        QCOMPARE(drive.rootPath(), ordinary->rootPath());
        QVERIFY(settings.open(QIODevice::ReadOnly)); QCOMPARE(settings.readAll(), settingsBytes);
        QVERIFY(payload.open(QIODevice::ReadOnly)); QCOMPARE(payload.readAll(), QByteArray("old image bytes"));
        QCOMPARE(host.account()->societyContainerDrive().value("imagePath").toString(), image);
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
        engine.addImageProvider(QStringLiteral("society-preview"), new iiSocietyContainer::PreviewProvider);
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

    void creationApisCreateOnlyAPackageInTheSelectedParent_data()
    {
        QTest::addColumn<bool>("useUrl");
        QTest::newRow("createContainerAt") << false;
        QTest::newRow("openContainerUrl") << true;
    }

    void creationApisCreateOnlyAPackageInTheSelectedParent()
    {
        QFETCH(bool, useUrl);
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/package-creation-XXXXXX");
        QVERIFY(fixture.isValid());
        const auto previousSettings = qgetenv("SOCIETY_STORAGE_SETTINGS_PATH");
        const auto previousContainer = qgetenv("SOCIETY_CONTAINER_PATH");
        const auto previousNativeGate = qgetenv("SOCIETY_DISABLE_NATIVE_DRIVE");
        const auto restore = qScopeGuard([&] {
            qputenv("SOCIETY_STORAGE_SETTINGS_PATH", previousSettings);
            if (previousContainer.isNull()) qunsetenv("SOCIETY_CONTAINER_PATH");
            else qputenv("SOCIETY_CONTAINER_PATH", previousContainer);
            if (previousNativeGate.isNull()) qunsetenv("SOCIETY_DISABLE_NATIVE_DRIVE");
            else qputenv("SOCIETY_DISABLE_NATIVE_DRIVE", previousNativeGate);
        });
        const auto settingsPath = fixture.filePath("settings.json");
        qputenv("SOCIETY_STORAGE_SETTINGS_PATH", settingsPath.toUtf8());
        qunsetenv("SOCIETY_CONTAINER_PATH");
        qputenv("SOCIETY_DISABLE_NATIVE_DRIVE", "1");
        const auto parent = fixture.filePath("Society");
        QVERIFY(QDir().mkdir(parent));
        QVERIFY(QDir::isAbsolutePath(parent));
        QFile userFile(parent + "/user.txt");
        QVERIFY(userFile.open(QIODevice::WriteOnly));
        QCOMPARE(userFile.write("preserved user content"), qint64(22)); userFile.close();
        const auto entries = QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot;
        QCOMPARE(QDir(parent).entryList(entries, QDir::Name), QStringList{"user.txt"});
        const auto package = parent + "/Society.societycontainer";
        QVERIFY(!QFileInfo::exists(package)); QVERIFY(!QFileInfo::exists(settingsPath));
        DriveController drive;
        QString identifier;
        for (int attempt = 0; attempt < 2; ++attempt) {
            const bool accepted = useUrl ? drive.openContainerUrl(QUrl::fromLocalFile(parent))
                : drive.createContainerAt(parent);
            QVERIFY2(accepted, qPrintable(drive.errorString()));
            QTRY_VERIFY_WITH_TIMEOUT(!drive.busy(), 30000);
            QVERIFY2(drive.hasDrive(), qPrintable(drive.errorString()));
            QCOMPARE(drive.rootPath(), package); QVERIFY(drive.systemPath().isEmpty());
            QVERIFY(!QUuid(drive.identifier()).isNull());
            if (attempt == 0) identifier = drive.identifier();
            else QCOMPARE(drive.identifier(), identifier);
            QCOMPARE(QDir(parent).entryList(entries, QDir::Name),
                QStringList({"Society.societycontainer", "user.txt"}));
            QVERIFY(!QFileInfo::exists(parent + "/.society-drive.json"));
            QCOMPARE(iiSocietyContainer::allStoreSections().size(), 9);
            for (const auto section : iiSocietyContainer::allStoreSections()) {
                const auto name = iiSocietyContainer::storeSectionName(section);
                QVERIFY(!QFileInfo::exists(parent + '/' + name));
                const QFileInfo folder(package + '/' + name);
                QVERIFY(folder.isDir()); QVERIFY(!folder.isSymLink());
            }
            QVERIFY(QDir(package + "/Models").entryList(entries).isEmpty());
            QFile manifest(package + "/.society-drive.json");
            QVERIFY(manifest.open(QIODevice::ReadOnly));
            const auto identity = QJsonDocument::fromJson(manifest.readAll()).object();
            QCOMPARE(identity.value("identifier").toString(), identifier);
            QCOMPARE(identity.value("sections").toArray().size(), 9);
            QFile settings(settingsPath); QVERIFY(settings.open(QIODevice::ReadOnly));
            const auto saved = QJsonDocument::fromJson(settings.readAll()).object();
            QCOMPARE(saved.value("schemaVersion").toInt(), 1);
            QCOMPARE(saved.value("path").toString(), package);
            QCOMPARE(saved.value("containerId").toString(), identifier);
            QVERIFY(!saved.contains("imagePath"));
            QVERIFY(userFile.open(QIODevice::ReadOnly));
            QCOMPARE(userFile.readAll(), QByteArray("preserved user content")); userFile.close();
        }
    }

    void onboardingStartsWithoutAContainer_data()
    {
        QTest::addColumn<QSize>("viewport");
        QTest::newRow("desktop") << QSize(1440, 900);
        QTest::newRow("compact") << QSize(800, 600);
        QTest::newRow("minimum") << QSize(360, 320);
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
        engine.addImageProvider(QStringLiteral("society-preview"), new iiSocietyContainer::PreviewProvider);
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        engine.setInitialProperties({{"width", viewport.width()}, {"height", viewport.height()},
            {"mobileLayout", viewport.width() == 390}});
        loadSociety(engine);
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
        auto *diskDialog = window->findChild<QObject *>("onboardingDiskDialog"); QVERIFY(diskDialog);
        QVERIFY(diskDialog->metaObject()->indexOfProperty("selectedFile") >= 0);
        auto *creationDialog = window->findChild<QObject *>("containerCreationDialog"); QVERIFY(creationDialog);
        auto *onboardingCreationDialog = window->findChild<QObject *>("onboardingFolderDialog"); QVERIFY(onboardingCreationDialog);
        QVERIFY(creationDialog->property("title").toString().contains("Society.societycontainer"));
        QVERIFY(onboardingCreationDialog->property("title").toString().contains("Society.societycontainer"));

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
        QCOMPARE(QFileInfo(container).canonicalFilePath(), container);
        const auto selectedFolder = QUrl::fromLocalFile(container);
        QCOMPARE(selectedFolder.toLocalFile(), container);
        // Exercise the same accepted-folder handler used by the native dialog.
        QVERIFY(QMetaObject::invokeMethod(onboarding, "createDisk", Q_ARG(QVariant, selectedFolder)));
        QVERIFY(drive->busy());
        QVERIFY(!proceed->isEnabled());
        QTRY_VERIFY_WITH_TIMEOUT(!drive->busy(), 120000);
        QVERIFY2(drive->hasDrive(), qPrintable(drive->errorString()));
        QVERIFY(!window->property("onboardingRequired").toBool());
        QVERIFY(!onboarding->isVisible()); QVERIFY(content->isVisible());
        QCOMPARE(drive->rootPath(), container + "/Society.societycontainer");
        QCOMPARE(QStorageInfo(drive->rootPath()).device(), QStorageInfo(container).device());
        QVERIFY(drive->systemPath().isEmpty());
        for (const auto section : iiSocietyContainer::allStoreSections())
            QVERIFY(QFileInfo::exists(drive->rootPath() + '/' + iiSocietyContainer::storeSectionName(section)));
        QVERIFY(QFileInfo::exists(container + "/Society.societycontainer"));
        QVERIFY(!QFileInfo::exists(container + "/Society"));
        QVERIFY(!QFileInfo::exists(drive->rootPath() + "/bands"));
        QVERIFY(drive->navigate(drive->rootPath() + "/Files"));
        QCOMPARE(drive->currentSection(), QString("Files"));
        drive->goUp(); QVERIFY(drive->atRoot());
        const auto shared = iiSocietyContainer::SharedStorage::open(); QVERIFY(shared);
        QCOMPARE(shared->drive().identifier(), drive->identifier());

        // The next window goes directly to the dashboard using the saved location.
        QQmlApplicationEngine reopened;
        reopened.addImageProvider(QStringLiteral("society-preview"), new iiSocietyContainer::PreviewProvider);
        reopened.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        reopened.load(QUrl::fromLocalFile(QString::fromUtf8(SOCIETY_QML_FILE)));
        QCOMPARE(reopened.rootObjects().size(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(!reopened.rootObjects().first()->property("onboardingRequired").toBool(), 60000);
        QCOMPARE(reopened.rootObjects().first()->findChild<DriveController *>("driveController")->identifier(), drive->identifier());
    }

    void mobileShellOpensWithoutAccountContainerOrHost()
    {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/offline-shell-XXXXXX.societycontainer");
        QVERIFY(fixture.isValid());
        const auto previous = qgetenv("SOCIETY_STORAGE_SETTINGS_PATH");
        const auto restore = qScopeGuard([&] { qputenv("SOCIETY_STORAGE_SETTINGS_PATH", previous); });
        qputenv("SOCIETY_STORAGE_SETTINGS_PATH", fixture.filePath("settings.json").toUtf8());
        QQmlApplicationEngine engine;
        engine.addImageProvider(QStringLiteral("society-preview"), new iiSocietyContainer::PreviewProvider);
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        engine.setInitialProperties({{"mobileLayout", true}, {"width", 390}, {"height", 844}});
        loadSociety(engine);
        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *network = window->findChild<NetworkDriveController *>("networkDriveController"); QVERIFY(network);
        auto *drive = window->findChild<DriveController *>("driveController"); QVERIFY(drive);
        QVERIFY(!network->signedIn()); QVERIFY(!network->connected()); QVERIFY(!drive->hasDrive());
        QVERIFY(!window->property("onboardingRequired").toBool());
        auto *content = window->findChild<QQuickItem *>("societyContent"); QVERIFY(content && content->isVisible());
        for (const auto *tab : {"Storage", "Tools", "Dashboard"}) {
            QVERIFY(window->setProperty("selectedTab", tab));
            QCoreApplication::processEvents();
            QCOMPARE(window->property("selectedTab").toString(), QString::fromUtf8(tab));
            QVERIFY(content->isVisible());
        }
        network->disconnectSession();
        QCoreApplication::processEvents();
        QVERIFY(!window->property("onboardingRequired").toBool());
        QVERIFY(content->isVisible());
    }

    void onboardingDoesNotRestoreTheSavedLegacyImage()
    {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/onboarding-no-image-XXXXXX.societycontainer");
        QVERIFY(fixture.isValid());
        const auto previousSettings = qgetenv("SOCIETY_STORAGE_SETTINGS_PATH");
        const auto previousContainer = qgetenv("SOCIETY_CONTAINER_PATH");
        const auto restore = qScopeGuard([&] {
            qputenv("SOCIETY_STORAGE_SETTINGS_PATH", previousSettings);
            qputenv("SOCIETY_CONTAINER_PATH", previousContainer);
        });
        const auto settingsPath = fixture.filePath("settings.json");
        qputenv("SOCIETY_STORAGE_SETTINGS_PATH", settingsPath.toUtf8());
        qunsetenv("SOCIETY_CONTAINER_PATH");
        const auto image = fixture.filePath("Old.sparsebundle");
        QVERIFY(QDir().mkpath(image + "/bands"));
        const auto container = fixture.filePath("unmounted-root");
        const auto configuration = QJsonDocument(QJsonObject{{"schemaVersion", 2},
            {"imagePath", image}, {"path", container}, {"containerId", "legacy-id"}}).toJson();
        QFile saved(settingsPath); QVERIFY(saved.open(QIODevice::WriteOnly));
        QCOMPARE(saved.write(configuration), configuration.size()); saved.close();
        QQmlApplicationEngine engine;
        engine.addImageProvider(QStringLiteral("society-preview"), new iiSocietyContainer::PreviewProvider);
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        loadSociety(engine);
        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *drive = window->findChild<DriveController *>("driveController"); QVERIFY(drive);
        QTRY_VERIFY_WITH_TIMEOUT(!drive->busy(), 60000);
        QVERIFY(window->property("onboardingRequired").toBool());
        QVERIFY(!drive->hasDrive()); QVERIFY(drive->errorString().contains("not restored"));
        auto *retry = window->findChild<QQuickItem *>("onboardingRetry"); QVERIFY(retry);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, retry->mapToScene(retry->boundingRect().center()).toPoint());
        QTRY_VERIFY_WITH_TIMEOUT(!drive->busy(), 60000);
        QVERIFY(window->property("onboardingRequired").toBool());
        QVERIFY(!drive->hasDrive()); QVERIFY(!QFileInfo::exists(container));
        QVERIFY(saved.open(QIODevice::ReadOnly)); QCOMPARE(saved.readAll(), configuration);
    }

    void onboardingRecoversInvalidInitialPath()
    {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/onboarding-cli-XXXXXX");
        QVERIFY(fixture.isValid());
        const auto previousSettings = qgetenv("SOCIETY_STORAGE_SETTINGS_PATH");
        const auto restoreSettings = qScopeGuard([&] { qputenv("SOCIETY_STORAGE_SETTINGS_PATH", previousSettings); });
        qputenv("SOCIETY_STORAGE_SETTINGS_PATH", fixture.filePath("settings.json").toUtf8());
        const auto container = fixture.filePath("existing.societycontainer");
        QVERIFY(QDir().mkpath(container));
        const auto original = iiSocietyContainer::SocietyDrive::create(container); QVERIFY(original);
        QQmlApplicationEngine engine;
        engine.addImageProvider(QStringLiteral("society-preview"), new iiSocietyContainer::PreviewProvider);
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        engine.setInitialProperties({{"initialContainerPath", "relative-path"}});
        loadSociety(engine);
        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QVERIFY(window->property("onboardingRequired").toBool());
        auto *drive = window->findChild<DriveController *>("driveController"); QVERIFY(drive);
        QVERIFY(drive->localContainerPath(QUrl("https://example.invalid/container")).isEmpty());
        auto *onboarding = window->findChild<QQuickItem *>("containerOnboarding"); QVERIFY(onboarding);
        const auto image = QUrl::fromLocalFile(container);
        QVERIFY(QMetaObject::invokeMethod(onboarding, "selectDisk", Q_ARG(QVariant, image)));
        QTRY_VERIFY_WITH_TIMEOUT(!window->property("onboardingRequired").toBool(), 60000);
        QCOMPARE(drive->identifier(), original->identifier());
    }

    void ordinaryFoldersPreserveSettingsDuringReconnect()
    {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/directory-storage-XXXXXX");
        const auto previous = qgetenv("SOCIETY_STORAGE_SETTINGS_PATH");
        const auto restore = qScopeGuard([&] { qputenv("SOCIETY_STORAGE_SETTINGS_PATH", previous); });
        qputenv("SOCIETY_STORAGE_SETTINGS_PATH", fixture.filePath("settings.json").toUtf8());
        DriveController drive;
        QVERIFY(!drive.openContainer(fixture.path()));
        QVERIFY(!QFileInfo::exists(fixture.filePath("Files")));
        const auto original = iiSocietyContainer::SocietyDrive::createAt(fixture.path()); QVERIFY(original);
        QVERIFY(iiSocietyContainer::SharedStorage::setDefaultContainer(original->rootPath()));
        drive.openDefaultContainer();
        QTRY_VERIFY_WITH_TIMEOUT(!drive.busy(), 60000);
        QVERIFY2(drive.hasDrive(), qPrintable(drive.errorString()));
        QCOMPARE(drive.identifier(), original->identifier());
        QVERIFY(drive.systemPath().isEmpty());
        drive.connectToSystem(); QVERIFY(!drive.busy());
        QVERIFY(drive.systemPath().isEmpty());
        drive.refreshSystem(); QVERIFY(!drive.busy());
        QCOMPARE(drive.identifier(), original->identifier());
        QVERIFY(drive.systemPath().isEmpty());
        // This test target has no native adapter. Direct folder access must
        // preserve storage configuration without claiming an OS mount.
        QVERIFY(drive.systemStatus().contains("Society.societycontainer"));
        QVERIFY(QStorageInfo(original->rootPath()).rootPath() != original->rootPath());
        QFile settings(fixture.filePath("settings.json")); QVERIFY(settings.open(QIODevice::ReadOnly));
        const auto configuration = QJsonDocument::fromJson(settings.readAll()).object();
        QCOMPARE(configuration.value("schemaVersion").toInt(), 1);
        QVERIFY(!configuration.contains("imagePath"));
        QVERIFY(QFileInfo::exists(fixture.filePath("Society.societycontainer")));
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
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/alert-figma-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        QQmlApplicationEngine engine;
        engine.addImageProvider(QStringLiteral("society-preview"), new iiSocietyContainer::PreviewProvider);
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        engine.setInitialProperties({{"initialContainerPath", fixture.path()},
            {"width", viewport.width()}, {"height", viewport.height()}});
        loadSociety(engine);
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
        QCOMPARE(primaryColor, QColor("#00E71F"));
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
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/fixed-files-ui-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        QImage photo(32, 32, QImage::Format_RGB32); photo.fill(Qt::blue);
        QVERIFY(photo.save(fixture.filePath("Photos/photo.png")));
        QFile video(fixture.filePath("Photos/video.mp4"));
        QVERIFY(video.open(QIODevice::WriteOnly)); video.write("video fixture"); video.close();
        QQmlApplicationEngine engine;
        engine.addImageProvider(QStringLiteral("society-preview"), new iiSocietyContainer::PreviewProvider);
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        engine.setInitialProperties({{"initialContainerPath", fixture.path()}});
        loadSociety(engine);
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
        engine.addImageProvider(QStringLiteral("society-preview"), new iiSocietyContainer::PreviewProvider);
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
            {"width", 170}, {"height", 844}, {"guilds", memberships}, {"organizations", memberships}}));
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *sidebar = qobject_cast<QQuickItem *>(object.data()); QVERIFY(sidebar);
        QQuickWindow window;
        window.resize(170, 844);
        sidebar->setParentItem(window.contentItem());
        window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
        const QStringList groups{"workspace", "guild", "organization"};
        const QStringList titles{"Workspace", "Guilds", "Organization"};
        for (int i = 0; i < groups.size(); ++i) {
            auto *heading = visualItem(sidebar, "dashboardHeading" + groups[i]); QVERIFY(heading);
            QCOMPARE(heading->property("text").toString(), titles[i]);
            QVERIFY(!heading->isVisible());
        }
        const QStringList keys{"home", "projects", "calendar", "activity", "people"};
        const QStringList names{"Workspace", "Projects", "Calendar", "Activity", "People"};
        const QStringList iconNames{"workspace", "projects", "calendar", "activity", "people"};
        for (int i = 0; i < keys.size(); ++i) {
            auto *row = visualItem(sidebar, "dashboardItemworkspace_" + keys[i]); QVERIFY(row);
            QCOMPARE(row->property("label").toString(), names[i]);
            QCOMPARE(row->size(), QSizeF(170, 32));
            QCOMPARE(row->mapToItem(sidebar, QPointF()), QPointF(0, i * 32));
            auto *icon = visualItem(row, "dashboardIconSlotworkspace_" + keys[i]); QVERIFY(icon);
            QCOMPARE(icon->size(), QSizeF(18, 18));
            QCOMPARE(icon->mapToItem(row, QPointF()), QPointF(12, 7));
            auto *glyph = visualItem(row, "dashboardIconworkspace_" + keys[i]); QVERIFY(glyph);
            const auto extent = keys[i] == "people" ? 16 : 18;
            QCOMPARE(glyph->size(), QSizeF(extent, extent));
            QCOMPARE(glyph->mapToItem(icon, QPointF()), QPointF(0, 0));
            QVERIFY(glyph->property("source").toUrl().toString().endsWith("/" + iconNames[i] + ".svg"));
            QTRY_COMPARE(glyph->property("status").toInt(), 1); // Image.Ready
            auto *label = visualItem(row, "listItem_label"); QVERIFY(label);
            const QPointF labelOrigin = label->mapToItem(row, QPointF());
            QCOMPARE(labelOrigin.x(), 38.0);
            QVERIFY(qAbs(labelOrigin.y() - 9.5) <= 0.5); // Qt may align text to whole pixels.
            QVERIFY(!row->property("showTrailingIcon").toBool());
            const auto *accessible = QAccessible::queryAccessibleInterface(row); QVERIFY(accessible);
            QCOMPARE(accessible->text(QAccessible::Name), names[i]);
        }
        for (const auto &[group, top] : QList<QPair<QString, int>>{{"guild", 168}, {"organization", 272}}) {
            for (int i = 0; i < memberships.size(); ++i) {
                auto *row = visualItem(sidebar, "dashboardItem" + group + "_" + QString::number(i + 1)); QVERIFY(row);
                QCOMPARE(row->size(), QSizeF(170, 32));
                QCOMPARE(row->mapToItem(sidebar, QPointF()), QPointF(0, top + i * 32));
                auto *glyph = visualItem(row, "dashboardIcon" + group + "_" + QString::number(i + 1)); QVERIFY(glyph);
                QCOMPARE(glyph->size(), QSizeF(18, 18));
                QCOMPARE(glyph->mapToItem(row, QPointF()), QPointF(12, 7));
                QVERIFY(glyph->property("source").toUrl().toString().endsWith("/" + group + ".svg"));
                QTRY_COMPARE(glyph->property("status").toInt(), 1);
            }
        }
        auto *home = visualItem(sidebar, "dashboardItemworkspace_home"); QVERIFY(home);
        auto *project = visualItem(sidebar, "dashboardItemworkspace_projects"); QVERIFY(project);
        QVERIFY(home->property("selected").toBool());
        QCOMPARE(home->property("backgroundColor").value<QColor>().name(), QString("#1d4c24"));
        QVERIFY(sidebar->setProperty("activeKey", "projects"));
        QTRY_VERIFY(project->property("selected").toBool());
        QVERIFY(!home->property("selected").toBool());
        QCOMPARE(project->property("backgroundColor").value<QColor>().name(), QString("#1d4c24"));
        QVERIFY(sidebar->setProperty("activeKey", "home"));
        QVERIFY(!visualItem(sidebar, "dashboardLocal"));
        QVERIFY(!visualItem(sidebar, "dashboardThisDevice"));
        QSignalSpy homes(sidebar, SIGNAL(homeRequested()));
        QSignalSpy calendars(sidebar, SIGNAL(calendarRequested()));
        QSignalSpy projects(sidebar, SIGNAL(projectsRequested()));
        QSignalSpy activity(sidebar, SIGNAL(activityRequested()));
        QSignalSpy features(sidebar, SIGNAL(featureRequested(QString)));
        QSignalSpy workspaces(sidebar, SIGNAL(workspaceRequested(QString,QString)));
        for (const auto &key : keys) {
            auto *row = visualItem(sidebar, "dashboardItemworkspace_" + key);
            QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                row->mapToScene(QPointF(row->width()/2, row->height()/2)).toPoint());
        }
        QCOMPARE(homes.size(), 1); QCOMPARE(calendars.size(), 1); QCOMPARE(activity.size(), 1); QCOMPARE(projects.size(), 1);
        QCOMPARE(features, QList<QList<QVariant>>({{QString("People")}}));
        auto *organization = visualItem(sidebar, "dashboardItemorganization_3"); QVERIFY(organization);
        organization->forceActiveFocus(); QTest::keyClick(&window, Qt::Key_Space);
        QTRY_COMPARE(workspaces.size(), 1);
        QCOMPARE(workspaces.first(), QVariantList({"organization", "3"}));
        sidebar->forceActiveFocus();
        QTest::mouseMove(&window, QPoint(165, 800));
        const auto capture = qEnvironmentVariable("SOCIETY_DASHBOARD_SIDEBAR_SCREENSHOT_PATH");
        if (!capture.isEmpty()) { QTest::qWait(100); QVERIFY(window.grabWindow().save(capture)); }
        window.resize(170, 240); sidebar->setHeight(240);
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
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&](const QList<QQmlError> &errors) {
            for (const auto &error : errors) warnings.append(error.toString());
        });
        engine.addImageProvider(QStringLiteral("society-preview"), new iiSocietyContainer::PreviewProvider);
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
        QVERIFY(!calendar->isVisible());
        auto *composer = visualItem(dashboard, "dashboardAgentComposer"); QVERIFY(composer);
        auto *submit = visualItem(dashboard, "dashboardAgentSubmit"); QVERIFY(submit);
        auto *prompt = visualItem(dashboard, "dashboardAgentPrompt"); QVERIFY(prompt);
        QVERIFY(prompt->setProperty("text", "Keep this draft"));
        QVERIFY(composer->isVisible());
        QVERIFY(!submit->isEnabled()); // A model/session host is not configured.
        QVERIFY(visualItem(dashboard, "dashboardFilesPanel"));
        QVERIFY(visualItem(dashboard, "dashboardAgentActivity"));
        auto *activity = visualItem(dashboard, "dashboardItemworkspace_activity"); QVERIFY(activity);
        QVERIFY(QMetaObject::invokeMethod(activity, "clicked"));
        QCOMPARE(calendar->property("expandedSection").toString(), QString("activity"));
        QVERIFY(calendar->isVisible());
        QVERIFY(QMetaObject::invokeMethod(visualItem(dashboard, "dashboardItemworkspace_calendar"), "clicked"));
        QCOMPARE(calendar->property("expandedSection").toString(), QString());
        auto *projectsLoader = dashboard->findChild<QObject *>("dashboardProjectsLoader");
        QVERIFY(projectsLoader); QVERIFY(projectsLoader->property("asynchronous").toBool());
        QVERIFY(QMetaObject::invokeMethod(visualItem(dashboard, "dashboardItemworkspace_projects"), "clicked"));
        QTRY_VERIFY(dashboard->property("projectsPageVisible").toBool());
        QTRY_VERIFY(visualItem(dashboard, "projectsView"));
        auto *projectsView = visualItem(dashboard, "projectsView");
        QVERIFY(QMetaObject::invokeMethod(visualItem(dashboard, "dashboardItemworkspace_home"), "clicked"));
        QVERIFY(!dashboard->property("projectsPageVisible").toBool());
        QCOMPARE(prompt->property("text").toString(), QString("Keep this draft"));
        QVERIFY(QMetaObject::invokeMethod(visualItem(dashboard, "dashboardItemworkspace_projects"), "clicked"));
        QTRY_COMPARE(visualItem(dashboard, "projectsView"), projectsView);
        QSignalSpy requested(&navigation, &StorageNavigation::workspaceRequested);
        QVERIFY(QMetaObject::invokeMethod(visualItem(dashboard, "dashboardItemguild_real-id"), "clicked"));
        QCOMPARE(requested.size(), 1);
        QCOMPARE(requested.first(), QVariantList({"guild", "real-id", "My workspace"}));
        navigation.setAccountId("");
        QTRY_VERIFY(!visualItem(dashboard, "dashboardItemguild_real-id"));
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    }

    void dashboardFilesUseReadyDriveAndNavigateWithinFiles()
    {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/dashboard-files-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        QVERIFY(QDir().mkpath(fixture.filePath("Files/Frames")));
        const auto previous = qgetenv("SOCIETY_STORAGE_SETTINGS_PATH");
        const auto restore = qScopeGuard([&] { qputenv("SOCIETY_STORAGE_SETTINGS_PATH", previous); });
        qputenv("SOCIETY_STORAGE_SETTINGS_PATH", fixture.filePath("test-settings.json").toUtf8());
        DriveController drive; QVERIFY(drive.openContainer(fixture.path()));
        DashboardFiles files;
        files.setContainerPath(fixture.path());
        QQmlEngine engine;
        engine.addImageProvider(QStringLiteral("society-preview"), new iiSocietyContainer::PreviewProvider);
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        const auto directory = QFileInfo(QString::fromUtf8(SOCIETY_QML_FILE)).dir();
        QQmlComponent component(&engine, QUrl::fromLocalFile(directory.filePath("Dashboard/Dashboard.qml")));
        QScopedPointer<QObject> object(component.createWithInitialProperties({
            {"width", 1440}, {"height", 900}, {"viewModel", QVariant::fromValue(&files)},
            {"drive", QVariant::fromValue(&drive)}}));
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *dashboard = qobject_cast<QQuickItem *>(object.data()); QVERIFY(dashboard);
        QQuickWindow window; window.resize(1440, 900); dashboard->setParentItem(window.contentItem());
        window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
        const auto filesRoot = QDir(fixture.filePath("Files")).canonicalPath();
        QTRY_COMPARE(dashboard->property("filesRoot").toString(), filesRoot);
        QTRY_VERIFY(visualItem(dashboard, "dashboardFilesBrowser"));
        auto *browser = visualItem(dashboard, "dashboardFilesBrowser");
        QTRY_VERIFY(browser->property("folderCount").toInt() >= 1);
        QVERIFY(QMetaObject::invokeMethod(dashboard, "navigateDashboard",
                                          Q_ARG(QString, fixture.filePath("Files/Frames"))));
        QTRY_COMPARE(dashboard->property("dashboardPath").toString(), QDir(fixture.filePath("Files/Frames")).canonicalPath());
        QVERIFY(QMetaObject::invokeMethod(dashboard, "upDashboard"));
        QTRY_COMPARE(dashboard->property("dashboardPath").toString(), filesRoot);
        QVERIFY(QMetaObject::invokeMethod(dashboard, "navigateDashboard", Q_ARG(QString, fixture.path())));
        QCOMPARE(dashboard->property("dashboardPath").toString(), filesRoot);
        drive.setMirrorPending(true);
        QTRY_VERIFY(dashboard->property("filesRoot").toString().isEmpty());
        QTRY_VERIFY(!visualItem(dashboard, "dashboardFilesBrowser"));
        dashboard->setParentItem(nullptr);
    }

    void sidebarMatchesFigmaAndRoutesDynamicTargets()
    {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/sidebar-figma-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        QQmlApplicationEngine engine;
        engine.addImageProvider(QStringLiteral("society-preview"), new iiSocietyContainer::PreviewProvider);
        QStringList warnings;
        connect(&engine, &QQmlApplicationEngine::warnings, this, [&](const QList<QQmlError> &errors) {
            for (const auto &error : errors) warnings.append(error.toString());
        });
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        engine.setInitialProperties({{"initialContainerPath", fixture.path()}, {"width", 1440}, {"height", 900}});
        loadSociety(engine);
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
        const QList<qreal> positions{12, 319, 434, 549};
        for (int i = 0; i < groups.size(); ++i) {
            auto *heading = visualItem(sidebar, "storageHeading" + groups[i]); QVERIFY(heading);
            QCOMPARE(heading->property("text").toString(), titles[i]);
            QTRY_COMPARE(heading->mapToItem(sidebar, QPointF()), QPointF(12, positions[i]));
            QCOMPARE(heading->height(), 11.0);
        }
        auto *models = visualItem(sidebar, "storageSectionmodels"); QVERIFY(models);
        QCOMPARE(models->size(), QSizeF(204, 32)); QVERIFY(models->property("selected").toBool());
        const QStringList sectionKeys{"files", "photos", "asset-library", "generation-history", "models",
            "thinking-space", "forked", "published", "deleted"};
        const QStringList sectionIcons{"database", "fileTypesimage", "nodesfolder", "profileCPU", "warehouse",
            "fileTypestext", "RemoteChanges", "application", "generaldelete"};
        const auto leadingIconFits = [](QQuickItem *row, QQuickItem *glyph) {
            auto *content = visualItem(row, "listItem_content");
            if (!content || !glyph || !content->clip()) return false;
            return content->height() == row->height()
                && QRectF(QPointF(), content->size()).contains(
                glyph->mapRectToItem(content, QRectF(QPointF(), glyph->size())));
        };
        for (int i = 0; i < sectionKeys.size(); ++i) {
            auto *row = visualItem(sidebar, "storageSection" + sectionKeys[i]); QVERIFY(row);
            QCOMPARE(row->size(), QSizeF(204, 32));
            QCOMPARE(row->mapToItem(sidebar, QPointF()), QPointF(12, 23 + i * 32));
            QCOMPARE(row->property("topPadding").toReal(), 0.0);
            QCOMPARE(row->property("bottomPadding").toReal(), 0.0);
            QCOMPARE(row->property("iconName").toString(), sectionIcons[i]);
            if (i > 0) {
                auto *glyph = visualItem(row, "listItem_leadingIcon"); QVERIFY(glyph);
                QCOMPARE(glyph->size(), QSizeF(18, 18));
                QVERIFY(!glyph->childItems().isEmpty());
                QTRY_COMPARE(glyph->childItems().first()->property("status").toInt(), 1); // Image.Ready
                QVERIFY2(leadingIconFits(row, glyph), qPrintable(sectionKeys[i] + " leading icon is clipped"));
            }
        }
        auto *filesIconSlot = visualItem(sidebar, "storageFilesIconSlot"); QVERIFY(filesIconSlot);
        QCOMPARE(filesIconSlot->size(), QSizeF(18, 18));
        QVERIFY2(leadingIconFits(visualItem(sidebar, "storageSectionfiles"), filesIconSlot),
                 "Files leading icon is clipped");
        auto *filesIcon = visualItem(sidebar, "storageFilesIcon"); QVERIFY(filesIcon);
        QCOMPARE(filesIcon->position(), QPointF(2.3125, 1.1875));
        QCOMPARE(filesIcon->size(), QSizeF(13.375, 15.6659));
        QTRY_COMPARE(filesIcon->property("status").toInt(), 1); // Image.Ready
        auto *history = visualItem(sidebar, "storageSectiongeneration-history"); QVERIFY(history);
        QVERIFY(history->property("iconSource").toUrl().path().endsWith("Drive/icons/generation-history.svg"));
        auto *published = visualItem(sidebar, "storageSectionpublished"); QVERIFY(published);
        QVERIFY(published->property("iconSource").toUrl().toLocalFile().endsWith("Drive/icons/published.svg"));
        const QStringList targetKeys{"devicesdesktop", "devicesphone", "devicestablet",
            "guild1", "guild2", "guild3", "organization1", "organization2"};
        const QList<qreal> targetPositions{330, 362, 394, 445, 477, 509, 560, 592};
        const QStringList targetIcons{"screens", "iPhoneDevice", "nodesdataColumn",
            "option", "option", "option", "warehouse", "warehouse"};
        for (int i = 0; i < targetKeys.size(); ++i) {
            auto *row = visualItem(sidebar, "storageTarget" + targetKeys[i]); QVERIFY(row);
            QCOMPARE(row->size(), QSizeF(204, 32));
            QCOMPARE(row->mapToItem(sidebar, QPointF()), QPointF(12, targetPositions[i]));
            QCOMPARE(row->property("topPadding").toReal(), 0.0);
            QCOMPARE(row->property("bottomPadding").toReal(), 0.0);
            QCOMPARE(row->property("iconName").toString(), targetIcons[i]);
            auto *glyph = visualItem(row, "listItem_leadingIcon"); QVERIFY(glyph);
            QCOMPARE(glyph->size(), QSizeF(18, 18));
            QVERIFY(!glyph->childItems().isEmpty());
            QTRY_COMPARE(glyph->childItems().first()->property("status").toInt(), 1);
            QVERIFY2(leadingIconFits(row, glyph), qPrintable(targetKeys[i] + " leading icon is clipped"));
        }
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
        QVERIFY(sidebar->setProperty("touchNavigation", true));
        for (const auto &key : sectionKeys) {
            auto *row = visualItem(sidebar, "storageSection" + key); QVERIFY(row);
            QTRY_COMPARE(row->height(), 44.0);
            QCOMPARE(row->property("topPadding").toReal(), 0.0);
            QCOMPARE(row->property("bottomPadding").toReal(), 0.0);
            auto *glyph = visualItem(row, key == "files" ? "storageFilesIconSlot" : "listItem_leadingIcon");
            QVERIFY2(leadingIconFits(row, glyph), qPrintable(key + " touch leading icon is clipped"));
        }
        window->resize(390, 844); QTRY_VERIFY(!sidebar->isVisible());
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        window->close();
    }

    void modelCatalogUsesHostIdentificationBeforeLocalHeaders()
    {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/model-map-XXXXXX.societycontainer");
        const auto drive = iiSocietyContainer::SocietyDrive::create(fixture.path()); QVERIFY(drive);
        const auto modelPath = fixture.filePath("Models/Checkpoint/cached.safetensors");
        QVERIFY(writeCatalogModel(modelPath, {{"modelspec.title", "Payload header title"}}));
        iiSocietyContainer::StorageMap map(*drive);
        QVERIFY(map.publish({QJsonObject{{"path", "models/Checkpoint/cached.safetensors"}, {"kind", "file"},
            {"size", QString::number(QFileInfo(modelPath).size())}, {"resident", true}, {"version", QString(64, 'a')}}}));
        StorageModels catalog; catalog.setDirectory(fixture.filePath("Models"));
        QTRY_VERIFY(!catalog.loading());
        QCOMPARE(catalog.count(), 1);
        const auto row = catalog.groups().value("Checkpoint").toList().first().toMap();
        QCOMPARE(row.value("name").toString(), "cached.safetensors");
        QVERIFY(map.pendingRequests().isEmpty());
    }

    void modelCatalogGroupsPhysicalTypesAndWatchesChanges()
    {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/model-catalog-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        for (const auto type : iiSocietyContainer::allModelTypes()) {
            const auto name = iiSocietyContainer::modelTypeName(type);
            QVERIFY(writeCatalogModel(fixture.filePath("Models/" + name + "/sample.safetensors"),
                {{"society.modality", "audio"}, {"modelspec.architecture", "SDXL"}}));
        }
        const auto outside = fixture.filePath("outside.safetensors");
        QVERIFY(writeCatalogModel(outside, {{"society.modality", "audio"}}));
        QVERIFY(createNativeTestLink(outside, fixture.filePath("Models/escaped.safetensors")));
        StorageModels catalog;
        catalog.setDirectory(fixture.filePath("Models"));
        QTRY_VERIFY(!catalog.loading());
        QCOMPARE(catalog.groups().size(), 23);
        QCOMPARE(catalog.count(), 23); QCOMPARE(catalog.uncategorizedCount(), 1);
        for (const auto type : iiSocietyContainer::allModelTypes())
            QCOMPARE(catalog.groups().value(iiSocietyContainer::modelTypeName(type)).toList().size(), 1);
        const auto row = catalog.groups().value("Checkpoint").toList().first().toMap();
        QCOMPARE(row.value("precision").toString(), QString("FP16"));
        QCOMPARE(row.value("format").toString(), QString("Safetensors"));
        QCOMPARE(row.value("architecture").toString(), QString("SDXL"));
        QCOMPARE(row.value("bytes").toLongLong(), QFileInfo(row.value("path").toString()).size());
        QSignalSpy changes(&catalog, &StorageModels::modelsChanged);
        catalog.refresh(); QTRY_VERIFY(!catalog.loading()); QCOMPARE(changes.size(), 0);
        const auto newcomer = fixture.filePath("Models/LoRA/new.safetensors");
        QVERIFY(writeCatalogModel(newcomer, {{"society.modality", "audio"}}));
        QTRY_COMPARE(catalog.groups().value("LoRA").toList().size(), 2);
        QVERIFY(QFile::rename(newcomer, fixture.filePath("Models/DoRA/new.safetensors")));
        QTRY_COMPARE(catalog.groups().value("LoRA").toList().size(), 1);
        QTRY_COMPARE(catalog.groups().value("DoRA").toList().size(), 2);
        QVERIFY(QFile::remove(fixture.filePath("Models/DoRA/new.safetensors")));
        QTRY_COMPARE(catalog.groups().value("DoRA").toList().size(), 1);
        catalog.refresh(); catalog.setDirectory("");
        QTRY_VERIFY(!catalog.loading()); QCOMPARE(catalog.count(), 0);
        QTest::qWait(200); QCOMPARE(catalog.count(), 0);
        catalog.setDirectory("relative"); QTRY_VERIFY(!catalog.loading());
        QVERIFY(!catalog.errorString().isEmpty()); QCOMPARE(catalog.count(), 0);
    }

    void modelsBrowsePhysicalFoldersInListAndGrid()
    {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/models-tree-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        const QStringList categories{"Checkpoint", "Embedding", "LLM", "LoRA"};
        for (const auto &category : categories)
            for (int i = 0; i < 2; ++i)
                QVERIFY(writeCatalogModel(fixture.filePath(QString("Models/%1/sample-%2.safetensors").arg(category).arg(i)), {}));
        const auto modelPath = fixture.filePath("Models/Checkpoint/sample-0.safetensors");
        QFile metadata(modelPath + ".model.json"); QVERIFY(metadata.open(QIODevice::WriteOnly));
        QVERIFY(metadata.write("{\"type\":\"Checkpoint\"}") > 0); metadata.close();
        QFile previewFile(fixture.filePath("Models/Checkpoint/sample-0.preview.png"));
        QVERIFY(previewFile.open(QIODevice::WriteOnly)); QVERIFY(previewFile.write("preview") > 0); previewFile.close();
        QQmlApplicationEngine engine;
        engine.addImageProvider(QStringLiteral("society-preview"), new iiSocietyContainer::PreviewProvider);
        QStringList warnings;
        connect(&engine, &QQmlApplicationEngine::warnings, this, [&](const QList<QQmlError> &errors) {
            for (const auto &error : errors) warnings.append(error.toString());
        });
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        engine.setInitialProperties({{"initialContainerPath", fixture.path()}, {"width", 1440}, {"height", 900}});
        loadSociety(engine);
        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *drive = window->findChild<DriveController *>("driveController"); QVERIFY(drive);
        auto *importer = window->findChild<ModelImporter *>("modelImporter"); QVERIFY(importer);
        QTRY_VERIFY_WITH_TIMEOUT(!importer->busy(), 30000);
        importer->setBackendManaged(false);
        QVERIFY(window->setProperty("selectedTab", "Storage"));
        QVERIFY(drive->openSection("models"));
        QQuickItem *models = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((models = window->findChild<QQuickItem *>("modelsView")) && models->isVisible(), 30000);
        auto *browser = models->findChild<QQuickItem *>("modelFilesBrowser"); QVERIFY(browser);
        auto *catalog = window->findChild<StorageModels *>("modelCatalog"); QVERIFY(catalog);
        QTRY_COMPARE_WITH_TIMEOUT(catalog->count(), 8, 30000);
        QTRY_COMPARE_WITH_TIMEOUT(browser->property("count").toInt(), 4, 30000);
        QVERIFY(browser->property("listMode").toBool());
        auto entries = [&] { return browser->property("entries").value<QJSValue>().toVariant().toList(); };
        for (const auto &entry : entries()) QVERIFY(entry.toMap().value("directory").toBool());
        int checkpoint = -1;
        for (int i = 0; i < entries().size(); ++i)
            if (entries().at(i).toMap().value("name").toString() == "Checkpoint") checkpoint = i;
        QVERIFY(checkpoint >= 0);
        auto *import = visualItem(browser, "modelsImport"); QVERIFY(import);
        QSignalSpy requested(models, SIGNAL(importRequested())); QVERIFY(requested.isValid());
        QVERIFY(QMetaObject::invokeMethod(import, "clicked")); QCOMPARE(requested.size(), 1);
        auto *dialog = window->findChild<QObject *>("modelFileDialog"); QVERIFY(dialog);
        QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
        QVERIFY(QMetaObject::invokeMethod(browser, "selectEntry", Q_ARG(QVariant, checkpoint)));
        QVERIFY(QMetaObject::invokeMethod(browser, "openSelected"));
        QTRY_COMPARE_WITH_TIMEOUT(drive->currentPath(), fixture.filePath("Models/Checkpoint"), 30000);
        QTRY_COMPARE_WITH_TIMEOUT(browser->property("path").toString(), drive->currentPath(), 30000);
        QTRY_COMPARE_WITH_TIMEOUT(browser->property("count").toInt(), 2, 30000);
        for (const auto &entry : entries()) QVERIFY(!entry.toMap().value("directory").toBool());
        QVERIFY(QMetaObject::invokeMethod(browser, "selectEntry", Q_ARG(QVariant, 0)));
        QCOMPARE(browser->property("selectedPath").toString(), modelPath);
        QVERIFY(browser->setProperty("listMode", false));
        auto *grid = visualItem(browser, "filesBrowserGrid"); QVERIFY(grid);
        QTRY_VERIFY(grid->isVisible());
        QCOMPARE(browser->property("selectedPath").toString(), modelPath);
        QVERIFY(browser->setProperty("listMode", true));
        QQuickItem *cell = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((cell = visualItem(browser, "filesCell_0_0")), 30000);
        QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier, cell->mapToScene(cell->boundingRect().center()).toPoint());
        auto *menu = browser->findChild<QObject *>("filesContextMenu"); QVERIFY(menu);
        QTRY_VERIFY(menu->property("opened").toBool());
        QTRY_COMPARE(menu->property("currentCategory").toString(), QStringLiteral("Checkpoint"));
        const auto categoryValue = menu->property("categoryOptions");
        const auto categoryOptions = categoryValue.metaType() == QMetaType::fromType<QJSValue>()
            ? categoryValue.value<QJSValue>().toVariant().toList() : categoryValue.toList();
        QCOMPARE(categoryOptions.size(), 23);
        int llmCategory = -1;
        for (int i = 0; i < categoryOptions.size(); ++i)
            if (categoryOptions.at(i).toMap().value("key").toString() == "LLM") llmCategory = i;
        QVERIFY(llmCategory >= 0);
        QSignalSpy moved(importer, SIGNAL(recategorized(QString,QString,QString))); QVERIFY(moved.isValid());
        QVERIFY(QMetaObject::invokeMethod(menu, "triggerEntry", Q_ARG(QVariant, 9)));
        auto *categoryMenu = menu->property("categoryMenu").value<QObject *>(); QVERIFY(categoryMenu);
        QTRY_VERIFY(categoryMenu->property("opened").toBool());
        QVERIFY(QMetaObject::invokeMethod(categoryMenu, "triggerEntry", Q_ARG(QVariant, llmCategory)));
        QTRY_COMPARE_WITH_TIMEOUT(moved.size(), 1, 30000);
        const auto destination = fixture.filePath("Models/LLM/sample-0 (1).safetensors");
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(destination), 30000);
        QVERIFY(QFileInfo::exists(destination + ".model.json"));
        QVERIFY(QFileInfo::exists(fixture.filePath("Models/LLM/sample-0 (1).preview.png")));
        QTRY_COMPARE_WITH_TIMEOUT(browser->property("count").toInt(), 1, 30000);
        auto *back = visualItem(browser, "modelsBack"); QVERIFY(back); QVERIFY(back->isVisible());
        QVERIFY(QMetaObject::invokeMethod(back, "clicked"));
        QTRY_COMPARE(drive->currentPath(), fixture.filePath("Models"));
        QTRY_COMPARE_WITH_TIMEOUT(browser->property("count").toInt(), 4, 30000);
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        window->close();
    }
    void reloadAdoptedIdentityAndHideAnIncompleteMirror()
    {
        QTemporaryDir root(QStringLiteral(SOCIETY_TEST_DIRECTORY "/drive-mirror-XXXXXX.societycontainer"));
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
    void fileGridRendersPersistentPreviewAndInvalidatesEdits()
    {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/preview-grid-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        const auto source = fixture.filePath("Files/picture.png");
        QImage image(1600, 1200, QImage::Format_RGB32); image.fill(Qt::red); QVERIFY(image.save(source));
        QQmlApplicationEngine engine;
        engine.addImageProvider(QStringLiteral("society-preview"), new iiSocietyContainer::PreviewProvider);
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        engine.load(QUrl::fromLocalFile(QFileInfo(QString::fromUtf8(SOCIETY_QML_FILE)).dir().filePath("../../tests/FileGridHarness.qml")));
        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        auto *view = window->findChild<QQuickItem *>("fileGridView"); QVERIFY(view);
        view->setProperty("path", fixture.filePath("Files"));
        QTRY_COMPARE(view->property("count").toInt(), 1);
        QTRY_VERIFY(visualItem(window->contentItem(), "fileThumbnail"));
        auto *thumbnail = visualItem(window->contentItem(), "fileThumbnail");
        QTRY_COMPARE(thumbnail->property("status").toInt(), 1); // Image.Ready, not merely a non-empty URL.
        const auto original = thumbnail->property("source").toUrl();
        QCOMPARE(original.scheme(), QString("image")); QCOMPARE(original.host(), QString("society-preview"));
        image.fill(Qt::blue); QVERIFY(image.save(source));
        QTRY_VERIFY(thumbnail->property("source").toUrl() != original);
        QTRY_COMPARE(thumbnail->property("status").toInt(), 1);
        iiSocietyContainer::PreviewCache cache;
        const auto hit = cache.load(source); QVERIFY(hit.cacheHit); QCOMPARE(hit.image.pixelColor(5, 5), QColor(Qt::blue));
        window->close();
    }
    void synchronizationPreservesFileModelSelectionAndScroll()
    {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/drive-sync-view-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        for (int i = 0; i < 80; ++i) {
            QFile file(fixture.filePath(QString("Files/file-%1.txt").arg(i, 3, 10, QChar('0'))));
            QVERIFY(file.open(QIODevice::WriteOnly)); QVERIFY(file.write("unchanged") > 0);
        }
        QQmlApplicationEngine engine;
        engine.addImageProvider(QStringLiteral("society-preview"), new iiSocietyContainer::PreviewProvider);
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        engine.setInitialProperties({{"initialContainerPath", fixture.path()}});
        loadSociety(engine);
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
        QTemporaryDir first(SOCIETY_TEST_DIRECTORY "/refresh-first-XXXXXX.societycontainer"), second(SOCIETY_TEST_DIRECTORY "/refresh-second-XXXXXX.societycontainer");
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
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/dashboard-stable-XXXXXX.societycontainer");
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
        QTemporaryDir fixture(QStringLiteral(SOCIETY_TEST_DIRECTORY "/drive-core-XXXXXX.societycontainer"));
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
        QTemporaryDir first(QStringLiteral(SOCIETY_TEST_DIRECTORY "/drive-first-XXXXXX.societycontainer"));
        QTemporaryDir second(QStringLiteral(SOCIETY_TEST_DIRECTORY "/drive-conflict-XXXXXX.societycontainer"));
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
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/drive-public-source-XXXXXX.societycontainer");
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
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/storage-entry-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        QVERIFY(QDir().mkpath(fixture.filePath("Files/Nested")));
        QQmlApplicationEngine engine;
        engine.addImageProvider(QStringLiteral("society-preview"), new iiSocietyContainer::PreviewProvider);
        QStringList warnings;
        connect(&engine, &QQmlApplicationEngine::warnings, this, [&](const QList<QQmlError> &errors) {
            for (const auto &error : errors) warnings.append(error.toString());
        });
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        engine.setInitialProperties({{"initialContainerPath", fixture.path()}, {"mobileLayout", mobile},
            {"width", mobile ? 390 : 1440}, {"height", mobile ? 844 : 900}});
        loadSociety(engine);
        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *drive = window->findChild<DriveController *>("driveController"); QVERIFY(drive);
        auto *navigation = window->findChild<StorageNavigation *>("storageNavigation"); QVERIFY(navigation);
        auto *storageTab = mobile ? visualItem(window->contentItem(), "mobileStorageTab") : nullptr;
        auto *dashboardTab = mobile ? visualItem(window->contentItem(), "mobileDashboardTab") : nullptr;
        QVERIFY(!mobile || (storageTab && dashboardTab));
        const auto click = [&](QQuickItem *item) {
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                item->mapToScene(item->boundingRect().center()).toPoint());
        };
        const auto openStorage = [&] { if (mobile) click(storageTab); else QVERIFY(clickNavigationSegment(window, 2)); };
        const auto openDashboard = [&] { if (mobile) click(dashboardTab); else QVERIFY(window->setProperty("selectedTab", "Dashboard")); };

        openStorage();
        QTRY_COMPARE(window->findChild<QObject *>("storageLoader")->property("status").toInt(), 1);
        auto *files = window->findChild<QQuickItem *>("filesBrowser"); QVERIFY(files);
        QTRY_COMPARE(drive->currentPath(), fixture.filePath("Files"));
        QTRY_VERIFY(files->isVisible());
        QCOMPARE(navigation->selectedSection(), QString("files"));
        QVERIFY(!window->findChild<QQuickItem *>("sectionsGrid"));

        // Both a repeated click and returning from another tab start at Files.
        QVERIFY(drive->openSection("models"));
        openStorage();
        QTRY_COMPARE(drive->currentPath(), fixture.filePath("Files"));
        QVERIFY(drive->navigate(fixture.filePath("Files/Nested")));
        openDashboard();
        openStorage();
        QTRY_COMPARE(drive->currentPath(), fixture.filePath("Files"));

        // Explicit destinations still open their requested section.
        openDashboard();
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

    void storageSectionsUseFullHeightWithoutCommonFooter_data()
    {
        QTest::addColumn<QSize>("size");
        QTest::addColumn<bool>("mobile");
        QTest::newRow("desktop") << QSize(1120, 720) << false;
        QTest::newRow("desktop-landscape") << QSize(844, 390) << false;
        QTest::newRow("desktop-compact") << QSize(360, 320) << false;
        QTest::newRow("mobile") << QSize(390, 844) << true;
    }

    void storageSectionsUseFullHeightWithoutCommonFooter()
    {
        QFETCH(QSize, size);
        QFETCH(bool, mobile);
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/storage-no-footer-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        QQmlApplicationEngine engine;
        engine.addImageProvider(QStringLiteral("society-preview"), new iiSocietyContainer::PreviewProvider);
        QStringList warnings;
        connect(&engine, &QQmlApplicationEngine::warnings, this, [&](const QList<QQmlError> &errors) {
            for (const auto &error : errors) warnings.append(error.toString());
        });
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        engine.setInitialProperties({{"initialContainerPath", fixture.path()}, {"selectedTab", "Storage"},
            {"mobileLayout", mobile}, {"desktopMinWidth", 320}, {"desktopMinHeight", 240},
            {"width", size.width()}, {"height", size.height()}});
        loadSociety(engine);
        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTRY_COMPARE(window->size(), size);
        auto *drive = window->findChild<DriveController *>("driveController"); QVERIFY(drive);
        QTRY_VERIFY(window->findChild<QQuickItem *>("storageView"));
        auto *storage = window->findChild<QQuickItem *>("storageView"); QVERIFY(storage);
        QTRY_VERIFY(storage->isVisible());
        QVERIFY(drive->errorString().isEmpty());
        for (const QString name : {"importModels", "modelImportStatus", "cancelModelImport", "modelImportError",
                "systemStatus", "connectToSystem", "revealInSystem"})
            QVERIFY2(!storage->findChild<QQuickItem *>(name), qPrintable(name));

        // Every section owns the full Storage viewport, including the area
        // previously reserved for the shared import and system footer.
        for (const auto section : iiSocietyContainer::allStoreSections()) {
            QVERIFY(drive->openSection(iiSocietyContainer::storeSectionKey(section)));
            const auto name = section == iiSocietyContainer::StoreSection::Files ? "filesBrowser"
                : section == iiSocietyContainer::StoreSection::Models ? "modelsView"
                : section == iiSocietyContainer::StoreSection::Photos ? "photosView" : "fileGridView";
            QQuickItem *view = nullptr;
            QTRY_VERIFY_WITH_TIMEOUT((view = visualItem(storage, name)), 30000);
            QTRY_VERIFY(view->isVisible());
            QTRY_VERIFY(view->width() > 0 && view->height() > 0);
            QTRY_COMPARE(view->mapToItem(storage, QPointF()).y(), 0.0);
            QTRY_COMPARE(view->mapToItem(storage, QPointF(0, view->height())).y(), storage->height());
            if (section == iiSocietyContainer::StoreSection::Models) {
                QVERIFY(visualItem(view, "filesStatus"));
                QVERIFY(visualItem(view, "modelsImport"));
            }
        }
        // The existing actions sheet remains available after removing the
        // desktop footer; its controls are created even while it is closed.
        QVERIFY(storage->findChild<QObject *>("storageActions"));
        QVERIFY(window->findChild<QQuickItem *>("mobileImportModels"));
        QVERIFY(window->findChild<QQuickItem *>("mobileConnectToSystem"));
        QVERIFY(window->findChild<QQuickItem *>("mobileRevealInSystem"));
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        window->close();
    }

    void driveWindowOpensFilesAndNavigatesSections()
    {
        QTemporaryDir fixture(QStringLiteral(SOCIETY_TEST_DIRECTORY "/drive-gui-XXXXXX.societycontainer"));
        QVERIFY(fixture.isValid());
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        QVERIFY(QDir().mkpath(fixture.filePath("Files/Nested")));
        QFile file(fixture.filePath("Files/Example.txt"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("example");
        file.close();
        QVERIFY(QDir().mkpath(fixture.filePath("Models/Wildcards")));
        QFile privateFile(fixture.filePath("Models/Wildcards/App only.txt"));
        QVERIFY(privateFile.open(QIODevice::WriteOnly));
        privateFile.write("Available through Society");
        privateFile.close();

        QQmlApplicationEngine engine;
        engine.addImageProvider(QStringLiteral("society-preview"), new iiSocietyContainer::PreviewProvider);
        QStringList warnings;
        connect(&engine, &QQmlApplicationEngine::warnings, this, [&](const QList<QQmlError> &errors) {
            for (const auto &error : errors)
                warnings.append(error.toString());
        });
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        engine.setInitialProperties({{"initialContainerPath", fixture.path()}});
        loadSociety(engine);
        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        QCOMPARE(window->title(), QString("Society"));
        auto *controller = window->findChild<DriveController *>("driveController");
        QVERIFY(controller);
        auto *importer = window->findChild<ModelImporter *>("modelImporter"); QVERIFY(importer);
        QTRY_VERIFY_WITH_TIMEOUT(!importer->busy(), 30000);
        importer->setBackendManaged(false);
        QTRY_VERIFY(window->isVisible());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QCOMPARE(controller->sections().size(), 9);
        QVERIFY(clickNavigationSegment(window, 2));
        QTRY_COMPARE(window->property("selectedTab").toString(), QString("Storage"));
        QQuickItem *driveTitle = nullptr, *driveHome = nullptr, *files = nullptr;
        QQuickItem *modelGrid = nullptr, *content = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((driveTitle = window->findChild<QQuickItem *>("societyDriveTitle"))
            && (driveHome = visualItem(window->contentItem(), "storageSectionfiles"))
            && (files = window->findChild<QQuickItem *>("filesBrowser"))
            && (modelGrid = window->findChild<QQuickItem *>("fileGridView"))
            && (content = window->findChild<QQuickItem *>("driveContent")), 30000);
        QCOMPARE(driveTitle->property("text").toString(), QString("Society"));
        QCOMPARE(driveHome->property("label").toString(), QString("Files"));
        QVERIFY(!window->findChild<QQuickItem *>("sectionsGrid"));
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
        QQuickItem *models = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((models = window->findChild<QQuickItem *>("modelsView")), 15000);
        QTRY_VERIFY(models->isVisible());
        QVERIFY(!files->isVisible());
        auto *modelBrowser = models->findChild<QQuickItem *>("modelFilesBrowser"); QVERIFY(modelBrowser);
        QVERIFY(modelBrowser->isVisible()); QVERIFY(!files->isVisible());
        QTRY_COMPARE(modelBrowser->property("path").toString(), fixture.filePath("Models"));
        // The fixture populates only Wildcards; unused categories are not created.
        QTRY_COMPARE(modelBrowser->property("count").toInt(), 1);
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
        // OS URL drops target Models from Files and a phone-size header.
        QTemporaryDir dropped(SOCIETY_TEST_DIRECTORY "/model-drop-XXXXXX.societycontainer");
        QVERIFY(dropped.isValid());
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
            QVERIFY(writeCatalogModel(model.fileName(), {}));
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
            QTRY_COMPARE_WITH_TIMEOUT(imported.size(), index + 1, 30000);
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
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/recent-storage-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        const auto epoch = QDateTime::fromString("2026-01-01T00:00:00Z", Qt::ISODate);
        for (int i = 0; i < 80; ++i) {
            QFile file(fixture.filePath(QString("Files/photo-%1.txt").arg(79 - i, 2, 10, QChar('0'))));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QVERIFY(file.write("fixture") > 0); QVERIFY(file.flush());
            QVERIFY(file.setFileTime(epoch.addSecs(i), QFileDevice::FileModificationTime));
        }
        QQmlApplicationEngine engine;
        engine.addImageProvider(QStringLiteral("society-preview"), new iiSocietyContainer::PreviewProvider);
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        engine.setInitialProperties({{"initialContainerPath", fixture.path()}});
        loadSociety(engine);
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
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/dashboard-files-XXXXXX.societycontainer");
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
        QTemporaryDir outside(SOCIETY_TEST_DIRECTORY "/dashboard-outside-XXXXXX.societycontainer");
        QFile secret(outside.filePath("outside.txt"));
        QVERIFY(secret.open(QIODevice::WriteOnly)); secret.write("outside"); secret.close();
        QVERIFY(createNativeTestLink(secret.fileName(), fixture.filePath("Files/linked.txt")));
        QVERIFY(createNativeTestLink(outside.path(), fixture.filePath("Files/Linked folder")));

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
        QCOMPARE(image.value("thumbnailSource").toUrl(), StorageDirectoryModel::thumbnailUrl(fixture.filePath("Generation History/finished.PNG")));
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

        QTemporaryDir historyOnly(SOCIETY_TEST_DIRECTORY "/dashboard-history-only-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(historyOnly.path()));
        QFile generated(historyOnly.filePath("Generation History/generated.png"));
        QVERIFY(generated.open(QIODevice::WriteOnly)); generated.write("fixture"); generated.close();
        files.setContainerPath(historyOnly.path()); QTRY_VERIFY(!files.loading());
        QVERIFY(files.recentFiles().isEmpty());
        QCOMPARE(files.generationHistory().size(), 1);
    }

    void dashboardLoadsLocalFilesWhileNetworkMirrorIsPending()
    {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/dashboard-startup-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        QFile document(fixture.filePath("Files/Existing.txt"));
        QVERIFY(document.open(QIODevice::WriteOnly)); document.write("existing"); document.close();
        QImage image(16, 16, QImage::Format_RGB32); image.fill(Qt::cyan);
        QVERIFY(image.save(fixture.filePath("Generation History/Existing.png")));
        QQmlApplicationEngine engine;
        engine.addImageProvider(QStringLiteral("society-preview"), new iiSocietyContainer::PreviewProvider);
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        engine.setInitialProperties({{"initialContainerPath", fixture.path()}, {"mobileLayout", true}});
        loadSociety(engine);
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
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/dashboard-creation-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        DashboardFiles files;
        files.setContainerPath(fixture.path()); QTRY_VERIFY(!files.loading());
        QVERIFY(files.recentFiles().isEmpty());
        QQmlEngine engine;
        engine.addImageProvider(QStringLiteral("society-preview"), new iiSocietyContainer::PreviewProvider);
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
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/dashboard-watch-XXXXXX.societycontainer");
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
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/dashboard-limit-XXXXXX.societycontainer");
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
        engine.addImageProvider(QStringLiteral("society-preview"), new iiSocietyContainer::PreviewProvider);
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        engine.setInitialProperties({{"initialContainerPath", fixture.path()}, {"mobileLayout", mobile},
            {"desktopMinWidth", 320}, {"width", mobile ? 390 : 1440}, {"height", 844}});
        loadSociety(engine);
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

    void calendarWorkspaceRoutesEditsAndRendersFiveDesigns()
    {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/calendar-workspace-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        QQmlApplicationEngine engine;
        engine.addImageProvider(QStringLiteral("society-preview"), new iiSocietyContainer::PreviewProvider);
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&](const QList<QQmlError> &errors) {
            for (const auto &error : errors) warnings.append(error.toString());
        });
        engine.setInitialProperties({{"initialContainerPath", fixture.path()}, {"width", 1600},
            {"height", 960}, {"desktopMinWidth", 320}});
        loadSociety(engine);
        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window)); window->requestActivate();
        auto *dashboard = visualItem(window->contentItem(), "dashboardView"); QVERIFY(dashboard);
        auto *calendar = window->findChild<DashboardCalendar *>("dashboardCalendarModel"); QVERIFY(calendar);
        auto *workspace = window->findChild<CalendarWorkspace *>("calendarWorkspaceModel"); QVERIFY(workspace);
        // Test runners can show a Cocoa window without owning foreground app
        // activation. Explicitly enable the projections for deterministic CI.
        QVERIFY(QQmlProperty::write(calendar, "active", true));
        calendar->setTimeZone("Asia/Seoul"); calendar->selectDate("2026-10-23");
        QTRY_VERIFY(!calendar->loading());
        const auto click = [window](QQuickItem *item) {
            QTest::qWait(50); // Let popup and responsive layouts polish before hit testing.
            if (!item || !item->isVisible()) return false;
            const auto point = item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
            if (!QRect(QPoint(), window->size()).contains(point)) return false;
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                point);
            return true;
        };
        QVERIFY(click(visualItem(window->contentItem(), "dashboardItemworkspace_calendar")));
        QTRY_VERIFY(dashboard->property("calendarPageVisible").toBool());
        QTRY_VERIFY(!calendar->databasePath().isEmpty() && !calendar->loading());
        QVERIFY(QQmlProperty::write(workspace, "active", true));
        QTRY_VERIFY(workspace->active());
        const auto review = workspace->saveEvent({{"title", "Tidal Archive review"}, {"startLocal", "2026-10-23 14:00"},
            {"endLocal", "2026-10-23 15:00"}, {"timeZone", "Asia/Seoul"}, {"calendarId", "studio"}, {"colorKey", "green"},
            {"description", "Review the lighting, materials and final canvas order before publishing the Tidal Archive study."},
            {"location", "Video call · Studio room"}, {"invitees", "Lea Park; Jun Lee; Mira Yoon"},
            {"meetingUrl", "https://example.com/review"}, {"alarmMinutes", 10}});
        QVERIFY2(!review.isEmpty(), qPrintable(workspace->errorString()));
        const QStringList titles{"Studio sync", "Lighting pass", "Reference review", "Portfolio review", "Canvas polish", "Gallery visit", "Weekly reset"};
        const QStringList colors{"green", "blue", "purple", "purple", "blue", "orange", "orange"};
        const QStringList collections{"studio", "projects", "publishing", "publishing", "projects", "default", "default"};
        for (int i = 0; i < 7; ++i) {
            const auto date = QString::fromStdString(iiCalendar::Date(2026, 10, 19).addDays(i).toString());
            const auto id = workspace->saveEvent({{"title", titles[i]}, {"startLocal", date + " 09:00"},
                {"endLocal", date + " 10:30"}, {"timeZone", "Asia/Seoul"}, {"calendarId", collections[i]}, {"colorKey", colors[i]}});
            QVERIFY2(!id.isEmpty(), qPrintable(workspace->errorString()));
        }
        calendar->selectDate("2026-10-23"); workspace->refresh();
        QTRY_COMPARE(workspace->visibleEvents().size(), 8);
        workspace->selectEvent(review);
        const auto capturePath = qEnvironmentVariable("SOCIETY_CALENDAR_SCREENSHOT_DIR");
        if (!capturePath.isEmpty()) QVERIFY(QDir().mkpath(capturePath));
        const auto capture = [&](const QString &name) {
            QTest::qWait(150);
            return capturePath.isEmpty() || window->grabWindow().save(QDir(capturePath).filePath(name + ".png"));
        };
        QQuickItem *page = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((page = visualItem(window->contentItem(), "calendarWorkspaceView")), 15000);
        auto *sidebar = visualItem(window->contentItem(), "dashboardSidebar"); QVERIFY(sidebar);
        QTRY_COMPARE(page->mapToScene(QPointF()), QPointF(sidebar->width(), 56));
        QCOMPARE(page->width(), window->width() - sidebar->width());
        for (const auto &mode : QStringList{"week", "day", "month", "year"}) {
            QVERIFY(click(visualItem(window->contentItem(), "calendarMode_" + mode)));
            QTRY_COMPARE(workspace->viewMode(), mode);
            QTRY_VERIFY(!workspace->loading());
            if (mode == "month") {
                auto *date = visualItem(window->contentItem(), "calendarMonthDate_2026-10-23"); QVERIFY(date);
                QCOMPARE(date->property("text").toString(), "23");
                auto *label = visualItem(date, "labelButton_label"); QVERIFY(label);
                QVERIFY(label->width() >= QFontMetricsF(qvariant_cast<QFont>(label->property("font"))).horizontalAdvance("23"));
            }
            QVERIFY(capture(mode));
        }
        QVERIFY(click(visualItem(window->contentItem(), "calendarMode_week")));
        QVERIFY(click(visualItem(window->contentItem(), "calendarNewEvent")));
        auto *editor = window->findChild<QObject *>("calendarEventEditor"); QVERIFY(editor);
        QTRY_VERIFY(editor->property("visible").toBool());
        auto draft = workspace->eventDraft(); draft["title"] = "Publish checklist";
        QVERIFY(editor->setProperty("draft", draft));
        QVERIFY(capture("quick-add"));
        QVERIFY(click(visualItem(window->contentItem(), "calendarSaveEvent")));
        QTRY_VERIFY(!editor->property("visible").toBool());
        iiCalendar::Store store(openSocietyCalendarDatabase(calendar->databasePath().toStdString()));
        const auto matches = [&] {
            auto events = store.query();
            std::erase_if(events, [](const auto &event) { return event.title != "Publish checklist"; });
            return events;
        };
        QTRY_COMPARE(matches().size(), size_t(1));
        const auto created = QString::fromStdString(matches().front().id);
        workspace->selectEvent(created); QTest::qWait(100);
        QVERIFY(click(visualItem(window->contentItem(), "calendarEditEvent")));
        QTRY_VERIFY(editor->property("visible").toBool());
        draft = workspace->eventDraft(created); draft["title"] = "Published checklist";
        QVERIFY(editor->setProperty("draft", draft));
        QVERIFY(click(visualItem(window->contentItem(), "calendarSaveEvent")));
        QTRY_VERIFY(!editor->property("visible").toBool());
        QCOMPARE(store.get(created.toStdString())->title, std::string("Published checklist"));
        // The completed form round-trips through the same durable SDK store.
        QVERIFY(workspace->removeEvent(created, workspace->eventDraft(created).value("revision").toString()));
        QVERIFY(!store.get(created.toStdString()));
        for (int width : {760, 390}) {
            window->resize(width, 844); QTest::qWait(150);
            QVERIFY(page->isVisible());
            auto *newEvent = visualItem(window->contentItem(), "calendarNewEvent"); QVERIFY(newEvent);
            QVERIFY(newEvent->mapToScene(QPointF(newEvent->width(), 0)).x() <= width);
            QVERIFY(click(newEvent)); QTRY_VERIFY(editor->property("visible").toBool());
            QVERIFY(editor->property("width").toReal() <= width - 24);
            QVERIFY(capture("editor-width-" + QString::number(width)));
            QVERIFY(click(visualItem(window->contentItem(), "calendarCancelEvent")));
            QVERIFY(capture("width-" + QString::number(width)));
        }
        window->resize(1600, 960); QTest::qWait(100);
        QVERIFY(click(visualItem(window->contentItem(), "dashboardItemworkspace_home")));
        QTRY_VERIFY(!dashboard->property("calendarPageVisible").toBool());
        workspace->setActive(false); // The fixture overrides the OS-activation binding above.
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        window->close();
    }

    void dashboardCalendarMatchesFigmaAndNavigates()
    {
        using namespace iiCalendar;
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/calendar-dashboard-XXXXXX.societycontainer");
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
            Store store(openSocietyCalendarDatabase(calendar.databasePath().toStdString()));
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
        engine.addImageProvider(QStringLiteral("society-preview"), new iiSocietyContainer::PreviewProvider);
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
        QVERIFY(!view->isVisible());
        QVERIFY(QMetaObject::invokeMethod(item("dashboardItemworkspace_activity"), "clicked"));
        QTRY_VERIFY(view->isVisible());
        QVERIFY(QMetaObject::invokeMethod(view, "resetDetails"));
        QTRY_VERIFY(month->width() > 0 && detail->width() > 0);
        QCOMPARE(month->height(), 370.0); QCOMPARE(detail->height(), 370.0);
        QVERIFY(detail->mapToItem(dashboard, QPointF()).x() > month->mapToItem(dashboard, QPointF()).x());
        QVERIFY(item("calendarDot_2026-09-19")->isVisible());
        QCOMPARE(item("calendarDayTitle")->property("text").toString(), "Sat, Sep 19");
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
            QTRY_COMPARE(month->width(), view->width());
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
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/dashboard-cards-XXXXXX.societycontainer");
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
        engine.addImageProvider(QStringLiteral("society-preview"), new iiSocietyContainer::PreviewProvider);
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
        QTRY_VERIFY(recent->width() > 400);
        QCOMPARE(recent->height(), 160.0);
        QTRY_VERIFY(history->mapToItem(dashboard, QPointF()).y()
                    > recent->mapToItem(dashboard, QPointF()).y() + recent->height());
        auto *filesPanel = visualItem(dashboard, "dashboardFilesPanel"); QVERIFY(filesPanel);
        QVERIFY(filesPanel->mapToItem(dashboard, QPointF()).y()
                > recent->mapToItem(dashboard, QPointF()).y());
        QVERIFY(history->mapToItem(dashboard, QPointF()).y()
                > filesPanel->mapToItem(dashboard, QPointF()).y());
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
        QCOMPARE(first->property("previewSource").toUrl(), recentFiles.first().toMap().value("thumbnailSource").toUrl());
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

        revealDashboardItem(dashboard, history);
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
            QTRY_VERIFY(recent->width() > 0);
            QTRY_COMPARE(recent->height(), 160.0);
            QTRY_COMPARE(history->size(), recent->size());
            QTRY_VERIFY(recent->mapToItem(dashboard, QPointF(recent->width(), 0)).x() <= width - 24 + 0.5);
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

    void quickGenerateUsesLvrsComposer_data()
    {
        QTest::addColumn<int>("width");
        QTest::newRow("mobile-320") << 320;
        QTest::newRow("mobile-390") << 390;
        QTest::newRow("desktop-960") << 960;
        QTest::newRow("figma-exact") << 1220;
    }

    void quickGenerateUsesLvrsComposer()
    {
        QFETCH(int, width);
        QQmlEngine engine;
        engine.addImageProvider(QStringLiteral("society-preview"), new iiSocietyContainer::PreviewProvider);
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        const auto path = QFileInfo(QString::fromUtf8(SOCIETY_QML_FILE)).dir().filePath("Tools/ToolsView.qml");
        QQmlComponent component(&engine, QUrl::fromLocalFile(path));
        QScopedPointer<QObject> object(component.createWithInitialProperties({{"width", width}, {"height", 600}}));
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *tools = qobject_cast<QQuickItem *>(object.data()); QVERIFY(tools);
        QQuickWindow window; window.resize(width, 600);
        window.setColor(QColor("#0b0b0b"));
        tools->setParentItem(window.contentItem()); window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *quick = visualItem(tools, "quickGenerate");
        auto *content = visualItem(tools, "quickGenerateContent");
        auto *ratio = visualItem(tools, "aspectRatioButton");
        auto *quantity = visualItem(tools, "generationCountButton");
        auto *prompt = visualItem(tools, "promptField");
        auto *media = visualItem(tools, "mediaTypeButton");
        auto *generate = visualItem(tools, "generateButton");
        auto *menu = tools->findChild<QObject *>("mediaTypeMenu");
        QVERIFY(quick && content && prompt && media && ratio && quantity && generate && menu);
        QVERIFY(!tools->findChild<QObject *>("quickGenerateComposer"));
        QTRY_COMPARE(content->height(), 52.0);
        QCOMPARE(quick->height(), 72.0);
        QCOMPARE(prompt->property("placeholderText").toString(), QString("Prompt"));
        for (auto *control : {prompt, media, ratio, quantity, generate}) {
            QCOMPARE(control->height(), 22.0);
            QVERIFY(window.contentItem()->boundingRect().contains(control->mapRectToScene(control->boundingRect())));
        }
        for (auto* control : {media, ratio, quantity}) {
            auto *chevron = visualItem(control, "quickGenerateChevron"); QVERIFY(chevron);
            QCOMPARE(chevron->size(), QSizeF(18, 18));
            QTRY_COMPARE(chevron->property("status").toInt(), 1);
        }
        QSignalSpy submitted(tools, SIGNAL(generateRequested(QString,QString,QString,int)));
        QVERIFY(submitted.isValid());
        const auto click = [&](QQuickItem *item) {
            QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                item->mapToScene(item->boundingRect().center()).toPoint());
        };
        click(generate); QCOMPARE(submitted.size(), 0);
        auto *ratioMenu = tools->findChild<QObject *>("aspectRatioMenu");
        auto *countMenu = tools->findChild<QObject *>("generationCountMenu");
        QVERIFY(ratioMenu && countMenu);
        click(ratio); QTRY_VERIFY(ratioMenu->property("opened").toBool());
        QVERIFY(QMetaObject::invokeMethod(ratioMenu, "triggerEntry", Q_ARG(QVariant, 3)));
        QTRY_VERIFY(!ratioMenu->property("visible").toBool());
        QCOMPARE(ratio->property("text").toString(), QString("16:9"));
        click(quantity); QTRY_VERIFY(countMenu->property("opened").toBool());
        QVERIFY(QMetaObject::invokeMethod(countMenu, "triggerEntry", Q_ARG(QVariant, 16)));
        QTRY_VERIFY(!countMenu->property("visible").toBool());
        QCOMPARE(quantity->property("text").toString(), QString("100"));
        QVERIFY(prompt->setProperty("text", "  A quiet landscape  "));
        click(media); QTRY_VERIFY(menu->property("opened").toBool());
        QVERIFY(QMetaObject::invokeMethod(menu, "triggerEntry", Q_ARG(QVariant, 1)));
        QTRY_VERIFY(!menu->property("visible").toBool());
        QCOMPARE(quick->property("mediaType").toString(), QString("Video"));
        click(generate);
        QTRY_COMPARE(submitted.size(), 1);
        QCOMPARE(submitted.first(), QVariantList({"A quiet landscape", "Video", "16:9", 100}));
        QCOMPARE(prompt->property("text").toString(), QString("  A quiet landscape  "));
        click(media); QTRY_VERIFY(menu->property("opened").toBool());
        QVERIFY(QMetaObject::invokeMethod(menu, "triggerEntry", Q_ARG(QVariant, 0)));
        QTRY_VERIFY(!menu->property("visible").toBool());
        auto *touch = QTest::createTouchDevice();
        const auto point = generate->mapToScene(generate->boundingRect().center()).toPoint();
        QTest::touchEvent(&window, touch).press(0, point, &window);
        QTest::touchEvent(&window, touch).release(0, point, &window);
        QTRY_COMPARE(submitted.size(), 2);
        QCOMPARE(submitted.last(), QVariantList({"A quiet landscape", "Image", "16:9", 100}));
        // Drain Cocoa's synthesized mouse events before destroying the touch window.
        QTest::qWait(500);
        const auto directory = qEnvironmentVariable("SOCIETY_COMPOSER_CAPTURE_DIR");
        if (!directory.isEmpty()) {
            QVERIFY(quick->setProperty("prompt", ""));
            QVERIFY(quick->setProperty("aspectRatio", "1:1"));
            QVERIFY(QDir().mkpath(directory)); QTest::qWait(100);
            QVERIFY(window.grabWindow().save(directory + '/' + QTest::currentDataTag() + ".png"));
            auto grab = content->grabToImage();
            QSignalSpy ready(grab.data(), &QQuickItemGrabResult::ready);
            QVERIFY(ready.wait(3000));
            QVERIFY(grab->saveToFile(directory + "/quick-" + QTest::currentDataTag() + ".png"));
        }
        tools->setParentItem(nullptr);
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
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/mobile-views-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        QVERIFY(QDir().mkpath(fixture.filePath("Files/Work")));
        QImage sample(240, 160, QImage::Format_RGB32); sample.fill(Qt::darkCyan);
        QVERIFY(sample.save(fixture.filePath("Files/Mobile landscape.png")));
        QVERIFY(sample.save(fixture.filePath("Generation History/Mobile study.png")));
        QQmlApplicationEngine engine;
        engine.addImageProvider(QStringLiteral("society-preview"), new iiSocietyContainer::PreviewProvider);
        QStringList warnings;
        connect(&engine, &QQmlApplicationEngine::warnings, this, [&](const QList<QQmlError> &errors) {
            for (const auto &error : errors) warnings.append(error.toString());
        });
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        engine.setInitialProperties({{"initialContainerPath", fixture.path()}, {"mobileLayout", true},
            {"desktopMinWidth", 320}, {"width", 390}, {"height", 844}});
        loadSociety(engine);
        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *view = window->findChild<QQuickItem *>("societyContent");
        auto *dashboard = window->findChild<QQuickItem *>("dashboardView");
        QQuickItem *tools = nullptr;
        QQuickItem *storage = nullptr;
        auto *drive = window->findChild<DriveController *>("driveController");
        auto *files = window->findChild<DashboardFiles *>("dashboardFiles");
        QQuickItem *prompt = nullptr;
        auto *tabs = window->findChild<QQuickItem *>("mobileTabBar");
        QVERIFY(view && dashboard && drive && files && tabs);
        QVERIFY(tabs->setProperty("platformStyle", tabPlatformStyle));
        QTRY_COMPARE(tabs->property("count").toInt(), 5);
        QCOMPARE(tabs->property("autoSelect").toBool(), false);
        QCOMPARE(tabs->property("bottomSafeInset").toReal(), 0.0); // Main already owns the system safe area.
        auto *tabList = QAccessible::queryAccessibleInterface(tabs); QVERIFY(tabList);
        QCOMPARE(tabList->role(), QAccessible::PageTabList);
        for (const QString name : {"mobileNavigationToggle", "mobileSearchToggle", "mobileNotifications", "mobileAccount"}) {
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
        QCOMPARE(window->findChildren<QQuickItem *>("toolsView").size(), 0);
        QCOMPARE(window->findChildren<QQuickItem *>("storageView").size(), 0);
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
        QVERIFY(tap("mobileToolsTab"));
        QTRY_COMPARE(window->findChild<QObject *>("toolsLoader")->property("status").toInt(), 1);
        QTRY_VERIFY((tools = window->findChild<QQuickItem *>("toolsView")) && tools->isVisible());
        QTRY_VERIFY((prompt = window->findChild<QQuickItem *>("promptField")));
        QVERIFY(prompt->setProperty("text", "Keep this mobile prompt"));
        QTRY_COMPARE(tabs->property("currentIndex").toInt(), 1);
        auto *toolsTab = visualItem(window->contentItem(), "mobileToolsTab"); QVERIFY(toolsTab);
        auto *accessibleTab = QAccessible::queryAccessibleInterface(toolsTab); QVERIFY(accessibleTab);
        QCOMPARE(accessibleTab->role(), QAccessible::PageTab);
        QTRY_VERIFY(accessibleTab->state().selected);
        QVERIFY(!window->findChild<QQuickItem *>("modelMergeTool"));
        QVERIFY(tap("toolCard-model-merge"));
        QQuickItem *merge = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((merge = window->findChild<QQuickItem *>("modelMergeTool")), 15000);
        QTRY_VERIFY(merge->isVisible());
        QVERIFY(merge->setProperty("outputName", "mobile-session"));
        QVERIFY(tap("mobileStorageTab"));
        QTRY_COMPARE(window->findChild<QObject *>("storageLoader")->property("status").toInt(), 1);
        QTRY_VERIFY((storage = window->findChild<QQuickItem *>("storageView")) && storage->isVisible());
        QVERIFY(drive->navigate(fixture.filePath("Files/Work")));
        QVERIFY(tap("mobileDashboardTab")); QTRY_VERIFY(dashboard->isVisible());
        QCOMPARE(prompt->property("text").toString(), QString("Keep this mobile prompt"));
        QVERIFY(tap("mobileToolsTab")); QTRY_VERIFY(tools->isVisible());
        QCOMPARE(merge->property("outputName").toString(), QString("mobile-session"));
        QVERIFY(tap("mobileStorageTab")); QTRY_VERIFY(storage->isVisible());
        QCOMPARE(drive->currentPath(), fixture.filePath("Files"));
        auto *searchToggle = visualItem(window->contentItem(), "mobileSearchToggle"); QVERIFY(searchToggle);
        QSignalSpy searchClicks(searchToggle, SIGNAL(clicked())); QVERIFY(searchClicks.isValid());
        QVERIFY(tap("mobileSearchToggle"));
        auto *search = window->findChild<QQuickItem *>("mobileSearch"); QVERIFY(search);
        QTRY_VERIFY(search->isVisible() && dashboard->isVisible());
        QTRY_COMPARE(searchClicks.count(), 1);
        QTRY_COMPARE(search->height(), 22.0);
        QSignalSpy searchNavigation(view, SIGNAL(tabRequested(QString))); QVERIFY(searchNavigation.isValid());
        QVERIFY(search->setProperty("text", "Mobile"));
        QVERIFY(search->setProperty("text", "Mobile landscape"));
        QTRY_COMPARE(files->recentFiles().size(), 1);
        QCOMPARE(searchNavigation.count(), 0); // Typing must not navigate again and dismiss the keyboard.
        QCOMPARE(view->property("query").toString(), QString("Mobile landscape"));
        QVERIFY(search->setProperty("text", ""));
        QTest::qWait(600); // Separate this toggle from the synthetic opening touch.
        QVERIFY(tap("mobileSearchToggle"));
        QTRY_COMPARE(searchClicks.count(), 2);
        QTRY_VERIFY(!search->isVisible());
        QVERIFY(tap("mobileBrowseTab"));
        QCOMPARE(tabs->property("currentIndex").toInt(), 3);
        auto *devices = window->findChild<QObject *>("networkDevices"); QVERIFY(devices);
        QVERIFY(!devices->property("visible").toBool());
        QVERIFY(tap("mobileEnvironmentTab"));
        QCOMPARE(tabs->property("currentIndex").toInt(), 4);
        QTRY_COMPARE(window->findChild<QObject *>("environmentLoader")->property("status").toInt(), 1);
        QTRY_VERIFY(window->findChild<QQuickItem *>("environmentView"));
        auto *environment = window->findChild<QQuickItem *>("environmentView"); QVERIFY(environment);
        QTRY_VERIFY(environment->isVisible());
        QVERIFY(!window->findChild<QQuickWindow *>("preferencesWindow"));
        QVERIFY(tap("mobileDashboardTab"));
        auto *mobileNotificationCenter = window->findChild<QObject *>("notificationCenter"); QVERIFY(mobileNotificationCenter);
        QVERIFY(QMetaObject::invokeMethod(mobileNotificationCenter, "record",
            Q_ARG(QVariant, QString("Models")), Q_ARG(QVariant, QString("Mobile warning"))));
        auto *mobileNotificationButton = visualItem(window->contentItem(), "mobileNotifications"); QVERIFY(mobileNotificationButton);
        QTRY_COMPARE(mobileNotificationButton->property("text").toString(), QString("1"));
        QTRY_VERIFY(mobileNotificationButton->width() > 22);
        QVERIFY(tap("mobileNotifications"));
        auto *mobileNotifications = window->findChild<QObject *>("notificationPopover"); QVERIFY(mobileNotifications);
        QTRY_VERIFY(mobileNotifications->property("visible").toBool());
        QVERIFY(tap("notificationDismiss0"));
        QTRY_COMPARE(mobileNotificationCenter->property("count").toInt(), 0);
        QTRY_COMPARE(mobileNotificationButton->property("text").toString(), QString());
        QVERIFY(QMetaObject::invokeMethod(mobileNotifications, "close"));
        QTRY_VERIFY(!mobileNotifications->property("visible").toBool());
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
                for (const QString name : {"mobileNavigationToggle", "mobileSearchToggle", "mobileNotifications", "mobileAccount"}) {
                    auto *button = visualItem(window->contentItem(), name); QVERIFY(button);
                    QTRY_COMPARE(button->mapRectToScene(button->boundingRect()).size(), QSizeF(22, 22));
                }
                for (const QString label : {"Dashboard", "Tools", "Storage", "Browse", "Environment"}) {
                    auto *item = visualItem(window->contentItem(), "mobile" + label + "Label"); QVERIFY(item);
                    QTRY_VERIFY(!item->property("truncated").toBool());
                }
            } else {
                for (const QString name : {"dashboardTabs", "dashboardSearch"}) {
                    auto *control = visualItem(window->contentItem(), name); QVERIFY(control);
                    QTRY_COMPARE(control->mapRectToScene(control->boundingRect()).height(), 22.0);
                }
                auto *account = visualItem(window->contentItem(), "dashboardAccount");
                QVERIFY(account);
                QCOMPARE(account->size(), QSizeF(40, 40));
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
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/mobile-storage-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        QImage photo(90, 160, QImage::Format_RGB32); photo.fill(Qt::darkMagenta);
        QVERIFY(photo.save(fixture.filePath("Generation History/Portrait.png")));
        QQmlApplicationEngine engine;
        engine.addImageProvider(QStringLiteral("society-preview"), new iiSocietyContainer::PreviewProvider);
        QStringList warnings;
        connect(&engine, &QQmlApplicationEngine::warnings, this, [&](const QList<QQmlError> &errors) {
            for (const auto &error : errors) warnings.append(error.toString());
        });
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        engine.setInitialProperties({{"initialContainerPath", fixture.path()}, {"mobileLayout", true},
            {"selectedTab", "Storage"}, {"desktopMinWidth", 320}, {"width", 320}, {"height", 568}});
        loadSociety(engine);
        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *drive = window->findChild<DriveController *>("driveController"); QVERIFY(drive);
        QVERIFY(!window->findChild<QQuickItem *>("sectionsGrid"));
        QQuickItem *browser = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((browser = window->findChild<QQuickItem *>("filesBrowser")), 15000);
        QTRY_VERIFY(browser->isVisible());
        QTRY_COMPARE(drive->currentPath(), fixture.filePath("Files"));
        QCOMPARE(drive->sections().size(), 9);
        QObject *actions = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((actions = window->findChild<QObject *>("storageActions")), 15000);
        QQuickItem *toggle = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((toggle = visualItem(window->contentItem(), "storageActionsToggle")), 15000);
        QTRY_VERIFY(toggle->isVisible() && toggle->width() > 0 && toggle->height() > 0);
        QSignalSpy toggleClicked(toggle, SIGNAL(clicked())); QVERIFY(toggleClicked.isValid());
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
            toggle->mapToScene(QPointF(toggle->width()/2, toggle->height()/2)).toPoint());
        QTRY_COMPARE(toggleClicked.count(), 1);
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
        QQuickItem *models = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((models = window->findChild<QQuickItem *>("modelsView")), 15000);
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
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/window-content-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        QQmlApplicationEngine engine;
        engine.addImageProvider(QStringLiteral("society-preview"), new iiSocietyContainer::PreviewProvider);
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        engine.setInitialProperties({{"initialContainerPath", fixture.path()}});
        loadSociety(engine);
        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        QTRY_VERIFY(window->isVisible());
        QQmlComponent theme(&engine);
        theme.setData("import QtQuick\nimport LVRS 1.0 as LV\nQtObject { property color accent: LV.Theme.accentGreen }", {});
        QScopedPointer<QObject> themeValues(theme.create());
        QVERIFY2(themeValues, qPrintable(theme.errorString()));
        const auto accent = themeValues->property("accent").value<QColor>();
        QCOMPARE(window->property("primaryColor").value<QColor>(), accent);
        auto *material = window->findChild<QQuickItem *>("applicationWindowMaterial");
        QVERIFY(material);
        QCOMPARE(material->property("primaryColor").value<QColor>(), accent);
        const auto baseColor = window->property("windowColor").value<QColor>();
        QCOMPARE(material->property("color").value<QColor>(), baseColor);
        QVERIFY(window->setProperty("primaryColor", QColor("#4d6fc1")));
        QTRY_COMPARE(material->property("color").value<QColor>(), baseColor);
        QVERIFY(window->setProperty("primaryColor", accent));
        QVERIFY(QMetaObject::invokeMethod(window, "openPreferences"));
        auto *preferences = window->findChild<QQuickWindow *>("preferencesWindow");
        QVERIFY(preferences);
        auto *preferencesMaterial = preferences->findChild<QQuickItem *>("applicationWindowMaterial");
        QVERIFY(preferencesMaterial);
        QCOMPARE(preferencesMaterial->property("color").value<QColor>(), baseColor);
        QVERIFY(preferences->setProperty("primaryColor", QColor("#4d6fc1")));
        QTRY_COMPARE(preferencesMaterial->property("color").value<QColor>(), baseColor);
        preferences->close();
        QCOMPARE(material->property("accentMix").toReal(), 0.02);
        QVERIFY(!material->findChild<QQuickItem *>("applicationWindowAccentOverlay"));
        QVERIFY(material->findChild<QQuickItem *>("applicationWindowNoiseTexture"));
        QCOMPARE(window->property("windowBackgroundOpacity").toReal(), 1.0);
        QCOMPARE(window->opacity(), 1.0);
        QCOMPARE(material->opacity(), 1.0);
        const bool nativeBlur = window->property("isDesktopPlatform").toBool()
            && window->property("backgroundBlurSupported").toBool();
        QCOMPARE(window->property("backgroundBlurEnabled").toBool(), nativeBlur);
        if (nativeBlur) {
            QVERIFY(QTest::qWaitForWindowExposed(window));
            QTRY_VERIFY(window->property("backgroundBlurActive").toBool());
            QCOMPARE(window->color().alpha(), 0);
            const QImage rendered = window->grabWindow();
            QVERIFY(!rendered.isNull());
            QVERIFY(rendered.hasAlphaChannel());
            const qreal dpr = window->devicePixelRatio();
            const QColor backgroundPixel = rendered.pixelColor(qRound(8 * dpr), qRound(80 * dpr));
            QVERIFY2(qAbs(backgroundPixel.alpha() - baseColor.alpha()) <= 2,
                     qPrintable(QStringLiteral("The rendered window must retain the base surface opacity; got alpha %1.")
                         .arg(backgroundPixel.alpha())));
            auto capture = material->grabToImage();
            QVERIFY(capture);
            QTRY_VERIFY(!capture->image().isNull());
            const QImage background = capture->image();
            const auto backgroundAt = [&](qreal x, qreal y) {
                return background.pixelColor(qRound(x * background.width() / material->width()),
                                             qRound(y * background.height() / material->height()));
            };
            const QColor edge = backgroundAt(48, 80);
            const QColor quiet = backgroundAt(material->width() / 2, 80);
            QVERIFY2(qAbs(edge.red() - quiet.red()) <= 1 && qAbs(edge.green() - quiet.green()) <= 1
                         && qAbs(edge.blue() - quiet.blue()) <= 1
                         && qAbs(quiet.red() - baseColor.red()) <= 1 && qAbs(quiet.green() - baseColor.green()) <= 1
                         && qAbs(quiet.blue() - baseColor.blue()) <= 1,
                     qPrintable(QStringLiteral("The window surface must remain uniformly neutral: edge %1, center %2.")
                         .arg(edge.name(QColor::HexArgb), quiet.name(QColor::HexArgb))));
            QVERIFY(qAbs(quiet.alpha() - baseColor.alpha()) <= 2);
            const QString captureDirectory = qEnvironmentVariable("SOCIETY_PANEL_CAPTURE_DIR");
            if (!captureDirectory.isEmpty())
                QVERIFY(rendered.save(captureDirectory + QStringLiteral("/window-accent-overlay.png")));
#ifdef Q_OS_MACOS
            QVERIFY2(societyNativeWindowHasTranslucentBackdrop(window),
                     "Society must retain an active behind-window AppKit backdrop in a nonopaque window.");
#endif
        }
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
        QVERIFY(!view->findChild<QQuickItem *>("storageView"));
        QVERIFY(window->setProperty("selectedTab", "Storage"));
        QTRY_VERIFY(view->findChild<QQuickItem *>("storageView"));
        for (int i = 0; i < 3; ++i)
            QVERIFY(!visualItem(window->contentItem(), QString("windowControl%1").arg(i)));
        QTRY_COMPARE(view->mapToScene(QPointF()).y(), 0.0);
        QTRY_COMPARE(view->mapToScene(QPointF(0, view->height())).y(),
                     window->height() - window->property("mobileSystemSafeBottomInset").toReal());
        window->close();
    }

    void panelsRevealApplicationWindowBackground_data()
    {
        QTest::addColumn<QSize>("size");
        QTest::addColumn<bool>("mobile");
        QTest::newRow("desktop") << QSize(1440, 900) << false;
        QTest::newRow("mobile") << QSize(390, 844) << true;
    }

    void panelsRevealApplicationWindowBackground()
    {
        QFETCH(QSize, size);
        QFETCH(bool, mobile);
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/panel-backgrounds-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        QImage preview(80, 80, QImage::Format_RGB32); preview.fill(Qt::green);
        QVERIFY(preview.save(fixture.filePath("Files/Preview.png")));
        QVERIFY(writeCatalogModel(fixture.filePath("Models/Checkpoint/Fixture.safetensors"),
                                  {{"architecture", "stable-diffusion-v1"}}));
        QQmlApplicationEngine engine;
        engine.addImageProvider(QStringLiteral("society-preview"), new iiSocietyContainer::PreviewProvider);
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        engine.setInitialProperties({{"initialContainerPath", fixture.path()}, {"mobileLayout", mobile},
            {"width", size.width()}, {"height", size.height()}, {"backgroundBlurEnabled", false},
            {"windowBackgroundOpacity", 1.0}});
        loadSociety(engine);
        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        for (const auto &tab : {"Storage", "Environment", "Tools"}) {
            QVERIFY(window->setProperty("selectedTab", QString::fromLatin1(tab)));
            const QString name = QString::fromLatin1(tab).toLower() + "View";
            QTRY_VERIFY(window->findChild<QQuickItem *>(name));
        }
        QVERIFY(window->setProperty("selectedTab", "Dashboard"));
        const QStringList panels{"globalToolbarSurface", "dashboardSidebar", "driveSidebar",
            "calendarMonthPanel", "calendarDetailPanel", "environmentView", "environmentAccountPanel"};
        for (const auto &color : {QColor("#123447"), QColor("#452136")}) {
            QVERIFY(window->setProperty("windowColor", color));
            for (const auto &name : panels) {
                auto *panel = visualItem(window->contentItem(), name); QVERIFY2(panel, qPrintable(name));
                QCOMPARE(panel->property("color").value<QColor>().alpha(), 0);
            }
        }
        // Only the outline changes: the transparent panel still reveals the window.
        for (const auto &name : {"calendarMonthPanel", "calendarDetailPanel",
                                 "environmentAccountPanel", "environmentOverviewPanel"}) {
            auto *panel = visualItem(window->contentItem(), name); QVERIFY2(panel, name);
            QCOMPARE(panel->property("panelBorderColor").value<QColor>(), QColor("#0c0c0c"));
            QCOMPARE(QQmlProperty(panel, "border.color").read().value<QColor>(),
                     panel->property("panelBorderColor").value<QColor>());
        }
        const QString captureDirectory = qEnvironmentVariable("SOCIETY_PANEL_CAPTURE_DIR");
        int checkedCards = 0;
        const auto checkCards = [&](auto &&self, QQuickItem *item) -> bool {
            if (item->metaObject()->indexOfProperty("imageCard") >= 0) {
                auto *background = qvariant_cast<QQuickItem *>(item->property("background"));
                if (!background) return false;
                const auto state = item->property("displayState");
                item->setProperty("displayState", 1); // LV.Card.DefaultState
                const bool transparent = background->property("color").value<QColor>().alpha() == 0;
                item->setProperty("displayState", state);
                if (!transparent) { qWarning() << "Opaque card:" << item->objectName(); return false; }
                ++checkedCards;
            }
            for (auto *child : item->childItems()) if (!self(self, child)) return false;
            return true;
        };
        QVERIFY(checkCards(checkCards, window->contentItem()));
        QVERIFY(checkedCards >= 8);
        auto *toolCard = visualItem(window->contentItem(), "toolCard-model-merge"); QVERIFY(toolCard);
        auto *toolBackground = qvariant_cast<QQuickItem *>(toolCard->property("background")); QVERIFY(toolBackground);
        toolCard->setProperty("displayState", 2); // LV.Card.HoverState
        QCOMPARE(toolBackground->property("color").value<QColor>(), toolCard->property("surfaceColor").value<QColor>());
        toolCard->setProperty("displayState", 0);
        const auto capture = [&](const QString &name) {
            if (captureDirectory.isEmpty()) return true;
            QTest::qWait(250);
            return window->grabWindow().save(QDir(captureDirectory).filePath(
                name + (mobile ? "-mobile.png" : "-desktop.png")));
        };
        QVERIFY(window->setProperty("windowColor", QColor("#0b0b0b")));
        for (const auto &tab : {"Dashboard", "Storage", "Environment", "Tools"}) {
            QVERIFY(window->setProperty("selectedTab", QString::fromLatin1(tab)));
            QTest::qWait(150);
            QVERIFY(capture(QString::fromLatin1(tab).toLower()));
        }
        auto *tools = visualItem(window->contentItem(), "toolsView"); QVERIFY(tools);
        for (const auto &tool : {"model-merge", "model-packaging"}) {
            QVERIFY(tools->setProperty("activeTool", QString::fromLatin1(tool)));
            QTest::qWait(100);
            const QStringList cardNames = QString::fromLatin1(tool) == "model-merge"
                ? QStringList{"mergeInputPanel", "mergeSettingsPanel", "mergeOutputPanel", "mergeReviewPanel"}
                : QStringList{"packagingSourcePanel", "packagingComponentsPanel", "packagingOutputPanel", "packagingSummaryPanel"};
            for (const auto &name : cardNames) {
                auto *card = visualItem(tools, name); QVERIFY2(card, qPrintable(name));
                auto *background = qvariant_cast<QQuickItem *>(card->property("background")); QVERIFY(background);
                QCOMPARE(background->property("color").value<QColor>().alpha(), 0);
                QCOMPARE(background->property("panelBorderColor").value<QColor>(), QColor("#0c0c0c"));
                QCOMPARE(QQmlProperty(background, "border.color").read().value<QColor>(),
                         background->property("panelBorderColor").value<QColor>());
            }
            QVERIFY(capture(QString::fromLatin1(tool)));
        }
        window->close();
    }

    void globalToolbarShowsOnlyTheSelectedSegmentBackground()
    {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/toolbar-segment-background-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        QQmlApplicationEngine engine;
        engine.addImageProvider(QStringLiteral("society-preview"), new iiSocietyContainer::PreviewProvider);
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        engine.setInitialProperties({{"initialContainerPath", fixture.path()}, {"width", 1440}, {"height", 900}});
        loadSociety(engine);
        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTest::mouseMove(window, QPoint(2, window->height() - 2));
        auto *tabs = window->findChild<NativeNavigationSurface *>("dashboardTabs"); QVERIFY(tabs);
        auto *accessible = QAccessible::queryAccessibleInterface(tabs); QVERIFY(accessible);
        QCOMPARE(accessible->childCount(), 7);
        const QList<QPair<QString, int>> pages{{"Dashboard", 0}, {"Environment", 0},
            {"Tools", 1}, {"Storage", 2}, {"Browse", 4}};
        for (const auto &[page, selected] : pages) {
            QVERIFY(window->setProperty("selectedTab", page));
            QTRY_COMPARE(tabs->currentPage(), page);
            for (int index = 0; index < 7; ++index)
                QTRY_COMPARE(accessible->child(index)->state().selected, index == selected);
        }
        auto *legacy = window->findChild<QQuickItem *>("legacyNavigationSegments");
        QVERIFY(legacy && !legacy->isVisible() && !legacy->isEnabled());
        QCOMPARE(LegacyNavigationBridge::install(window), tabs);
        window->close();
    }

    void tasksSegmentOpensExistingTools()
    {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/tasks-navigation-XXXXXX.societycontainer");
        QVERIFY(fixture.isValid());
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        QQmlApplicationEngine engine;
        engine.addImageProvider(QStringLiteral("society-preview"), new iiSocietyContainer::PreviewProvider);
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        engine.setInitialProperties({{"initialContainerPath", fixture.path()}, {"width", 1440}, {"height", 900}});
        loadSociety(engine);
        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *tabs = window->findChild<NativeNavigationSurface *>("dashboardTabs");
        auto *loader = window->findChild<QObject *>("toolsLoader");
        QVERIFY(tabs && loader);
        auto *group = QAccessible::queryAccessibleInterface(tabs); QVERIFY(group);
        auto *accessible = group->child(1); QVERIFY(accessible);
        QCOMPARE(accessible->text(QAccessible::Name), QString("Tasks"));
        QVERIFY(!accessible->state().selected);
        QVERIFY(!window->findChild<QObject *>("toolsView"));

        const auto click = [window](QQuickItem *item) {
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                              item->mapToScene(item->boundingRect().center()).toPoint());
        };
        QVERIFY(clickNavigationSegment(window, 1));
        QTRY_COMPARE(window->property("selectedTab").toString(), QString("Tools"));
        QTRY_COMPARE_WITH_TIMEOUT(loader->property("status").toInt(), 1, 15000);
        auto *tools = window->findChild<QQuickItem *>("toolsView"); QVERIFY(tools);
        QTRY_VERIFY(tools->isVisible());
        QTRY_VERIFY(accessible->state().selected);
        QVERIFY(visualItem(tools, "quickGenerate"));
        QTRY_VERIFY(visualItem(tools, "toolCard-model-merge"));
        QVERIFY(visualItem(tools, "toolCard-model-packaging"));
        QVERIFY(visualItem(tools, "toolCard-model-calibrator"));
        const auto toolCatalog = tools->property("tools").value<QJSValue>().toVariant().toList();
        QCOMPARE(toolCatalog.size(), 3);
        QCOMPARE(toolCatalog.at(2).toMap().value("key").toString(), QString("model-calibrator"));
        auto *toolSearch = visualItem(tools, "toolsSearch"); QVERIFY(toolSearch);
        QVERIFY(toolSearch->setProperty("text", "calibrator"));
        QTRY_COMPARE(tools->property("filteredTools").value<QJSValue>().toVariant().toList().size(), 1);
        QVERIFY(toolSearch->setProperty("text", ""));
        QTRY_COMPARE(tools->property("filteredTools").value<QJSValue>().toVariant().toList().size(), 3);
        QTRY_VERIFY(visualItem(tools, "toolCard-model-merge") && visualItem(tools, "toolCard-model-merge")->isVisible());
        QTest::qWait(100);
        click(visualItem(tools, "toolCard-model-merge"));
        QTRY_COMPARE(tools->property("activeTool").toString(), QString("model-merge"));

        QVERIFY(clickNavigationSegment(window, 0));
        QTRY_COMPARE(window->property("selectedTab").toString(), QString("Environment"));
        QTRY_VERIFY(!accessible->state().selected);
        tabs->focusAccessibleItem(1);
        QTest::keyClick(window, Qt::Key_Space);
        QTRY_COMPARE(window->property("selectedTab").toString(), QString("Tools"));
        QCOMPARE(window->findChild<QQuickItem *>("toolsView"), tools);
        QCOMPARE(tools->property("activeTool").toString(), QString("model-merge"));
        QTRY_VERIFY(accessible->state().selected);
        window->close();
    }

    void globalToolbarMatchesFigmaAndRoutesActions()
    {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/global-toolbar-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        QQmlApplicationEngine engine;
        engine.addImageProvider(QStringLiteral("society-preview"), new iiSocietyContainer::PreviewProvider);
        QStringList warnings;
        connect(&engine, &QQmlApplicationEngine::warnings, this, [&](const QList<QQmlError> &errors) {
            for (const auto &error : errors) warnings.append(error.toString());
        });
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        engine.setInitialProperties({{"initialContainerPath", fixture.path()}, {"width", 1440}, {"height", 900}});
        loadSociety(engine);
        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *toolbar = window->findChild<QQuickItem *>("dashboardToolbar");
        auto *surface = window->findChild<QQuickItem *>("globalToolbarSurface");
        auto *tabs = window->findChild<NativeNavigationSurface *>("dashboardTabs");
        auto *search = window->findChild<QQuickItem *>("dashboardSearch");
        auto *searchIcon = window->findChild<QQuickItem *>("globalSearchIcon");
        auto *notifications = window->findChild<QQuickItem *>("dashboardNotifications");
        auto *account = window->findChild<QQuickItem *>("dashboardAccount");
        auto *content = window->findChild<QQuickItem *>("societyContent");
        auto *drive = window->findChild<DriveController *>("driveController");
        auto *notice = window->findChild<QObject *>("dashboardNotice");
        auto *notificationCenter = window->findChild<QObject *>("notificationCenter");
        auto *notificationPopover = window->findChild<QObject *>("notificationPopover");
        QVERIFY(toolbar && surface && tabs && search && searchIcon && notifications && account && content && drive && notice);
        QVERIFY(notificationCenter && notificationPopover);
        QTRY_COMPARE(toolbar->height(), 56.0);
        QCOMPARE(surface->property("color").value<QColor>().alpha(), 0);
        auto *segments = QAccessible::queryAccessibleInterface(tabs); QVERIFY(segments);
        QTRY_COMPARE(segments->childCount(), 7);
        QTRY_COMPARE(tabs->size(), QSizeF(166, 22));
        QTRY_VERIFY(qAbs(tabs->mapToItem(toolbar, QPointF()).x()
            - window->property("nativeTitleBarControlsRect").toRectF().right()
            - (window->property("nativeTitleBarControlsRect").toRectF().width() > 0 ? 8.0 : 12.0)) <= 0.5);
        QTRY_COMPARE(search->width(), 300.0);
        QCOMPARE(search->height(), 22.0);
        QCOMPARE(search->mapToItem(toolbar, QPointF()), QPointF(1050, 17));
        QCOMPARE(notifications->size(), QSizeF(22, 22));
        QCOMPARE(notifications->mapToItem(toolbar, QPointF()), QPointF(1358, 17));
        QCOMPARE(account->size(), QSizeF(40, 40));
        QCOMPARE(account->mapToItem(toolbar, QPointF()), QPointF(1388, 8));
        QCOMPARE(searchIcon->size(), QSizeF(12, 12));
        QTRY_COMPARE(searchIcon->property("status").toInt(), 1); // Image.Ready
        QVERIFY(searchIcon->property("source").toUrl().toString().endsWith("Toolbar/icons/search.svg"));
        auto *icon = visualItem(notifications, "iconButton_icon"); QVERIFY(icon);
        QCOMPARE(icon->size(), QSizeF(18, 18));
        QTRY_COMPARE(icon->property("status").toInt(), 1);
        QTest::mouseMove(window, QPoint(2, window->height() - 2));
        QCoreApplication::processEvents();
        QCOMPARE(notifications->property("backgroundColor").value<QColor>(),
                 notifications->property("toneBackgroundColor").value<QColor>());
        QVERIFY(notifications->property("resolvedIconSource").toUrl().toString().endsWith("Toolbar/icons/notifications.svg"));
        auto *avatar = visualItem(account, "dashboardAvatar"); QVERIFY(avatar);
        auto *fallback = visualItem(avatar, "accountAvatarFallback"); QVERIFY(fallback);
        QCOMPARE(avatar->size(), QSizeF(40, 40));
        QCOMPARE(fallback->size(), QSizeF(20, 20));
        QVERIFY(fallback->isVisible());
        QTRY_COMPARE(fallback->property("status").toInt(), 1);
        const auto *accountAccessible = QAccessible::queryAccessibleInterface(account); QVERIFY(accountAccessible);
        QCOMPARE(accountAccessible->role(), QAccessible::Button);
        const auto click = [window](QQuickItem *item) {
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                item->mapToScene(item->boundingRect().center()).toPoint());
        };
        const QStringList labels{"Environment", "Tasks", "Library", "Space", "Browse", "People", "Hosting"};
        for (int index = 0; index < labels.size(); ++index) {
            auto *button = segments->child(index); QVERIFY(button);
            QCOMPARE(button->text(QAccessible::Name), labels[index]);
        }
        for (const auto &[index, page] : QList<QPair<int, QString>>{{0, "Environment"}, {2, "Storage"}, {4, "Browse"}}) {
            QVERIFY(clickNavigationSegment(window, index));
            QTRY_COMPARE(window->property("selectedTab").toString(), page);
            if (page == "Storage" || page == "Environment")
                QTRY_COMPARE(window->findChild<QObject *>(page.toLower() + "Loader")->property("status").toInt(), 1);
            QTRY_VERIFY(segments->child(index)->state().selected);
        }
        auto *toolsAction = window->findChild<QObject *>("globalToolsAction"); QVERIFY(toolsAction);
        auto *nativeFilesAction = window->findChild<QObject *>("globalNativeFilesAction"); QVERIFY(nativeFilesAction);
        QCOMPARE(nativeFilesAction->property("text").toString(),QStringLiteral("Files (LVUI preview)"));
        QVERIFY(clickNavigationSegment(window, 1));
        QTRY_COMPARE(window->property("selectedTab").toString(), QString("Tools"));
        QTRY_COMPARE(window->findChild<QObject *>("toolsLoader")->property("status").toInt(), 1);
        QVERIFY(window->setProperty("selectedTab", "Dashboard"));
        QVERIFY(QMetaObject::invokeMethod(toolsAction, "triggered"));
        QTRY_COMPARE(window->property("selectedTab").toString(), QString("Tools"));
        QTRY_COMPARE(window->findChild<QObject *>("toolsLoader")->property("status").toInt(), 1);
        QVERIFY(window->setProperty("selectedTab", "Dashboard"));
        QTest::keyClick(window, Qt::Key_2, Qt::ControlModifier);
        QTRY_COMPARE(window->property("selectedTab").toString(), QString("Tools"));
        auto *legacy = window->findChild<QQuickItem *>("legacyNavigationSegments");
        QVERIFY(legacy && !legacy->isVisible() && !legacy->isEnabled());
        for (int index : {3, 5}) {
            QVERIFY(clickNavigationSegment(window, index));
            QTRY_VERIFY(notice->property("open").toBool());
            QCOMPARE(window->property("selectedTab").toString(), QString("Tools"));
            QVERIFY(notice->setProperty("open", false));
        }
        QVERIFY(clickNavigationSegment(window, 6));
        QTRY_COMPARE(window->property("selectedTab").toString(), QString("Environment"));
        QTRY_VERIFY(segments->child(6)->state().selected);
        QQuickItem *environment = nullptr;
        QTRY_VERIFY((environment = visualItem(window->contentItem(), "environmentView")));
        auto *environmentScroll = visualItem(environment, "environmentScroll"); QVERIFY(environmentScroll);
        auto *environmentFlick = qvariant_cast<QQuickItem *>(environmentScroll->property("contentItem"));
        QVERIFY(environmentFlick);
        QTRY_VERIFY(environmentFlick->property("contentY").toReal() > 0);
        QVERIFY(QMetaObject::invokeMethod(environment, "showDevices"));
        QTRY_VERIFY(segments->child(0)->state().selected);
        QVERIFY(clickNavigationSegment(window, 0));
        QTRY_COMPARE(environment->property("page").toString(), QString("Overview"));
        QQuickItem *society = nullptr;
        QTRY_VERIFY((society = visualItem(environment, "environmentLaunch-society")));
        click(society);
        QTRY_COMPARE(window->property("selectedTab").toString(), QString("Dashboard"));
        QSignalSpy moves(window, SIGNAL(windowMoveAttempted(bool))); QVERIFY(moves.isValid());
        click(search); QTest::keyClick(window, Qt::Key_Q);
        QTRY_COMPARE(window->property("selectedTab").toString(), QString("Dashboard"));
        QCOMPARE(search->property("text").toString(), QString("q"));
        auto *clear = search->findChild<QQuickItem *>("dashboardSearch_clearButton"); QVERIFY(clear);
        QTRY_VERIFY(clear->isVisible()); click(clear);
        QTRY_COMPARE(search->property("text").toString(), QString());
        click(notifications);
        QTRY_VERIFY(notificationPopover->property("visible").toBool());
        QVERIFY(!notice->property("open").toBool());
        auto *empty = visualItem(window->contentItem(), "notificationEmptyState"); QVERIFY(empty);
        QTRY_VERIFY(empty->isVisible());
        click(notifications);
        QTRY_VERIFY(!notificationPopover->property("visible").toBool());
        click(notifications);
        QTRY_VERIFY(notificationPopover->property("visible").toBool());
        const auto record = [notificationCenter](const QString &source, const QString &message) {
            return QMetaObject::invokeMethod(notificationCenter, "record",
                Q_ARG(QVariant, source), Q_ARG(QVariant, message));
        };
        const QString longWarning = QStringLiteral("The moved asset for /Volumes/Storage/")
            + QString(120, QLatin1Char('x')) + QStringLiteral(".safetensors.iisacc.json cannot be identified safely.");
        const QString batch = longWarning + QStringLiteral("\nSecond model warning");
        QVERIFY(record("Models", batch));
        QTRY_COMPARE(notificationCenter->property("count").toInt(), 2);
        QTRY_COMPARE(notifications->property("text").toString(), QString("2"));
        auto *notificationCountLabel = visualItem(notifications, "labelButton_label"); QVERIFY(notificationCountLabel);
        QTRY_VERIFY(notificationCountLabel->isVisible());
        QTRY_COMPARE(notificationCountLabel->property("text").toString(), QString("2"));
        auto *mobileNotificationButton = visualItem(window->contentItem(), "mobileNotifications"); QVERIFY(mobileNotificationButton);
        QTRY_COMPARE(mobileNotificationButton->property("text").toString(), QString("2"));
        QVERIFY(record("Models", batch));
        QCOMPARE(notificationCenter->property("count").toInt(), 2);
        QTRY_VERIFY(!empty->isVisible());
        auto *notificationList = visualItem(window->contentItem(), "notificationList"); QVERIFY(notificationList);
        QTRY_COMPARE(notificationList->property("count").toInt(), 2);
        QQuickItem *firstNotification = nullptr;
        QTRY_VERIFY((firstNotification = visualItem(window->contentItem(), "notificationMessage0")));
        QCOMPARE(firstNotification->property("text").toString(), longWarning);
        QTRY_VERIFY(firstNotification->property("lineCount").toInt() > 1);
        QQuickItem *dismissFirst = nullptr;
        QTRY_VERIFY((dismissFirst = visualItem(window->contentItem(), "notificationDismiss0")));
        const auto *dismissAccessible = QAccessible::queryAccessibleInterface(dismissFirst); QVERIFY(dismissAccessible);
        QVERIFY(dismissAccessible->text(QAccessible::Name).contains(longWarning));
        click(dismissFirst);
        QTRY_COMPARE(notificationCenter->property("count").toInt(), 1);
        QTRY_COMPARE(notifications->property("text").toString(), QString("1"));
        QQuickItem *remainingNotification = nullptr;
        QTRY_VERIFY((remainingNotification = visualItem(window->contentItem(), "notificationMessage0")));
        QTRY_COMPARE(remainingNotification->property("text").toString(), QString("Second model warning"));
        QVERIFY(record("Models", batch));
        QCOMPARE(notificationCenter->property("count").toInt(), 1);
        QVERIFY(record("Models", ""));
        QVERIFY(record("Models", batch));
        QTRY_COMPARE(notificationCenter->property("count").toInt(), 2);
        QString manyWarnings = batch;
        for (int i = 0; i < 15; ++i) manyWarnings += QStringLiteral("\nAnother warning %1").arg(i);
        QVERIFY(record("Models", manyWarnings));
        QTRY_COMPARE(notificationList->property("count").toInt(), 17);
        QTRY_COMPARE(notifications->property("text").toString(), QString("17"));
        QTRY_VERIFY(notificationList->property("contentHeight").toReal() > notificationList->height());
        QVERIFY(notificationList->setProperty("contentY", notificationList->property("contentHeight").toReal() - notificationList->height()));
        QQuickItem *lastNotification = nullptr;
        QTRY_VERIFY((lastNotification = visualItem(window->contentItem(), "notificationMessage16")));
        QCOMPARE(lastNotification->property("text").toString(), QStringLiteral("Another warning 14"));
        auto *clearNotifications = visualItem(window->contentItem(), "clearNotifications"); QVERIFY(clearNotifications);
        click(clearNotifications);
        QTRY_COMPARE(notificationCenter->property("count").toInt(), 0);
        QTRY_COMPARE(notifications->property("text").toString(), QString());
        QVERIFY(record("Models", manyWarnings));
        QCOMPARE(notificationCenter->property("count").toInt(), 0);
        QVERIFY(record("Models", ""));
        QVERIFY(record("Models", batch));
        QTRY_COMPARE(notificationCenter->property("count").toInt(), 2);
        QTRY_VERIFY(notificationPopover->property("visible").toBool());
        QTRY_VERIFY(clearNotifications->isVisible());
        QTest::qWait(60); // Let the restored button finish its layout before hit testing.
        click(clearNotifications);
        QTRY_COMPARE(notificationCenter->property("count").toInt(), 0);
        QVERIFY(QMetaObject::invokeMethod(notificationPopover, "close"));
        QVERIFY(!drive->navigate(fixture.filePath("Unavailable")));
        QVERIFY(!drive->errorString().isEmpty());
        QTRY_COMPARE(notificationCenter->property("count").toInt(), 1);
        auto *importer = window->findChild<ModelImporter *>("modelImporter"); QVERIFY(importer);
        QVERIFY(!importer->importFiles({QUrl::fromLocalFile(fixture.filePath("not-a-model.txt"))}));
        QVERIFY(!importer->errorString().isEmpty());
        QTRY_COMPARE(notificationCenter->property("count").toInt(), 2);
        click(notifications);
        QTRY_VERIFY(notificationPopover->property("visible").toBool());
        QTRY_COMPARE(notificationList->property("count").toInt(), 2);
        QQuickItem *importNotification = nullptr;
        QTRY_VERIFY((importNotification = visualItem(window->contentItem(), "notificationMessage1")));
        QCOMPARE(importNotification->property("text").toString(), importer->errorString());
        QQuickItem *dismissImport = nullptr;
        QTRY_VERIFY((dismissImport = visualItem(window->contentItem(), "notificationDismiss1")));
        click(dismissImport);
        QTRY_COMPARE(notificationCenter->property("count").toInt(), 1);
        QTRY_COMPARE(notifications->property("text").toString(), QString("1"));
        QTRY_VERIFY((dismissFirst = visualItem(window->contentItem(), "notificationDismiss0")));
        click(dismissFirst);
        QTRY_COMPARE(notificationCenter->property("count").toInt(), 0);
        QTRY_COMPARE(notifications->property("text").toString(), QString());
        QTRY_VERIFY(empty->isVisible());
        QVERIFY(QMetaObject::invokeMethod(notificationPopover, "close"));
        QVERIFY(drive->openSection("files"));
        click(account);
        auto *accountViews = window->findChild<QObject *>("accountViews"); QVERIFY(accountViews);
        auto *accountPanel = accountViews->property("dialog").value<QObject *>(); QVERIFY(accountPanel);
        QTRY_VERIFY(accountPanel->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(accountPanel, "close"));
        QTRY_VERIFY(!accountPanel->property("visible").toBool());
        account->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_Space);
        QTRY_VERIFY(accountPanel->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(accountPanel, "close"));
        QTRY_VERIFY(!accountPanel->property("visible").toBool());
        QCOMPARE(moves.count(), 0);
        const auto captures = qEnvironmentVariable("SOCIETY_GLOBAL_TOOLBAR_CAPTURE_DIR");
        if (!captures.isEmpty()) {
            // Match the reference's populated Search state, including LVRS's clear affordance.
            QVERIFY(search->setProperty("text", "Search"));
            QVERIFY(QDir().mkpath(captures)); QTest::qWait(100);
            QVERIFY(window->grabWindow().save(captures + "/desktop.png"));
            auto grab = toolbar->grabToImage(); QVERIFY(grab);
            QSignalSpy ready(grab.data(), &QQuickItemGrabResult::ready); QVERIFY(ready.wait(3000));
            QVERIFY(grab->saveToFile(captures + "/toolbar.png"));
            QVERIFY(search->setProperty("text", ""));
        }
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        window->close();
    }

    void toolbarAvatarTracksAccountProfileAndFallback()
    {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/toolbar-avatar-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        AccountServer authority; QVERIFY(authority.server.listen(QHostAddress::LocalHost));
        const QString avatarPath = "/media/avatars/" + QString(64, 'a') + ".webp";
        authority.profile.insert("avatarUrl", avatarPath);
        QImage image(32, 32, QImage::Format_RGB32); image.fill(Qt::magenta);
        QBuffer bytes(&authority.avatarData); QVERIFY(bytes.open(QIODevice::WriteOnly));
        QVERIFY(image.save(&bytes, "PNG")); bytes.close();
        MemorySessionStore store;
        AccountController account(authority.url(), &store, nullptr);
        QQmlApplicationEngine engine;
        engine.addImageProvider(QStringLiteral("society-preview"), new iiSocietyContainer::PreviewProvider);
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        engine.setInitialProperties({{"initialContainerPath", fixture.path()}, {"width", 1440}, {"height", 900}});
        loadSociety(engine);
        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *content = window->findChild<QQuickItem *>("societyContent"); QVERIFY(content);
        QVERIFY(content->setProperty("account", QVariant::fromValue(&account)));
        auto *avatar = visualItem(content, "dashboardAvatar"); QVERIFY(avatar);
        auto *fallback = visualItem(avatar, "accountAvatarFallback"); QVERIFY(fallback);
        QVERIFY(fallback->isVisible());
        QVERIFY(account.login("builder@example.com", "FixtureOnly1!"));
        QTRY_VERIFY(account.signedIn());
        QTRY_COMPARE(avatar->property("source").toUrl(), authority.url().resolved(QUrl(avatarPath)));
        QTRY_COMPARE_WITH_TIMEOUT(avatar->property("status").toInt(), 1, 15000);
        QTRY_VERIFY(!fallback->isVisible());

        authority.profile.insert("avatarUrl", QJsonValue::Null);
        QTRY_VERIFY(!account.busy());
        QVERIFY(account.refresh());
        QTRY_VERIFY(avatar->property("source").toUrl().isEmpty());
        QTRY_VERIFY(fallback->isVisible());
        QVERIFY(account.signedIn());
        QTRY_VERIFY(!account.busy());
        authority.avatarData.clear();
        authority.profile.insert("avatarUrl", "/media/avatars/" + QString(64, 'b') + ".webp");
        QVERIFY(account.refresh());
        QTRY_COMPARE_WITH_TIMEOUT(avatar->property("status").toInt(), 3, 15000); // Image.Error
        QTRY_VERIFY(fallback->isVisible());
        QTRY_VERIFY(!account.busy());
        QVERIFY(account.logout());
        QTRY_VERIFY(!account.signedIn());
        QVERIFY(fallback->isVisible());
        window->close();
    }

    void toolbarSharesWindowControlsRow()
    {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/toolbar-row-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        QQmlApplicationEngine engine;
        engine.addImageProvider(QStringLiteral("society-preview"), new iiSocietyContainer::PreviewProvider);
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        engine.setInitialProperties({{"initialContainerPath", fixture.path()}});
        loadSociety(engine);
        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        auto *toolbar = window->findChild<QQuickItem *>("dashboardToolbar");
        auto *tabs = window->findChild<QQuickItem *>("dashboardTabs");
        auto *search = window->findChild<QQuickItem *>("dashboardSearch");
        auto *account = window->findChild<QQuickItem *>("dashboardAccount");
        QVERIFY(toolbar && tabs && search && account);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTRY_COMPARE(toolbar->mapToScene(QPointF()).y(), 0.0);
        QCOMPARE(toolbar->height(), 56.0);
        QCOMPARE(window->property("windowDragHandleHeight").toReal(), toolbar->height());
#ifdef Q_OS_MACOS
        const bool nativeControls = QGuiApplication::platformName() == QStringLiteral("cocoa");
        if (nativeControls) {
            // Cached LVRS geometry alone cannot detect a later AppKit reflow.
            QTest::qWait(100);
            QTRY_VERIFY(!societyNativeTitleBarControlsRect(window).isEmpty());
            QTRY_VERIFY(qAbs(societyNativeTitleBarControlsRect(window).center().y() - 28.0) <= 0.5);
        }
#endif
        QSignalSpy moves(window, SIGNAL(windowMoveAttempted(bool)));
        QVERIFY(moves.isValid());
        const auto center = [](QQuickItem *item) {
            return item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
        };
        for (const QSize size : {QSize(1440, 900), QSize(760, 540), QSize(360, 640)}) {
            window->resize(size);
            QTRY_COMPARE(window->size(), size);
            QTRY_COMPARE(toolbar->width(), qreal(size.width()));
            QTRY_VERIFY2(qAbs(center(tabs).y() - toolbar->height() * 0.5) <= 0.01,
                         qPrintable(QString("Tab center %1, toolbar center %2")
                             .arg(center(tabs).y()).arg(toolbar->height() * 0.5)));
            QTRY_VERIFY(qAbs(center(account).y() - 28.0) <= 0.5);
#ifdef Q_OS_MACOS
            if (nativeControls) {
                QTRY_VERIFY(qAbs(societyNativeTitleBarControlsRect(window).center().y()
                                 - center(tabs).y()) <= 0.5);
                QTRY_VERIFY(qAbs(societyNativeTitleBarControlsRect(window).center().y()
                                 - center(account).y()) <= 0.5);
                qInfo().nospace() << "Toolbar alignment: width=" << size.width()
                                 << ", native=" << societyNativeTitleBarControlsRect(window).center().y()
                                 << ", tabs=" << center(tabs).y() << ", account=" << center(account).y();
            }
#endif
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
            QTest::keyClick(window, Qt::Key_2, Qt::ControlModifier);
            QTRY_COMPARE(window->property("selectedTab").toString(), QString("Tools"));
            QVERIFY(clickNavigationSegment(window, 2));
            QTRY_COMPARE(window->property("selectedTab").toString(), QString("Storage"));
        }
        QCOMPARE(moves.count(), 0);
        window->resize(1440, 900);
        QTRY_COMPARE(toolbar->width(), 1440.0);
        // Cocoa/Metal can update logical widths before presenting the resized
        // scene. Send the drag to the geometry that has actually been drawn.
        QSignalSpy frames(window, &QQuickWindow::frameSwapped);
        window->requestUpdate();
        QTRY_VERIFY(!frames.isEmpty());
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, QPoint(600, 28));
        QCOMPARE(moves.count(), 1);
        window->close();
    }

    void dashboardKeepsStorageAndPromptState()
    {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/dashboard-gui-XXXXXX.societycontainer");
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
        engine.addImageProvider(QStringLiteral("society-preview"), new iiSocietyContainer::PreviewProvider);
        QStringList warnings;
        connect(&engine, &QQmlApplicationEngine::warnings, this, [&](const QList<QQmlError> &errors) {
            for (const auto &error : errors) warnings.append(error.toString());
        });
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        engine.setInitialProperties({{"initialContainerPath", fixture.path()}});
        loadSociety(engine);
        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        if (QGuiApplication::platformName() == "cocoa") {
            window->requestActivate();
            QVERIFY(QTest::qWaitForWindowActive(window));
        }

        auto *dashboard = window->findChild<QQuickItem *>("dashboardView");
        QQuickItem *tools = nullptr;
        QQuickItem *storage = nullptr;
        auto *tabs = window->findChild<NativeNavigationSurface *>("dashboardTabs");
        QQuickItem *prompt = nullptr;
        auto *drive = window->findChild<DriveController *>("driveController");
        QVERIFY(dashboard && tabs && drive);
        QTRY_VERIFY(window->isVisible() && dashboard->isVisible());
        QVERIFY(!window->findChild<QQuickItem *>("toolsView"));
        QVERIFY(!window->findChild<QQuickItem *>("storageView"));
        auto *groupAccessibility = QAccessible::queryAccessibleInterface(tabs); QVERIFY(groupAccessibility);
        const auto *workspaceAccessibility = groupAccessibility->child(0);
        QVERIFY(workspaceAccessibility);
        QCOMPARE(workspaceAccessibility->text(QAccessible::Name), QString("Environment"));
        QVERIFY(workspaceAccessibility->state().selected);
        const auto *localNavigation = QAccessible::queryAccessibleInterface(visualItem(dashboard, "dashboardItemworkspace_home"));
        QVERIFY(localNavigation);
        QCOMPARE(localNavigation->text(QAccessible::Name), QString("Workspace"));
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
        const auto openDashboard = [&] { QVERIFY(window->setProperty("selectedTab", "Dashboard")); };
        const auto openStorage = [&] { QVERIFY(clickNavigationSegment(window, 2)); };
        const auto openTools = [&] { QTest::keyClick(window, Qt::Key_2, Qt::ControlModifier); };
        openTools();
        QTRY_COMPARE(window->findChild<QObject *>("toolsLoader")->property("status").toInt(), 1);
        QTRY_VERIFY((tools = window->findChild<QQuickItem *>("toolsView")) && tools->isVisible());
        QTRY_VERIFY((prompt = window->findChild<QQuickItem *>("promptField")));
        QVERIFY(prompt->setProperty("text", "A quiet lunar landscape"));
        auto *quickGenerate = window->findChild<QQuickItem *>("quickGenerate");
        QVERIFY(quickGenerate);
        QVERIFY(window->findChild<QQuickItem *>("aspectRatioButton"));
        QVERIFY(window->findChild<QQuickItem *>("generationCountButton"));
        auto *mediaButton = window->findChild<QQuickItem *>("mediaTypeButton");
        auto *mediaMenu = window->findChild<QObject *>("mediaTypeMenu");
        QVERIFY(mediaButton && mediaMenu); click(mediaButton);
        QTRY_VERIFY(mediaMenu->property("opened").toBool());
        QVERIFY(QMetaObject::invokeMethod(mediaMenu, "triggerEntry", Q_ARG(QVariant, 1)));
        QTRY_VERIFY(!mediaMenu->property("visible").toBool());
        QCOMPARE(quickGenerate->property("mediaType").toString(), QString("Video"));
        openDashboard();
        auto *search = window->findChild<QQuickItem *>("dashboardSearch");
        QVERIFY(search); search->setProperty("text", "CHAPTER");
        QTRY_COMPARE(files->recentFiles().size(), 1);
        QVERIFY(files->generationHistory().isEmpty());
        search->setProperty("text", "");
        QTRY_COMPARE(files->recentFiles().size(), 3);
        openTools();
        QTRY_COMPARE(window->property("selectedTab").toString(), QString("Tools"));
        QTRY_VERIFY(tools->isVisible() && !dashboard->isVisible() && (!storage || !storage->isVisible()));
        QVERIFY(!workspaceAccessibility->state().selected);
        const auto toolsScreenshot = qEnvironmentVariable("SOCIETY_TOOLS_SCREENSHOT_PATH");
        if (!toolsScreenshot.isEmpty()) { QTest::qWait(200); QVERIFY(window->grabWindow().save(toolsScreenshot)); }
        QVERIFY(!window->findChild<QQuickItem *>("modelMergeTool"));
        const auto catalog = tools->property("tools").value<QJSValue>().toVariant().toList();
        QCOMPARE(catalog.size(), 3);
        QCOMPARE(catalog.first().toMap().value("key").toString(), QString("model-merge"));
        QCOMPARE(catalog.at(1).toMap().value("key").toString(), QString("model-packaging"));
        QCOMPARE(catalog.last().toMap().value("key").toString(), QString("model-calibrator"));
        auto *packagingCard = visualItem(tools, "toolCard-model-packaging"); QVERIFY(packagingCard);
        click(packagingCard);
        QQuickItem *packaging = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((packaging = window->findChild<QQuickItem *>("modelPackagingTool")), 15000);
        QTRY_VERIFY(packaging->isVisible());
        QVERIFY(packaging->setProperty("outputName", "retained-package-name"));
        QVERIFY(QMetaObject::invokeMethod(tools, "goBack"));
        click(packagingCard); QTRY_VERIFY(packaging->isVisible());
        QCOMPARE(packaging->property("outputName").toString(), QString("retained-package-name"));
        QVERIFY(QMetaObject::invokeMethod(tools, "goBack"));
        auto *mergeCard = visualItem(tools, "toolCard-model-merge"); QVERIFY(mergeCard);
        click(mergeCard);
        QQuickItem *merge = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((merge = window->findChild<QQuickItem *>("modelMergeTool")), 15000);
        QTRY_VERIFY(merge->isVisible());
        QVERIFY(merge->setProperty("outputName", "desktop-session"));
        QVERIFY(merge->setProperty("mode", "weighted-difference"));
        auto *toolsBack = visualItem(tools, "toolsBack"); QVERIFY(toolsBack);
        QTest::qWait(60);
        click(toolsBack);
        QTRY_VERIFY(!merge->isVisible() && mergeCard->isVisible());
        click(mergeCard);
        QTRY_VERIFY(merge->isVisible());
        QCOMPARE(merge->property("outputName").toString(), QString("desktop-session"));
        QCOMPARE(merge->property("mode").toString(), QString("weighted-difference"));
        openStorage();
        QTRY_COMPARE(window->findChild<QObject *>("storageLoader")->property("status").toInt(), 1);
        QTRY_VERIFY((storage = window->findChild<QQuickItem *>("storageView"))
                    && storage->isVisible() && !dashboard->isVisible() && !tools->isVisible());
        QVERIFY(!workspaceAccessibility->state().selected);
        QVERIFY(drive->navigate(fixture.filePath("Files/Work")));
        openTools();
        QTRY_VERIFY(tools->isVisible() && !storage->isVisible());
        QCOMPARE(merge->property("outputName").toString(), QString("desktop-session"));
        QCOMPARE(merge->property("mode").toString(), QString("weighted-difference"));
        openDashboard();
        QTRY_VERIFY(dashboard->isVisible() && !storage->isVisible() && !tools->isVisible());
        QCOMPARE(prompt->property("text").toString(), QString("A quiet lunar landscape"));
        QCOMPARE(quickGenerate->property("mediaType").toString(), QString("Video"));
        QCOMPARE(quickGenerate->property("aspectRatio").toString(), QString("1:1"));
        QCOMPARE(quickGenerate->property("generationCount").toInt(), 1);
        openStorage();
        QTRY_VERIFY(storage->isVisible());
        QCOMPARE(drive->currentPath(), fixture.filePath("Files"));
        openDashboard();
        QTRY_VERIFY(visualItem(window->contentItem(), "viewAllRecentFiles"));
        auto *allFiles = visualItem(window->contentItem(), "viewAllRecentFiles");
        QVERIFY(allFiles); revealDashboardItem(dashboard, allFiles); click(allFiles);
        QTRY_VERIFY(storage->isVisible());
        QCOMPARE(drive->currentSection(), QString("Files"));
        auto *fileGrid = window->findChild<QQuickItem *>("filesBrowser"); QVERIFY(fileGrid);
        // Files is a table; switching to image-only history must keep its grid.
        QTRY_VERIFY(!fileGrid->property("loading").toBool());
        QTRY_COMPARE(fileGrid->property("count").toInt(), 5);
        openDashboard();
        QTRY_VERIFY(visualItem(window->contentItem(), "viewAllGenerationHistory"));
        auto *history = visualItem(window->contentItem(), "viewAllGenerationHistory");
        QVERIFY(history); revealDashboardItem(dashboard, history); click(history);
        QTRY_VERIFY(storage->isVisible());
        QCOMPARE(drive->currentSection(), QString("Generation History"));
        const auto storageScreenshot = qEnvironmentVariable("SOCIETY_STORAGE_SCREENSHOT_PATH");
        if (!storageScreenshot.isEmpty()) { QTest::qWait(200); QVERIFY(window->grabWindow().save(storageScreenshot)); }
        openDashboard();
        QVERIFY(!window->findChild<QQuickItem *>("dashboardDeleted"));
        openStorage();
        auto *deleted = visualItem(storage, "storageSectiondeleted");
        QVERIFY(deleted); click(deleted);
        QTRY_VERIFY(storage->isVisible());
        QCOMPARE(drive->currentSection(), QString("Deleted"));
        openDashboard();
        openTools();
        QVERIFY(QMetaObject::invokeMethod(tools, "goBack"));
        QSignalSpy submitted(window, SIGNAL(generateRequested(QString,QString,QString,int)));
        auto *generate = window->findChild<QQuickItem *>("generateButton");
        QVERIFY(generate); click(generate);
        QCOMPARE(submitted.size(), 1);
        QCOMPARE(submitted.first(), QVariantList({QString("A quiet lunar landscape"), QString("Video"), QString("1:1"), 1}));
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
        QVERIFY(clickNavigationSegment(window, 0));
        QTRY_COMPARE(window->findChild<QObject *>("environmentLoader")->property("status").toInt(), 1);
        auto *environment = window->findChild<QQuickItem *>("environmentView");
        QVERIFY(environment); QTRY_VERIFY(environment->isVisible());
        QVERIFY(!window->findChild<QQuickWindow *>("preferencesWindow"));
        auto *devices = window->findChild<QObject *>("networkDevices");
        QVERIFY(devices); QVERIFY(clickNavigationSegment(window, 4));
        QCOMPARE(window->property("selectedTab").toString(), QString("Browse"));
        QVERIFY(!devices->property("visible").toBool());
        openDashboard();
        for (const auto size : {QSize(1024, 768), QSize(760, 640), QSize(390, 844), QSize(1440, 900)}) {
            window->resize(size); QTRY_COMPARE(window->size(), size);
            const auto *scroll = window->findChild<QQuickItem *>("dashboardScroll");
            QVERIFY(scroll && scroll->width() > 0 && scroll->height() > 0);
            QTRY_VERIFY(scroll->width() <= size.width());
        }
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        window->close();
    }

    void environmentPagesPairingAndResponsiveLayout()
    {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/environment-gui-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        QQmlApplicationEngine engine;
        engine.addImageProvider(QStringLiteral("society-preview"), new iiSocietyContainer::PreviewProvider);
        QStringList warnings;
        connect(&engine, &QQmlApplicationEngine::warnings, this, [&](const QList<QQmlError> &errors) {
            for (const auto &error : errors) warnings.append(error.toString());
        });
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        engine.setInitialProperties({{"initialContainerPath", fixture.path()}});
        loadSociety(engine);
        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QVERIFY(clickNavigationSegment(window, 0));
        QQuickItem *view = nullptr;
        QTRY_VERIFY((view = visualItem(window->contentItem(), "environmentView")));
        auto *apps = visualItem(view, "environmentApps"); QVERIFY(apps);
        auto *collectionTab = visualItem(apps, "environmentCollection0"); QVERIFY(collectionTab);
        QCOMPARE(collectionTab->property("iconName").toString(), QStringLiteral("folder@14x14"));
        auto *compactTabs = visualItem(view, "environmentCompactNavigation"); QVERIFY(compactTabs);
        const auto compactModel = compactTabs->property("model").value<QJSValue>().toVariant().toList();
        QCOMPARE(compactModel.size(), 3);
        for (const auto &entry : compactModel)
            QVERIFY(!entry.toMap().value("iconName").toString().isEmpty());
        auto *devices = window->findChild<QObject *>("networkDevices"); QVERIFY(devices);
        const auto click = [&](const QString &name) {
            QTest::qWait(100);
            auto *item = visualItem(window->contentItem(), name);
            if (!item || !item->isVisible() || !item->isEnabled()) return false;
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                item->mapToScene(QPointF(item->width()/2, item->height()/2)).toPoint());
            return true;
        };
        QTRY_VERIFY(view->isVisible());
        QCOMPARE(window->property("selectedTab").toString(), QString("Environment"));
        QVERIFY(!window->findChild<QQuickWindow *>("preferencesWindow"));
        QVERIFY(!devices->property("visible").toBool());
        QVERIFY(click("environmentNavDevices"));
        QTRY_COMPARE(view->property("page").toString(), QString("Devices"));
        QVERIFY(click("environmentAddDevice-desktop"));
        QTRY_VERIFY(devices->property("visible").toBool());
        QCOMPARE(window->property("selectedTab").toString(), QString("Environment"));
        const auto modalCapture = qEnvironmentVariable("SOCIETY_ENVIRONMENT_CAPTURE_DIR");
        if (!modalCapture.isEmpty()) {
            QVERIFY(QDir().mkpath(modalCapture)); QTest::qWait(150);
            QVERIFY(window->grabWindow().save(modalCapture + "/add-device-modal.png"));
        }
        QVERIFY(click("networkPairing"));
        auto *pairing = window->findChild<QObject *>("pairingPanel"); QVERIFY(pairing);
        QTRY_VERIFY(pairing->property("visible").toBool());
        QTRY_VERIFY(!devices->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(pairing, "close"));
        QTRY_VERIFY(!pairing->property("visible").toBool());
        QVERIFY(clickNavigationSegment(window, 4));
        QTRY_VERIFY(visualItem(window->contentItem(), "browseView")->isVisible());
        QVERIFY(!devices->property("visible").toBool());
        QVERIFY(clickNavigationSegment(window, 0));
        QTRY_VERIFY(view->isVisible());
        QVERIFY(click("environmentNavApps"));
        QTRY_VERIFY(apps->isVisible());
        QVERIFY(click("environmentCollection1"));
        QTRY_COMPARE(apps->property("collection").toInt(), 1);
        auto *search = visualItem(apps, "environmentAppSearch"); QVERIFY(search);
        QVERIFY(search->setProperty("text", "unlikely-no-match"));
        QTRY_VERIFY(!visualItem(apps, "environmentApp-society"));
        QVERIFY(search->setProperty("text", "Society"));
        QTRY_VERIFY(visualItem(apps, "environmentApp-society"));
        QVERIFY(click("environmentCollection3"));
        QCOMPARE(search->property("text").toString(), QString());
        QVERIFY(click("environmentAddApp"));
        auto *addWeb = window->findChild<QObject *>("environmentAddWebApp"); QVERIFY(addWeb);
        QTRY_VERIFY(addWeb->property("visible").toBool());
        auto *webName = visualItem(window->contentItem(), "environmentWebName");
        auto *webUrl = visualItem(window->contentItem(), "environmentWebUrl");
        auto *save = visualItem(window->contentItem(), "environmentSaveWebApp");
        QVERIFY(webName && webUrl && save);
        QVERIFY(!save->isEnabled());
        QVERIFY(webName->setProperty("text", "Example"));
        QVERIFY(webUrl->setProperty("text", "javascript:alert(1)"));
        QVERIFY(!save->isEnabled());
        QVERIFY(webUrl->setProperty("text", "https://example.com/app"));
        QTRY_VERIFY(save->isEnabled());
        QVERIFY(QMetaObject::invokeMethod(addWeb, "close")); // Never persist fixtures.
        QTRY_VERIFY(!addWeb->property("visible").toBool());

        const QString captures = qEnvironmentVariable("SOCIETY_ENVIRONMENT_CAPTURE_DIR");
        if (!captures.isEmpty()) QVERIFY(QDir().mkpath(captures));
        const auto capture = [&](const QString &name) {
            QTest::qWait(120);
            return captures.isEmpty() || window->grabWindow().save(captures + '/' + name + ".png");
        };
        // Save real provider state separately from the six populated visual fixtures.
        QVERIFY(view->setProperty("page", "Overview"));
        QVERIFY(capture("live-overview"));
        EnvironmentAppsModel catalogue;
        QVariantList entries = catalogue.apps();
        for (int i = 0; i < entries.size(); ++i) {
            auto app = entries[i].toMap();
            app["installed"] = i < 6;
            app["status"] = i < 6 ? "Installed · This Mac" : "Available for macOS";
            app["license"] = "Design fixture · Not a license grant";
            app["action"] = i < 6 ? "Open" : "Install";
            app["actionEnabled"] = true;
            entries[i] = app;
        }
        QVERIFY(view->setProperty("appEntries", entries));
        auto *navigation = window->findChild<StorageNavigation *>("storageNavigation"); QVERIFY(navigation);
        navigation->replaceDevices("design-fixture", "this-device", {
            QVariantMap{{"id", "studio"}, {"name", "Studio Mac"}, {"kind", "desktop"}},
            QVariantMap{{"id", "ipad"}, {"name", "iPad"}, {"kind", "tablet"}},
            QVariantMap{{"id", "iphone"}, {"name", "iPhone"}, {"kind", "phone"}}
        }, {}, {});
        QVariantList remote;
        for (const auto &id : {QString("studio"), QString("ipad")})
            for (int i = 0; i < 3; ++i) {
                auto entry = entries[i].toMap(); entry["deviceId"] = id;
                entry["action"] = "View device"; remote.append(entry);
            }
        QVERIFY(view->setProperty("remoteInventories", remote));
        QVariantList web;
        const QStringList webNames{"Society Web", "Boards", "Review", "Library Web", "Notes Web", "Account"};
        const QStringList symbols{"Sw", "Bo", "Rv", "Lw", "Nw", "Ac"};
        for (int i = 0; i < webNames.size(); ++i) {
            auto entry = entries[i].toMap(); entry["name"] = webNames[i]; entry["symbol"] = symbols[i];
            entry["url"] = "https://example.com"; entry["action"] = "Open web"; web.append(entry);
        }
        QVERIFY(view->setProperty("webEntries", web));
        QVERIFY(capture("252-954-overview"));
        QVERIFY(view->setProperty("page", "Devices"));
        QVERIFY(capture("252-1324-devices"));
        QVERIFY(QMetaObject::invokeMethod(view, "selectDevice", Q_ARG(QVariant, QVariant("studio"))));
        QTRY_COMPARE(view->property("selectedDeviceId").toString(), QString("studio"));
        auto *selectedDevice = visualItem(view, "environmentSelectedDevice"); QVERIFY(selectedDevice);
        QTRY_VERIFY(selectedDevice->mapToScene(QPointF(0, 0)).y() < window->height());
        QTRY_VERIFY(selectedDevice->mapToScene(QPointF(0, 0)).y() >= 0);
        QVERIFY(view->setProperty("page", "Apps"));
        const QStringList names{"252-2676-my-apps", "263-5417-all-apps", "270-5384-other-devices", "270-5472-web-apps"};
        for (int i = 0; i < 4; ++i) {
            QVERIFY(apps->setProperty("collection", i));
            QVERIFY(capture(names[i]));
        }
        for (const QSize size : {QSize(1024, 768), QSize(760, 640), QSize(390, 844), QSize(320, 640)}) {
            window->setProperty("desktopMinWidth", 320);
            window->setProperty("mobileLayout", size.width() < 760);
            window->resize(size);
            QTRY_COMPARE(window->size(), size);
            for (const QString page : {"Overview", "Devices", "Apps"}) {
                QVERIFY(view->setProperty("page", page));
                if (page == "Apps") apps->setProperty("collection", 0);
                QTest::qWait(100);
                QVERIFY(view->width() <= size.width());
                if (page == "Apps") {
                    auto *card = visualItem(apps, "environmentApp-society"); QVERIFY(card);
                    QVERIFY(card->width() > 200);
                    auto *action = visualItem(card, "environmentApp-societyAction"); QVERIFY(action);
                    const auto actionBottom = action->mapToItem(card, QPointF(action->width(), action->height()));
                    QVERIFY(actionBottom.y() <= card->height());
                    QVERIFY(actionBottom.x() <= card->width());
                    QVERIFY(card->mapToScene(QPointF(card->width(), 0)).x() <= size.width());
                }
                QVERIFY(capture(QString("%1-%2x%3").arg(page).arg(size.width()).arg(size.height())));
            }
        }
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        window->close();
    }

    void environmentCatalogueDoesNotInventInstallationsOrLicenses()
    {
        EnvironmentAppsModel catalogue;
        QCOMPARE(catalogue.apps().size(), 10);
        const auto society = catalogue.apps().first().toMap();
        QVERIFY(society.value("installed").toBool());
        QSignalSpy requested(&catalogue, &EnvironmentAppsModel::societyRequested);
        QVERIFY(catalogue.open("society"));
        QCOMPARE(requested.count(), 1);
        QVERIFY(!catalogue.open("unknown-app"));
        for (const auto &value : catalogue.apps()) {
            const auto app = value.toMap();
            if (app.value("id").toString() == "society") continue;
            QCOMPARE(app.value("license").toString(), QString("License not verified"));
            if (!app.value("installed").toBool()) QVERIFY(!app.value("actionEnabled").toBool());
        }
    }

    void preferencesDriveLocationAndWindowLifecycle()
    {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/preferences-gui-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        QQmlApplicationEngine engine;
        engine.addImageProvider(QStringLiteral("society-preview"), new iiSocietyContainer::PreviewProvider);
        QStringList warnings;
        connect(&engine, &QQmlApplicationEngine::warnings, this, [&](const QList<QQmlError> &errors) {
            for (const auto &error : errors) warnings.append(error.toString());
        });
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        engine.setInitialProperties({{"initialContainerPath", fixture.path()}});
        loadSociety(engine);
        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        auto *network = window->findChild<NetworkDriveController *>("networkDriveController");
        QVERIFY(network);
        QVERIFY(window->setProperty("selectedTab", "Storage"));
        QQuickItem *open = nullptr;
        QTRY_VERIFY((open = window->findChild<QQuickItem *>("openPreferences")));
        QVERIFY(!window->findChild<QQuickWindow *>("preferencesWindow")); // Created only when requested.
        auto *preferencesAction = window->findChild<QObject *>("globalPreferencesAction");
        QVERIFY(preferencesAction);
        QVERIFY(QMetaObject::invokeMethod(preferencesAction, "triggered"));
        auto *preferences = window->findChild<QQuickWindow *>("preferencesWindow");
        QVERIFY(preferences && preferences != window);
        QTRY_VERIFY(preferences->isVisible());
        const auto accent = window->property("primaryColor").value<QColor>();
        QCOMPARE(preferences->property("primaryColor").value<QColor>(), accent);
        auto *preferencesMaterial = preferences->findChild<QQuickItem *>("applicationWindowMaterial");
        QVERIFY(preferencesMaterial);
        QCOMPARE(preferencesMaterial->property("color").value<QColor>(), preferences->property("windowColor").value<QColor>());
        QCOMPARE(preferencesMaterial->property("accentMix").toReal(), 0.02);
        QVERIFY(!preferencesMaterial->findChild<QQuickItem *>("applicationWindowAccentOverlay"));
        QCOMPARE(preferencesMaterial->property("primaryColor").value<QColor>(), accent);
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
        QTemporaryDir other(SOCIETY_TEST_DIRECTORY "/preferences-other-XXXXXX.societycontainer");
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
    const bool useApplicationRendering = qEnvironmentVariableIntValue("SOCIETY_TEST_APPLICATION_RENDERING") == 1;
    options.bootstrapGraphicsBackend = useApplicationRendering;
    options.configureRenderQualityDefaults = useApplicationRendering;
    options.logBootstrapDiagnostics = false;
    options.logGraphicsBackend = false;
    if (!lvrs::preApplicationBootstrap(options).ok)
        return 1;
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("iisacc.tests"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("iisacc.com"));
    QTemporaryDir settings(SOCIETY_TEST_DIRECTORY "/drive-settings-XXXXXX.societycontainer");
    if (!settings.isValid()) return 1;
    qputenv("SOCIETY_STORAGE_SETTINGS_PATH", settings.filePath("storage.json").toUtf8());
    qputenv("SOCIETY_CALENDAR_DIRECTORY", settings.filePath("calendar").toUtf8());
    qunsetenv("IISOCIETY_FILE_CACHE_DIRECTORY"); // Each selected package owns its private cache.
    qunsetenv("SOCIETY_CONTAINER_PATH");
    lvrs::postApplicationBootstrap(app, options);
    SocietyDriveTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_drive.moc"

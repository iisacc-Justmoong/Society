#include "App/Drive/DriveController.h"
#include "App/Dashboard/DashboardFiles.h"
#include "App/Files/DirectoryLocation.h"
#include "App/Files/ModelImporter.h"
#include "backend/runtime/appbootstrap.h"

#include <QDir>
#include <QFile>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QGuiApplication>
#include <QAccessible>
#include <QImage>
#include <QJSValue>
#include <QQmlApplicationEngine>
#include <QQmlError>
#include <QQmlListReference>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include "App/Network/NetworkDriveController.h"
#include "App/Network/DevicePairing.h"
#include "App/Network/PairingQr.h"
#include "App/Network/QrScanner.h"
#include <SharedStorage.h>

static QQuickItem *visualItem(QQuickItem *parent, const QString &name)
{
    if (parent->objectName() == name) return parent;
    for (auto *child : parent->childItems())
        if (auto *found = visualItem(child, name)) return found;
    return nullptr;
}

class SocietyDriveTest final : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        qmlRegisterType<DirectoryLocation>("Society", 1, 0, "DirectoryLocation");
        qmlRegisterType<DriveController>("Society", 1, 0, "DriveController");
        qmlRegisterType<DashboardFiles>("Society", 1, 0, "DashboardFiles");
        qmlRegisterType<ModelImporter>("Society", 1, 0, "ModelImporter");
        qmlRegisterType<NetworkDriveController>("Society", 1, 0, "NetworkDriveController");
        qmlRegisterType<AccountController>("Society", 1, 0, "AccountController");
        qmlRegisterType<DevicePairing>("Society", 1, 0, "DevicePairing");
        qmlRegisterType<PairingQr>("Society", 1, 0, "PairingQr");
        qmlRegisterType<QrScanner>("Society", 1, 0, "QrScanner");
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
        QCOMPARE(controller.sections().size(), 8);
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
        QVERIFY(QDir(fixture.filePath("Files")).isEmpty());
        const auto shared = iiSocietyContainer::SharedStorage::open();
        QVERIFY(shared);
        QCOMPARE(shared->drive().rootPath(), fixture.path());
    }

    void driveWindowShowsEightSectionsAndNavigates()
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
        auto *driveHome = window->findChild<QQuickItem *>("societyDriveHome");
        QVERIFY(driveTitle && driveHome);
        QCOMPARE(driveTitle->property("text").toString(), QString("Society"));
        QCOMPARE(driveHome->property("text").toString(), QString("Society"));
        auto *controller = window->findChild<DriveController *>("driveController");
        auto *grid = window->findChild<QQuickItem *>("sectionsGrid");
        auto *files = window->findChild<QQuickItem *>("fileGridView");
        auto *content = window->findChild<QQuickItem *>("driveContent");
        QVERIFY(controller && grid && files && content);
        QTRY_VERIFY(window->isVisible());
        QTRY_COMPARE(grid->property("count").toInt(), 8);
        auto *storageTab = window->findChild<QQuickItem *>("storageTab");
        QVERIFY(storageTab);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
            storageTab->mapToScene(QPointF(storageTab->width()/2, storageTab->height()/2)).toPoint());
        QTRY_VERIFY(window->property("selectedTab").toString() == "Storage");
        QVERIFY(grid->isVisible());
        QVERIFY(!files->isVisible());
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
        QTRY_COMPARE(files->property("count").toInt(), 2);
        QVERIFY(QMetaObject::invokeMethod(files, "activated", Q_ARG(QString, fixture.filePath("Files/Nested")), Q_ARG(bool, true)));
        QTRY_COMPARE(controller->currentPath(), fixture.filePath("Files/Nested"));
        auto *up = window->findChild<QQuickItem *>("driveUp");
        QVERIFY(up);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          up->mapToScene(QPointF(up->width()/2, up->height()/2)).toPoint());
        QTRY_COMPARE(controller->currentPath(), fixture.filePath("Files"));
        QVERIFY(controller->openSection("models"));
        QTRY_COMPARE(files->property("path").toString(), fixture.filePath("Models"));
        QTRY_COMPARE(files->property("count").toInt(), 1);
        QVERIFY(files->isVisible());
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
            QTRY_COMPARE(files->property("count").toInt(), 2);
            controller->goHome();
        }
        // Phone width retains every logical area even without the desktop sidebar.
        window->resize(QSize(390, 844));
        QTRY_VERIFY(!window->findChild<QQuickItem *>("driveSidebar")->isVisible());
        for (const auto section : iiSocietyContainer::allStoreSections()) {
            QVERIFY(controller->openSection(iiSocietyContainer::storeSectionKey(section)));
            QTRY_VERIFY(files->isVisible());
            QCOMPARE(controller->currentSection(), iiSocietyContainer::storeSectionName(section));
            controller->goHome();
            QTRY_VERIFY(grid->isVisible());
            QCOMPARE(grid->property("count").toInt(), 8);
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
            QTRY_COMPARE(files->property("count").toInt(), index + 2);
            QTRY_VERIFY(!files->property("loading").toBool());
            QVERIFY(model.exists());
            QVERIFY(QFileInfo::exists(fixture.filePath("Models/" + name)));
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
        QCOMPARE(files.recentFiles().size(), 3);
        QCOMPARE(files.recentFiles().first().toMap().value("name").toString(), QString("latest.txt"));
        QCOMPARE(files.generationHistory().size(), 1);
        QCOMPARE(files.generationHistory().first().toMap().value("name").toString(), QString("finished.PNG"));
        files.setQuery("EARLIEST");
        QCOMPARE(files.recentFiles().size(), 1); // Search the snapshot, not just the first three cards.
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
        const auto chromeBottom = window->property("windowDragHandleTopMargin").toReal()
            + window->property("windowDragHandleHeight").toReal();
        QTRY_VERIFY(view->mapToScene(QPointF()).y() >= chromeBottom);
        QTRY_COMPARE(view->mapToScene(QPointF(0, view->height())).y(),
                     window->height() - window->property("mobileSystemSafeBottomInset").toReal());
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
        auto *storage = window->findChild<QQuickItem *>("storageView");
        auto *dashboardTab = window->findChild<QQuickItem *>("dashboardTab");
        auto *storageTab = window->findChild<QQuickItem *>("storageTab");
        auto *prompt = window->findChild<QQuickItem *>("promptField");
        auto *drive = window->findChild<DriveController *>("driveController");
        QVERIFY(dashboard && storage && dashboardTab && storageTab && prompt && drive);
        QTRY_VERIFY(window->isVisible() && dashboard->isVisible());
        QVERIFY(!storage->isVisible());
        const auto *localNavigation = QAccessible::queryAccessibleInterface(window->findChild<QQuickItem *>("dashboardLocal"));
        QVERIFY(localNavigation);
        QCOMPARE(localNavigation->text(QAccessible::Name), QString("Local"));
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
        QTRY_COMPARE(files->recentFiles().size(), 3);
        click(storageTab);
        QTRY_VERIFY(storage->isVisible() && !dashboard->isVisible());
        QVERIFY(drive->navigate(fixture.filePath("Files/Work")));
        click(dashboardTab);
        QTRY_VERIFY(dashboard->isVisible() && !storage->isVisible());
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

#include "App/Drive/DriveController.h"
#include "App/Files/DirectoryLocation.h"
#include "App/Files/ModelImporter.h"
#include "backend/runtime/appbootstrap.h"

#include <QDir>
#include <QFile>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QGuiApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <SharedStorage.h>

class SocietyDriveTest final : public QObject
{
    Q_OBJECT
private slots:
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

        qmlRegisterType<DirectoryLocation>("Society", 1, 0, "DirectoryLocation");
        qmlRegisterType<DriveController>("Society", 1, 0, "DriveController");
        qmlRegisterType<ModelImporter>("Society", 1, 0, "ModelImporter");
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
        auto *controller = window->findChild<DriveController *>("driveController");
        auto *grid = window->findChild<QQuickItem *>("sectionsGrid");
        auto *files = window->findChild<QQuickItem *>("fileGridView");
        auto *content = window->findChild<QQuickItem *>("driveContent");
        QVERIFY(controller && grid && files && content);
        QTRY_VERIFY(window->isVisible());
        QTRY_COMPARE(grid->property("count").toInt(), 8);
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
            QVERIFY(grid->width() > 0 && grid->height() > 0);
            // Keep header and bottom actions inside the platform's usable area.
            const auto top = window->property("mobileSystemSafeTopInset").toReal();
            const auto bottom = window->property("mobileSystemSafeBottomInset").toReal();
            const auto left = window->property("mobileSystemSafeLeftInset").toReal();
            const auto right = window->property("mobileSystemSafeRightInset").toReal();
            QTRY_COMPARE(content->mapToScene(QPointF()).y(), top);
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

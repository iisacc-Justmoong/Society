#include "Daemon/ModelBackendServer.h"
#include "Society/Native/LegacyWorkspaceBridge.h"
#include "Society/Native/NativeWorkspaceSurface.h"
#include "Society/Dashboard/ProjectRepository.h"
#include "Society/Semantics/SemanticRepository.h"
#include <iiSocietyContainer>
#include <QDir>
#include <QDirIterator>
#include <QClipboard>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMimeData>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>
#include <QtEndian>
#include <algorithm>

class WorkspaceIntegrationDashboard final : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(bool projectsPageVisible MEMBER projects NOTIFY projectsPageVisibleChanged)
    Q_PROPERTY(bool calendarPageVisible MEMBER calendar NOTIFY calendarPageVisibleChanged)
    Q_PROPERTY(bool activityPageVisible MEMBER activity NOTIFY activityPageVisibleChanged)
    Q_PROPERTY(QString filesRoot MEMBER root NOTIFY filesRootChanged)
    Q_PROPERTY(QString dashboardPath MEMBER path NOTIFY dashboardPathChanged)
    Q_PROPERTY(QObject* viewModel MEMBER files)
public:
    using QQuickItem::QQuickItem;
    bool projects=false,calendar=false,activity=false;
    QString root,path;
    QObject *files=nullptr;
    Q_INVOKABLE void goHome() {}
signals:
    void projectsPageVisibleChanged(); void calendarPageVisibleChanged(); void activityPageVisibleChanged();
    void filesRootChanged(); void dashboardPathChanged(); void fileRequested(const QString &path);
};

class WorkspaceIntegrationFiles final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString containerPath MEMBER containerPath NOTIFY containerPathChanged)
public:
    QString containerPath;
signals:
    void containerPathChanged(); void filesChanged(); void loadingChanged();
};

class WorkspaceFileIntegrationTest final : public QObject {
    Q_OBJECT
private slots:
    void ribbonOperatesOnRealContainerFiles() {
        QTemporaryDir fixture(QDir::cleanPath(QStringLiteral(SOCIETY_TEST_DIRECTORY)+"/../../.workspace-real-XXXXXX"));
        QVERIFY(fixture.isValid());
        const auto root=fixture.filePath("drive.societycontainer"), broker=fixture.filePath("broker");
        QVERIFY(QDir().mkdir(root)); QVERIFY(iiSocietyContainer::SocietyDrive::create(root));
        QVERIFY(QDir().mkdir(broker));
        QVERIFY(QFile::setPermissions(broker,QFileDevice::ReadOwner|QFileDevice::WriteOwner|QFileDevice::ExeOwner));
        QVERIFY(QDir().mkdir(root+"/Files/Drafts")); QVERIFY(QDir().mkdir(root+"/Files/Target"));
        QVERIFY(QDir().mkdir(root+"/Files/Other"));
        const auto original=root+"/Files/Drafts/draft.txt";
        QFile source(original); QVERIFY(source.open(QIODevice::WriteOnly)); QCOMPARE(source.write("draft content"),qint64(13)); source.close();
        const auto external=fixture.filePath("external.txt"); QFile outside(external);
        QVERIFY(outside.open(QIODevice::WriteOnly)); QCOMPARE(outside.write("external content"),qint64(16)); outside.close();
        const auto oldBroker=qgetenv("SOCIETY_LOCAL_BROKER_DIR"),oldJobs=qgetenv("SOCIETY_JOB_STATE_ROOT"),oldAuto=qgetenv("SOCIETY_DISABLE_BACKEND_AUTOSTART");
        const bool hadBroker=qEnvironmentVariableIsSet("SOCIETY_LOCAL_BROKER_DIR"),hadJobs=qEnvironmentVariableIsSet("SOCIETY_JOB_STATE_ROOT"),hadAuto=qEnvironmentVariableIsSet("SOCIETY_DISABLE_BACKEND_AUTOSTART");
        qputenv("SOCIETY_LOCAL_BROKER_DIR",broker.toUtf8()); qputenv("SOCIETY_JOB_STATE_ROOT",fixture.filePath("jobs").toUtf8());
        qputenv("SOCIETY_DISABLE_BACKEND_AUTOSTART","1");
        const auto restore=qScopeGuard([&] {
            if (hadBroker) qputenv("SOCIETY_LOCAL_BROKER_DIR",oldBroker); else qunsetenv("SOCIETY_LOCAL_BROKER_DIR");
            if (hadJobs) qputenv("SOCIETY_JOB_STATE_ROOT",oldJobs); else qunsetenv("SOCIETY_JOB_STATE_ROOT");
            if (hadAuto) qputenv("SOCIETY_DISABLE_BACKEND_AUTOSTART",oldAuto); else qunsetenv("SOCIETY_DISABLE_BACKEND_AUTOSTART");
        });
        ModelBackendServer server; server.selectContainer(root); QVERIFY(server.listen(broker));
        QQuickWindow window;
        WorkspaceIntegrationDashboard dashboard(window.contentItem()); dashboard.setObjectName("dashboardView"); dashboard.setSize({1200,760});
        WorkspaceIntegrationFiles files; files.containerPath=root; dashboard.files=&files; dashboard.root=root+"/Files";
        QObject projectActor; projectActor.setProperty("actorId",QStringLiteral("urn:user:test"));
        dashboard.setProperty("projectWorkspace",QVariant::fromValue(&projectActor));
        QQuickItem sidebar(&dashboard); sidebar.setObjectName("dashboardSidebar"); sidebar.setWidth(170);
        QQuickItem content(&dashboard); QQuickItem scroll(&content); scroll.setObjectName("dashboardScroll");
        QSignalSpy opened(&dashboard,SIGNAL(fileRequested(QString)));
        auto *surface=LegacyWorkspaceBridge::install(&window); QVERIFY(surface); auto *vm=surface->model(); QVERIFY(vm);
        QTRY_VERIFY_WITH_TIMEOUT(vm->fileActionsAvailable.get(),30000);
        vm->showRecent();
        const auto contains=[&](const QString &path) {
            return std::any_of(vm->rows.get().begin(),vm->rows.get().end(),[&](const auto &item){return QString::fromStdString(item.path)==path;});
        };
        const auto completed=[&] { return vm->operationStatus.get().find("completed")!=std::string::npos; };
        QTRY_VERIFY_WITH_TIMEOUT(contains(original),60000);
        vm->gridSelected({original.toStdString()}); vm->showRibbonHome(); vm->invokeRibbon("open")();
        QTRY_COMPARE_WITH_TIMEOUT(opened.size(),1,60000);
        QCOMPARE(opened.first().first().toString(),original);
        vm->gridSelected({original.toStdString()}); vm->invokeRibbon("rename")(); QVERIFY(vm->renameOpen.get());
        vm->renameEdited("renamed.txt"); vm->renameSelected();
        const auto renamed=root+"/Files/Drafts/renamed.txt";
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(renamed) && !QFileInfo::exists(original) && completed(),60000);
        QTRY_VERIFY_WITH_TIMEOUT(contains(renamed),60000);
        vm->gridSelected({renamed.toStdString()}); vm->invokeRibbon("duplicate")();
        QTRY_VERIFY_WITH_TIMEOUT(QDir(root+"/Files/Drafts").entryList({"*.txt"},QDir::Files).size()==2 && completed(),60000);
        QVERIFY(QFileInfo::exists(renamed));
        QTRY_VERIFY_WITH_TIMEOUT(contains(renamed),60000);
        vm->gridSelected({renamed.toStdString()}); vm->invokeRibbon("copy-to")(); QVERIFY(vm->moveOpen.get());
        vm->moveEdited("Files/Target"); vm->moveSelected();
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(root+"/Files/Target/renamed.txt") && completed(),60000);
        QVERIFY(QFileInfo::exists(renamed));
        vm->gridSelected({renamed.toStdString()}); vm->invokeRibbon("move")(); QVERIFY(vm->moveOpen.get());
        vm->moveEdited("Files/Other"); vm->moveSelected();
        const auto moved=root+"/Files/Other/renamed.txt";
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(moved) && !QFileInfo::exists(renamed) && completed(),60000);
        QTRY_VERIFY_WITH_TIMEOUT(contains(moved),60000);
        vm->gridSelected({moved.toStdString()}); vm->invokeRibbon("trash")();
        QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(moved) && QFileInfo::exists(root+"/Deleted/renamed.txt") && completed(),60000);
        vm->invokeRibbon("new-folder")(); QVERIFY(vm->folderOpen.get()); vm->folderEdited("NewFolder"); vm->createFolder();
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo(root+"/Files/NewFolder").isDir() && completed(),60000);
        vm->invokeRibbon("import")(); QVERIFY(vm->importOpen.get()); vm->importEdited(external.toStdString());
        vm->importDestinationEdited("Files/Target"); vm->importFile();
        const auto imported=root+"/Files/Target/external.txt";
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(imported) && completed(),60000);
        QFile result(imported); QVERIFY(result.open(QIODevice::ReadOnly)); QCOMPARE(result.readAll(),QByteArray("external content"));
        QVERIFY2(!vm->hasOperationError.get(),vm->operationError.get().c_str());

        vm->showRibbonAdd(); vm->invokeRibbon("new-project")(); QVERIFY(vm->addOpen.get());
        vm->addEdited1("Ribbon Project"); vm->addEdited2("Produce a canvas"); vm->submitAdd();
        QTRY_VERIFY_WITH_TIMEOUT(vm->operationStatus.get().find("Project created")!=std::string::npos || vm->hasOperationError.get(),60000);
        QVERIFY2(!vm->hasOperationError.get(),vm->operationError.get().c_str());
        ProjectRepository projects(root,QStringLiteral("urn:user:test"));
        QCOMPARE(projects.load().projects.size(),1);
        const auto projectKey=projects.load().projects.first().toMap().value("key").toString();
        QCOMPARE(projects.load(projectKey).project.value("goal").toString(),QStringLiteral("Produce a canvas"));

        vm->invokeRibbon("new-canvas")(); QVERIFY(vm->addOpen.get());
        vm->addEdited1("Ribbon Canvas"); vm->addEdited2("320"); vm->addEdited3("240"); vm->submitAdd();
        QTRY_VERIFY_WITH_TIMEOUT(vm->operationStatus.get().find("Canvas created")!=std::string::npos || vm->hasOperationError.get(),60000);
        QVERIFY2(!vm->hasOperationError.get(),vm->operationError.get().c_str());
        QVERIFY(QFileInfo::exists(root+"/Files/Ribbon Canvas.iisc"));

        const auto folder=fixture.filePath("external folder"); QVERIFY(QDir().mkdir(folder));
        QFile child(folder+"/child.txt"); QVERIFY(child.open(QIODevice::WriteOnly)); QCOMPARE(child.write("folder content"),qint64(14)); child.close();
        vm->invokeRibbon("add-import-folder")(); QVERIFY(vm->addOpen.get());
        vm->addEdited1(folder.toStdString()); vm->addEdited2("Files/Target"); vm->submitAdd();
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(root+"/Files/Target/external folder/child.txt") || vm->hasOperationError.get(),60000);
        QVERIFY2(!vm->hasOperationError.get(),vm->operationError.get().c_str());

        const auto first=fixture.filePath("first import.txt"),second=fixture.filePath("second import.txt");
        for (const auto &path : {first,second}) { QFile item(path); QVERIFY(item.open(QIODevice::WriteOnly)); QCOMPARE(item.write("import"),qint64(6)); }
        vm->invokeRibbon("add-import-files")(); QVERIFY(vm->addOpen.get());
        vm->addEdited1((first+"\n"+second).toStdString()); vm->addEdited2("Files/Target"); vm->submitAdd();
        QTRY_VERIFY_WITH_TIMEOUT((QFileInfo::exists(root+"/Files/Target/first import.txt") && QFileInfo::exists(root+"/Files/Target/second import.txt")) || vm->hasOperationError.get(),60000);
        QVERIFY2(!vm->hasOperationError.get(),vm->operationError.get().c_str());

        const auto clipboardSource=fixture.filePath("clipboard.txt"); QFile clipboardFile(clipboardSource);
        QVERIFY(clipboardFile.open(QIODevice::WriteOnly)); QCOMPARE(clipboardFile.write("clipboard"),qint64(9)); clipboardFile.close();
        auto *mime=new QMimeData; mime->setUrls({QUrl::fromLocalFile(clipboardSource)}); QGuiApplication::clipboard()->setMimeData(mime);
        QTRY_VERIFY_WITH_TIMEOUT(vm->clipboardHasFiles.get(),10000);
        vm->invokeRibbon("paste")(); QVERIFY(vm->pasteOpen.get()); vm->pasteEdited("Files/Target"); vm->pasteFiles();
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(root+"/Files/Target/clipboard.txt") || vm->hasOperationError.get(),60000);
        QVERIFY2(!vm->hasOperationError.get(),vm->operationError.get().c_str()); QVERIFY(QFileInfo::exists(clipboardSource));

        QTRY_VERIFY_WITH_TIMEOUT(contains(imported),60000);
        vm->gridSelected({imported.toStdString()}); vm->invokeRibbon("link-url")(); QVERIFY(vm->addOpen.get());
        vm->addEdited1("https://example.com/reference"); vm->addEdited2("example:references"); vm->submitAdd();
        QTRY_VERIFY_WITH_TIMEOUT(vm->operationStatus.get().find("Reference saved")!=std::string::npos || vm->hasOperationError.get(),60000);
        QVERIFY2(!vm->hasOperationError.get(),vm->operationError.get().c_str());
        SemanticRepository semantic(root,QStringLiteral("urn:user:test"));
        QCOMPARE(semantic.relations(imported).value("outgoing").toArray().size(),1);
        vm->gridSelected({imported.toStdString()}); vm->invokeRibbon("project-add-selection")(); QVERIFY(vm->addOpen.get());
        vm->addEdited1("Ribbon Project"); vm->addEdited2("Reference"); vm->submitAdd();
        QTRY_VERIFY_WITH_TIMEOUT(vm->operationStatus.get().find("Processed 1 resource")!=std::string::npos || vm->hasOperationError.get(),60000);
        QVERIFY2(!vm->hasOperationError.get(),vm->operationError.get().c_str());
        QCOMPARE(projects.load(projectKey).resources.size(),1);

        const QByteArray payload("model bytes");
        const auto header=QJsonDocument(QJsonObject{{"tensor",QJsonObject{{"dtype","U8"}, {"shape",QJsonArray{payload.size()}}, {"data_offsets",QJsonArray{0,payload.size()}}}}}).toJson(QJsonDocument::Compact);
        QByteArray modelBytes(8,'\0'); qToLittleEndian(quint64(header.size()),modelBytes.data()); modelBytes+=header+payload;
        const auto modelPath=fixture.filePath("ribbon-model.safetensors"); QFile model(modelPath);
        QVERIFY(model.open(QIODevice::WriteOnly)); QCOMPARE(model.write(modelBytes),qint64(modelBytes.size())); model.close();
        vm->invokeRibbon("add-import-models")(); QVERIFY(vm->addOpen.get()); vm->addEdited1(modelPath.toStdString()); vm->submitAdd();
        const auto importedModel=[&] {
            QDirIterator it(root+"/Models", {"ribbon-model.safetensors"}, QDir::Files, QDirIterator::Subdirectories);
            if (!it.hasNext()) return false;
            QFile file(it.next()); return file.open(QIODevice::ReadOnly) && file.readAll()==modelBytes;
        };
        QTRY_VERIFY_WITH_TIMEOUT(importedModel() || vm->hasOperationError.get(),120000);
        QVERIFY2(!vm->hasOperationError.get(),vm->operationError.get().c_str());
        QTRY_VERIFY_WITH_TIMEOUT(vm->operationStatus.get().find("Imported ribbon-model.safetensors")!=std::string::npos || vm->hasOperationError.get(),60000);
        QVERIFY2(!vm->hasOperationError.get(),vm->operationError.get().c_str());
    }
};

QTEST_MAIN(WorkspaceFileIntegrationTest)
#include "tst_workspace_file_integration.moc"

#include "Society/Dashboard/ProjectRepository.h"
#include "Society/Dashboard/ProjectWorkspace.h"
#include "Society/Dashboard/ProjectResourceBinding.h"
#include <iiSocietyContainer>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QImage>
#include <QSignalSpy>
#include <iiFileProvider>
#include <algorithm>
#include <chrono>

static QQuickItem *visual(QQuickItem *parent, const QString &name) {
    if (parent->objectName() == name)
        return parent;
    for (auto *child : parent->childItems())
        if (auto *found = visual(child, name))
            return found;
    return nullptr;
}

class ProjectTests : public QObject {
    Q_OBJECT
  private slots:
    void projectBindingExposesTypedResourceIdentity() {
        const QJsonObject binding{{"kind","files-catalog-v1"},{"container","drive-id"},{"key","catalog-key"}};
        const auto value=ProjectResourceBinding::resourceId(binding); QVERIFY(value);
        QCOMPARE(QString::fromStdString(value->value),ProjectResourceBinding::assetId(binding));
        QVERIFY(!ProjectResourceBinding::resourceId(QJsonObject{{"kind","files-catalog-v1"},{"container","drive-id"}}));
    }
    void createEditAndReopenAsSingleCatalogPackage() {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/projects-XXXXXX.societycontainer");
        QVERIFY(fixture.isValid());
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        ProjectRepository repo(fixture.path(), "urn:user:creator");
        const auto key = repo.create({{"name", "Character production"},
                                      {"goal", "Complete a commercial model"},
                                      {"requirements", QStringList{"Reusable rig"}},
                                      {"constraints", QStringList{"Licensed references"}},
                                      {"completionConditions", QStringList{"Approved export"}}});
        QVERIFY(!key.isEmpty());
        auto state = repo.load(key);
        QCOMPARE(state.projects.size(), 1);
        QCOMPARE(state.project.value("goal").toString(), "Complete a commercial model");
        const auto id = state.project.value("projectId").toString(),
                   revision = state.project.value("revision").toString();
        iiFileProvider::FileCatalog catalog(QDir(fixture.path()).filePath("Files").toStdString(),
                                            iiFileProvider::FileCatalog::Access::ReadOnly);
        QCOMPARE(catalog.count(), std::uint64_t(1));
        QCOMPARE(catalog.itemKind(key.toStdString()), iiFileProvider::CatalogEntryKind::Package);
        const auto entry = catalog.lookup(key.toStdString());
        QVERIFY(entry.has_value());
        QVERIFY(QString::fromStdString(entry->path).startsWith("Projects/"));
        QString storageError;
        auto storage = iiSocietyContainer::SharedStorage::open(fixture.path(), &storageError);
        QVERIFY2(storage.has_value(), qPrintable(storageError));
        auto fileRepository = storage->fileRepository();
        fileRepository->reconcile();
        QCOMPARE(catalog.count(), std::uint64_t(1));
        QVERIFY(!fileRepository->lookup("files/" + entry->path + "/manifest.json"));
        auto draft = state.project;
        draft["name"] = "Renamed project";
        draft["goal"] = "Deliver the final model";
        draft["status"] = "in_progress";
        repo.save(key, revision, draft);
        state = repo.load(key);
        QCOMPARE(state.project.value("name").toString(), "Renamed project");
        QCOMPARE(state.project.value("projectId").toString(), id);
        QCOMPARE(state.project.value("status").toString(), "in_progress");
        QVERIFY_THROWS_EXCEPTION(std::exception, repo.save(key, revision, draft));
        ProjectRepository reopened(fixture.path(), "urn:user:creator");
        QCOMPARE(reopened.load(key).project.value("goal").toString(), "Deliver the final model");
    }
    void resourcesAndRelationsRetainUserMeaning() {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/projects-graph-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        ProjectRepository repo(fixture.path(), "urn:user:creator");
        const auto key = repo.create({{"name", "Visual project"}, {"goal", "Create character"}});
        auto state = repo.load(key);
        repo.addResource(key, state.project.value("revision").toString(),
                         {{"name", "Reference"},
                          {"purpose", "Character appearance"},
                          {"locator", "society:reference:one"},
                          {"location", "reference"},
                          {"derived", true}});
        state = repo.load(key);
        QCOMPARE(state.resources.size(), 1);
        QVERIFY(state.resources.first().toMap().value("derived").toBool());
        repo.addEntity(
            key, state.project.value("revision").toString(),
            {{"type", "iip:CharacterConcept"}, {"name", "Concept"}, {"description", "Character visual"}});
        state = repo.load(key);
        const auto entity = state.entities.first().toMap().value("id").toString();
        const auto resource = state.resources.first().toMap().value("id").toString();
        repo.addRelation(key, state.project.value("revision").toString(),
                         {{"subject", entity}, {"type", "iip:hasAsset"}, {"object", resource}});
        state = repo.load(key);
        QCOMPARE(state.relations.size(), 1);
        QCOMPARE(state.relations.first().toMap().value("origin").toString(), "user_asserted");
        const auto selectedBy = state.resources.first().toMap().value("selectedBy");
        repo.updateResource(key, state.project.value("revision").toString(), resource,
                            {{"name", "Revised reference"},
                             {"purpose", "Updated appearance"},
                             {"locator", "society:reference:two"},
                             {"derived", true}});
        state = repo.load(key);
        QCOMPARE(state.resources.first().toMap().value("id").toString(), resource);
        QCOMPARE(state.resources.first().toMap().value("selectedBy"), selectedBy);
        QCOMPARE(state.relations.first().toMap().value("object").toString(), resource);
        QCOMPARE(state.resources.first().toMap().value("name").toString(), "Revised reference");
        QVERIFY_THROWS_EXCEPTION(
            std::exception, repo.removeResource(key, state.project.value("revision").toString(), resource));
        QVERIFY(repo.validate(key, true).value("ok").toBool());
        repo.addEntityType(key, state.project.value("revision").toString(),
                           {{"id", "ex:Material"}, {"label", "Material"}, {"description", "A material"},
                            {"parents", QStringList{"iip:Entity"}}});
        state = repo.load(key);
        repo.addRelationType(key, state.project.value("revision").toString(),
                             {{"id", "ex:uses"}, {"label", "Uses"}, {"description", "Uses a resource"},
                              {"sourceTypes", QStringList{"iip:Entity"}}, {"targetTypes", QStringList{"iip:Resource"}}});
        state = repo.load(key);
        const auto types = state.schema.value("entities").toList();
        const auto relationTypes = state.schema.value("relations").toList();
        QVERIFY(std::any_of(types.begin(), types.end(), [](const QVariant &v) { return v.toMap().value("id").toString() == "ex:Material"; }));
        QVERIFY(std::any_of(relationTypes.begin(), relationTypes.end(), [](const QVariant &v) { return v.toMap().value("id").toString() == "ex:uses"; }));
    }
    void managedReferencesSurviveRenameAndDoNotDuplicate() {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/projects-identity-XXXXXX.societycontainer");
        auto drive = iiSocietyContainer::SocietyDrive::create(fixture.path());
        QVERIFY(drive.has_value());
        const auto files = drive->sectionPath(iiSocietyContainer::StoreSection::Files);
        QVERIFY(QDir().mkpath(files + "/Art"));
        QVERIFY(QDir().mkpath(files + "/Archive"));
        const auto source = files + "/Art/original.txt";
        QFile input(source);
        QVERIFY(input.open(QIODevice::WriteOnly));
        QCOMPARE(input.write("shared asset"), qint64(12));
        input.close();

        ProjectRepository repo(fixture.path(), "urn:user:creator");
        const auto first = repo.create({{"name", "First"}, {"goal", "Use the asset"}});
        const auto second = repo.create({{"name", "Second"}, {"goal", "Reuse the asset"}});
        const QVariantMap draft{{"name", "Original"}, {"purpose", "Reference"},
                                {"locator", source}, {"location", "reference"}};
        auto one = repo.load(first);
        repo.addResource(first, one.project.value("revision").toString(), draft);
        one = repo.load(first);
        auto two = repo.load(second);
        repo.addResource(second, two.project.value("revision").toString(), draft);
        two = repo.load(second);
        QCOMPARE(one.resources.size(), 1);
        QCOMPARE(two.resources.size(), 1);
        const auto firstResource = one.resources.first().toMap();
        const auto secondResource = two.resources.first().toMap();
        const auto assetId = firstResource.value("assetId").toString();
        QVERIFY(!assetId.isEmpty());
        QCOMPARE(secondResource.value("assetId").toString(), assetId);
        const auto revision = one.project.value("revision").toString();
        repo.addResource(first, revision, draft);
        one = repo.load(first);
        QCOMPARE(one.resources.size(), 1);
        QCOMPARE(one.project.value("revision").toString(), revision);

        iiSocietyContainer::FileOperations operations(*drive);
        const auto renamed = operations.perform(iiSocietyContainer::FileOperations::Action::Rename,
                                                source, "renamed.txt");
        QVERIFY2(bool(renamed), qPrintable(renamed.error));
        const auto moved = operations.perform(iiSocietyContainer::FileOperations::Action::Move,
                                              renamed.path, files + "/Archive");
        QVERIFY2(bool(moved), qPrintable(moved.error));
        QCOMPARE(repo.resourcePath(first, firstResource.value("id").toString()), moved.path);
        QCOMPARE(repo.resourcePath(second, secondResource.value("id").toString()), moved.path);
        one = repo.load(first);
        QCOMPARE(one.resources.first().toMap().value("assetId").toString(), assetId);
        QCOMPARE(one.resources.first().toMap().value("locator").toString(), moved.path);

        const auto other = files + "/Art/different.txt";
        QFile otherFile(other);
        QVERIFY(otherFile.open(QIODevice::WriteOnly));
        QCOMPARE(otherFile.write("shared asset"), qint64(12));
        otherFile.close();
        auto distinct = draft;
        distinct["locator"] = other;
        distinct["name"] = "Different asset";
        repo.addResource(first, one.project.value("revision").toString(), distinct);
        one = repo.load(first);
        QCOMPARE(one.resources.size(), 2);
        QVERIFY(one.resources.last().toMap().value("assetId").toString() != assetId);
    }
    void repeatedPackageCopyDoesNotDuplicateTheSameInput() {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/projects-copy-identity-XXXXXX.societycontainer");
        auto drive = iiSocietyContainer::SocietyDrive::create(fixture.path());
        QVERIFY(drive.has_value());
        const auto source = drive->sectionPath(iiSocietyContainer::StoreSection::Files) + "/source.txt";
        QFile input(source);
        QVERIFY(input.open(QIODevice::WriteOnly));
        QCOMPARE(input.write("copy source"), qint64(11));
        input.close();
        ProjectRepository repo(fixture.path(), "urn:user:creator");
        const auto key = repo.create({{"name", "Package"}, {"goal", "Bundle resources"}});
        QVariantMap draft{{"name", "Source"}, {"purpose", "Bundle"},
                          {"locator", source}, {"location", "embedded"}};
        auto state = repo.load(key);
        repo.addResource(key, state.project.value("revision").toString(), draft);
        state = repo.load(key);
        QCOMPARE(state.resources.size(), 1);
        const auto revision = state.project.value("revision").toString();
        const auto copied = repo.resourcePath(key, state.resources.first().toMap().value("id").toString());
        repo.addResource(key, revision, draft);
        state = repo.load(key);
        QCOMPARE(state.resources.size(), 1);
        QCOMPARE(state.project.value("revision").toString(), revision);
        QCOMPARE(repo.resourcePath(key, state.resources.first().toMap().value("id").toString()), copied);
        QVERIFY(input.open(QIODevice::ReadOnly));
        QCOMPARE(input.readAll(), QByteArray("copy source"));
        input.close();
        QVERIFY(input.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(input.write("new source content"), qint64(18));
        input.close();
        repo.addResource(key, state.project.value("revision").toString(), draft);
        state = repo.load(key);
        QCOMPARE(state.resources.size(), 2);
        QFile firstCopy(copied);
        QVERIFY(firstCopy.open(QIODevice::ReadOnly));
        QCOMPARE(firstCopy.readAll(), QByteArray("copy source"));
        QFile secondCopy(repo.resourcePath(key, state.resources.last().toMap().value("id").toString()));
        QVERIFY(secondCopy.open(QIODevice::ReadOnly));
        QCOMPARE(secondCopy.readAll(), QByteArray("new source content"));
    }
    void changedDuplicateRequiresEditingExistingResource() {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/projects-duplicate-intent-XXXXXX.societycontainer");
        auto drive = iiSocietyContainer::SocietyDrive::create(fixture.path());
        QVERIFY(drive.has_value());
        const auto source = drive->sectionPath(iiSocietyContainer::StoreSection::Files) + "/source.txt";
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("asset"), qint64(5));
        file.close();
        ProjectRepository repo(fixture.path(), "urn:user:creator");
        const auto key = repo.create({{"name", "Intent"}, {"goal", "Keep intent"}});
        QVariantMap draft{{"name", "Source"}, {"purpose", "Reference"},
                          {"locator", source}, {"location", "reference"}};
        auto state = repo.load(key);
        repo.addResource(key, state.project.value("revision").toString(), draft);
        state = repo.load(key);
        const auto revision = state.project.value("revision").toString();
        draft["purpose"] = "Different purpose";
        QVERIFY_THROWS_EXCEPTION(std::exception, repo.addResource(key, revision, draft));
        state = repo.load(key);
        QCOMPARE(state.project.value("revision").toString(), revision);
        QCOMPARE(state.resources.size(), 1);
        QCOMPARE(state.resources.first().toMap().value("purpose").toString(), QString("Reference"));
    }
    void staleCatalogReferenceDoesNotOpenAnUnregisteredReplacement() {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/projects-replaced-XXXXXX.societycontainer");
        auto drive = iiSocietyContainer::SocietyDrive::create(fixture.path());
        QVERIFY(drive.has_value());
        const auto files = drive->sectionPath(iiSocietyContainer::StoreSection::Files);
        const auto source = files + "/source.txt";
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("original"), qint64(8));
        file.close();
        ProjectRepository repo(fixture.path(), "urn:user:creator");
        const auto key = repo.create({{"name", "Safety"}, {"goal", "Keep the right resource"}});
        auto state = repo.load(key);
        repo.addResource(key, state.project.value("revision").toString(),
                         {{"name", "Source"}, {"purpose", "Reference"},
                          {"locator", source}, {"location", "reference"}});
        state = repo.load(key);
        const auto id = state.resources.first().toMap().value("id").toString();
        QVERIFY(QFile::rename(source, files + "/moved-without-catalog.txt"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("different"), qint64(9));
        file.close();
        QVERIFY_THROWS_EXCEPTION(std::exception, (void)repo.resourcePath(key, id));
        QVERIFY(!repo.validate(key, true).value("warnings").toStringList().isEmpty());
        const auto second = repo.create({{"name", "Second"}, {"goal", "Do not rebind the old identity"}});
        const auto secondState = repo.load(second);
        QVERIFY_THROWS_EXCEPTION(std::exception,
            repo.addResource(second, secondState.project.value("revision").toString(),
                {{"name", "Replacement"}, {"purpose", "Reference"},
                 {"locator", source}, {"location", "reference"}}));
        QVERIFY_THROWS_EXCEPTION(std::exception, (void)repo.resourcePath(key, id));
        QCOMPARE(repo.load(second).resources.size(), 0);
    }
    void externallyMovedFileResolvesAfterCatalogReconciliation() {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/projects-external-move-XXXXXX.societycontainer");
        auto drive = iiSocietyContainer::SocietyDrive::create(fixture.path());
        QVERIFY(drive.has_value());
        const auto files = drive->sectionPath(iiSocietyContainer::StoreSection::Files);
        const auto source = files + "/original.txt";
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("asset"), qint64(5));
        file.close();
        ProjectRepository repo(fixture.path(), "urn:user:creator");
        const auto key = repo.create({{"name", "External move"}, {"goal", "Reconcile the reference"}});
        const QVariantMap draft{{"name", "Source"}, {"purpose", "Reference"},
                                {"locator", source}, {"location", "reference"}};
        auto state = repo.load(key);
        repo.addResource(key, state.project.value("revision").toString(), draft);
        state = repo.load(key);
        const auto resource = state.resources.first().toMap();
        const auto id = resource.value("id").toString();
        const auto asset = resource.value("assetId").toString();
        const auto moved = files + "/moved.txt";
        QVERIFY(QFile::rename(source, moved));
        QVERIFY_THROWS_EXCEPTION(std::exception, (void)repo.resourcePath(key, id));
        iiFileProvider::FileCatalog catalog(files.toStdString());
        const auto registration = catalog.registerFile("moved.txt");
        QVERIFY(registration.metadataWritten);
        QCOMPARE(repo.resourcePath(key, id), moved);
        state = repo.load(key);
        QCOMPARE(state.resources.first().toMap().value("assetId").toString(), asset);
        const auto revision = state.project.value("revision").toString();
        repo.addResource(key, revision, draft);
        state = repo.load(key);
        QCOMPARE(state.resources.size(), 1);
        QCOMPARE(state.project.value("revision").toString(), revision);
    }
    void explicitlyEditingAnUnboundReferenceCanBindItToTheCatalog() {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/projects-bind-existing-XXXXXX.societycontainer");
        auto drive = iiSocietyContainer::SocietyDrive::create(fixture.path());
        QVERIFY(drive.has_value());
        const auto external = fixture.filePath("external.txt");
        QFile original(external);
        QVERIFY(original.open(QIODevice::WriteOnly));
        QCOMPARE(original.write("asset"), qint64(5));
        original.close();
        const auto managed = drive->sectionPath(iiSocietyContainer::StoreSection::Files) + "/managed.txt";
        QVERIFY(QFile::copy(external, managed));
        ProjectRepository repo(fixture.path(), "urn:user:creator");
        const auto key = repo.create({{"name", "Existing"}, {"goal", "Bind an existing reference"}});
        auto state = repo.load(key);
        repo.addResource(key, state.project.value("revision").toString(),
                         {{"name", "Source"}, {"purpose", "Reference"},
                          {"locator", external}, {"location", "reference"}});
        state = repo.load(key);
        auto resource = state.resources.first().toMap();
        QVERIFY(resource.value("assetId").toString().isEmpty());
        resource["locator"] = managed;
        repo.updateResource(key, state.project.value("revision").toString(), resource.value("id").toString(), resource);
        state = repo.load(key);
        resource = state.resources.first().toMap();
        QVERIFY(!resource.value("assetId").toString().isEmpty());
        iiSocietyContainer::FileOperations operations(*drive);
        const auto moved = operations.perform(iiSocietyContainer::FileOperations::Action::Rename, managed, "renamed.txt");
        QVERIFY2(bool(moved), qPrintable(moved.error));
        QCOMPARE(repo.resourcePath(key, resource.value("id").toString()), moved.path);
        QVERIFY(QFile::exists(external));
    }
    void embeddedFileAndSourceArePreserved() {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/projects-embed-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        const auto original = fixture.filePath("source.txt");
        QFile source(original);
        QVERIFY(source.open(QIODevice::WriteOnly));
        QCOMPARE(source.write("creator asset"), qint64(13));
        source.close();
        ProjectRepository repo(fixture.path(), "urn:user:creator");
        const auto key = repo.create({{"name", "Assets"}, {"goal", "Deliver a useful asset"}});
        auto state = repo.load(key);
        repo.addResource(key, state.project.value("revision").toString(),
                         {{"name", "Source"},
                          {"purpose", "Deliverable"},
                          {"locator", original},
                          {"location", "embedded"},
                          {"mediaType", "text/plain"}});
        QVERIFY(iiFileProvider::native::Presentation::waitForIdle(std::chrono::seconds(30)));
        state = repo.load(key);
        QCOMPARE(state.resources.size(), 1);
        const auto resource = state.resources.first().toMap();
        QCOMPARE(resource.value("location").toString(), "embedded");
        QFile embedded(repo.resourcePath(key, resource.value("id").toString()));
        QVERIFY(embedded.open(QIODevice::ReadOnly));
        QCOMPARE(embedded.readAll(), QByteArray("creator asset"));
        QVERIFY(source.open(QIODevice::ReadOnly));
        QCOMPARE(source.readAll(), QByteArray("creator asset"));
        repo.removeResource(key, state.project.value("revision").toString(), resource.value("id").toString());
        QVERIFY(QFile::exists(original));
        QVERIFY(QFile::exists(embedded.fileName()));
    }
    void galleryCoverUsesOnlyTheChosenResource() {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/projects-cover-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        const auto imagePath = fixture.filePath("cover.png");
        QImage image(4, 4, QImage::Format_ARGB32);
        image.fill(Qt::green);
        QVERIFY(image.save(imagePath));
        ProjectRepository repo(fixture.path(), "urn:user:creator");
        const auto key = repo.create({{"name", "Cover"}, {"goal", "Present the selected artwork"}});
        auto state = repo.load(key);
        repo.addResource(key, state.project.value("revision").toString(),
                         {{"name", "Artwork"},
                          {"purpose", "Project cover"},
                          {"locator", imagePath},
                          {"location", "reference"},
                          {"mediaType", "image/png"}});
        state = repo.load(key);
        QVERIFY(state.projects.first().toMap().value("preview").toString().isEmpty());
        const auto resource = state.resources.first().toMap().value("id").toString();
        auto draft = state.project;
        draft["coverResource"] = resource;
        repo.save(key, state.project.value("revision").toString(), draft);
        state = repo.load(key);
        QCOMPARE(state.project.value("coverResource").toString(), resource);
        QCOMPARE(QUrl(state.projects.first().toMap().value("preview").toString()).toLocalFile(), imagePath);
        repo.removeResource(key, state.project.value("revision").toString(), resource);
        state = repo.load(key);
        QVERIFY(state.project.value("coverResource").toString().isEmpty());
        QVERIFY(state.projects.first().toMap().value("preview").toString().isEmpty());
        QVERIFY(QFile::exists(imagePath));
    }
    void fourViewsAreReadOnlyAndStaleDraftIsRejected() {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/projects-workspace-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        ProjectWorkspace workspace;
        workspace.setContainerPath(fixture.path());
        workspace.setActorId("urn:user:creator");
        workspace.setActive(true);
        QTRY_VERIFY_WITH_TIMEOUT(!workspace.busy(), 15000);
        workspace.createProject({{"name", "View state"}, {"goal", "Work without extra revisions"}});
        QTRY_VERIFY_WITH_TIMEOUT(!workspace.busy(), 15000);
        QVERIFY2(workspace.errorString().isEmpty(), qPrintable(workspace.errorString()));
        const auto key = workspace.selectedKey();
        const auto revision = workspace.project().value("revision").toString();
        ProjectRepository repo(fixture.path(), "urn:user:creator");
        const auto path = QDir(fixture.filePath("Files/Projects"))
                              .entryInfoList({"*.iiproject"}, QDir::Dirs)
                              .first()
                              .filePath() +
                          "/manifest.json";
        QFile manifest(path);
        QVERIFY(manifest.open(QIODevice::ReadOnly));
        const auto before = manifest.readAll();
        manifest.close();
        for (const auto &mode : {"focus", "catalog", "gallery", "studio"}) {
            workspace.setViewMode(mode);
            QCOMPARE(workspace.viewMode(), QString(mode));
            QVERIFY(!workspace.busy());
        }
        workspace.setViewMode("invalid");
        QCOMPARE(workspace.viewMode(), QString("studio"));
        QVERIFY(manifest.open(QIODevice::ReadOnly));
        QCOMPARE(manifest.readAll(), before);
        manifest.close();
        auto stale = workspace.project();
        stale["_expectedRevision"] = revision;
        stale["goal"] = "Do not overwrite";
        auto other = repo.load(key).project;
        other["goal"] = "Another editor's goal";
        repo.save(key, revision, other);
        workspace.saveProject(stale);
        QTRY_VERIFY_WITH_TIMEOUT(!workspace.busy(), 15000);
        QVERIFY(workspace.errorString().contains("changed", Qt::CaseInsensitive));
        QCOMPARE(workspace.project().value("goal").toString(), QString("Another editor's goal"));
    }
    void qmlProvidesStudioWorkspaceWithOneProject() {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/projects-ui-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        qmlRegisterType<ProjectWorkspace>("Society", 1, 0, "ProjectWorkspace");
        QStringList warnings;
        QQmlApplicationEngine engine;
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        connect(&engine, &QQmlApplicationEngine::warnings, this, [&](const QList<QQmlError> &errors) {
            for (const auto &error : errors)
                warnings.append(error.toString());
        });
        engine.load(QUrl::fromLocalFile(QString::fromUtf8(SOCIETY_PROJECTS_QML_FILE)));
        QVERIFY2(!engine.rootObjects().isEmpty(), qPrintable(warnings.join('\n')));
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        auto *workspace = window->findChild<ProjectWorkspace *>("testProjectWorkspace");
        QVERIFY(workspace);
        workspace->setActorId("urn:user:creator");
        workspace->setContainerPath(fixture.path());
        QTRY_VERIFY_WITH_TIMEOUT(!workspace->busy(), 15000);
        auto *newButton = visual(window->contentItem(), "newProject");
        QVERIFY(newButton);
        QVERIFY(QMetaObject::invokeMethod(newButton, "clicked"));
        auto *dialog = window->findChild<QObject *>("createProjectDialog");
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("opened").toBool());
        auto *nameInput = window->findChild<QObject *>("newProjectName");
        auto *goalInput = window->findChild<QObject *>("newProjectGoal");
        auto *requirementsInput = window->findChild<QObject *>("newProjectRequirements");
        auto *create = window->findChild<QObject *>("confirmCreateProject");
        QVERIFY(nameInput && goalInput && requirementsInput && create);
        QVERIFY(nameInput->setProperty("text", "Interface project"));
        QVERIFY(goalInput->setProperty("text", "Preserve <goal>\nDeliver a usable asset"));
        QVERIFY(requirementsInput->setProperty("text", "Concept; Rig"));
        QVERIFY(QMetaObject::invokeMethod(create, "clicked"));
        QTRY_VERIFY_WITH_TIMEOUT(!workspace->busy(), 15000);
        QVERIFY2(workspace->errorString().isEmpty(), qPrintable(workspace->errorString()));
        QCOMPARE(workspace->project().value("goal").toString(), QString("Preserve <goal>\nDeliver a usable asset"));
        QCOMPARE(workspace->project().value("requirements").toStringList(), QStringList({"Concept", "Rig"}));
        const auto revision = workspace->project().value("revision").toString();
        auto *navigator = visual(window->contentItem(), "projectNavigator");
        auto *studio = visual(window->contentItem(), "projectStudioPane");
        auto *inspector = visual(window->contentItem(), "projectInspector");
        QVERIFY(navigator && studio && inspector);
        QVERIFY(navigator->isVisible() && studio->isVisible() && inspector->isVisible());
        QVERIFY(!visual(window->contentItem(), "projectsViewSelector"));
        QCOMPARE(workspace->viewMode(), QString("studio"));
        QCOMPARE(workspace->project().value("revision").toString(), revision);
        auto *search = visual(window->contentItem(), "projectSearch");
        QVERIFY(search);
        QVERIFY(search->setProperty("text", "missing project"));
        QTRY_COMPARE(navigator->property("visibleCount").toInt(), 0);
        QVERIFY(search->setProperty("text", "interface"));
        QTRY_COMPARE(navigator->property("visibleCount").toInt(), 1);
        auto *view = visual(window->contentItem(), "projectsView");
        QVERIFY(view);
        QVERIFY(view->setProperty("statusFilter", "completed"));
        QTRY_COMPARE(navigator->property("visibleCount").toInt(), 0);
        QVERIFY(view->setProperty("statusFilter", "draft"));
        QTRY_COMPARE(navigator->property("visibleCount").toInt(), 1);
        QVERIFY(view->setProperty("statusFilter", ""));
        QCOMPARE(workspace->project().value("revision").toString(), revision);
        auto *projectRow = visual(window->contentItem(), "projectNavigatorRow0");
        QVERIFY(projectRow);
        QSignalSpy projectChanges(workspace, &ProjectWorkspace::changed);
        projectRow->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(!workspace->busy(), 15000);
        QVERIFY(!projectChanges.isEmpty());
        QCOMPARE(workspace->project().value("revision").toString(), revision);
        auto *addResource = visual(window->contentItem(), "projectStudioAddResource");
        auto *resourceForm = visual(window->contentItem(), "projectAddResourceForm");
        QVERIFY(addResource && resourceForm);
        QVERIFY(QMetaObject::invokeMethod(addResource, "clicked"));
        QVERIFY(resourceForm->property("checked").toBool());
        auto *goalEditor = visual(window->contentItem(), "projectGoal");
        QVERIFY(goalEditor);
        auto *save = visual(window->contentItem(), "saveProject");
        QVERIFY(save);
        QVERIFY(!save->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(goalEditor, "textEdited", Q_ARG(QString, "Edit through the inspector")));
        QVERIFY(save->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(save, "clicked"));
        QTRY_VERIFY_WITH_TIMEOUT(!workspace->busy(), 15000);
        QVERIFY2(workspace->errorString().isEmpty(), qPrintable(workspace->errorString()));
        QCOMPARE(workspace->project().value("goal").toString(), QString("Edit through the inspector"));
        workspace->addResource({{"name", "Missing source"}, {"purpose", "Validate availability"},
                                {"locator", fixture.filePath("missing.txt")}, {"location", "reference"}});
        QTRY_VERIFY_WITH_TIMEOUT(!workspace->busy(), 15000);
        QVERIFY2(workspace->errorString().isEmpty(), qPrintable(workspace->errorString()));
        workspace->validateProject(true);
        QTRY_VERIFY_WITH_TIMEOUT(!workspace->busy(), 15000);
        QVERIFY(!workspace->validation().value("warnings").toStringList().isEmpty());
        auto *validationWarnings = visual(window->contentItem(), "projectValidationWarnings");
        QVERIFY(validationWarnings);
        QVERIFY(!validationWarnings->property("text").toString().isEmpty());
        QVERIFY(validationWarnings->property("visible").toBool());
        auto *toggle = visual(window->contentItem(), "projectInspectorToggle");
        QVERIFY(view && toggle);
        window->resize(700, 700);
        QTRY_VERIFY(toggle->isVisible());
        QVERIFY(QMetaObject::invokeMethod(toggle, "clicked"));
        QVERIFY(view->property("showInspector").toBool());
        QVERIFY(QMetaObject::invokeMethod(toggle, "clicked"));
        QVERIFY(!view->property("showInspector").toBool());
        QCoreApplication::processEvents();
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        window->close();
    }
};
QTEST_MAIN(ProjectTests)
#include "tst_projects.moc"

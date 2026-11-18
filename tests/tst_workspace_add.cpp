#include "Daemon/WorkspaceAddService.h"
#include "Society/Dashboard/ProjectRepository.h"
#include "Society/Semantics/SemanticRepository.h"
#include <iiSocietyContainer>
#include <iiSharedCanvas>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>

class WorkspaceAddTests final : public QObject {
    Q_OBJECT
private slots:
    void creationAndReferencesUseCanonicalStores() {
        QTemporaryDir fixture(QStringLiteral(SOCIETY_TEST_DIRECTORY) + "/add-XXXXXX.societycontainer");
        QVERIFY(fixture.isValid()); QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        const auto call=[&](const QString &action,const QJsonArray &fields,const QJsonArray &selection=QJsonArray{}) {
            return society::workspace::executeAdd(fixture.path(),QStringLiteral("urn:user:test"),
                                                  {{"action",action},{"fields",fields},{"selection",selection}});
        };
        auto result=call("new-project",{"Studio","Finish the artwork"});
        QVERIFY2(result.value("accepted").toBool(),qPrintable(result.value("error").toString()));
        const auto key=result.value("projectKey").toString(); QVERIFY(!key.isEmpty());
        ProjectRepository projects(fixture.path(),QStringLiteral("urn:user:test"));
        auto state=projects.load(key); QCOMPARE(state.project.value("goal").toString(),QStringLiteral("Finish the artwork"));
        result=call("new-canvas",{"Sketch","320","240"});
        QVERIFY2(result.value("accepted").toBool(),qPrintable(result.value("error").toString()));
        const auto path=QDir(fixture.path()).filePath("Files/Sketch.iisc");
        iiSharedCanvas::Document document; const auto opened=iiSharedCanvas::DocumentFile::readSnapshot(path.toStdString(),document);
        QVERIFY2(opened.ok(),opened.message.c_str()); QCOMPARE(document.extent.width,320); QCOMPARE(document.extent.height,240);
        QFile canvas(path); QVERIFY(canvas.open(QIODevice::ReadOnly)); const auto digest=QCryptographicHash::hash(canvas.readAll(),QCryptographicHash::Sha256); canvas.close();
        QVERIFY(!call("new-canvas",{"Sketch","100","100"}).value("accepted").toBool());
        QVERIFY(canvas.open(QIODevice::ReadOnly)); QCOMPARE(QCryptographicHash::hash(canvas.readAll(),QCryptographicHash::Sha256),digest); canvas.close();
        result=call("project-add-selection",{"Studio","Reference image"},{path});
        QVERIFY2(result.value("accepted").toBool(),qPrintable(result.value("error").toString()));
        state=projects.load(key); QCOMPARE(state.resources.size(),1);
        QCOMPARE(state.resources.first().toMap().value("location").toString(),QStringLiteral("reference"));
        QFile second(QDir(fixture.path()).filePath("Files/reference.txt")); QVERIFY(second.open(QIODevice::WriteOnly));
        QCOMPARE(second.write("reference"),qint64(9)); second.close();
        QTemporaryDir externalDirectory(QStringLiteral(SOCIETY_TEST_DIRECTORY) + "/add-external-XXXXXX"); QVERIFY(externalDirectory.isValid());
        const auto external=externalDirectory.filePath("external.txt "); QFile outside(external);
        QVERIFY(outside.open(QIODevice::WriteOnly)); QCOMPARE(outside.write("external"),qint64(8)); outside.close();
        result=call("project-add-resource",{"Studio",external,"External reference"});
        QVERIFY2(result.value("accepted").toBool(),qPrintable(result.value("error").toString()));
        state=projects.load(key); QCOMPARE(state.resources.size(),2);
        QVERIFY(std::any_of(state.resources.cbegin(),state.resources.cend(),[&](const auto &resource) {
            return resource.toMap().value("locator").toString()==external;
        }));
        projects.addEntity(key,state.project.value("revision").toString(),{{"type","iip:CharacterConcept"},{"name","Character"}});
        result=call("project-create-relation",{"Studio","iip:hasAsset","Character","Sketch.iisc"});
        QVERIFY2(result.value("accepted").toBool(),qPrintable(result.value("error").toString()));
        state=projects.load(key); QCOMPARE(state.relations.size(),1);
        result=call("link-resource",{second.fileName(),"example:references"},{path});
        QVERIFY2(result.value("accepted").toBool(),qPrintable(result.value("error").toString()));
        const auto modelPath=QDir(fixture.path()).filePath("Models/Other/reference.safetensors");
        QVERIFY(QDir().mkpath(QFileInfo(modelPath).absolutePath())); QFile model(modelPath);
        QVERIFY(model.open(QIODevice::WriteOnly)); QCOMPARE(model.write("model"),qint64(5)); model.close();
        result=call("link-resource",{modelPath,"example:model"},{path});
        QVERIFY2(result.value("accepted").toBool(),qPrintable(result.value("error").toString()));
        result=call("link-external",{external,"example:external"},{path});
        QVERIFY2(result.value("accepted").toBool(),qPrintable(result.value("error").toString()));
        SemanticRepository semantic(fixture.path(),QStringLiteral("urn:user:test"));
        const auto url=QStringLiteral("https://example.com/reference");
        auto revision=semantic.read(path).value("revision").toString();
        (void)semantic.putRelation(path,QStringLiteral("example:references"),url,{{"note","Keep this metadata"}},revision);
        revision=semantic.read(path).value("revision").toString();
        (void)semantic.setMetadata(path,{{"description","Changed separately"}},revision);
        QVERIFY_THROWS_EXCEPTION(std::exception, (void)semantic.putRelationIfAbsent(path,QStringLiteral("example:references"),url,{},revision));
        result=call("link-url",{"https://example.com/reference","example:references"},{path});
        QVERIFY2(result.value("accepted").toBool(),qPrintable(result.value("error").toString()));
        QCOMPARE(result.value("message").toString(),QStringLiteral("Reference already exists."));
        const auto relations=semantic.relations(path).value("outgoing").toArray(); QCOMPARE(relations.size(),4);
        const auto existing=std::find_if(relations.cbegin(),relations.cend(),[&](const auto &item) { return item.toObject().value("target").toString()==url; });
        QVERIFY(existing!=relations.cend());
        QCOMPARE(existing->toObject().value("metadata").toObject().value("note").toString(),QStringLiteral("Keep this metadata"));
        QVERIFY(!call("link-url",{"file:///tmp/secret","example:references"},{path}).value("accepted").toBool());
    }
    void rejectsInvalidOrAmbiguousRequests() {
        QTemporaryDir fixture(QStringLiteral(SOCIETY_TEST_DIRECTORY) + "/add-invalid-XXXXXX.societycontainer");
        QVERIFY(fixture.isValid()); QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        const auto call=[&](const QString &action,const QJsonArray &fields,const QJsonArray &selection=QJsonArray{}) {
            return society::workspace::executeAdd(fixture.path(),QStringLiteral("urn:user:test"),
                                                  {{"action",action},{"fields",fields},{"selection",selection}});
        };
        QVERIFY(!call("new-project",{"Missing goal",""}).value("accepted").toBool());
        QVERIFY(!call("new-canvas",{"../outside","1","1"}).value("accepted").toBool());
        QVERIFY(!call("new-canvas",{"Invalid","0","100"}).value("accepted").toBool());
        QVERIFY(!call("link-resource",{"/outside","example:references"},{"/outside"}).value("accepted").toBool());
        const auto publicFile=fixture.filePath("Files/resource.txt"); QFile resource(publicFile);
        QVERIFY(resource.open(QIODevice::WriteOnly)); resource.write("resource"); resource.close();
        const auto privateSidecar=fixture.filePath("Files/resource.txt.iisacc.json"); QFile sidecar(privateSidecar);
        QVERIFY(sidecar.open(QIODevice::WriteOnly)); sidecar.write("{}"); sidecar.close();
        QVERIFY(!call("link-resource",{privateSidecar,"example:references"},{publicFile}).value("accepted").toBool());
        QVERIFY(!call("link-external",{fixture.filePath(".society-drive.json"),"example:references"},{publicFile}).value("accepted").toBool());
        ProjectRepository repo(fixture.path(),QStringLiteral("urn:user:test"));
        (void)repo.create({{"name","Protected"},{"goal","Keep internal data private"}});
        QVERIFY(!call("project-add-resource",{"Protected",privateSidecar,"Reference"}).value("accepted").toBool());
        (void)repo.create({{"name","Duplicate"},{"goal","One"}});
        (void)repo.create({{"name","Duplicate"},{"goal","Two"}});
        QVERIFY(!call("project-add-resource",{"Duplicate","/outside","Reference"}).value("accepted").toBool());
    }
    void existingProjectReferenceKeepsItsMetadata() {
        QTemporaryDir fixture(QStringLiteral(SOCIETY_TEST_DIRECTORY) + "/add-project-link-XXXXXX.societycontainer");
        QVERIFY(fixture.isValid()); QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        const auto actor=QStringLiteral("urn:user:test"); ProjectRepository projects(fixture.path(),actor);
        (void)projects.create({{"name","Project"},{"goal","Preserve context"}});
        QDir directory(fixture.filePath("Files/Projects")); const auto packages=directory.entryList({"*.iiproject"},QDir::Dirs|QDir::NoDotAndDotDot);
        QCOMPARE(packages.size(),1); const auto source=directory.filePath(packages.first());
        const auto url=QStringLiteral("https://example.com/project-reference"); const auto predicate=QStringLiteral("example:references");
        SemanticRepository semantic(fixture.path(),actor); const auto revision=semantic.read(source).value("revision").toString();
        (void)semantic.putRelation(source,predicate,url,{{"note","Project provenance"}},revision);
        const auto result=society::workspace::executeAdd(fixture.path(),actor,{{"action","link-url"},{"fields",QJsonArray{url,predicate}},{"selection",QJsonArray{source}}});
        QVERIFY2(result.value("accepted").toBool(),qPrintable(result.value("error").toString()));
        QCOMPARE(result.value("message").toString(),QStringLiteral("Reference already exists."));
        const auto links=semantic.relations(source).value("outgoing").toArray(); QCOMPARE(links.size(),1);
        QCOMPARE(links.first().toObject().value("metadata").toObject().value("note").toString(),QStringLiteral("Project provenance"));
    }
};
QTEST_GUILESS_MAIN(WorkspaceAddTests)
#include "tst_workspace_add.moc"

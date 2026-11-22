#include "Society/Resources/ContainerResourceAdapter.h"
#include "Society/Resources/ResourceInspection.h"
#include <iiSocietyContainer>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>
#include <cstdint>

namespace r=ii::resource;
class ResourceAdapterTest final : public QObject {
    Q_OBJECT
private slots:
    void declarationsRemainUnverified() {
        iiSocietyContainer::ResourceRecord source;
        source.id="asset-a"; source.containerId="store-a"; source.contentRevision="revision-a";
        source.location="files/image.png"; source.localLocator="/disk/work/image.png";
        source.hashes["sha256"]="digest-a";
        source.replicas.push_back({"replica-a","older-revision","remote:/image.png"});
        source.publications.push_back({"post-a","revision-a","community:post-a"});
        const auto result=society::resources::project(source); QVERIFY(result.has_value());
        QCOMPARE(result->resource.id.value,std::string("asset-a"));
        QCOMPARE(result->resource.displayPath,std::string("files/image.png"));
        QVERIFY(result->resource.observedRevision.has_value());
        QCOMPARE(result->resource.observedRevision->value,std::string("revision-a"));
        QVERIFY(result->revision.has_value()); QVERIFY(result->revision->resourceId==result->resource.id);
        QVERIFY(!result->revision->baseRevision); QVERIFY(result->revision->digest);
        QCOMPARE(result->revision->digest->evidence,r::Evidence::observed);
        QCOMPARE(result->locations.size(),std::size_t(1)); QVERIFY(!result->locations[0].id);
        QCOMPARE(result->locations[0].storageId,std::string("store-a"));
        QCOMPARE(result->locations[0].availability,r::Availability::unknown);
        QCOMPARE(result->replicas.size(),std::size_t(1));
        QCOMPARE(result->replicas[0].revisionId.value,std::string("older-revision"));
        QVERIFY(!result->replicas[0].locationId); QCOMPARE(result->replicas[0].state,r::ReplicaState::unknown);
        source.replicas.push_back(source.replicas.front());
        const auto invalid=society::resources::project(source); QVERIFY(!invalid);
        QCOMPARE(invalid.error(),r::Error::duplicate_replica);
    }
    void realContainerKeepsIdentityAcrossRename() {
        QTemporaryDir fixture(QStringLiteral(SOCIETY_TEST_DIRECTORY)+"/resource-adapter-XXXXXX"); QVERIFY(fixture.isValid());
        const auto root=fixture.filePath("drive.societycontainer"); QVERIFY(QDir().mkdir(root));
        QVERIFY(iiSocietyContainer::SocietyDrive::create(root));
        const auto storage=iiSocietyContainer::SharedStorage::open(root); QVERIFY(storage);
        QFile file(root+"/Files/original.txt"); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write("one"),qint64(3)); file.close();
        auto repository=storage->fileRepository(); (void)repository->observe("files/original.txt");
        const auto original=repository->resource("files/original.txt"); QVERIFY(original);
        repository->setReplica("files/original.txt",{"replica-a",original->contentRevision,"device-b:/copy"});
        const auto before=society::resources::project(*repository->resource("files/original.txt")); QVERIFY(before);
        QVERIFY(QFile::rename(root+"/Files/original.txt",root+"/Files/renamed.txt"));
        repository->relocated("files/original.txt","files/renamed.txt",true);
        const auto afterRaw=repository->resource("files/renamed.txt"); QVERIFY(afterRaw);
        const auto after=society::resources::project(*afterRaw); QVERIFY(after);
        QVERIFY(after->resource.id==before->resource.id);
        QVERIFY(after->revision->id==before->revision->id);
        QVERIFY(after->locations[0].locator!=before->locations[0].locator);
        QCOMPARE(after->replicas[0].state,r::ReplicaState::unknown);
        repository.reset();
        const auto inspected=society::resources::inspect(root,"Files/renamed.txt");
        QVERIFY2(inspected.error.isEmpty(),qPrintable(inspected.error));
        QCOMPARE(inspected.snapshot.value("resource").toObject().value("id").toString(),QString::fromStdString(after->resource.id.value));
        QCOMPARE(inspected.snapshot.value("contentRevision").toObject().value("id").toString(),QString::fromStdString(after->revision->id.value));
        QCOMPARE(inspected.snapshot.value("replicas").toArray().at(0).toObject().value("state").toString(),QStringLiteral("unknown"));
        QVERIFY(!society::resources::inspect(root,"Files/../outside").error.isEmpty());
    }
    void replacementAtSamePathDoesNotInheritOldIdentity() {
        QTemporaryDir fixture(QStringLiteral(SOCIETY_TEST_DIRECTORY)+"/resource-replacement-XXXXXX"); QVERIFY(fixture.isValid());
        const auto root=fixture.filePath("drive.societycontainer"); QVERIFY(QDir().mkdir(root));
        QVERIFY(iiSocietyContainer::SocietyDrive::create(root));
        const auto storage=iiSocietyContainer::SharedStorage::open(root); QVERIFY(storage);
        const auto path=root+"/Files/example.txt";
        QFile original(path); QVERIFY(original.open(QIODevice::WriteOnly)); QCOMPARE(original.write("original"),qint64(8)); original.close();
        auto repository=storage->fileRepository(); (void)repository->observe("files/example.txt"); repository.reset();
        QVERIFY(society::resources::inspect(root,"Files/example.txt").error.isEmpty());
        QVERIFY(QFile::rename(path,root+"/Files/parked.txt"));
        QFile replacement(path); QVERIFY(replacement.open(QIODevice::WriteOnly)); QCOMPARE(replacement.write("new"),qint64(3)); replacement.close();
        const auto result=society::resources::inspect(root,"Files/example.txt");
        QVERIFY(!result.error.isEmpty());
        QVERIFY(result.snapshot.isEmpty());
    }
    void modelFilesUseTheSameResourceModel() {
        QTemporaryDir fixture(QStringLiteral(SOCIETY_TEST_DIRECTORY)+"/resource-model-XXXXXX"); QVERIFY(fixture.isValid());
        const auto root=fixture.filePath("drive.societycontainer"); QVERIFY(QDir().mkdir(root));
        QVERIFY(iiSocietyContainer::SocietyDrive::create(root));
        const auto storage=iiSocietyContainer::SharedStorage::open(root); QVERIFY(storage);
        QVERIFY(QDir().mkpath(root+"/Models/Checkpoint"));
        QFile file(root+"/Models/Checkpoint/model.safetensors"); QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("fixture"),qint64(7)); file.close();
        auto repository=storage->fileRepository(); (void)repository->observe("models/Checkpoint/model.safetensors"); repository.reset();
        const auto result=society::resources::inspect(root,"Models/Checkpoint/model.safetensors");
        QVERIFY2(result.error.isEmpty(),qPrintable(result.error));
        QVERIFY(!result.snapshot.value("resource").toObject().value("id").toString().isEmpty());
        QCOMPARE(result.snapshot.value("contentRevision").toObject().value("resourceId"),
                 result.snapshot.value("resource").toObject().value("id"));
    }
    void indexedDuplicateCandidatesKeepIdentityAndExposeUncertainty() {
        QTemporaryDir fixture(QStringLiteral(SOCIETY_TEST_DIRECTORY)+"/duplicate-candidates-XXXXXX"); QVERIFY(fixture.isValid());
        const auto root=fixture.filePath("drive.societycontainer"); QVERIFY(QDir().mkdir(root));
        QVERIFY(iiSocietyContainer::SocietyDrive::create(root));
        const auto storage=iiSocietyContainer::SharedStorage::open(root); QVERIFY(storage);
        QVERIFY(QDir().mkpath(root+"/Models/Other"));
        const auto write=[&](const QString &path,const QByteArray &bytes) {
            QFile file(root+"/"+path);
            return file.open(QIODevice::WriteOnly|QIODevice::Truncate) && file.write(bytes)==bytes.size();
        };
        QVERIFY(write("Files/reference.txt","same bytes"));
        QVERIFY(write("Files/copy.txt","same bytes"));
        QVERIFY(write("Files/stale.txt","same bytes"));
        QVERIFY(write("Files/unindexed.txt","same bytes"));
        QVERIFY(write("Files/different.txt","other bytes"));
        QVERIFY(write("Models/Other/copy.bin","same bytes"));
        std::uint64_t copyVersion=0;
        {
            auto repository=storage->fileRepository();
            for (const auto &path:{"files/reference.txt","files/copy.txt","files/stale.txt",
                                   "files/different.txt","models/Other/copy.bin"}) (void)repository->observe(path);
            copyVersion=repository->lookup("files/copy.txt")->version;
        }
        QVERIFY(write("Files/stale.txt","changed after indexing"));
        QJsonObject cursor, firstCursor; QSet<QString> paths, identities; int skipped=0;
        for (int page=0;page<10;++page) {
            const auto result=society::resources::duplicateCandidates(root,"Files/reference.txt",1,cursor);
            QVERIFY2(result.error.isEmpty(),qPrintable(result.error));
            QCOMPARE(result.snapshot.value("evidence").toString(),QStringLiteral("matching_last_observed_sha256"));
            QCOMPARE(result.snapshot.value("completeness").toString(),QStringLiteral("unknown"));
            skipped+=result.snapshot.value("skipped").toInt();
            const auto reference=result.snapshot.value("reference").toObject();
            QVERIFY(!reference.value("resourceId").toString().isEmpty());
            for (const auto &value:result.snapshot.value("candidates").toArray()) {
                const auto candidate=value.toObject();
                const auto id=candidate.value("resourceId").toString();
                QVERIFY(!id.isEmpty()); QVERIFY(id!=reference.value("resourceId").toString());
                QVERIFY(!candidate.value("revisionId").toString().isEmpty());
                QVERIFY(!identities.contains(id)); identities.insert(id);
                paths.insert(candidate.value("path").toString());
            }
            cursor=result.snapshot.value("next").toObject();
            if (firstCursor.isEmpty() && !cursor.isEmpty()) firstCursor=cursor;
            if (cursor.isEmpty()) break;
            QVERIFY2(page<9,"duplicate candidate pagination did not terminate");
        }
        QCOMPARE(paths,QSet<QString>({"Files/copy.txt","Models/Other/copy.bin"}));
        QCOMPARE(identities.size(),2);
        QVERIFY(skipped>=1);
        QVERIFY(!firstCursor.isEmpty());
        auto wrongContainer=firstCursor; wrongContainer.insert("containerId","different-container");
        QVERIFY(!society::resources::duplicateCandidates(root,"Files/reference.txt",1,wrongContainer).error.isEmpty());
        { auto repository=storage->fileRepository({},iiSocietyContainer::FileRepository::Access::ReadOnly);
          QCOMPARE(repository->lookup("files/copy.txt")->version,copyVersion); }
        QVERIFY(!society::resources::duplicateCandidates(root,"Files/../outside",20,{}).error.isEmpty());
        QVERIFY(!society::resources::duplicateCandidates(root,"Files/unindexed.txt",20,{}).error.isEmpty());
        QVERIFY(!society::resources::duplicateCandidates(root,"Files/reference.txt",0,{}).error.isEmpty());
        QVERIFY(write("Files/later.txt","same bytes"));
        { auto repository=storage->fileRepository(); (void)repository->observe("files/later.txt"); }
        QVERIFY(!society::resources::duplicateCandidates(root,"Files/reference.txt",1,firstCursor).error.isEmpty());
        QVERIFY(write("Files/reference.txt","new content"));
        QVERIFY(!society::resources::duplicateCandidates(root,"Files/reference.txt",20,firstCursor).error.isEmpty());
        { auto repository=storage->fileRepository(); (void)repository->observe("files/reference.txt"); }
        QVERIFY(!society::resources::duplicateCandidates(root,"Files/reference.txt",20,firstCursor).error.isEmpty());
        QFile copy(root+"/Files/copy.txt"); QVERIFY(copy.open(QIODevice::ReadOnly)); QCOMPARE(copy.readAll(),QByteArray("same bytes"));
    }
};
QTEST_GUILESS_MAIN(ResourceAdapterTest)
#include "tst_resource_adapter.moc"

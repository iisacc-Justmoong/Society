#include "Society/Content/ContentIndex.h"
#include "Society/Content/FileDiscovery.h"
#include "Daemon/ContentIndexScheduler.h"
#include <iiSocietyContainer>
#include <iiFileProviderDatabase.h>
#include <DirectoryMetadataIndex.h>
#include <FileCatalog.h>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QLockFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTest>
#include <functional>
#include <atomic>
#include <stdexcept>

namespace content = society::content;

// A deterministic model double makes the test exercise content relevance
// without loading a model or asserting a particular embedding model's quality.
class FixtureEmbedder final : public content::Embedder {
public:
    QString modelRevision = QStringLiteral("fixture-weights-v1");
    QStringList inputs;
    std::atomic<int> calls{0};
    bool fail = false;
    bool failRevision = false;
    QString failFor;
    bool zero = false;
    int failAt = 0;
    std::function<void()> afterEmbed;
    std::function<void()> afterRevision;
    QString revision(const QString &, std::stop_token) override {
        if (failRevision) throw std::runtime_error("fixture model is unavailable");
        if (afterRevision) afterRevision();
        return modelRevision;
    }
    QList<float> embed(const QString &, const QString &text, std::stop_token) override {
        inputs.append(text);
        ++calls;
        if (fail || (!failFor.isEmpty() && text.contains(failFor)) || (failAt>0 && inputs.size()==failAt))
            throw std::runtime_error("fixture model failed");
        if (afterEmbed) afterEmbed();
        if (zero) return {0.0f, 0.0f};
        if (text.contains("ocean") || text.contains("waves")) return {1.0f, 0.0f};
        return {0.0f, 1.0f};
    }
};

class ContentIndexTest final : public QObject {
    Q_OBJECT
private slots:
    // An old provider must be migrated before background discovery may assign
    // a second metadata owner to any resident file in that container.
    void hashDiscoveryWaitsForLegacyStoreMigration() {
        QTemporaryDir fixture(QStringLiteral(SOCIETY_TEST_DIRECTORY)+"/hash-unmigrated-XXXXXX");
        QVERIFY(fixture.isValid());
        const auto root=fixture.filePath("drive.societycontainer"); QVERIFY(QDir().mkdir(root));
        const auto drive=iiSocietyContainer::SocietyDrive::create(root); QVERIFY(drive);
        QFile file(root+"/Files/legacy.bin"); QVERIFY(file.open(QIODevice::WriteOnly)); QVERIFY(file.write("legacy")>0); file.close();
        const auto native=std::filesystem::path(root.toStdU16String());
        const auto legacy=native/".society-file-provider"/std::string(64,'a');
        std::filesystem::create_directories(legacy);
        { iiFileProvider::ObjectStore store(legacy,drive->storageIdentifier().toStdString(),iiFileProvider::ObjectStore::Access::Create); }
        content::FileDiscovery discovery(root,[](const QString &,quint64) {return true;},content::FileDiscovery::Mode::HashMetadata);
        bool failed=false;
        for (int attempt=0;attempt<10 && !failed;++attempt) {
            const auto batch=discovery.step(false); failed=!batch.error.isEmpty() || batch.status.value("failed").toInt()>0;
        }
        QVERIFY(failed);
        QVERIFY(!QFileInfo::exists(root+"/Files/legacy.bin.iisacc.json"));
        QVERIFY(!QFileInfo::exists(root+"/.iisacc-file-provider/metadata-paths.sqlite3"));
    }
    // A missing legacy sidecar does not authorize creating a competing owner
    // for the file still identified by an existing catalog record.
    void hashDiscoveryPreservesLegacyCatalogOwnership() {
        QTemporaryDir fixture(QStringLiteral(SOCIETY_TEST_DIRECTORY)+"/hash-legacy-XXXXXX");
        QVERIFY(fixture.isValid());
        const auto root=fixture.filePath("drive.societycontainer"); QVERIFY(QDir().mkdir(root));
        QVERIFY(iiSocietyContainer::SocietyDrive::create(root));
        const auto file=root+"/Files/legacy.bin";
        QFile source(file); QVERIFY(source.open(QIODevice::WriteOnly)); QVERIFY(source.write("legacy")>0); source.close();
        {
            iiFileProvider::FileCatalog catalog(std::filesystem::path((root+"/Files").toStdU16String()));
            QVERIFY(catalog.registerFile("legacy.bin").metadataWritten);
        }
        QVERIFY(QFile::remove(file+".iisacc.json"));
        content::FileDiscovery discovery(root,[](const QString &,quint64) {return true;},content::FileDiscovery::Mode::HashMetadata);
        bool failed=false;
        for (int attempt=0;attempt<10 && !failed;++attempt) {
            const auto batch=discovery.step(false); QVERIFY2(batch.error.isEmpty(),qPrintable(batch.error));
            failed=batch.status.value("failed").toInt()>0;
        }
        QVERIFY(failed);
        QVERIFY(!QFileInfo::exists(file+".iisacc.json"));
        QVERIFY(!QDir(root+"/.iisacc-file-provider").exists());
    }
    // Launch and filesystem events schedule bounded hashes without loading a
    // model, embedding file content, or storing payloads in an object database.
    void schedulerLazilyIndexesOnlyHashAndDirectoryMetadata() {
        QTemporaryDir fixture(QStringLiteral(SOCIETY_TEST_DIRECTORY)+"/hash-events-XXXXXX");
        QVERIFY(fixture.isValid());
        const auto root=fixture.filePath("drive.societycontainer"); QVERIFY(QDir().mkdir(root));
        QVERIFY(iiSocietyContainer::SocietyDrive::create(root));
        const auto path=root+"/Files/sample.bin";
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write("first bytes"),qint64(11)); file.close();
        std::atomic<int> providers{0};
        ContentIndexScheduler scheduler([&] { ++providers; return content::EmbedderAccess{}; },{},100);
        scheduler.selectContainer(root);
        const auto current=[&](const QByteArray &bytes) {
            try {
                const auto native=std::filesystem::path(root.toStdU16String());
                if (!std::filesystem::exists(native/".iisacc-file-provider/metadata-paths.sqlite3")) return false;
                iiFileProvider::DirectoryMetadataIndex index(native,iiFileProvider::DirectoryMetadataIndex::Access::ReadOnly);
                const auto digest=index.currentHash("Files/sample.bin");
                return digest && *digest==QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex().toStdString();
            } catch (const std::exception &) { return false; }
        };
        QTRY_VERIFY_WITH_TIMEOUT(current("first bytes"),30000);
        QTRY_VERIFY_WITH_TIMEOUT(!scheduler.status().value("busy").toBool(),30000);
        QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::Truncate)); QCOMPARE(file.write("changed bytes"),qint64(13)); file.close();
        QTRY_VERIFY_WITH_TIMEOUT(current("changed bytes"),30000);
        QCOMPARE(providers.load(),0);
        QVERIFY(!QDir(root+"/.society-file-provider").exists());
        QVERIFY(!QDir(root+"/.society-runtime/content-index").exists());
        scheduler.selectContainer({});
    }
    // File indexing is limited to hashes and per-directory metadata. The old
    // text embedding endpoints must not read, embed, or persist file content.
    void fileIndexPolicyRejectsContentEmbeddings() {
        QTemporaryDir fixture(QStringLiteral(SOCIETY_TEST_DIRECTORY)+"/content-disabled-XXXXXX");
        QVERIFY(fixture.isValid());
        const auto root=fixture.filePath("drive.societycontainer"); QVERIFY(QDir().mkdir(root));
        QVERIFY(iiSocietyContainer::SocietyDrive::create(root));
        QFile file(root+"/Files/private.txt"); QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.write("private file text")>0); file.close();
        FixtureEmbedder embedder; const content::Profile profile{"model://fixture",{}, {}};
        QVERIFY(!content::indexText(root,"Files/private.txt",profile,embedder).error.isEmpty());
        QVERIFY(!content::searchText(root,"private",profile,embedder).error.isEmpty());
        QVERIFY(!content::indexTrackedTextBatch(root,profile,embedder).error.isEmpty());
        QVERIFY(!content::setTrackedTextPolicy(root,profile,&embedder).error.isEmpty());
        QCOMPARE(embedder.calls.load(),0);
        QVERIFY(!QDir(root+"/.society-file-provider").exists());
        QVERIFY(!QDir(root+"/.society-runtime/content-index").exists());
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(),QByteArray("private file text"));
    }

    // A previously saved automatic policy cannot restart content embedding;
    // inspecting the policy must preserve its existing on-disk record.
    void legacyContentPolicyDoesNotStartEmbedding() {
        QTemporaryDir fixture(QStringLiteral(SOCIETY_TEST_DIRECTORY)+"/content-policy-disabled-XXXXXX");
        QVERIFY(fixture.isValid());
        const auto root=fixture.filePath("drive.societycontainer"); QVERIFY(QDir().mkdir(root));
        QVERIFY(iiSocietyContainer::SocietyDrive::create(root));
        const auto storage=iiSocietyContainer::SharedStorage::open(root); QVERIFY(storage);
        QString error; QVERIFY(!storage->ensureRuntimeDirectory("content-index",&error).isEmpty());
        const auto path=storage->runtimePath("content-index/queue-v1.sqlite3",&error); QVERIFY(!path.isEmpty());
        namespace sql=iiFileProvider::native::sql;
        sql::Options options; options.mode=sql::OpenMode::create;
        {
            auto opened=sql::Database::open(std::filesystem::path(path.toStdU16String()),options); QVERIFY(opened);
            auto &db=*opened;
            QVERIFY(db.execute("CREATE TABLE progress(scope TEXT PRIMARY KEY,baseline_after INTEGER NOT NULL,baseline_done INTEGER NOT NULL,journal_after INTEGER NOT NULL)"));
            QVERIFY(db.execute("CREATE TABLE pending(scope TEXT NOT NULL,resource_id TEXT NOT NULL,version INTEGER NOT NULL,attempts INTEGER NOT NULL,retry_at INTEGER NOT NULL,PRIMARY KEY(scope,resource_id))"));
            QVERIFY(db.execute("CREATE TABLE policy(singleton INTEGER PRIMARY KEY,model TEXT NOT NULL,document_prefix TEXT NOT NULL,query_prefix TEXT NOT NULL)"));
            QVERIFY(db.execute("INSERT INTO policy VALUES(1,'model://fixture','','')"));
            QVERIFY(db.execute("PRAGMA application_id=1396920664")); QVERIFY(db.execute("PRAGMA user_version=2"));
        }
        const auto policy=content::trackedTextPolicy(root);
        QVERIFY2(policy.error.isEmpty(),qPrintable(policy.error));
        QVERIFY(!policy.value.value("enabled").toBool());
        QVERIFY(policy.value.value("configured").toBool());
        std::atomic<int> providers{0}; auto embedder=std::make_shared<FixtureEmbedder>();
        ContentIndexScheduler scheduler([&] { ++providers; return content::EmbedderAccess{embedder,{}}; },{},100);
        scheduler.selectContainer(root);
        QTRY_COMPARE_WITH_TIMEOUT(scheduler.status().value("mode").toString(),QStringLiteral("hash_metadata_only"),10000);
        QCOMPARE(providers.load(),0); QCOMPARE(embedder->calls.load(),0);
        auto opened=sql::Database::open(std::filesystem::path(path.toStdU16String()),options); QVERIFY(opened);
        auto query=opened->prepare("SELECT model FROM policy WHERE singleton=1"); QVERIFY(query);
        const auto row=query->step(); QVERIFY(row && *row==sql::Step::row);
        const auto model=query->value(0); QVERIFY(model && std::get<std::string>(*model)=="model://fixture");
    }

    // Event-driven discovery admits only a path; text is read from the
    // resident file when indexing or validating a search hit.
    void indexesLazilyDiscoveredFileWithoutPayloadCopy() {
        if (!content::fileContentEmbeddingEnabled()) QSKIP("File-content embeddings are disabled by the hash-metadata-only policy.");
        QTemporaryDir fixture(QStringLiteral(SOCIETY_TEST_DIRECTORY)+"/content-lazy-XXXXXX");
        QVERIFY(fixture.isValid());
        const auto root=fixture.filePath("drive.societycontainer"); QVERIFY(QDir().mkdir(root));
        QVERIFY(iiSocietyContainer::SocietyDrive::create(root));
        QFile file(root+"/Files/live.txt"); QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("ocean lazy content"),qint64(18));file.close();
        content::FileDiscovery discovery(root,content::isTextCandidate);
        bool discovered=false;
        for(int attempt=0;attempt<20 && !discovered;++attempt) {
            const auto batch=discovery.step(false);QVERIFY2(batch.error.isEmpty(),qPrintable(batch.error));
            discovered=batch.files.contains(file.fileName());
        }
        QVERIFY(discovered);
        QVERIFY(!QFileInfo::exists(file.fileName()+".iisacc.json"));
        QVERIFY(!QDir(root+"/.society-file-provider").exists());
        QVERIFY(!QDir(root+"/.iisacc-file-provider").exists());
        FixtureEmbedder embedder;const content::Profile profile{"model://fixture",{}, {}};
        const auto result=content::indexText(root,"Files/live.txt",profile,embedder,fixture.filePath("missing-stage"));
        QVERIFY2(result.error.isEmpty(),qPrintable(result.error));
        QVERIFY(!QFileInfo::exists(fixture.filePath("missing-stage")));
        const auto found=content::searchText(root,"waves",profile,embedder,10);
        QVERIFY2(found.error.isEmpty(),qPrintable(found.error));
        QCOMPARE(found.value.value("items").toArray().size(),1);
        QCOMPARE(found.value.value("items").toArray().first().toObject().value("path").toString(),QStringLiteral("Files/live.txt"));
        QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::Truncate));QCOMPARE(file.write("forest replacement"),qint64(18));file.close();
        const auto stale=content::searchText(root,"waves",profile,embedder,10);
        QVERIFY2(stale.error.isEmpty(),qPrintable(stale.error));QVERIFY(stale.value.value("items").toArray().isEmpty());
        const auto updated=content::indexText(root,"Files/live.txt",profile,embedder);
        QVERIFY2(updated.error.isEmpty(),qPrintable(updated.error));
        const auto current=content::searchText(root,"trees",profile,embedder,10);
        QVERIFY2(current.error.isEmpty(),qPrintable(current.error));QCOMPARE(current.value.value("items").toArray().size(),1);
        QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::Truncate)); QVERIFY(file.write("ocean before inference")>0); file.close();
        bool changedDuringInference=false;
        embedder.afterEmbed=[&] {
            if (changedDuringInference) return;
            changedDuringInference=true;
            QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::Truncate));
            QVERIFY(file.write("forest after inference")>0); file.close();
        };
        const auto raced=content::indexText(root,"Files/live.txt",profile,embedder);
        QVERIFY(changedDuringInference); QVERIFY(!raced.error.isEmpty()); embedder.afterEmbed={};
        const auto rejected=content::searchText(root,"trees",profile,embedder,10);
        QVERIFY2(rejected.error.isEmpty(),qPrintable(rejected.error)); QVERIFY(rejected.value.value("items").toArray().isEmpty());
        QVERIFY(!QDir(root+"/.society-file-provider").exists());
    }

    // Existing directory-owned metadata is loaded only for the selected file;
    // replacing that file cannot make its old vectors describe the newcomer.
    void indexesDirectoryOwnedFileWithoutCapturingPayload() {
        if (!content::fileContentEmbeddingEnabled()) QSKIP("File-content embeddings are disabled by the hash-metadata-only policy.");
        QTemporaryDir fixture(QStringLiteral(SOCIETY_TEST_DIRECTORY)+"/content-owned-XXXXXX");
        QVERIFY(fixture.isValid());
        const auto root=fixture.filePath("drive.societycontainer"); QVERIFY(QDir().mkdir(root));
        QVERIFY(iiSocietyContainer::SocietyDrive::create(root));
        QFile file(root+"/Files/owned.txt"); QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("ocean owned text"),qint64(16)); file.close();
        iiFileProvider::DirectoryMetadataIndex index(std::filesystem::path(root.toStdU16String()),
            iiFileProvider::DirectoryMetadataIndex::Access::Create);
        (void)index.observeAsset("Files/owned.txt");
        FixtureEmbedder embedder; const content::Profile profile{"model://fixture",{}, {}};
        const auto indexed=content::indexText(root,"Files/owned.txt",profile,embedder);
        QVERIFY2(indexed.error.isEmpty(),qPrintable(indexed.error));
        QVERIFY(indexed.value.value("resourceId").toString().startsWith("file-"));
        const auto before=content::searchText(root,"waves",profile,embedder,10);
        QVERIFY2(before.error.isEmpty(),qPrintable(before.error)); QCOMPARE(before.value.value("items").toArray().size(),1);
        QSaveFile replacement(file.fileName()); QVERIFY(replacement.open(QIODevice::WriteOnly));
        QVERIFY(replacement.write("ocean different")>0); QVERIFY(replacement.commit());
        const auto after=content::searchText(root,"waves",profile,embedder,10);
        QVERIFY2(after.error.isEmpty(),qPrintable(after.error)); QVERIFY(after.value.value("items").toArray().isEmpty());
        QVERIFY(!QDir(root+"/.society-file-provider").exists());
    }

    // The real scheduler uses its event loop to discover an initial file and
    // a later filesystem event, without creating an object payload store.
    void schedulerIndexesFilesThroughTheEventLoop() {
        if (!content::fileContentEmbeddingEnabled()) QSKIP("File-content embeddings are disabled by the hash-metadata-only policy.");
        QTemporaryDir fixture(QStringLiteral(SOCIETY_TEST_DIRECTORY)+"/content-scheduler-XXXXXX");
        QVERIFY(fixture.isValid());
        const auto root=fixture.filePath("drive.societycontainer"); QVERIFY(QDir().mkdir(root));
        QVERIFY(iiSocietyContainer::SocietyDrive::create(root));
        QFile first(root+"/Files/first.txt"); QVERIFY(first.open(QIODevice::WriteOnly));
        QVERIFY(first.write("ocean event")>0); first.close();
        auto embedder=std::make_shared<FixtureEmbedder>();
        const content::Profile profile{"model://fixture",{}, {}};
        const auto policy=content::setTrackedTextPolicy(root,profile,embedder.get());
        QVERIFY2(policy.error.isEmpty(),qPrintable(policy.error));
        ContentIndexScheduler scheduler([embedder] { return content::EmbedderAccess{embedder,{}}; },{},100);
        scheduler.selectContainer(root);
        QTRY_VERIFY_WITH_TIMEOUT(embedder->calls.load()>=1 && !scheduler.status().value("busy").toBool(),30000);
        QFile second(root+"/Files/second.txt"); QVERIFY(second.open(QIODevice::WriteOnly));
        QVERIFY(second.write("forest event")>0); second.close();
        QTRY_VERIFY_WITH_TIMEOUT(embedder->calls.load()>=2 && !scheduler.status().value("busy").toBool(),30000);
        scheduler.selectContainer({});
        const auto found=content::searchText(root,"trees",profile,*embedder,10);
        QVERIFY2(found.error.isEmpty(),qPrintable(found.error));
        QCOMPARE(found.value.value("items").toArray().size(),2);
        QVERIFY(!QDir(root+"/.society-file-provider").exists());
        QVERIFY(!QFileInfo::exists(first.fileName()+".iisacc.json"));
        QVERIFY(!QFileInfo::exists(second.fileName()+".iisacc.json"));
    }
    // An iterator retained across slices must not start watching or ingesting
    // a different tree if one of its ancestors is replaced with a symlink.
    void externalIntakeRejectsAReplacedParent() {
#ifdef Q_OS_WIN
        QSKIP("Creating directory symlinks may require Windows developer privileges.");
#else
        QTemporaryDir fixture(QStringLiteral(SOCIETY_TEST_DIRECTORY)+"/content-parent-XXXXXX");
        QVERIFY(fixture.isValid());
        const auto root=fixture.filePath("drive.societycontainer"),outside=fixture.filePath("outside");
        QVERIFY(QDir().mkdir(root)); QVERIFY(iiSocietyContainer::SocietyDrive::create(root));
        const auto storage=iiSocietyContainer::SharedStorage::open(root); QVERIFY(storage);
        const auto branch=root+"/Files/branch";
        for (int d=0;d<5;++d) {
            const auto sub=QString("/folder%1").arg(d);
            QVERIFY(QDir().mkpath(branch+sub)); QVERIFY(QDir().mkpath(outside+sub));
            for (int f=0;f<5;++f) {
                const auto name=QString("/file%1.txt").arg(f);
                QFile inside(branch+sub+name); QVERIFY(inside.open(QIODevice::WriteOnly)); inside.write("inside"); inside.close();
                QFile external(outside+sub+name); QVERIFY(external.open(QIODevice::WriteOnly)); external.write("outside"); external.close();
            }
        }
        content::FileDiscovery intake(root,content::isTextCandidate);
        const auto first=intake.step(false); QVERIFY2(first.error.isEmpty(),qPrintable(first.error));
        QCOMPARE(first.status.value("observed").toInt(),1);
        QCOMPARE(first.status.value("state").toString(),QStringLiteral("working"));
        QVERIFY(QDir().rename(branch,fixture.filePath("retained-branch")));
        QVERIFY(QFile::link(outside,branch));
        bool idle=false,sawFailure=false;
        for (int i=0;i<100 && !idle;++i) {
            const auto batch=intake.step(false); QVERIFY2(batch.error.isEmpty(),qPrintable(batch.error));
            sawFailure |= batch.status.value("failed").toInt()>0;
            QVERIFY(batch.files.isEmpty());
            const auto externalRoot=QFileInfo(outside).canonicalFilePath();
            for (const auto &directory:batch.directories) {
                const auto canonical=QFileInfo(directory).canonicalFilePath();
                QVERIFY(canonical!=externalRoot && !canonical.startsWith(externalRoot+"/"));
            }
            idle=batch.status.value("state")=="idle";
        }
        QVERIFY(idle); QVERIFY(sawFailure);
        QVERIFY(!QDir(root+"/.society-file-provider").exists());
#endif
    }

    // A scan must be bounded and preserve paths that need a different owner
    // or explicit reconciliation, while still making progress on valid files.
    void externalIntakePreservesUnsupportedAndConflictingEntries() {
        QTemporaryDir fixture(QStringLiteral(SOCIETY_TEST_DIRECTORY)+"/content-intake-XXXXXX");
        QVERIFY(fixture.isValid());
        const auto root=fixture.filePath("drive.societycontainer"); QVERIFY(QDir().mkdir(root));
        QVERIFY(iiSocietyContainer::SocietyDrive::create(root));
        const auto storage=iiSocietyContainer::SharedStorage::open(root); QVERIFY(storage);
        const auto write=[&](const QString &path,const QByteArray &value) {
            QFile file(path); return file.open(QIODevice::WriteOnly|QIODevice::Truncate) && file.write(value)==value.size();
        };
        QVERIFY(QDir().mkpath(root+"/Files/Project.iiproject/assets"));
        QVERIFY(write(root+"/Files/Project.iiproject/assets/private.txt","project data"));
        QVERIFY(write(root+"/Files/.hidden.txt","hidden data"));
        QVERIFY(write(root+"/Files/legacy.md","legacy document"));
        const QByteArray legacy("unrecognized legacy companion");
        QVERIFY(write(root+"/Files/legacy.md.iisacc.json",legacy));
        QVERIFY(write(root+"/Files/valid.txt","a visible document"));
        QVERIFY(write(root+"/Files/owner.txt","original owner"));
        { auto repo=storage->fileRepository(); (void)repo->observe("files/owner.txt"); }
        const auto metadata=root+"/.society-runtime/classifications/files/";
        QFile owner(metadata+"owner.txt.iisacc.json"); QVERIFY(owner.open(QIODevice::ReadOnly));
        const auto ownerBytes=owner.readAll(); owner.close(); QVERIFY(!ownerBytes.isEmpty());
        QVERIFY(write(root+"/Files/foreign.txt","a different file"));
        QVERIFY(write(metadata+"foreign.txt.iisacc.json",ownerBytes));
        const auto outside=fixture.filePath("outside.txt"); QVERIFY(write(outside,"external data"));
#ifndef Q_OS_WIN
        QVERIFY(QFile::link(outside,root+"/Files/link.txt"));
#endif
        for (int i=0;i<40;++i) QVERIFY(write(root+QString("/Files/%1.bin").arg(i),"binary"));
        content::FileDiscovery intake(root,content::isTextCandidate);
        std::stop_source cancelled; cancelled.request_stop();
        QVERIFY(!intake.step(false,cancelled.get_token()).error.isEmpty());
        bool idle=false,sawFailure=false; QStringList admitted;
        for (int i=0;i<100 && !idle;++i) {
            const auto batch=intake.step(false);
            QVERIFY2(batch.error.isEmpty(),qPrintable(batch.error));
            admitted.append(batch.logicalFiles);
            QVERIFY(batch.status.value("visited").toInt()<=32);
            QVERIFY(batch.status.value("observed").toInt()<=1);
            sawFailure |= batch.status.value("failed").toInt()>0;
            if (sawFailure) {
                QVERIFY(!batch.status.value("lastError").toString().isEmpty());
                QVERIFY(batch.status.value("lastPath").toString().startsWith("Files/"));
            }
            idle=batch.status.value("state")=="idle";
        }
        QVERIFY(idle); QVERIFY(sawFailure);
        auto repo=storage->fileRepository({},iiSocietyContainer::FileRepository::Access::ReadOnly);
        QVERIFY(admitted.contains("Files/valid.txt"));
        QVERIFY(!admitted.contains("Files/foreign.txt"));
        QVERIFY(!repo->objects().lookupPath("files/valid.txt"));
        QVERIFY(!repo->objects().lookupPath("files/.hidden.txt"));
        QVERIFY(!repo->objects().lookupPath("files/Project.iiproject/assets/private.txt"));
        QVERIFY(!repo->objects().lookupPath("files/legacy.md"));
        QVERIFY(!repo->objects().lookupPath("files/foreign.txt"));
        QVERIFY(!repo->objects().lookupPath("files/link.txt"));
        QFile legacyAfter(root+"/Files/legacy.md.iisacc.json"); QVERIFY(legacyAfter.open(QIODevice::ReadOnly));
        QCOMPARE(legacyAfter.readAll(),legacy); legacyAfter.close();
        QFile foreignAfter(metadata+"foreign.txt.iisacc.json"); QVERIFY(foreignAfter.open(QIODevice::ReadOnly));
        QCOMPARE(foreignAfter.readAll(),ownerBytes); foreignAfter.close();
        QVERIFY(QFile::remove(root+"/Files/valid.txt"));
        const auto next=intake.step(true); QVERIFY2(next.error.isEmpty(),qPrintable(next.error));
        QVERIFY(!QFileInfo::exists(root+"/Files/valid.txt.iisacc.json"));
        const auto original=repo->objects().lookupPath("files/owner.txt"); QVERIFY(original);
        QSaveFile replaced(root+"/Files/owner.txt"); QVERIFY(replaced.open(QIODevice::WriteOnly));
        QVERIFY(replaced.write("atomically replaced")>0); QVERIFY(replaced.commit());
        QVERIFY(write(root+"/Files/fresh.txt","another visible document"));
        const auto changed=intake.step(true); QVERIFY2(changed.error.isEmpty(),qPrintable(changed.error));
        admitted.append(changed.logicalFiles);
        idle=changed.status.value("state")=="idle";
        // Drain without continually adding fresh hints, which would request
        // another complete pass each time.
        for (int i=0;i<100 && !idle;++i) {
            const auto batch=intake.step(false); QVERIFY2(batch.error.isEmpty(),qPrintable(batch.error));
            admitted.append(batch.logicalFiles);
            idle=batch.status.value("state")=="idle";
        }
        QVERIFY(idle); QVERIFY(admitted.contains("Files/fresh.txt"));
        QVERIFY(!repo->objects().lookupPath("files/fresh.txt"));
        const auto originalAfter=repo->objects().lookup(original->key); QVERIFY(originalAfter);
        QCOMPARE(originalAfter->sha256,original->sha256);
        const auto current=repo->objects().lookupPath("files/owner.txt");
        if (current && current->key==original->key) QCOMPARE(current->sha256,original->sha256);
        QFile replacedAfter(root+"/Files/owner.txt"); QVERIFY(replacedAfter.open(QIODevice::ReadOnly));
        QCOMPARE(replacedAfter.readAll(),QByteArray("atomically replaced"));
    }

    void indexesStoredBytesAndRevalidatesCurrentResources() {
        if (!content::fileContentEmbeddingEnabled()) QSKIP("File-content embeddings are disabled by the hash-metadata-only policy.");
        QTemporaryDir fixture(QStringLiteral(SOCIETY_TEST_DIRECTORY)+"/content-index-XXXXXX");
        QVERIFY(fixture.isValid());
        const auto root=fixture.filePath("drive.societycontainer"); QVERIFY(QDir().mkdir(root));
        QVERIFY(iiSocietyContainer::SocietyDrive::create(root));
        const auto storage=iiSocietyContainer::SharedStorage::open(root); QVERIFY(storage);
        const auto write=[&](const QString &name,const QByteArray &bytes) {
            QFile file(root+"/Files/"+name);
            return file.open(QIODevice::WriteOnly|QIODevice::Truncate) && file.write(bytes)==bytes.size();
        };
        QVERIFY(write("first.txt","ocean secret-long-unique-9f15"));
        QVERIFY(write("second.md","forest and trees"));
        { auto repository=storage->fileRepository();
          (void)repository->observe("files/first.txt"); (void)repository->observe("files/second.md"); }
        FixtureEmbedder embedder;
        const content::Profile profile{"model://fixture","document: ","query: "};
        // The source must be read lazily from Files; a nonexistent extraction
        // directory must neither be created nor prevent indexing.
        const auto unusedStage=fixture.filePath("no-extraction-directory");
        const auto first=content::indexText(root,"Files/first.txt",profile,embedder,unusedStage);
        QVERIFY2(first.error.isEmpty(),qPrintable(first.error));
        QVERIFY(!QFileInfo::exists(unusedStage));
        QCOMPARE(first.value.value("state").toString(),QStringLiteral("indexed"));
        const auto second=content::indexText(root,"Files/second.md",profile,embedder,fixture.path());
        QVERIFY2(second.error.isEmpty(),qPrintable(second.error));
        const auto count=embedder.inputs.size();
        const auto unchanged=content::indexText(root,"Files/first.txt",profile,embedder,fixture.path());
        QVERIFY2(unchanged.error.isEmpty(),qPrintable(unchanged.error));
        QCOMPARE(unchanged.value.value("state").toString(),QStringLiteral("current"));
        QCOMPARE(embedder.inputs.size(),count);
        const auto found=content::searchText(root,"waves",profile,embedder,10);
        QVERIFY2(found.error.isEmpty(),qPrintable(found.error));
        QCOMPARE(found.value.value("evidence").toString(),QStringLiteral("local_text_embeddings"));
        QCOMPARE(found.value.value("completeness").toString(),QStringLiteral("unknown"));
        auto items=found.value.value("items").toArray(); QCOMPARE(items.size(),2);
        QCOMPARE(items.first().toObject().value("path").toString(),QStringLiteral("Files/first.txt"));
        QVERIFY(items.first().toObject().value("score").toDouble()>items.last().toObject().value("score").toDouble());
        QVERIFY(embedder.inputs.first().startsWith("document: "));
        QVERIFY(embedder.inputs.last().startsWith("query: "));
        const auto db=storage->runtimePath("content-index/text-v1.sqlite3");
        QFile persisted(db); QVERIFY(persisted.open(QIODevice::ReadOnly));
        QVERIFY(!persisted.readAll().contains("secret-long-unique-9f15")); persisted.close();
        QVERIFY(write("first.txt","changed outside Society"));
        const auto stale=content::searchText(root,"waves",profile,embedder,10);
        QVERIFY2(stale.error.isEmpty(),qPrintable(stale.error));
        QCOMPARE(stale.value.value("items").toArray().size(),1);
        QVERIFY(QFile::rename(root+"/Files/second.md",root+"/Files/renamed.md"));
        { auto repository=storage->fileRepository(); repository->relocated("files/second.md","files/renamed.md",true); }
        const auto moved=content::searchText(root,"trees",profile,embedder,10);
        QVERIFY2(moved.error.isEmpty(),qPrintable(moved.error));
        QCOMPARE(moved.value.value("items").toArray().first().toObject().value("path").toString(),QStringLiteral("Files/renamed.md"));
        embedder.modelRevision="fixture-weights-v2";
        const auto otherModel=content::searchText(root,"waves",profile,embedder,10);
        QVERIFY2(otherModel.error.isEmpty(),qPrintable(otherModel.error));
        QVERIFY(otherModel.value.value("items").toArray().isEmpty());
        embedder.modelRevision="fixture-weights-v1";
        const content::Profile otherPrefix{"model://fixture","different: ","query: "};
        const auto differentSpace=content::searchText(root,"waves",otherPrefix,embedder,10);
        QVERIFY2(differentSpace.error.isEmpty(),qPrintable(differentSpace.error));
        QVERIFY(differentSpace.value.value("items").toArray().isEmpty());
        QVERIFY(QDir(fixture.path()).entryList({"society-content-XXXXXX","society-content-*"},QDir::Dirs|QDir::NoDotAndDotDot).isEmpty());
    }

    void rejectsInvalidInputsAndDoesNotPublishFailedEmbeddings() {
        if (!content::fileContentEmbeddingEnabled()) QSKIP("File-content embeddings are disabled by the hash-metadata-only policy.");
        QTemporaryDir fixture(QStringLiteral(SOCIETY_TEST_DIRECTORY)+"/content-errors-XXXXXX");
        QVERIFY(fixture.isValid());
        const auto root=fixture.filePath("drive.societycontainer"); QVERIFY(QDir().mkdir(root));
        QVERIFY(iiSocietyContainer::SocietyDrive::create(root));
        const auto storage=iiSocietyContainer::SharedStorage::open(root); QVERIFY(storage);
        QFile good(root+"/Files/good.txt"); QVERIFY(good.open(QIODevice::WriteOnly)); good.write("ocean"); good.close();
        QFile binary(root+"/Files/binary.txt"); QVERIFY(binary.open(QIODevice::WriteOnly)); binary.write(QByteArray::fromHex("fffefd")); binary.close();
        QFile unknown(root+"/Files/unindexed.txt"); QVERIFY(unknown.open(QIODevice::WriteOnly)); unknown.write("ocean"); unknown.close();
        { auto repository=storage->fileRepository(); (void)repository->observe("files/good.txt"); (void)repository->observe("files/binary.txt"); }
        FixtureEmbedder embedder; const content::Profile profile{"model://fixture",{}, {}};
        const auto unstarted=content::searchText(root,"waves",profile,embedder,10);
        QVERIFY2(unstarted.error.isEmpty(),qPrintable(unstarted.error));
        QCOMPARE(unstarted.value.value("index").toString(),QStringLiteral("not_started"));
        QVERIFY(!QDir(root+"/.society-runtime/content-index").exists());
        std::stop_source interruptedDuringResolution;
        embedder.afterRevision=[&] { interruptedDuringResolution.request_stop(); };
        QVERIFY(!content::searchText(root,"waves",profile,embedder,10,interruptedDuringResolution.get_token()).error.isEmpty());
        embedder.afterRevision={};
        QVERIFY(!content::indexText(root,"Files/../outside",profile,embedder,fixture.path()).error.isEmpty());
        QFile foreign(root+"/Files/unindexed.txt.iisacc.json"); QVERIFY(foreign.open(QIODevice::WriteOnly));
        QVERIFY(foreign.write("unknown owner")>0); foreign.close();
        QVERIFY(!content::indexText(root,"Files/unindexed.txt",profile,embedder,fixture.path()).error.isEmpty());
        QVERIFY(!content::indexText(root,"Files/binary.txt",profile,embedder,fixture.path()).error.isEmpty());
        QVERIFY(!content::searchText(root,{},profile,embedder,10).error.isEmpty());
        QVERIFY(!content::searchText(root,"waves",profile,embedder,0).error.isEmpty());
        std::stop_source cancelled; cancelled.request_stop();
        QVERIFY(!content::indexText(root,"Files/good.txt",profile,embedder,fixture.path(),cancelled.get_token()).error.isEmpty());
        embedder.zero=true;
        QVERIFY(!content::indexText(root,"Files/good.txt",profile,embedder,fixture.path()).error.isEmpty());
        embedder.zero=false;
        const auto success=content::indexText(root,"Files/good.txt",profile,embedder,fixture.path());
        QVERIFY2(success.error.isEmpty(),qPrintable(success.error));
        embedder.fail=true;
        QVERIFY(!content::searchText(root,"waves",profile,embedder,10).error.isEmpty());
        embedder.fail=false;
        const auto found=content::searchText(root,"waves",profile,embedder,10);
        QVERIFY2(found.error.isEmpty(),qPrintable(found.error)); QCOMPARE(found.value.value("items").toArray().size(),1);
    }

    void sourceAndModelChangesCannotPublishPartialVectors() {
        if (!content::fileContentEmbeddingEnabled()) QSKIP("File-content embeddings are disabled by the hash-metadata-only policy.");
        QTemporaryDir fixture(QStringLiteral(SOCIETY_TEST_DIRECTORY)+"/content-races-XXXXXX"); QVERIFY(fixture.isValid());
        const auto root=fixture.filePath("drive.societycontainer"); QVERIFY(QDir().mkdir(root));
        QVERIFY(iiSocietyContainer::SocietyDrive::create(root));
        const auto storage=iiSocietyContainer::SharedStorage::open(root); QVERIFY(storage);
        const auto write=[&](const QString &name,const QByteArray &bytes) {
            QFile file(root+"/Files/"+name);
            return file.open(QIODevice::WriteOnly|QIODevice::Truncate) && file.write(bytes)==bytes.size();
        };
        QVERIFY(write("baseline.txt","forest")); QVERIFY(write("target.txt",QByteArray(1500,'x')+" ocean"));
        { auto repository=storage->fileRepository(); (void)repository->observe("files/baseline.txt");
          (void)repository->observe("files/target.txt"); }
        FixtureEmbedder embedder; const content::Profile profile{"model://fixture",{}, {}};
        const auto baseline=content::indexText(root,"Files/baseline.txt",profile,embedder,fixture.path());
        QVERIFY2(baseline.error.isEmpty(),qPrintable(baseline.error));
        bool changed=false;
        embedder.afterEmbed=[&] { if (!changed) { changed=true; QVERIFY(write("target.txt","changed externally")); } };
        const auto raced=content::indexText(root,"Files/target.txt",profile,embedder,fixture.path());
        QVERIFY(changed); QVERIFY(!raced.error.isEmpty()); embedder.afterEmbed={};
        const auto afterRace=content::searchText(root,"waves",profile,embedder,10);
        QVERIFY2(afterRace.error.isEmpty(),qPrintable(afterRace.error));
        QCOMPARE(afterRace.value.value("items").toArray().size(),1);
        QVERIFY(write("target.txt",QByteArray(1500,'x')+" ocean"));
        { auto repository=storage->fileRepository(); (void)repository->observe("files/target.txt"); }
        embedder.failAt=embedder.inputs.size()+2;
        const auto failed=content::indexText(root,"Files/target.txt",profile,embedder,fixture.path());
        QVERIFY(!failed.error.isEmpty()); embedder.failAt=0;
        const auto afterFailure=content::searchText(root,"waves",profile,embedder,10);
        QVERIFY2(afterFailure.error.isEmpty(),qPrintable(afterFailure.error));
        QCOMPARE(afterFailure.value.value("items").toArray().size(),1);
        const auto complete=content::indexText(root,"Files/target.txt",profile,embedder,fixture.path());
        QVERIFY2(complete.error.isEmpty(),qPrintable(complete.error)); QVERIFY(complete.value.value("chunks").toInt()>1);
        const auto found=content::searchText(root,"waves",profile,embedder,10);
        QVERIFY2(found.error.isEmpty(),qPrintable(found.error)); QCOMPARE(found.value.value("items").toArray().size(),2);
        QVERIFY(write("target.txt",QByteArray(1200,'y')+" ocean"));
        { auto repository=storage->fileRepository(); (void)repository->observe("files/target.txt"); }
        bool changedModel=false;
        embedder.afterEmbed=[&] { if (!changedModel) { changedModel=true; embedder.modelRevision="fixture-weights-v2"; } };
        const auto wrongModel=content::indexText(root,"Files/target.txt",profile,embedder,fixture.path());
        QVERIFY(changedModel); QVERIFY(!wrongModel.error.isEmpty()); embedder.afterEmbed={};
        const auto newSpace=content::searchText(root,"waves",profile,embedder,10);
        QVERIFY2(newSpace.error.isEmpty(),qPrintable(newSpace.error)); QVERIFY(newSpace.value.value("items").toArray().isEmpty());
        QVERIFY(QDir(fixture.path()).entryList({"society-content-*"},QDir::Dirs|QDir::NoDotAndDotDot).isEmpty());
    }

    void trackedBatchesResumeAndConsumeTheObservedJournal() {
        if (!content::fileContentEmbeddingEnabled()) QSKIP("File-content embeddings are disabled by the hash-metadata-only policy.");
        QTemporaryDir fixture(QStringLiteral(SOCIETY_TEST_DIRECTORY)+"/content-queue-XXXXXX"); QVERIFY(fixture.isValid());
        const auto root=fixture.filePath("drive.societycontainer"); QVERIFY(QDir().mkdir(root));
        QVERIFY(iiSocietyContainer::SocietyDrive::create(root));
        const auto storage=iiSocietyContainer::SharedStorage::open(root); QVERIFY(storage);
        const auto write=[&](const QString &name,const QByteArray &bytes) {
            QFile file(root+"/Files/"+name);
            return file.open(QIODevice::WriteOnly|QIODevice::Truncate) && file.write(bytes)==bytes.size();
        };
        QVERIFY(write("a.txt","ocean queue-private-63ad")); QVERIFY(write("b.txt","forest"));
        QVERIFY(write("invalid.txt",QByteArray::fromHex("fffefd"))); QVERIFY(write("unsupported.bin","ocean"));
        QVERIFY(write("unobserved.txt","ocean"));
        { auto repository=storage->fileRepository();
          for (const auto &path : {"files/a.txt","files/b.txt","files/invalid.txt","files/unsupported.bin"})
              (void)repository->observe(path); }
        FixtureEmbedder embedder; embedder.failFor="ocean";
        const content::Profile profile{"model://fixture","document: ","query: "};
        content::BatchOptions options; options.scanLimit=32; options.workLimit=16; options.nowMs=1000; options.temporaryRoot=fixture.path();
        const auto first=content::indexTrackedTextBatch(root,profile,embedder,options);
        QVERIFY2(first.error.isEmpty(),qPrintable(first.error));
        QVERIFY(first.value.value("baselineComplete").toBool());
        QCOMPARE(first.value.value("pending").toInt(),1);
        QVERIFY(first.value.value("failed").toInt()>=1);
        QVERIFY(first.value.value("skipped").toInt()>=2);
        const auto partial=content::searchText(root,"trees",profile,embedder,10);
        QVERIFY2(partial.error.isEmpty(),qPrintable(partial.error));
        QCOMPARE(partial.value.value("items").toArray().size(),1);
        QCOMPARE(partial.value.value("items").toArray().first().toObject().value("path").toString(),QStringLiteral("Files/b.txt"));
        embedder.failFor.clear(); options.nowMs=1100;
        const auto beforeRetry=content::indexTrackedTextBatch(root,profile,embedder,options);
        QVERIFY2(beforeRetry.error.isEmpty(),qPrintable(beforeRetry.error)); QCOMPARE(beforeRetry.value.value("pending").toInt(),1);
        options.nowMs=5000;
        const auto resumed=content::indexTrackedTextBatch(root,profile,embedder,options);
        QVERIFY2(resumed.error.isEmpty(),qPrintable(resumed.error)); QCOMPARE(resumed.value.value("pending").toInt(),0);
        const auto complete=content::searchText(root,"waves",profile,embedder,10);
        QVERIFY2(complete.error.isEmpty(),qPrintable(complete.error)); QCOMPARE(complete.value.value("items").toArray().size(),2);
        QFile queue(root+"/.society-runtime/content-index/queue-v1.sqlite3"); QVERIFY(queue.open(QIODevice::ReadOnly));
        QVERIFY(!queue.readAll().contains("queue-private-63ad")); queue.close();
        // A newer observed revision must supersede an older delayed failure.
        QVERIFY(write("a.txt","ocean revision"));
        { auto repository=storage->fileRepository(); (void)repository->observe("files/a.txt"); }
        embedder.failFor="ocean"; options.nowMs=6000;
        const auto failedRevision=content::indexTrackedTextBatch(root,profile,embedder,options);
        QVERIFY2(failedRevision.error.isEmpty(),qPrintable(failedRevision.error)); QCOMPARE(failedRevision.value.value("pending").toInt(),1);
        QVERIFY(write("a.txt","forest revised"));
        { auto repository=storage->fileRepository(); (void)repository->observe("files/a.txt"); }
        embedder.failFor.clear(); options.nowMs=6100;
        const auto newerRevision=content::indexTrackedTextBatch(root,profile,embedder,options);
        QVERIFY2(newerRevision.error.isEmpty(),qPrintable(newerRevision.error)); QCOMPARE(newerRevision.value.value("pending").toInt(),0);
        QVERIFY(write("c.txt","ocean replacement"));
        QVERIFY(QFile::remove(root+"/Files/b.txt"));
        { auto repository=storage->fileRepository(); (void)repository->observe("files/c.txt"); repository->removed("files/b.txt"); }
        options.nowMs=10000;
        const auto changed=content::indexTrackedTextBatch(root,profile,embedder,options);
        QVERIFY2(changed.error.isEmpty(),qPrintable(changed.error)); QCOMPARE(changed.value.value("pending").toInt(),0);
        const auto current=content::searchText(root,"waves",profile,embedder,10);
        QVERIFY2(current.error.isEmpty(),qPrintable(current.error)); const auto items=current.value.value("items").toArray(); QCOMPARE(items.size(),2);
        QCOMPARE(items.first().toObject().value("path").toString(),QStringLiteral("Files/c.txt"));
        QCOMPARE(items.last().toObject().value("path").toString(),QStringLiteral("Files/a.txt"));
        embedder.modelRevision="fixture-weights-v2";
        const auto newSpace=content::searchText(root,"waves",profile,embedder,10);
        QVERIFY2(newSpace.error.isEmpty(),qPrintable(newSpace.error)); QVERIFY(newSpace.value.value("items").toArray().isEmpty());
        const auto newBatch=content::indexTrackedTextBatch(root,profile,embedder,options);
        QVERIFY2(newBatch.error.isEmpty(),qPrintable(newBatch.error));
        const auto newResults=content::searchText(root,"waves",profile,embedder,10);
        QVERIFY2(newResults.error.isEmpty(),qPrintable(newResults.error)); QCOMPARE(newResults.value.value("items").toArray().size(),2);
    }

    void interruptedBatchRetainsWorkAndWritersAreExclusive() {
        if (!content::fileContentEmbeddingEnabled()) QSKIP("File-content embeddings are disabled by the hash-metadata-only policy.");
        QTemporaryDir fixture(QStringLiteral(SOCIETY_TEST_DIRECTORY)+"/content-resume-XXXXXX"); QVERIFY(fixture.isValid());
        const auto root=fixture.filePath("drive.societycontainer"); QVERIFY(QDir().mkdir(root));
        QVERIFY(iiSocietyContainer::SocietyDrive::create(root));
        const auto storage=iiSocietyContainer::SharedStorage::open(root); QVERIFY(storage);
        QFile file(root+"/Files/a.txt"); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("ocean"); file.close();
        { auto repository=storage->fileRepository(); (void)repository->observe("files/a.txt"); }
        FixtureEmbedder embedder; const content::Profile profile{"model://fixture",{}, {}};
        content::BatchOptions options; options.scanLimit=8; options.workLimit=8; options.nowMs=1000; options.temporaryRoot=fixture.path();
        std::stop_source stop; embedder.afterEmbed=[&] { stop.request_stop(); };
        const auto stopped=content::indexTrackedTextBatch(root,profile,embedder,options,stop.get_token());
        QVERIFY(!stopped.error.isEmpty()); embedder.afterEmbed={};
        const auto empty=content::searchText(root,"waves",profile,embedder,10);
        QVERIFY2(empty.error.isEmpty(),qPrintable(empty.error)); QVERIFY(empty.value.value("items").toArray().isEmpty());
        const auto retried=content::indexTrackedTextBatch(root,profile,embedder,options);
        QVERIFY2(retried.error.isEmpty(),qPrintable(retried.error)); QCOMPARE(retried.value.value("pending").toInt(),0);
        const auto found=content::searchText(root,"waves",profile,embedder,10);
        QVERIFY2(found.error.isEmpty(),qPrintable(found.error)); QCOMPARE(found.value.value("items").toArray().size(),1);
        QProcess holder;
        auto environment=QProcessEnvironment::systemEnvironment();
        environment.insert("SOCIETY_CONTENT_LOCK_PROBE",root+"/.society-runtime/content-index/text-write.lock");
        holder.setProcessEnvironment(environment);
        holder.start(QCoreApplication::applicationFilePath(),{"holdWriterLockForProbe"});
        QVERIFY(holder.waitForStarted(30000)); QByteArray output;
        QTRY_VERIFY_WITH_TIMEOUT((output+=holder.readAllStandardOutput()).contains("CONTENT_LOCK_READY"),30000);
        const auto concurrent=content::indexText(root,"Files/a.txt",profile,embedder,fixture.path());
        QVERIFY(!concurrent.error.isEmpty()); holder.write("release\n"); holder.closeWriteChannel();
        QVERIFY(holder.waitForFinished(30000)); QCOMPARE(holder.exitCode(),0);
        const auto after=content::indexText(root,"Files/a.txt",profile,embedder,fixture.path());
        QVERIFY2(after.error.isEmpty(),qPrintable(after.error));
    }

    // Invoked by the parent test in another process, with no model or app run.
    void holdWriterLockForProbe() {
        const auto path=qEnvironmentVariable("SOCIETY_CONTENT_LOCK_PROBE");
        if (path.isEmpty()) return;
        QLockFile lock(path); lock.setStaleLockTime(0); QVERIFY(lock.tryLock(0));
        QFile output; QVERIFY(output.open(stdout,QIODevice::WriteOnly,QFileDevice::DontCloseHandle));
        QCOMPARE(output.write("CONTENT_LOCK_READY\n"),qint64(19)); QVERIFY(output.flush());
        QFile input; QVERIFY(input.open(stdin,QIODevice::ReadOnly,QFileDevice::DontCloseHandle));
        QCOMPARE(input.readLine(),QByteArray("release\n"));
    }

    void unknownQueueIsNeverOverwritten() {
        QTemporaryDir fixture(QStringLiteral(SOCIETY_TEST_DIRECTORY)+"/content-foreign-XXXXXX"); QVERIFY(fixture.isValid());
        const auto root=fixture.filePath("drive.societycontainer"); QVERIFY(QDir().mkdir(root));
        QVERIFY(iiSocietyContainer::SocietyDrive::create(root));
        const auto storage=iiSocietyContainer::SharedStorage::open(root); QVERIFY(storage);
        QString error; QVERIFY(!storage->ensureRuntimeDirectory("content-index",&error).isEmpty());
        const auto path=storage->runtimePath("content-index/queue-v1.sqlite3",&error); QVERIFY(!path.isEmpty());
        QFile foreign(path); QVERIFY(foreign.open(QIODevice::WriteOnly|QIODevice::NewOnly));
        QCOMPARE(foreign.write("unrelated-data"),qint64(14)); foreign.close();
        FixtureEmbedder embedder; const content::Profile profile{"model://fixture",{}, {}};
        const auto refused=content::indexTrackedTextBatch(root,profile,embedder);
        QVERIFY(!refused.error.isEmpty()); QVERIFY(foreign.open(QIODevice::ReadOnly));
        QCOMPARE(foreign.readAll(),QByteArray("unrelated-data"));
    }

    void automaticPolicyRequiresAnExplicitResolvableModel() {
        if (!content::fileContentEmbeddingEnabled()) QSKIP("File-content embeddings are disabled by the hash-metadata-only policy.");
        QTemporaryDir fixture(QStringLiteral(SOCIETY_TEST_DIRECTORY)+"/content-policy-XXXXXX"); QVERIFY(fixture.isValid());
        const auto first=fixture.filePath("first.societycontainer"),second=fixture.filePath("second.societycontainer");
        QVERIFY(QDir().mkdir(first)); QVERIFY(iiSocietyContainer::SocietyDrive::create(first));
        QVERIFY(QDir().mkdir(second)); QVERIFY(iiSocietyContainer::SocietyDrive::create(second));
        FixtureEmbedder embedder; const content::Profile profile{"model://fixture","passage: ","query: "};
        const auto absent=content::trackedTextPolicy(first); QVERIFY2(absent.error.isEmpty(),qPrintable(absent.error));
        QVERIFY(!absent.value.value("enabled").toBool());
        QVERIFY(!QDir(first+"/.society-runtime/content-index").exists());
        const auto disabled=content::setTrackedTextPolicy(first,std::nullopt);
        QVERIFY2(disabled.error.isEmpty(),qPrintable(disabled.error)); QVERIFY(!disabled.value.value("enabled").toBool());
        QVERIFY(!QDir(first+"/.society-runtime/content-index").exists());
        embedder.failRevision=true;
        QVERIFY(!content::setTrackedTextPolicy(first,profile,&embedder).error.isEmpty()); embedder.failRevision=false;
        QVERIFY(!QDir(first+"/.society-runtime/content-index").exists());
        const auto enabled=content::setTrackedTextPolicy(first,profile,&embedder);
        QVERIFY2(enabled.error.isEmpty(),qPrintable(enabled.error)); QVERIFY(enabled.value.value("enabled").toBool());
        QVERIFY(embedder.inputs.isEmpty());
        const auto stored=content::trackedTextPolicy(first); QVERIFY2(stored.error.isEmpty(),qPrintable(stored.error));
        QCOMPARE(stored.value.value("model").toString(),profile.model);
        QCOMPARE(stored.value.value("documentPrefix").toString(),profile.documentPrefix);
        QCOMPARE(stored.value.value("queryPrefix").toString(),profile.queryPrefix);
        const auto isolated=content::trackedTextPolicy(second); QVERIFY2(isolated.error.isEmpty(),qPrintable(isolated.error));
        QVERIFY(!isolated.value.value("enabled").toBool());
        std::stop_source cancelled; embedder.afterRevision=[&] { cancelled.request_stop(); };
        QVERIFY(!content::setTrackedTextPolicy(first,content::Profile{"model://other",{},{}},&embedder,cancelled.get_token()).error.isEmpty());
        embedder.afterRevision={};
        const auto unchanged=content::trackedTextPolicy(first); QVERIFY2(unchanged.error.isEmpty(),qPrintable(unchanged.error));
        QCOMPARE(unchanged.value.value("model").toString(),profile.model);
        const auto stopped=content::setTrackedTextPolicy(first,std::nullopt);
        QVERIFY2(stopped.error.isEmpty(),qPrintable(stopped.error));
        const auto after=content::trackedTextPolicy(first); QVERIFY2(after.error.isEmpty(),qPrintable(after.error));
        QVERIFY(!after.value.value("enabled").toBool());
    }

    void legacyQueuePolicyMigrationPreservesPendingProgress() {
        if (!content::fileContentEmbeddingEnabled()) QSKIP("File-content embeddings are disabled by the hash-metadata-only policy.");
        QTemporaryDir fixture(QStringLiteral(SOCIETY_TEST_DIRECTORY)+"/content-migration-XXXXXX"); QVERIFY(fixture.isValid());
        const auto root=fixture.filePath("drive.societycontainer"); QVERIFY(QDir().mkdir(root));
        QVERIFY(iiSocietyContainer::SocietyDrive::create(root));
        const auto storage=iiSocietyContainer::SharedStorage::open(root); QVERIFY(storage);
        FixtureEmbedder embedder; const content::Profile profile{"model://fixture",{}, {}};
        const auto created=content::setTrackedTextPolicy(root,profile,&embedder);
        QVERIFY2(created.error.isEmpty(),qPrintable(created.error));
        const auto path=storage->runtimePath("content-index/queue-v1.sqlite3");
        namespace sql=iiFileProvider::native::sql;
        const auto open=[&] { return sql::Database::open(std::filesystem::path(path.toStdU16String())); };
        {
            auto opened=open(); QVERIFY(opened.has_value()); auto &db=*opened;
            QVERIFY(db.execute("INSERT INTO progress(scope,baseline_after,baseline_done,journal_after) VALUES('retained',42,1,77)").has_value());
            QVERIFY(db.execute("INSERT INTO pending(scope,resource_id,version,attempts,retry_at) VALUES('retained','asset',3,1,123)").has_value());
            QVERIFY(db.execute("DROP TABLE policy").has_value()); QVERIFY(db.execute("PRAGMA user_version = 1").has_value());
        }
        const auto legacy=content::trackedTextPolicy(root); QVERIFY2(legacy.error.isEmpty(),qPrintable(legacy.error));
        QVERIFY(!legacy.value.value("enabled").toBool());
        {
            auto opened=open(); QVERIFY(opened.has_value());
            auto version=opened->prepare("PRAGMA user_version"); QVERIFY(version.has_value());
            const auto row=version->step(); QVERIFY(row.has_value() && *row==sql::Step::row);
            const auto value=version->value(0); QVERIFY(value.has_value() && std::holds_alternative<std::int64_t>(*value));
            QCOMPARE(std::get<std::int64_t>(*value),std::int64_t(1));
        }
        const auto migrated=content::setTrackedTextPolicy(root,profile,&embedder);
        QVERIFY2(migrated.error.isEmpty(),qPrintable(migrated.error));
        {
            auto opened=open(); QVERIFY(opened.has_value());
            auto query=opened->prepare("SELECT baseline_after,journal_after FROM progress WHERE scope='retained'"); QVERIFY(query.has_value());
            const auto row=query->step(); QVERIFY(row.has_value() && *row==sql::Step::row);
            const auto baseline=query->value(0),journal=query->value(1);
            QVERIFY(baseline.has_value() && std::holds_alternative<std::int64_t>(*baseline));
            QVERIFY(journal.has_value() && std::holds_alternative<std::int64_t>(*journal));
            QCOMPARE(std::get<std::int64_t>(*baseline),std::int64_t(42));
            QCOMPARE(std::get<std::int64_t>(*journal),std::int64_t(77));
            auto pending=opened->prepare("SELECT version FROM pending WHERE scope='retained' AND resource_id='asset'"); QVERIFY(pending.has_value());
            const auto task=pending->step(); QVERIFY(task.has_value() && *task==sql::Step::row);
            const auto version=pending->value(0); QVERIFY(version.has_value() && std::holds_alternative<std::int64_t>(*version));
            QCOMPARE(std::get<std::int64_t>(*version),std::int64_t(3));
        }
        const auto active=content::trackedTextPolicy(root); QVERIFY2(active.error.isEmpty(),qPrintable(active.error));
        QVERIFY(active.value.value("enabled").toBool());
    }
};

QTEST_GUILESS_MAIN(ContentIndexTest)
#include "tst_content_index.moc"

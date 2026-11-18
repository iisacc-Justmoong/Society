#include "Daemon/SuggestedCatalog.h"
#include <iiSocietyContainer>
#include <iiFileProvider>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <filesystem>

using namespace society::suggested;
class SuggestedCatalogTest final : public QObject {
    Q_OBJECT
private slots:
    void readsIndexedEvidenceAndRejectsChangedFiles() {
        QTemporaryDir fixture(QStringLiteral(SOCIETY_TEST_DIRECTORY)+"/suggested-XXXXXX");
        QVERIFY(fixture.isValid());
        const auto root=fixture.filePath("drive.societycontainer"); QVERIFY(QDir().mkdir(root));
        QVERIFY(iiSocietyContainer::SocietyDrive::create(root));
        const auto storage=iiSocietyContainer::SharedStorage::open(root); QVERIFY(storage);
        const auto write=[&](const QString &name,const QByteArray &value) {
            QFile f(root+"/Files/"+name); if (!f.open(QIODevice::WriteOnly)) return false;
            return f.write(value)==value.size();
        };
        QVERIFY(write("draft.md","A draft")); QVERIFY(write("done.md","TODO: irrelevant after approval"));
        QVERIFY(write("tasks.md","- [ ] finish the first frame\n"));
        QVERIFY(write("unknown.txt","A detailed description of an ongoing illustration"));
        QVERIFY(write("estimate.md","A scene description"));
        auto repo=storage->fileRepository();
        iiFileProvider::FileMetadata draft; draft.fields={{"workflow.status","draft"}};
        iiFileProvider::FileMetadata done; done.fields={{"workflow.status","published"}};
        iiFileProvider::FileMetadata estimate; estimate.fields={{"workflow.status","draft"},{"workflow.status.source","ai"}};
        (void)repo->observe("files/draft.md",draft); (void)repo->observe("files/done.md",done);
        (void)repo->observe("files/tasks.md"); (void)repo->observe("files/unknown.txt");
        (void)repo->observe("files/estimate.md",estimate); repo.reset();
        const auto scan=SuggestedCatalog::scan(root.toStdString(),64);
        QVERIFY2(scan.error.empty(),scan.error.c_str()); QVERIFY(!scan.pending); QCOMPARE(scan.items.size(),std::size_t(5));
        const auto find=[&](const std::string &name) -> Candidate {
            for (const auto &item:scan.items) if (item.evidence.name==name) return item;
            return {};
        };
        const auto draftCandidate=find("draft.md"), doneCandidate=find("done.md"), tasksCandidate=find("tasks.md"), unknownCandidate=find("unknown.txt");
        QVERIFY(!draftCandidate.evidence.resourceId.empty()); QVERIFY(!draftCandidate.evidence.version.empty());
        QCOMPARE(draftCandidate.resource.resource.id.value,draftCandidate.evidence.resourceId);
        QVERIFY(draftCandidate.resource.revision.has_value());
        QVERIFY(draftCandidate.resource.revision->resourceId==draftCandidate.resource.resource.id);
        QCOMPARE(Policy().classify(draftCandidate.evidence).kind,RuleKind::Recommend);
        QCOMPARE(Policy().classify(doneCandidate.evidence).kind,RuleKind::Exclude);
        QCOMPARE(Policy().classify(tasksCandidate.evidence).reason,std::string("Open checklist"));
        QCOMPARE(Policy().classify(unknownCandidate.evidence).kind,RuleKind::Unknown);
        QCOMPARE(Policy().classify(find("estimate.md").evidence).kind,RuleKind::Unknown);
        QVERIFY(!unknownCandidate.evidence.text.empty());
        QVERIFY(SuggestedCatalog::validate(root.toStdString(),draftCandidate));
        QVERIFY(QFile::remove(root+"/Files/unknown.txt"));
        const auto stale=SuggestedCatalog::scan(root.toStdString(),64);
        QVERIFY(stale.partial);
        QVERIFY(write("draft.md","An externally modified draft with new bytes"));
        QVERIFY(!SuggestedCatalog::validate(root.toStdString(),draftCandidate));
        // The catalog can notice the working file before the repository has
        // observed a new revision. It must not pair that file with the old one.
        {
            const auto drive=std::filesystem::path(root.toStdString());
            iiFileProvider::FileCatalog catalog(drive/"Files",iiFileProvider::FileCatalog::Access::ReadWrite,
                iiSocietyContainer::FileRepository::classificationDirectory(drive,"files"));
            QVERIFY(catalog.registerFile("draft.md").metadataWritten);
        }
        const auto pendingRevision=SuggestedCatalog::scan(root.toStdString(),64);
        QVERIFY(pendingRevision.partial);
        for (const auto &item:pendingRevision.items) QVERIFY(item.evidence.name!="draft.md");
    }
};
QTEST_GUILESS_MAIN(SuggestedCatalogTest)
#include "tst_suggested_catalog.moc"

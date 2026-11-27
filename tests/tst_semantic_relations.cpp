#include "Society/Dashboard/ProjectRepository.h"
#include "Society/Semantics/RelocationVerification.h"
#include "Society/Semantics/SemanticRepository.h"
#include "Society/Cleanup/CleanupPolicy.h"
#include <iiFileProvider>
#include <iiSocietyContainer>
#include <ProviderPackage.h>
#include <iiProjectManager>
#include <QDir>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>

namespace {
QString fixtureTemplate(const char *pattern) {
    const auto directory = qEnvironmentVariable("SOCIETY_TEST_OUTPUT_DIR");
    return QDir(directory.isEmpty() ? QStringLiteral(SOCIETY_TEST_DIRECTORY) : directory).filePath(QLatin1String(pattern));
}
QString projectAddress(const QString &root, const QString &key) {
    iiFileProvider::FileCatalog catalog(QDir(root).filePath("Files").toStdString(),
                                        iiFileProvider::FileCatalog::Access::ReadOnly);
    const auto item = catalog.lookup(key.toStdString());
    if (!item) throw std::runtime_error("project not registered");
    return QStringLiteral("files/") + QString::fromStdString(item->path);
}
QString revision(const QJsonObject &value) { return value.value("revision").toString(); }
bool containsLink(const QJsonArray &values, const QString &subject, const QString &predicate, const QString &target) {
    return std::any_of(values.begin(), values.end(), [&](const QJsonValue &value) {
        const auto item = value.toObject();
        return item.value("source").toString() == subject && item.value("predicate").toString() == predicate
            && item.value("target").toString() == target;
    });
}
bool copyDirectory(const QString &source, const QString &destination) {
    if (!QDir().mkpath(destination)) return false;
    const auto entries = QDir(source).entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    for (const auto &entry : entries) {
        if (entry.isSymLink()) return false;
        const auto target = QDir(destination).filePath(entry.fileName());
        if (entry.isDir() ? !copyDirectory(entry.absoluteFilePath(), target) : !QFile::copy(entry.absoluteFilePath(), target)) return false;
    }
    return true;
}
}

class SemanticRelationsTests : public QObject {
    Q_OBJECT
private slots:
    void cleanupPolicyRequiresVerifiedEvidenceBeforeApproval() {
        society::cleanup::Evidence evidence;
        auto decision=society::cleanup::evaluate(evidence);
        QCOMPARE(decision.value("decision").toString(),QStringLiteral("blocked"));
        const auto reasons=decision.value("reasons").toArray();
        QVERIFY(reasons.contains("target_unverified"));
        QVERIFY(reasons.contains("dependency_coverage_unknown"));
        QVERIFY(reasons.contains("recoverability_unverified"));
        QVERIFY(!decision.value("executable").toBool());
        evidence.targetObservationMatches=true; evidence.dependencyCoverageComplete=true;
        evidence.recoverabilityVerified=true; evidence.hasRecordedRelations=true;
        decision=society::cleanup::evaluate(evidence);
        QCOMPARE(decision.value("decision").toString(),QStringLiteral("blocked"));
        QVERIFY(decision.value("reasons").toArray().contains("recorded_relations_require_review"));
        evidence.hasRecordedRelations=false;
        decision=society::cleanup::evaluate(evidence);
        QCOMPARE(decision.value("decision").toString(),QStringLiteral("approval_required"));
        QVERIFY(!decision.value("executable").toBool());
        evidence.relationshipObservationIncomplete=true;
        QVERIFY(society::cleanup::evaluate(evidence).value("reasons").toArray().contains("relationship_observation_incomplete"));
    }

    void cleanupPlanUsesRecordedRelationsAndPreservesUnknowns() {
        QTemporaryDir fixture(fixtureTemplate("cleanup-plan-XXXXXX.societycontainer"));
        QVERIFY(fixture.isValid()); QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        const auto storage=iiSocietyContainer::SharedStorage::open(fixture.path()); QVERIFY(storage);
        const auto path=QDir(fixture.path()).filePath("Files/asset.txt");
        iiFileProvider::native::File::create(std::filesystem::path(path.toStdString()), "asset");
        { auto repository=storage->fileRepository(); (void)repository->observe("files/asset.txt"); }
        SemanticRepository semantic(fixture.path(),"urn:user:artist");
        (void)semantic.putRelation("files/project.txt","example:uses","files/asset.txt",{},revision(semantic.read("files/project.txt")));
        const auto plan=society::cleanup::plan(fixture.path(),"Files/asset.txt",10);
        QVERIFY2(plan.error.isEmpty(),qPrintable(plan.error));
        QCOMPARE(plan.value.value("target").toObject().value("status").toString(),QStringLiteral("last_observed"));
        QVERIFY(!plan.value.value("target").toObject().value("bytesVerified").toBool());
        QVERIFY(!plan.value.value("target").toObject().value("resourceId").toString().isEmpty());
        const auto relations=plan.value.value("relations").toObject();
        QCOMPARE(relations.value("edges").toArray().size(),1);
        QCOMPARE(relations.value("completeness").toString(),QStringLiteral("unknown"));
        const auto policy=plan.value.value("policy").toObject();
        QCOMPARE(policy.value("decision").toString(),QStringLiteral("blocked"));
        QVERIFY(policy.value("reasons").toArray().contains("recorded_relations_require_review"));
        QVERIFY(policy.value("reasons").toArray().contains("dependency_coverage_unknown"));
        QVERIFY(policy.value("reasons").toArray().contains("recoverability_unverified"));
        QVERIFY(!policy.value("executable").toBool());
        (void)semantic.putRelation("files/another.txt","example:uses","files/asset.txt",{},revision(semantic.read("files/another.txt")));
        const auto truncated=society::cleanup::plan(fixture.path(),"Files/asset.txt",1);
        QVERIFY(truncated.error.isEmpty());
        QVERIFY(truncated.value.value("relations").toObject().value("truncated").toBool());
        QVERIFY(truncated.value.value("policy").toObject().value("reasons").toArray().contains("relationship_observation_incomplete"));
        const auto absent=society::cleanup::plan(fixture.path(),"Files/missing.txt",10);
        QVERIFY(absent.error.isEmpty());
        QVERIFY(absent.value.value("policy").toObject().value("reasons").toArray().contains("target_unverified"));
        QVERIFY(absent.value.value("recoverability").toObject().value("recordedReplicas").isNull());
        QVERIFY(!society::cleanup::plan(fixture.path(),"Files/../outside",10).error.isEmpty());
        QVERIFY(!society::cleanup::plan(fixture.path(),"Files/asset.txt",21).error.isEmpty());
        QVERIFY(QFileInfo::exists(path));
        const auto original=QDir(fixture.path()).filePath("Files/original.txt");
        QVERIFY(QFile::rename(path,original));
        iiFileProvider::native::File::create(std::filesystem::path(path.toStdString()), "replacement");
        const auto replaced=society::cleanup::plan(fixture.path(),"Files/asset.txt",10);
        QVERIFY(replaced.error.isEmpty());
        QVERIFY(replaced.value.value("policy").toObject().value("reasons").toArray().contains("target_unverified"));
        QVERIFY(QFileInfo::exists(original)); QVERIFY(QFileInfo::exists(path));
        QFile preserved(original); QVERIFY(preserved.open(QIODevice::ReadOnly)); QCOMPARE(preserved.readAll(),QByteArray("asset"));
        QFile current(path); QVERIFY(current.open(QIODevice::ReadOnly)); QCOMPARE(current.readAll(),QByteArray("replacement"));
    }

    void lineageTracesAssertedRelationsWithoutChangingStorage() {
        QTemporaryDir fixture(fixtureTemplate("semantic-lineage-XXXXXX.societycontainer"));
        QVERIFY(fixture.isValid()); QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        const auto storage=iiSocietyContainer::SharedStorage::open(fixture.path()); QVERIFY(storage);
        SemanticRepository repo(fixture.path(), "urn:user:artist");
        const auto link = [&](const QString &source, const QString &target) {
            (void)repo.putRelation(source, "example:derivedFrom", target, {}, revision(repo.read(source)));
        };
        link("files/a.txt", "files/b.txt");
        link("files/b.txt", "files/c.txt");
        link("files/c.txt", "files/a.txt");
        const auto trace = repo.traceRelations(*storage, "files/c.txt", "incoming", 3, 20);
        QCOMPARE(trace.value("evidence").toString(), QStringLiteral("user_asserted_relations"));
        QCOMPARE(trace.value("assertionAuthority").toString(), QStringLiteral("host_declared_not_authenticated"));
        QCOMPARE(trace.value("completeness").toString(), QStringLiteral("unknown"));
        QCOMPARE(trace.value("cleanupDecision").toString(), QStringLiteral("review_required"));
        QVERIFY(!trace.value("incomplete").toBool());
        const auto edges = trace.value("edges").toArray();
        QCOMPARE(edges.size(), 3);
        QCOMPARE(edges.at(0).toObject().value("source").toString(), QStringLiteral("files/b.txt"));
        QCOMPARE(edges.at(1).toObject().value("source").toString(), QStringLiteral("files/a.txt"));
        QCOMPARE(edges.at(2).toObject().value("source").toString(), QStringLiteral("files/c.txt"));
        for (int index=0; index<edges.size(); ++index) {
            QCOMPARE(edges.at(index).toObject().value("depth").toInt(), index+1);
            QCOMPARE(edges.at(index).toObject().value("owner").toString(), QStringLiteral("container"));
        }
        const auto limited = repo.traceRelations(*storage, "files/c.txt", "incoming", 3, 1);
        QCOMPARE(limited.value("edges").toArray().size(), 1);
        QVERIFY(limited.value("truncated").toBool());
        QVERIFY(limited.value("incomplete").toBool());
        QCOMPARE(limited.value("graphRevision"), trace.value("graphRevision"));
        const auto both=repo.traceRelations(*storage,"files/a.txt","both",1,10);
        QCOMPARE(both.value("edges").toArray().size(),2);
        QTemporaryDir other(fixtureTemplate("semantic-lineage-other-XXXXXX.societycontainer"));
        QVERIFY(other.isValid()); QVERIFY(iiSocietyContainer::SocietyDrive::create(other.path()));
        const auto otherStorage=iiSocietyContainer::SharedStorage::inspect(other.path()); QVERIFY(otherStorage);
        QVERIFY_THROWS_EXCEPTION(std::exception, (void)repo.traceRelations(*otherStorage,"files/a.txt","both",2,10));
        QVERIFY_THROWS_EXCEPTION(std::exception, (void)repo.traceRelations(*storage, "../outside", "both", 2, 10));
        QVERIFY_THROWS_EXCEPTION(std::exception, (void)repo.traceRelations(*storage, "files/a.txt", "sideways", 2, 10));
        QVERIFY_THROWS_EXCEPTION(std::exception, (void)repo.traceRelations(*storage, "files/a.txt", "both", 4, 10));
        QVERIFY_THROWS_EXCEPTION(std::exception, (void)repo.traceRelations(*storage, "files/a.txt", "both", 2, 0));
    }

    void lineageReportsExternalBoundaryAndChangedIdentity() {
        QTemporaryDir fixture(fixtureTemplate("semantic-lineage-identity-XXXXXX.societycontainer"));
        QVERIFY(fixture.isValid()); QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        const auto storage=iiSocietyContainer::SharedStorage::open(fixture.path()); QVERIFY(storage);
        const auto path=QDir(fixture.path()).filePath("Files/asset.txt");
        const auto saved=QDir(fixture.path()).filePath("Files/original.txt");
        iiFileProvider::native::File::create(std::filesystem::path(path.toStdString()), "original");
        SemanticRepository repo(fixture.path(), "urn:user:artist");
        (void)repo.putRelation("files/asset.txt", "example:references", "https://example.test/item",
                               {}, revision(repo.read("files/asset.txt")));
        const auto boundary=repo.traceRelations(*storage, "files/asset.txt", "outgoing", 2, 10);
        QCOMPARE(boundary.value("edges").toArray().size(), 1);
        QVERIFY(boundary.value("boundaryReached").toBool());
        QVERIFY(boundary.value("incomplete").toBool());
        QVERIFY(QFile::rename(path,saved));
        iiFileProvider::native::File::create(std::filesystem::path(path.toStdString()), "replacement");
        const auto stale=repo.traceRelations(*storage, "files/asset.txt", "outgoing", 2, 10);
        QVERIFY(stale.value("incomplete").toBool());
        QVERIFY(stale.value("reconciliationRequired").toBool());
        QVERIFY(stale.value("edges").toArray().isEmpty());
        QCOMPARE(stale.value("graphRevision"),boundary.value("graphRevision"));
        QVERIFY(QFileInfo::exists(path)); QVERIFY(QFileInfo::exists(saved));
    }

    void lineageLeavesPendingMoveAndUnavailableProjectUnchanged() {
        QTemporaryDir fixture(fixtureTemplate("semantic-lineage-pending-XXXXXX.societycontainer"));
        QVERIFY(fixture.isValid()); QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        const auto storage=iiSocietyContainer::SharedStorage::open(fixture.path()); QVERIFY(storage);
        ProjectRepository projects(fixture.path(), "urn:user:artist");
        const auto project=projectAddress(fixture.path(), projects.create({{"name", "Lineage"}, {"goal", "Trace"}}))+'/';
        SemanticRepository repo(fixture.path(), "urn:user:artist");
        (void)repo.putRelation(project, "example:uses", "files/asset.txt", {}, revision(repo.read(project)));
        const auto available=repo.traceRelations(*storage,"files/asset.txt","incoming",1,10);
        QCOMPARE(available.value("edges").toArray().size(),1);
        QCOMPARE(available.value("edges").toArray().first().toObject().value("owner").toString(),QStringLiteral("project"));
        QCOMPARE(available.value("edges").toArray().first().toObject().value("source").toString(),project);
        const auto from=QDir(fixture.path()).filePath("Files/move.txt");
        const auto to=QDir(fixture.path()).filePath("Files/moved.txt");
        iiFileProvider::native::File::create(std::filesystem::path(from.toStdString()), "moving");
        (void)repo.putRelation("files/move.txt", "example:references", "files/asset.txt", {}, revision(repo.read("files/move.txt")));
        repo.prepareRelocation(from,to); QVERIFY(QFile::rename(from,to));
        QVERIFY(QDir(QDir(fixture.path()).filePath("Files/"+project.mid(6))).removeRecursively());
        const auto first=repo.traceRelations(*storage, "files/asset.txt", "incoming", 1, 10);
        QVERIFY(first.value("incomplete").toBool());
        QVERIFY(first.value("reconciliationRequired").toBool());
        QVERIFY(first.value("unavailableProjects").toArray().contains(project));
        const auto again=repo.traceRelations(*storage, "files/asset.txt", "incoming", 1, 10);
        QCOMPARE(again.value("graphRevision"),first.value("graphRevision"));
        QVERIFY(again.value("reconciliationRequired").toBool());
        QVERIFY(!QFileInfo::exists(from)); QVERIFY(QFileInfo::exists(to));
    }

    void lineageDoesNotUpgradeAChangedLegacyLayout() {
        QTemporaryDir fixture(fixtureTemplate("semantic-lineage-legacy-XXXXXX.societycontainer"));
        QVERIFY(fixture.isValid()); QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        const auto storage=iiSocietyContainer::SharedStorage::inspect(fixture.path()); QVERIFY(storage);
        SemanticRepository repo(fixture.path(), "urn:user:artist");
        QFile manifest(QDir(fixture.path()).filePath(".society-drive.json"));
        QVERIFY(manifest.open(QIODevice::ReadOnly));
        auto data=QJsonDocument::fromJson(manifest.readAll()).object(); manifest.close();
        data.remove("filesLayoutVersion"); const auto original=QJsonDocument(data).toJson();
        QVERIFY(manifest.open(QIODevice::WriteOnly|QIODevice::Truncate));
        QCOMPARE(manifest.write(original),original.size()); manifest.close();
        QVERIFY_THROWS_EXCEPTION(std::exception, (void)repo.traceRelations(*storage,"files/a.txt","both",2,10));
        QVERIFY(manifest.open(QIODevice::ReadOnly)); QCOMPARE(manifest.readAll(),original);
    }

    void relocationVerificationRequiresAStableIdentity() {
        using society::semantics::DestinationObservation;
        using society::semantics::stabilizeDestination;
        int reads = 0;
        const auto settled = stabilizeDestination({"same-file", "before"}, [&] {
            ++reads;
            return DestinationObservation{"same-file", "settled"};
        });
        QCOMPARE(settled.stamp, QStringLiteral("settled"));
        QCOMPARE(reads, 2);

        QVERIFY_THROWS_EXCEPTION(std::exception, (void)stabilizeDestination({"same-file", "before"}, [] {
            return DestinationObservation{"replacement", "before"};
        }));
        QVERIFY_THROWS_EXCEPTION(std::exception, (void)stabilizeDestination({{}, "before"}, [] {
            return DestinationObservation{{}, "changed"};
        }));
        int changes = 0;
        QVERIFY_THROWS_EXCEPTION(std::exception, (void)stabilizeDestination({"same-file", "before"}, [&] {
            return DestinationObservation{"same-file", QString::number(++changes)};
        }));
    }

    void managedProjectRenameKeepsSemantics() {
        QTemporaryDir fixture(fixtureTemplate("semantic-managed-move-XXXXXX.societycontainer"));
        QVERIFY(fixture.isValid());
        const auto drive = iiSocietyContainer::SocietyDrive::create(fixture.path());
        QVERIFY(drive);
        ProjectRepository projects(fixture.path(), "urn:user:artist");
        const auto project = projectAddress(fixture.path(), projects.create({{"name", "Managed"}, {"goal", "Rename"}}));
        SemanticRepository repo(fixture.path(), "urn:user:artist");
        const auto addressedProject = project + '/';
        (void)repo.setMetadata(addressedProject, {{"role", "project"}}, revision(repo.read(addressedProject)));
        (void)repo.putRelation("files/unavailable.txt", "example:usedIn", addressedProject, {}, revision(repo.read("files/unavailable.txt")));
        iiSocietyContainer::FileOperations::setRelocationHandler(
            [](const QString &root, const QString &source, const QString &destination,
               iiSocietyContainer::FileOperations::RelocationPhase phase) {
                SemanticRepository semantic(root, "urn:user:artist");
                using Phase = iiSocietyContainer::FileOperations::RelocationPhase;
                if (phase == Phase::Prepare) semantic.prepareRelocation(source, destination);
                else if (phase == Phase::Commit) semantic.commitRelocation(source, destination);
                else semantic.cancelRelocation(source, destination);
            });
        const auto resetHandler = qScopeGuard([] { iiSocietyContainer::FileOperations::setRelocationHandler({}); });
        const auto source = QDir(fixture.path()).filePath("Files/" + project.mid(6));
        const auto moved = iiSocietyContainer::FileOperations(*drive).perform(
            iiSocietyContainer::FileOperations::Action::Rename, source, "managed-renamed.iiproject");
        QVERIFY2(moved, qPrintable(moved.error));
        const auto target = QStringLiteral("files/Projects/managed-renamed.iiproject");
        QVERIFY(containsLink(repo.relations("files/unavailable.txt").value("outgoing").toArray(),
                             "files/unavailable.txt", "example:usedIn", target));
        QCOMPARE(repo.read(target).value("metadata").toObject().value("role").toString(), "project");
        QVERIFY(containsLink(repo.relations(target).value("incoming").toArray(), "files/unavailable.txt", "example:usedIn", target));
    }

    void interruptedMoveRecoversFromVerifiedIdentity() {
        QTemporaryDir fixture(fixtureTemplate("semantic-recovery-XXXXXX.societycontainer"));
        QVERIFY(fixture.isValid()); QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        const auto oldNative = QDir(fixture.path()).filePath("Files/recover.txt");
        const auto newNative = QDir(fixture.path()).filePath("Files/recovered.txt");
        iiFileProvider::native::File::create(std::filesystem::path(oldNative.toStdString()), "recover me");
        SemanticRepository repo(fixture.path(), "urn:user:artist");
        (void)repo.setMetadata("files/recover.txt", {{"purpose", "recovery"}}, revision(repo.read("files/recover.txt")));
        repo.prepareRelocation(oldNative, newNative);
        QVERIFY(QFile::rename(oldNative, newNative)); // Simulate process loss before the commit callback.
        QCOMPARE(repo.read("files/recovered.txt").value("metadata").toObject().value("purpose").toString(), "recovery");

        const auto original = QDir(fixture.path()).filePath("Files/another.txt");
        const auto destination = QDir(fixture.path()).filePath("Files/ambiguous.txt");
        const auto retained = QDir(fixture.path()).filePath("Files/retained.txt");
        iiFileProvider::native::File::create(std::filesystem::path(original.toStdString()), "original");
        (void)repo.setMetadata("files/another.txt", {{"purpose", "protected"}}, revision(repo.read("files/another.txt")));
        repo.prepareRelocation(original, destination);
        QVERIFY(QFile::rename(original, retained));
        iiFileProvider::native::File::create(std::filesystem::path(destination.toStdString()), "different");
        QVERIFY_THROWS_EXCEPTION(std::exception, (void)repo.read("files/ambiguous.txt"));
    }
    void semanticsAddedDuringPreparedMoveFollowTheFile() {
        QTemporaryDir fixture(fixtureTemplate("semantic-concurrent-move-XXXXXX.societycontainer"));
        QVERIFY(fixture.isValid()); QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        const auto original = QDir(fixture.path()).filePath("Files/late.txt");
        const auto destination = QDir(fixture.path()).filePath("Files/late-moved.txt");
        iiFileProvider::native::File::create(std::filesystem::path(original.toStdString()), "content");
        SemanticRepository repo(fixture.path(), "urn:user:artist");
        repo.prepareRelocation(original, destination);
        (void)repo.setMetadata("files/late.txt", {{"role", "attached-during-move"}}, revision(repo.read("files/late.txt")));
        QVERIFY(QFile::rename(original, destination));
        repo.commitRelocation(original, destination);
        QCOMPARE(repo.read("files/late-moved.txt").value("metadata").toObject().value("role").toString(), "attached-during-move");
        const auto next = QDir(fixture.path()).filePath("Files/reserved-during-move.txt");
        repo.prepareRelocation(destination, next);
        QVERIFY_THROWS_EXCEPTION(std::exception, (void)repo.read("files/reserved-during-move.txt"));
        repo.cancelRelocation(destination, next);
        QVERIFY(repo.read("files/reserved-during-move.txt").value("metadata").toObject().isEmpty());
    }

    void managedDirectoryMoveAndDestinationConflict() {
        QTemporaryDir fixture(fixtureTemplate("semantic-tree-move-XXXXXX.societycontainer"));
        QVERIFY(fixture.isValid());
        const auto drive = iiSocietyContainer::SocietyDrive::create(fixture.path()); QVERIFY(drive);
        QVERIFY(QDir().mkpath(QDir(fixture.path()).filePath("Files/tree")));
        const auto native = QDir(fixture.path()).filePath("Files/tree/item.txt");
        iiFileProvider::native::File::create(std::filesystem::path(native.toStdString()), "tree data");
        SemanticRepository repo(fixture.path(), "urn:user:artist");
        (void)repo.setMetadata("files/tree/item.txt", {{"role", "tree"}}, revision(repo.read("files/tree/item.txt")));
        (void)repo.putRelation("files/tree/item.txt", "example:references", "external:target", {}, revision(repo.read("files/tree/item.txt")));
        iiSocietyContainer::FileOperations::setRelocationHandler(
            [](const QString &root, const QString &source, const QString &destination,
               iiSocietyContainer::FileOperations::RelocationPhase phase) {
                SemanticRepository semantic(root, "urn:user:artist");
                using Phase = iiSocietyContainer::FileOperations::RelocationPhase;
                if (phase == Phase::Prepare) semantic.prepareRelocation(source, destination);
                else if (phase == Phase::Commit) semantic.commitRelocation(source, destination);
                else semantic.cancelRelocation(source, destination);
            });
        const auto resetHandler = qScopeGuard([] { iiSocietyContainer::FileOperations::setRelocationHandler({}); });
        (void)repo.setMetadata("forked/tree/item.txt", {{"role", "reserved"}}, revision(repo.read("forked/tree/item.txt")));
        const auto denied = iiSocietyContainer::FileOperations(*drive).perform(iiSocietyContainer::FileOperations::Action::Move,
            QDir(fixture.path()).filePath("Files/tree"), QDir(fixture.path()).filePath("Forked"));
        QVERIFY(!denied); QVERIFY(QFileInfo::exists(native));
        // A distinct target with no existing semantic owner can be moved as one tree.
        QVERIFY(QDir().mkpath(QDir(fixture.path()).filePath("Forked/destination")));
        const QString newAddress = "forked/destination/tree/item.txt";
        const auto placeholder = repo.putRelation("external:temporary", "example:references", newAddress, {}, revision(repo.read("external:temporary")));
        (void)repo.removeRelation("external:temporary", placeholder.value("id").toString(), revision(repo.read("external:temporary")));
        const auto moved = iiSocietyContainer::FileOperations(*drive).perform(iiSocietyContainer::FileOperations::Action::Move,
            QDir(fixture.path()).filePath("Files/tree"), QDir(fixture.path()).filePath("Forked/destination"));
        QVERIFY2(moved, qPrintable(moved.error));
        QVERIFY(containsLink(repo.relations("external:target").value("incoming").toArray(), newAddress, "example:references", "external:target"));
        QCOMPARE(repo.read(newAddress).value("metadata").toObject().value("role").toString(), "tree");
        QCOMPARE(repo.read("forked/tree/item.txt").value("metadata").toObject().value("role").toString(), "reserved");
    }
    void externallyMovedFileCanBeResolvedAtAnAbsoluteAddress() {
        QTemporaryDir fixture(fixtureTemplate("semantic-external-XXXXXX.societycontainer"));
        QTemporaryDir external(fixtureTemplate("semantic-external-target-XXXXXX"));
        QVERIFY(fixture.isValid()); QVERIFY(external.isValid());
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        const auto oldNative = QDir(fixture.path()).filePath("Files/external.txt");
        const auto newNative = external.filePath("external.txt");
        iiFileProvider::native::File::create(std::filesystem::path(oldNative.toStdString()), "original");
        SemanticRepository repo(fixture.path(), "urn:user:artist");
        (void)repo.setMetadata("files/external.txt", {{"role", "external"}}, revision(repo.read("files/external.txt")));
        (void)repo.putRelation("files/external.txt", "example:references", "external:target", {}, revision(repo.read("files/external.txt")));
        QVERIFY(QFile::rename(oldNative, newNative));
        QCOMPARE(repo.read(newNative).value("metadata").toObject().value("role").toString(), "external");
        QVERIFY(containsLink(repo.relations("external:target").value("incoming").toArray(), newNative, "example:references", "external:target"));
    }
    void externallyCopiedAndRemovedProjectKeepsItsProjectIdentity() {
        QTemporaryDir fixture(fixtureTemplate("semantic-project-copy-XXXXXX.societycontainer"));
        QVERIFY(fixture.isValid()); QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        ProjectRepository projects(fixture.path(), "urn:user:artist");
        const auto oldProject = projectAddress(fixture.path(), projects.create({{"name", "Portable"}, {"goal", "Move volumes"}}));
        SemanticRepository repo(fixture.path(), "urn:user:artist");
        (void)repo.setMetadata(oldProject, {{"role", "portable"}}, revision(repo.read(oldProject)));
        (void)repo.putRelation("external:ref", "example:references", oldProject, {}, revision(repo.read("external:ref")));
        const auto original = QDir(fixture.path()).filePath("Files/" + oldProject.mid(6));
        const auto destination = QDir(fixture.path()).filePath("Files/Projects/copied.iiproject");
        QVERIFY(copyDirectory(original, destination));
        // A live copy does not steal the original project's bindings.
        QVERIFY(containsLink(repo.relations("external:ref").value("outgoing").toArray(), "external:ref", "example:references", oldProject));
        QVERIFY(QDir(original).removeRecursively());
        const auto moved = QStringLiteral("files/Projects/copied.iiproject");
        QVERIFY(containsLink(repo.relations("external:ref").value("outgoing").toArray(), "external:ref", "example:references", moved));
        QCOMPARE(repo.read(moved).value("metadata").toObject().value("role").toString(), "portable");
    }
    void explicitCrossVolumeRebindPreservesFileSemantics() {
        QTemporaryDir fixture(fixtureTemplate("semantic-rebind-XXXXXX.societycontainer"));
        QVERIFY(fixture.isValid()); QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        const auto original = QDir(fixture.path()).filePath("Files/original.bin");
        const auto destination = QDir(fixture.path()).filePath("Forked/moved.bin");
        iiFileProvider::native::File::create(std::filesystem::path(original.toStdString()), "content");
        SemanticRepository repo(fixture.path(), "urn:user:artist");
        (void)repo.setMetadata("files/original.bin", {{"role", "source"}}, revision(repo.read("files/original.bin")));
        (void)repo.putRelation("files/original.bin", "example:references", "external:target", {}, revision(repo.read("files/original.bin")));
        QVERIFY(QFile::copy(original, destination));
        QVERIFY_THROWS_EXCEPTION(std::exception, (void)repo.rebindRelocation("files/original.bin", "forked/moved.bin"));
        QVERIFY(QFile::remove(original)); // Different native identity simulates an external cross-volume copy and removal.
        QVERIFY(repo.relations("external:target").value("incomplete").toBool());
        const auto result = repo.rebindRelocation("files/original.bin", "forked/moved.bin");
        QCOMPARE(result.value("destination").toString(), QStringLiteral("forked/moved.bin"));
        QCOMPARE(repo.read("forked/moved.bin").value("metadata").toObject().value("role").toString(), "source");
        const auto links = repo.relations("external:target");
        QVERIFY(!links.value("incomplete").toBool());
        QVERIFY(containsLink(links.value("incoming").toArray(), "forked/moved.bin", "example:references", "external:target"));
        const auto saved = QDir(fixture.path()).filePath("Forked/saved.tmp");
        iiFileProvider::native::File::create(std::filesystem::path(saved.toStdString()), "updated content");
        QVERIFY(QFile::remove(destination)); QVERIFY(QFile::rename(saved, destination));
        QVERIFY(repo.read("forked/moved.bin").value("metadata").toObject().isEmpty());
        (void)repo.rebindRelocation("forked/moved.bin", "forked/moved.bin");
        QCOMPARE(repo.read("forked/moved.bin").value("metadata").toObject().value("role").toString(), "source");
    }

    void movedFilesAndProjectsKeepTheirSemanticIdentity() {
        QTemporaryDir fixture(fixtureTemplate("semantic-moves-XXXXXX.societycontainer"));
        QVERIFY(fixture.isValid());
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        ProjectRepository projects(fixture.path(), "urn:user:artist");
        const auto projectKey = projects.create({{"name", "Move"}, {"goal", "Keep meaning"}});
        const auto project = projectAddress(fixture.path(), projectKey);
        const auto otherProject = projectAddress(fixture.path(), projects.create({{"name", "Reference"}, {"goal", "Find meaning"}}));
        const QString oldFile = "files/source.txt", newFile = "files/renamed.txt";
        const auto nativeOld = QDir(fixture.path()).filePath("Files/source.txt");
        const auto nativeNew = QDir(fixture.path()).filePath("Files/renamed.txt");
        iiFileProvider::native::File::create(std::filesystem::path(nativeOld.toStdString()), "original bytes");
        SemanticRepository repo(fixture.path(), "urn:user:artist");
        (void)repo.setMetadata(oldFile, {{"role", "source"}}, revision(repo.read(oldFile)));
        (void)repo.putRelation(oldFile, "example:usedIn", project, {{"purpose", "source"}}, revision(repo.read(oldFile)));
        (void)repo.putRelation(otherProject, "example:references", oldFile, {}, revision(repo.read(otherProject)));
        (void)repo.putRelation(otherProject, "example:dependsOn", project, {}, revision(repo.read(otherProject)));
        (void)repo.setMetadata(project, {{"purpose", "keep"}}, revision(repo.read(project)));

        QVERIFY(QFile::rename(nativeOld, nativeNew));
        // The old spelling can be reused before Society observes the move.
        iiFileProvider::native::File::create(std::filesystem::path(nativeOld.toStdString()), "replacement bytes");
        QVERIFY(containsLink(repo.relations(project).value("incoming").toArray(), newFile, "example:usedIn", project));
        QVERIFY(containsLink(repo.relations(otherProject).value("outgoing").toArray(), otherProject, "example:references", newFile));
        QCOMPARE(repo.read(newFile).value("metadata").toObject().value("role").toString(), "source");
        QVERIFY(containsLink(repo.relations(newFile).value("incoming").toArray(), otherProject, "example:references", newFile));
        QVERIFY(containsLink(repo.relations(newFile).value("outgoing").toArray(), newFile, "example:usedIn", project));

        const auto oldProjectNative = QDir(fixture.path()).filePath("Files/" + project.mid(6));
        const auto newProject = QStringLiteral("files/Projects/renamed.iiproject");
        const auto newProjectNative = QDir(fixture.path()).filePath("Files/Projects/renamed.iiproject");
        QVERIFY(QDir().rename(oldProjectNative, newProjectNative));
        QVERIFY(containsLink(repo.relations(otherProject).value("outgoing").toArray(), otherProject, "example:dependsOn", newProject));
        QCOMPARE(repo.read(newProject).value("metadata").toObject().value("purpose").toString(), "keep");
        {
            iiFileProvider::FileCatalog catalog(QDir(fixture.path()).filePath("Files").toStdString(), iiFileProvider::FileCatalog::Access::ReadOnly);
            const auto item = catalog.lookup(projectKey.toStdString());
            QVERIFY(item.has_value());
            QCOMPARE(QString::fromStdString(item->path), QStringLiteral("Projects/renamed.iiproject"));
        }
        const auto links = repo.relations(newProject);
        QVERIFY(containsLink(links.value("incoming").toArray(), newFile, "example:usedIn", newProject));
        QVERIFY(containsLink(links.value("incoming").toArray(), otherProject, "example:dependsOn", newProject));
        QVERIFY(!links.value("incomplete").toBool());

        // Reusing the old pathname creates a distinct file, not an alias of the moved one.
        QVERIFY(repo.read(oldFile).value("metadata").toObject().isEmpty());
        (void)repo.setMetadata(oldFile, {{"role", "replacement"}}, revision(repo.read(oldFile)));
        QCOMPARE(repo.read(newFile).value("metadata").toObject().value("role").toString(), "source");
        QCOMPARE(repo.read(oldFile).value("metadata").toObject().value("role").toString(), "replacement");
        QVERIFY(repo.relations(oldFile).value("incoming").toArray().isEmpty());
    }

    void ownersAndAllAddressCombinations() {
        QTemporaryDir fixture(fixtureTemplate("semantic-relations-XXXXXX.societycontainer"));
        QVERIFY(fixture.isValid());
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        ProjectRepository projects(fixture.path(), "urn:user:artist");
        const auto projectAKey = projects.create({{"name", "A"}, {"goal", "Create A"}});
        const auto projectA = projectAddress(fixture.path(), projectAKey);
        const auto projectB = projectAddress(fixture.path(), projects.create({{"name", "B"}, {"goal", "Create B"}}));
        const QString fileA = "files/a.txt", fileB = "files/b.txt";
        iiFileProvider::native::File::create(std::filesystem::path(fixture.path().toStdString()) / "Files/a.txt", "asset A");
        iiFileProvider::native::File::create(std::filesystem::path(fixture.path().toStdString()) / "Files/b.txt", "asset B");
        SemanticRepository repo(fixture.path(), "urn:user:artist");

        const auto before = repo.read(fileA);
        QCOMPARE(before.value("owner").toString(), "container");
        QCOMPARE(revision(before), "0");
        const auto fileWrite = repo.setMetadata(fileA, {{"role", "reference"}, {"rating", 4}}, revision(before));
        QCOMPARE(repo.read(fileA).value("metadata").toObject().value("role").toString(), "reference");
        QVERIFY_THROWS_EXCEPTION(std::exception, (void)repo.setMetadata(fileA, {{"role", "stale"}}, revision(before)));
        QCOMPARE(repo.read(fileA).value("metadata").toObject().value("rating").toInt(), 4);

        const auto projectBefore = repo.read(projectA);
        QCOMPARE(projectBefore.value("owner").toString(), "project");
        const auto projectWrite = repo.setMetadata(projectA, {{"purpose", "publication"}, {"ready", true}}, revision(projectBefore));
        QCOMPARE(projectWrite.value("catalogStatus").toString(), "ready");
        {
            iiFileProvider::FileCatalog catalog(QDir(fixture.path()).filePath("Files").toStdString(),
                                                iiFileProvider::FileCatalog::Access::ReadOnly);
            QVERIFY(std::filesystem::is_directory(catalog.resolvePackage(projectAKey.toStdString())));
        }
        QCOMPARE(repo.read(projectA).value("metadata").toObject().value("purpose").toString(), "publication");
        QVERIFY_THROWS_EXCEPTION(std::exception, (void)repo.setMetadata(projectA, {{"purpose", "stale"}}, revision(projectBefore)));

        auto link = repo.putRelation(fileA, "example:derivedFrom", fileB, {{"strength", 0.8}}, revision(repo.read(fileA)));
        QVERIFY(!link.value("id").toString().isEmpty());
        (void)repo.putRelation(fileB, "example:usedIn", projectA, {}, revision(repo.read(fileB)));
        (void)repo.putRelation(projectA, "example:uses", fileA, {{"role", "reference"}}, revision(repo.read(projectA)));
        (void)repo.putRelation(projectB, "example:buildsOn", projectA, {}, revision(repo.read(projectB)));

        const auto fileLinks = repo.relations(fileA);
        QVERIFY(containsLink(fileLinks.value("outgoing").toArray(), fileA, "example:derivedFrom", fileB));
        QVERIFY(containsLink(fileLinks.value("incoming").toArray(), projectA, "example:uses", fileA));
        const auto projectLinks = repo.relations(projectA);
        QVERIFY(containsLink(projectLinks.value("outgoing").toArray(), projectA, "example:uses", fileA));
        QVERIFY(containsLink(projectLinks.value("incoming").toArray(), fileB, "example:usedIn", projectA));
        QVERIFY(containsLink(projectLinks.value("incoming").toArray(), projectB, "example:buildsOn", projectA));
        QVERIFY(!projectLinks.value("incomplete").toBool());
        QString error;
        const auto storage = iiSocietyContainer::SharedStorage::open(fixture.path(), &error);
        QVERIFY2(storage.has_value(), qPrintable(error));
        auto objects = storage->fileRepository({}, iiSocietyContainer::FileRepository::Access::ReadOnly);
        QVERIFY(!objects->lookup("files/a.txt")); // Semantic edits do not ingest or copy payloads.
        const auto projectKey = QStringLiteral("urn:iisacc:semantic:address:") + QString::fromLatin1(
            QCryptographicHash::hash(projectA.toUtf8(), QCryptographicHash::Sha256).toHex());
        const auto projectFacts = objects->ontology().statements(iiFileProvider::OntologyResource::entity(projectKey.toStdString()));
        QVERIFY(std::none_of(projectFacts.begin(), projectFacts.end(), [](const auto &entry) {
            return entry.propertyKey == "society:semantic:metadata";
        }));

        SemanticRepository reopened(fixture.path(), "urn:user:artist");
        QCOMPARE(reopened.read(projectA).value("metadata").toObject().value("purpose").toString(), "publication");
        QCOMPARE(reopened.read(fileA).value("metadata").toObject().value("role").toString(), "reference");
        QVERIFY(containsLink(reopened.relations(fileA).value("incoming").toArray(), projectA, "example:uses", fileA));
        QFile original(QDir(fixture.path()).filePath("Files/a.txt"));
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), QByteArray("asset A"));
    }

    void projectOwnedDataAndUnavailableReferences() {
        QTemporaryDir fixture(fixtureTemplate("semantic-owners-XXXXXX.societycontainer"));
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        ProjectRepository projects(fixture.path(), "urn:user:artist");
        const auto project = projectAddress(fixture.path(), projects.create({{"name", "Portable"}, {"goal", "Keep semantics"}}));
        const auto nativeProject = QDir(fixture.path()).filePath("Files/" + project.mid(6));
        SemanticRepository repo(fixture.path(), "urn:user:artist");
        (void)repo.setMetadata(project, {{"custom", QJsonObject{{"language", "ko"}}}}, revision(repo.read(project)));
        const auto relation = repo.putRelation(project, "example:references", "https://example.test/missing.png",
                                               {{"purpose", "inspiration"}}, revision(repo.read(project)));
        auto port = std::make_shared<iiProjectManagerFileProvider::ProviderPackage>(std::filesystem::path(nativeProject.toStdString()));
        const auto package = iiProjectManager::Project::open(port, {iiProjectManager::ActorKind::user, "urn:user:artist"});
        const auto snapshot = package.snapshot();
        QVERIFY(std::any_of(snapshot.entities.begin(), snapshot.entities.end(), [](const auto &entry) {
            return entry.propertiesJson.find("language") != std::string::npos;
        }));
        QVERIFY(std::any_of(snapshot.relations.begin(), snapshot.relations.end(), [](const auto &entry) {
            return entry.type == "example:references";
        }));
        QVERIFY(containsLink(repo.relations("https://example.test/missing.png").value("incoming").toArray(),
                             project, "example:references", "https://example.test/missing.png"));
        const auto moved = QStringLiteral("files/Projects/moved.iiproject");
        QVERIFY(QDir().rename(nativeProject, QDir(fixture.path()).filePath("Files/Projects/moved.iiproject")));
        QCOMPARE(repo.read(moved).value("metadata").toObject().value("custom").toObject().value("language").toString(), "ko");
        QVERIFY(containsLink(repo.relations(moved).value("outgoing").toArray(),
                             moved, "example:references", "https://example.test/missing.png"));
        const auto unresolved = repo.relations("https://example.test/missing.png");
        QVERIFY(!unresolved.value("incomplete").toBool());
        QVERIFY(containsLink(unresolved.value("incoming").toArray(), moved, "example:references", "https://example.test/missing.png"));
        const auto rebound = repo.putRelation(moved, "example:references", "https://example.test/missing.png",
                                              {{"purpose", "inspiration"}}, revision(repo.read(moved)));
        QCOMPARE(rebound.value("id"), relation.value("id"));
        QCOMPARE(rebound.value("source").toString(), moved);
        QCOMPARE(rebound.value("recordedSource").toString(), project);
        QVERIFY(containsLink(repo.relations("https://example.test/missing.png").value("incoming").toArray(),
                             moved, "example:references", "https://example.test/missing.png"));
        (void)repo.removeRelation(moved, relation.value("id").toString(), revision(repo.read(moved)));
        QVERIFY(repo.relations("https://example.test/missing.png").value("incoming").toArray().isEmpty());
        const auto fresh = repo.putRelation(moved, "example:references", "https://example.test/other.png", {}, revision(repo.read(moved)));
        auto movedPort = std::make_shared<iiProjectManagerFileProvider::ProviderPackage>(
            std::filesystem::path(QDir(fixture.path()).filePath("Files/Projects/moved.iiproject").toStdString()));
        auto editable = iiProjectManager::Project::open(movedPort, {iiProjectManager::ActorKind::user, "urn:user:artist"},
                                                        iiProjectManager::Capabilities::editor());
        auto graph = editable.snapshot();
        const auto existing = std::find_if(graph.relations.begin(), graph.relations.end(), [&](const auto &entry) {
            return entry.id == fresh.value("id").toString().toStdString();
        });
        QVERIFY(existing != graph.relations.end());
        auto changed = *existing;
        changed.object = "urn:test:other-resource";
        iiProjectManager::Resource other;
        other.id = changed.object; other.name = "Other"; other.purpose = "Another resource"; other.locator = "external:other";
        auto transaction = editable.begin(); transaction.putResource(other); transaction.putRelation(changed);
        (void)transaction.commit();
        QVERIFY_THROWS_EXCEPTION(std::exception, (void)repo.relations(moved));
        QVERIFY_THROWS_EXCEPTION(std::exception, (void)repo.setMetadata("files/missing.iiproject", {{"x", 1}}, "0"));
        QVERIFY_THROWS_EXCEPTION(std::exception, (void)repo.setMetadata("files/missing.iiproject/", {{"x", 1}}, "0"));
    }

    void addressOwnershipIdempotenceAndRemoval() {
        QTemporaryDir fixture(fixtureTemplate("semantic-addresses-XXXXXX.societycontainer"));
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        SemanticRepository repo(fixture.path(), "urn:user:artist");
        const QString source = "https://example.test/asset-a", target = "https://example.test/asset-b";
        auto first = repo.setMetadata(source, {{"color", "blue"}}, revision(repo.read(source)));
        const auto same = repo.setMetadata(source, {{"color", "blue"}}, revision(first));
        QCOMPARE(revision(same), revision(first));
        QVERIFY(repo.read(target).value("metadata").toObject().isEmpty());
        const auto edge = repo.putRelation(source, "example:references", target, {{"context", "draft"}}, revision(same));
        const auto again = repo.putRelation(source, "example:references", target, {{"context", "draft"}}, revision(edge));
        QCOMPARE(revision(again), revision(edge));
        const auto incoming = repo.relations(target).value("incoming").toArray();
        QCOMPARE(incoming.size(), 1);
        QCOMPARE(incoming.first().toObject().value("metadata").toObject().value("context").toString(), "draft");
        QVERIFY_THROWS_EXCEPTION(std::exception, (void)repo.removeRelation(source, edge.value("id").toString(), revision(first)));
        (void)repo.removeRelation(source, edge.value("id").toString(), revision(again));
        QVERIFY(repo.relations(source).value("outgoing").toArray().isEmpty());
        QVERIFY(repo.relations(target).value("incoming").toArray().isEmpty());
        QCOMPARE(repo.read(source).value("metadata").toObject().value("color").toString(), "blue");

        const QString missingProject = "files/unavailable.iiproject";
        (void)repo.putRelation(source, "example:references", missingProject, {}, revision(repo.read(source)));
        const auto missingLinks = repo.relations(missingProject);
        QVERIFY(missingLinks.value("incomplete").toBool());
        QVERIFY(missingLinks.value("unavailableProjects").toArray().contains(missingProject));
        QVERIFY(containsLink(missingLinks.value("incoming").toArray(), source, "example:references", missingProject));

        // A damaged or externally edited index must never report a different
        // target than the relation's authoritative payload.
        const auto fresh = repo.putRelation(source, "example:references", target, {}, revision(repo.read(source)));
        QString error;
        const auto storage = iiSocietyContainer::SharedStorage::open(fixture.path(), &error);
        QVERIFY2(storage.has_value(), qPrintable(error));
        auto fileRepository = storage->fileRepository();
        auto &graph = fileRepository->ontology();
        const auto node = iiFileProvider::OntologyResource::entity(fresh.value("id").toString().toStdString());
        const auto entries = graph.statements(node);
        const auto oldTarget = std::find_if(entries.begin(), entries.end(), [](const auto &entry) {
            return entry.propertyKey == "society:semantic:target";
        });
        QVERIFY(oldTarget != entries.end());
        const auto wrongKey = QStringLiteral("urn:iisacc:semantic:address:") + QString::fromLatin1(
            QCryptographicHash::hash(source.toUtf8(), QCryptographicHash::Sha256).toHex());
        graph.apply({iiFileProvider::OntologyAssertion{node, "society:semantic:target", oldTarget->value, false},
                     iiFileProvider::OntologyAssertion{node, "society:semantic:target", iiFileProvider::OntologyValue::resource(
                         iiFileProvider::OntologyResource::entity(wrongKey.toStdString()))}},
                    graph.revision(), fileRepository->sessionKey());
        QVERIFY_THROWS_EXCEPTION(std::exception, (void)repo.relations(source));
    }
};

QTEST_GUILESS_MAIN(SemanticRelationsTests)
#include "tst_semantic_relations.moc"

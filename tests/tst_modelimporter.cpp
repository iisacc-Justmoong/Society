#include "native_link.h"
#include "Society/Files/ModelImporter.h"
#include "Society/Jobs/JobStorage.h"
#include <Job.h>
#include <iiSocietyContainer>
#include <iiFileProvider>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtEndian>
#include <QDateTime>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QLockFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>
#include <chrono>
#include <optional>

namespace {
QByteArray validModel(const QByteArray &payload)
{
    const auto header = QJsonDocument(QJsonObject{{"tensor", QJsonObject{{"dtype", "U8"},
        {"shape", QJsonArray{payload.size()}}, {"data_offsets", QJsonArray{0, payload.size()}}}}}).toJson(QJsonDocument::Compact);
    QByteArray bytes(8, '\0'); qToLittleEndian(quint64(header.size()), bytes.data());
    return bytes + header + payload;
}
void writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(bytes), bytes.size());
}
QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}
QString modelClassification(const QString &container, const QString &asset)
{
    return QDir(container + "/.society-runtime/classifications/models")
        .filePath(QDir(container + "/Models").relativeFilePath(asset) + ".iisacc.json");
}
QStringList entries(const QString &path)
{
    auto names = QDir(path).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);
    names.removeIf([](const QString &name) { return iiFileProvider::native::Presentation::isControlFile(std::filesystem::path(name.toStdU16String())); });
    return names;
}
}

class ModelImporterTest : public QObject
{
    Q_OBJECT
private slots:
    void importsLocalModelWithItsProviderMetadata()
    {
        QTemporaryDir source(SOCIETY_TEST_DIRECTORY "/model-metadata-source-XXXXXX.societycontainer");
        QTemporaryDir target(SOCIETY_TEST_DIRECTORY "/model-metadata-drive-XXXXXX.societycontainer");
        QVERIFY(source.isValid() && target.isValid());
        QVERIFY(iiSocietyContainer::SocietyDrive::create(source.path()));
        QVERIFY(iiSocietyContainer::SocietyDrive::create(target.path()));
        const auto path = source.filePath("Models/Other/model.safetensors");
        QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
        writeFile(path, validModel("model bytes"));
        const auto storage = iiSocietyContainer::SharedStorage::open(source.path()); QVERIFY(storage);
        iiFileProvider::FileMetadata metadata;
        metadata.title = "Creator metadata"; metadata.description = "Keep this description";
        metadata.keywords = {"creative"};
        const auto repository = storage->fileRepository();
        repository->observe(storage->managedFilePath(iiSocietyContainer::StoreSection::Models, "Other/model.safetensors"), metadata);
        QVERIFY(iiFileProvider::native::Presentation::waitForIdle(std::chrono::seconds(30)));
        ModelImporter importer;
        QSignalSpy finished(&importer, &ModelImporter::finished);
        importer.setContainerPath(target.path());
        QVERIFY(importer.importFiles({QUrl::fromLocalFile(path)}));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 120000);
        QVERIFY2(importer.errorString().isEmpty(), qPrintable(importer.errorString()));
        const auto imported = finished.first().at(1).toStringList();
        QCOMPARE(imported.size(), 1);
        QVERIFY(!QFileInfo::exists(imported.first() + ".iisacc.json"));
        QVERIFY(QFileInfo::exists(modelClassification(target.path(), imported.first())));
        const auto document = QJsonDocument::fromJson(readFile(modelClassification(target.path(), imported.first()))).object();
        const auto annotation = document.value("metadata").toObject();
        QCOMPARE(annotation.value("title").toString(), QStringLiteral("Creator metadata"));
        QCOMPARE(annotation.value("description").toString(), QStringLiteral("Keep this description"));
        QCOMPARE(annotation.value("keywords").toArray(), QJsonArray{QStringLiteral("creative")});
        QVERIFY(!QFileInfo::exists(path + ".iisacc.json"));
        QVERIFY(QFileInfo::exists(modelClassification(source.path(), path)));
    }
    void backgroundOrganizationDoesNotRejectUserImport()
    {
        QTemporaryDir source(SOCIETY_TEST_DIRECTORY "/organize-admission-source-XXXXXX");
        QTemporaryDir target(SOCIETY_TEST_DIRECTORY "/organize-admission-drive-XXXXXX.societycontainer");
        QVERIFY(source.isValid() && target.isValid());
        const auto drive = iiSocietyContainer::SocietyDrive::create(target.path());
        QVERIFY(drive);
        const auto existing = target.filePath("Models/Checkpoint/existing.safetensors");
        QVERIFY(QDir().mkpath(QFileInfo(existing).absolutePath()));
        writeFile(existing, validModel("existing"));
        const auto storage = iiSocietyContainer::SharedStorage::open(target.path());
        QVERIFY(storage);
        QString error;
        QVERIFY2(storage->recordFile(iiSocietyContainer::StoreSection::Models,
                                     "Checkpoint/existing.safetensors", &error), qPrintable(error));
        const auto identity = drive->storageKind() == iiSocietyContainer::SocietyDrive::StorageKind::LogicalDisk
            ? drive->storageIdentifier() : drive->identifier();
        const auto operations = iiSocietyContainer::FileRepository::packagePath(
            std::filesystem::path(target.path().toStdU16String()), identity.toStdString()) / "operations";
        std::optional<iiFileProvider::TemporaryLease> held;
        held.emplace(operations, iiFileProvider::TemporaryLease::Mode::TryExclusive);
        QVERIFY(held->locked());
        const auto input = source.filePath("new.safetensors");
        writeFile(input, validModel("new"));
        ModelImporter importer;
        QSignalSpy finished(&importer, &ModelImporter::finished);
        importer.setContainerPath(target.path());
        for (int attempt = 0; attempt < 100 && !importer.organizing(); ++attempt) QTest::qWait(10);
        const bool organizing = importer.organizing();
        const bool notBlocking = !importer.busy();
        const bool admitted = importer.importFiles({QUrl::fromLocalFile(input)});
        held.reset();
        QVERIFY(organizing);
        QVERIFY(notBlocking);
        QVERIFY(admitted);
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 120000);
        QVERIFY2(importer.errorString().isEmpty(), qPrintable(importer.errorString()));
        QCOMPARE(finished.first().at(1).toStringList().size(), 1);
    }

    void recoversACompletedJobInterruptedBeforeFinalization()
    {
        QTemporaryDir target(SOCIETY_TEST_DIRECTORY "/job-finished-drive-XXXXXX.societycontainer");
        const auto drive = iiSocietyContainer::SocietyDrive::create(target.path());
        QVERIFY(drive);
        const auto driveId = drive->storageKind() == iiSocietyContainer::SocietyDrive::StorageKind::LogicalDisk
            ? drive->storageIdentifier() : drive->identifier();
        const auto jobId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        {
            SocietyJobStorage owner(SocietyJobStorage::defaultDirectory());
            auto store = owner.open(true);
            auto lease = owner.tryLease(jobId.toStdString(), true);
            QVERIFY(lease && lease->locked());
            store->create({jobId.toStdString(), "model-import", "test", driveId.toStdString(), "", "local", true},
                          {{"item-000000000000", "source", "", "", "completed.safetensors"}});
            store->begin(jobId.toStdString(), "local", "registering");
            const auto claim = store->claim(jobId.toStdString(), "item-000000000000");
            store->succeed(jobId.toStdString(), "item-000000000000", claim.token, "verified-output");
            // Simulate the worker exiting after its last durable result and before finish().
        }
        ModelImporter reopened;
        reopened.setContainerPath(target.path());
        const auto state = [&] {
            for (const auto &entry : reopened.jobs()) {
                const auto job = entry.toMap();
                if (job.value("id").toString() == jobId) return job.value("state").toString();
            }
            return QString();
        };
        QTRY_COMPARE_WITH_TIMEOUT(state(), QString("succeeded"), 30000);
    }

    void retryRejectsAChangedPreviouslyValidatedSource()
    {
        QTemporaryDir source(SOCIETY_TEST_DIRECTORY "/job-stamp-source-XXXXXX");
        QTemporaryDir target(SOCIETY_TEST_DIRECTORY "/job-stamp-drive-XXXXXX.societycontainer");
        QVERIFY(source.isValid() && target.isValid());
        const auto drive = iiSocietyContainer::SocietyDrive::create(target.path());
        QVERIFY(drive);
        const auto driveId = drive->storageKind() == iiSocietyContainer::SocietyDrive::StorageKind::LogicalDisk
            ? drive->storageIdentifier() : drive->identifier();
        const auto path = source.filePath("changed.safetensors");
        writeFile(path, validModel("original"));
        const QFileInfo before(path);
        const auto jobId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        {
            SocietyJobStorage owner(SocietyJobStorage::defaultDirectory());
            auto store = owner.open(true);
            auto lease = owner.tryLease(jobId.toStdString(), true);
            QVERIFY(lease && lease->locked());
            store->create({jobId.toStdString(), "model-import", "test", driveId.toStdString(), "", "local", true},
                          {{"item-000000000000", path.toStdString(), "", "", "changed.safetensors"}});
            store->begin(jobId.toStdString(), "local", "copying");
            const auto claim = store->claim(jobId.toStdString(), "item-000000000000");
            const auto checkpoint = QJsonDocument(QJsonObject{{"phase", "copying"}, {"bytes", 1},
                {"sourcePath", before.canonicalFilePath()}, {"sourceSize", QString::number(before.size())},
                {"sourceModifiedMs", QString::number(before.lastModified().toMSecsSinceEpoch())}})
                    .toJson(QJsonDocument::Compact);
            store->checkpoint(jobId.toStdString(), "item-000000000000", claim.token, checkpoint.toStdString());
            store->fail(jobId.toStdString(), "item-000000000000", claim.token, "copy failed", true);
            store->finish(jobId.toStdString());
        }
        writeFile(path, validModel("different content with a different size"));
        ModelImporter importer;
        QSignalSpy organized(&importer, &ModelImporter::organized);
        importer.setContainerPath(target.path());
        QTRY_COMPARE_WITH_TIMEOUT(organized.size(), 1, 120000);
        QSignalSpy done(&importer, &ModelImporter::finished);
        QVERIFY(importer.retryFailed(jobId));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 120000);
        QVERIFY(importer.errorString().contains("changed since"));
        QVERIFY(entries(target.filePath("Models/Other")).isEmpty());
        importer.selectJob(jobId);
        QTRY_COMPARE_WITH_TIMEOUT(importer.jobItems().size(), 1, 30000);
        QVERIFY(!importer.jobItems().first().toMap().value("retryable").toBool());
    }

    void persistsItemResultsAndRetriesOnlyFailedImports()
    {
        QTemporaryDir source(SOCIETY_TEST_DIRECTORY "/job-source-XXXXXX");
        QTemporaryDir target(SOCIETY_TEST_DIRECTORY "/job-drive-XXXXXX.societycontainer");
        QVERIFY(source.isValid() && target.isValid());
        QVERIFY(iiSocietyContainer::SocietyDrive::create(target.path()));
        const auto first = source.filePath("first.safetensors");
        const auto second = source.filePath("second.safetensors");
        const auto missing = source.filePath("later.safetensors");
        writeFile(first, validModel("first"));
        writeFile(second, validModel("second"));
        QString jobId;
        QStringList originalPaths;
        {
            ModelImporter importer;
            importer.setContainerPath(target.path());
            QSignalSpy done(&importer, &ModelImporter::finished);
            QVERIFY(importer.importFiles({QUrl::fromLocalFile(first), QUrl::fromLocalFile(second), QUrl::fromLocalFile(missing)}));
            jobId = importer.currentJobId();
            QVERIFY(!jobId.isEmpty());
            QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 120000);
            originalPaths = done.first().at(1).toStringList();
            QCOMPARE(originalPaths.size(), 2);
            QVERIFY(!importer.errorString().isEmpty());
        }
        writeFile(missing, validModel("recovered"));
        ModelImporter reopened;
        QSignalSpy organized(&reopened, &ModelImporter::organized);
        reopened.setContainerPath(target.path());
        QTRY_COMPARE_WITH_TIMEOUT(organized.size(), 1, 120000);
        reopened.selectJob(jobId);
        QTRY_COMPARE_WITH_TIMEOUT(reopened.jobItems().size(), 3, 30000);
        QCOMPARE(reopened.jobItems().at(0).toMap().value("state").toString(), QString("succeeded"));
        QCOMPARE(reopened.jobItems().at(2).toMap().value("state").toString(), QString("failed"));
        QVERIFY(reopened.jobItems().at(2).toMap().value("retryable").toBool());
        QSignalSpy retried(&reopened, &ModelImporter::finished);
        QVERIFY(reopened.retryFailed(jobId));
        QTRY_COMPARE_WITH_TIMEOUT(retried.size(), 1, 120000);
        QCOMPARE(retried.first().at(1).toStringList().size(), 1);
        QCOMPARE(readFile(originalPaths.at(0)), validModel("first"));
        QCOMPARE(readFile(originalPaths.at(1)), validModel("second"));
        QCOMPARE(entries(target.filePath("Models/Other")).size(), 3);
        const auto storage = iiSocietyContainer::SharedStorage::open(target.path());
        QVERIFY(storage);
        const auto tracking = storage->fileTracking(iiSocietyContainer::StoreSection::Models, "Other/first.safetensors");
        QCOMPARE(tracking.value("resourceStatus").toString(), QString("working-copy-only"));
        QVERIFY(!tracking.value("resource").toObject().value("id").toString().isEmpty());
        QVERIFY(tracking.value("sessionKey").toString().isEmpty());
        SocietyJobStorage owner(SocietyJobStorage::defaultDirectory());
        const auto store = owner.open(false); QVERIFY(store);
        const auto item = store->item(jobId.toStdString(), "item-000000000000"); QVERIFY(item);
        QCOMPARE(QString::fromStdString(item->artifact), originalPaths.at(0));
        const auto checkpoint = QJsonDocument::fromJson(QByteArray::fromStdString(item->checkpoint)).object();
        QCOMPARE(checkpoint.value("phase").toString(), QString("published"));
        QCOMPARE(checkpoint.value("sha256").toString(),
                 QString::fromLatin1(QCryptographicHash::hash(validModel("first"), QCryptographicHash::Sha256).toHex()));
        reopened.selectJob(jobId);
        QTRY_COMPARE_WITH_TIMEOUT(reopened.jobItems().size(), 3, 30000);
        QTRY_COMPARE_WITH_TIMEOUT(reopened.jobItems().at(2).toMap().value("state").toString(), QString("succeeded"), 30000);
        QCOMPARE(reopened.jobItems().at(0).toMap().value("attempts").toULongLong(), qulonglong(1));
        QCOMPARE(reopened.jobItems().at(2).toMap().value("attempts").toULongLong(), qulonglong(2));
    }

    void resumesRegistrationWithoutCopyingThePublishedFileAgain()
    {
        QTemporaryDir source(SOCIETY_TEST_DIRECTORY "/job-checkpoint-source-XXXXXX");
        QTemporaryDir target(SOCIETY_TEST_DIRECTORY "/job-checkpoint-drive-XXXXXX.societycontainer");
        const auto drive = iiSocietyContainer::SocietyDrive::create(target.path());
        QVERIFY(drive);
        const auto storage = iiSocietyContainer::SharedStorage::open(target.path());
        QVERIFY(storage);
        const auto driveId = drive->storageKind() == iiSocietyContainer::SocietyDrive::StorageKind::LogicalDisk
            ? drive->storageIdentifier() : drive->identifier();
        QVERIFY(QDir().mkpath(target.filePath("Models/Other")));
        const auto path = target.filePath("Models/Other/checkpoint.safetensors");
        const auto data = validModel("one published model");
        writeFile(path, data);
        const auto modified = QFileInfo(path).lastModified();
        const auto jobId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        {
            SocietyJobStorage owner(SocietyJobStorage::defaultDirectory());
            auto jobs = owner.open(true);
            auto lease = owner.tryLease(jobId.toStdString(), true); QVERIFY(lease && lease->locked());
            jobs->create({jobId.toStdString(), "model-import", "test", driveId.toStdString(), "", "local", true},
                         {{"item-000000000000", source.filePath("missing.safetensors").toStdString(), "", "", "checkpoint.safetensors"}});
            jobs->begin(jobId.toStdString(), "local", "indexing");
            const auto claim = jobs->claim(jobId.toStdString(), "item-000000000000");
            const auto checkpoint = QJsonDocument(QJsonObject{{"phase", "published"}, {"path", path},
                {"sha256", QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex())}})
                    .toJson(QJsonDocument::Compact);
            jobs->checkpoint(jobId.toStdString(), "item-000000000000", claim.token, checkpoint.toStdString());
            jobs->fail(jobId.toStdString(), "item-000000000000", claim.token, "index interrupted", true);
            jobs->finish(jobId.toStdString());
        }
        ModelImporter importer;
        importer.setContainerPath(target.path());
        QSignalSpy done(&importer, &ModelImporter::finished);
        QVERIFY(importer.retryFailed(jobId));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 120000);
        QVERIFY2(importer.errorString().isEmpty(), qPrintable(importer.errorString()));
        QCOMPARE(entries(target.filePath("Models/Other")).size(), 1);
        QCOMPARE(readFile(path), data);
        QCOMPARE(QFileInfo(path).lastModified(), modified);
        QCOMPARE(storage->fileTracking(iiSocietyContainer::StoreSection::Models,
                 "Other/checkpoint.safetensors").value("status").toString(), QString("ready"));
    }

    void recategorizesModelsAndPreservesManagedIdentity()
    {
        QTemporaryDir target(SOCIETY_TEST_DIRECTORY "/model-category-XXXXXX.societycontainer");
        QVERIFY(target.isValid());
        QVERIFY(iiSocietyContainer::SocietyDrive::create(target.path()));
        const auto path = target.filePath("Models/Checkpoint/style.safetensors");
        QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
        const auto bytes = validModel("unchanged model");
        writeFile(path, bytes);
        writeFile(target.filePath("Models/Checkpoint/style.preview.png"), "preview");
        QVERIFY(QDir().mkpath(target.filePath("Models/LoRA")));
        writeFile(target.filePath("Models/LoRA/style.safetensors"), "existing");
        const auto storage = iiSocietyContainer::SharedStorage::open(target.path()); QVERIFY(storage);
        QString error;
        QVERIFY2(storage->recordFile(iiSocietyContainer::StoreSection::Models, "Checkpoint/style.safetensors", &error), qPrintable(error));
        const auto originalKey = storage->fileTracking(iiSocietyContainer::StoreSection::Models, "Checkpoint/style.safetensors").value("objectKey").toString();
        QVERIFY(!originalKey.isEmpty());
        ModelImporter importer;
        importer.setContainerPath(target.path());
        QSignalSpy moved(&importer, &ModelImporter::recategorized);
        QVERIFY(!importer.recategorize(path, "not a category"));
        QVERIFY(importer.recategorize(path, "LoRA"));
        QTRY_COMPARE_WITH_TIMEOUT(moved.size(), 1, 120000);
        QVERIFY2(importer.errorString().isEmpty(), qPrintable(importer.errorString()));
        const auto destination = target.filePath("Models/LoRA/style (1).safetensors");
        QCOMPARE(moved.first().at(1).toString(), path);
        QCOMPARE(moved.first().at(2).toString(), destination);
        QCOMPARE(readFile(destination), bytes);
        QCOMPARE(readFile(target.filePath("Models/LoRA/style (1).preview.png")), QByteArray("preview"));
        QCOMPARE(readFile(target.filePath("Models/LoRA/style.safetensors")), QByteArray("existing"));
        const auto store = iiSocietyContainer::ModelStore::open(target.path()); QVERIFY(store);
        QCOMPARE(store->resolve("Checkpoint/style.safetensors"), destination);
        const auto tracking = storage->fileTracking(iiSocietyContainer::StoreSection::Models, "LoRA/style (1).safetensors");
        QCOMPARE(tracking.value("objectKey").toString(), originalKey);
        QTRY_VERIFY_WITH_TIMEOUT(!importer.busy(), 120000);
        QVERIFY(importer.recategorize(target.filePath("Files/outside.safetensors"), "LoRA"));
        QTRY_VERIFY_WITH_TIMEOUT(!importer.busy(), 120000);
        QVERIFY(!importer.errorString().isEmpty());
    }

    void categoryMoveWaitsForIndexAndMovesCompanionsTogether()
    {
        QTemporaryDir target(SOCIETY_TEST_DIRECTORY "/category-serialized-XXXXXX.societycontainer");
        QVERIFY(target.isValid());
        const auto drive = iiSocietyContainer::SocietyDrive::create(target.path()); QVERIFY(drive);
        QVERIFY(QDir().mkpath(target.filePath("Models/Checkpoint")));
        const auto source = target.filePath("Models/Checkpoint/style.safetensors");
        const auto companion = target.filePath("Models/Checkpoint/style.preview.png");
        const auto destination = target.filePath("Models/LLM/style.safetensors");
        const auto destinationCompanion = target.filePath("Models/LLM/style.preview.png");
        const auto bytes = validModel("model remains intact");
        writeFile(source, bytes);
        writeFile(companion, "preview");
        const auto storage = iiSocietyContainer::SharedStorage::open(target.path()); QVERIFY(storage);
        QString error;
        QVERIFY2(storage->recordFile(iiSocietyContainer::StoreSection::Models, "Checkpoint/style.safetensors", &error), qPrintable(error));
        QVERIFY2(storage->recordFile(iiSocietyContainer::StoreSection::Models, "Checkpoint/style.preview.png", &error), qPrintable(error));
        const auto original = storage->fileTracking(iiSocietyContainer::StoreSection::Models, "Checkpoint/style.safetensors").value("objectKey").toString();
        const auto companionId = storage->fileTracking(iiSocietyContainer::StoreSection::Models, "Checkpoint/style.preview.png").value("objectKey").toString();
        QVERIFY(!original.isEmpty() && !companionId.isEmpty());
        const auto id = drive->storageKind() == iiSocietyContainer::SocietyDrive::StorageKind::LogicalDisk
            ? drive->storageIdentifier() : drive->identifier();
        const auto operations = iiSocietyContainer::FileRepository::packagePath(
            std::filesystem::path(target.path().toStdU16String()), id.toUtf8().toStdString()) / "operations";
        std::optional<iiFileProvider::TemporaryLease> lock;
        lock.emplace(operations, iiFileProvider::TemporaryLease::Mode::TryExclusive);
        QVERIFY(lock->locked());
        ModelImporter importer;
        importer.setContainerPath(target.path());
        QSignalSpy moved(&importer, &ModelImporter::recategorized);
        QVERIFY(importer.recategorize(source, "LLM"));
        QTRY_VERIFY_WITH_TIMEOUT(importer.status().contains("Waiting for") || QFileInfo::exists(destination), 15000);
        const bool waitedBeforeMove = QFileInfo::exists(source) && QFileInfo::exists(companion)
            && !QFileInfo::exists(destination) && !QFileInfo::exists(destinationCompanion);
        lock.reset();
        QTRY_VERIFY_WITH_TIMEOUT(!importer.busy(), 30000);
        QVERIFY2(waitedBeforeMove, "The model moved before the managed repository became available.");
        QVERIFY2(importer.errorString().isEmpty(), qPrintable(importer.errorString()));
        QCOMPARE(moved.size(), 1);
        QCOMPARE(moved.first().at(2).toString(), destination);
        QCOMPARE(readFile(destination), bytes);
        QCOMPARE(readFile(destinationCompanion), QByteArray("preview"));
        QVERIFY(!QFileInfo::exists(source));
        QCOMPARE(storage->fileTracking(iiSocietyContainer::StoreSection::Models, "LLM/style.safetensors").value("objectKey").toString(), original);
        QCOMPARE(storage->fileTracking(iiSocietyContainer::StoreSection::Models, "LLM/style.preview.png").value("objectKey").toString(), companionId);
    }

    void categoryMoveDoesNotInheritAnEarlierModelsMetadata()
    {
        QTemporaryDir target(SOCIETY_TEST_DIRECTORY "/category-metadata-XXXXXX.societycontainer");
        QVERIFY(target.isValid());
        QVERIFY(iiSocietyContainer::SocietyDrive::create(target.path()));
        QVERIFY(QDir().mkpath(target.filePath("Models/Checkpoint")));
        QVERIFY(QDir().mkpath(target.filePath("Models/LoRA")));
        const auto first = target.filePath("Models/Checkpoint/same.safetensors");
        const auto second = target.filePath("Models/LoRA/same.safetensors");
        const auto firstDestination = target.filePath("Models/LLM/same.safetensors");
        const auto secondBytes = validModel("second distinct model");
        writeFile(first, validModel("first distinct model"));
        writeFile(second, secondBytes);
        const auto storage = iiSocietyContainer::SharedStorage::open(target.path()); QVERIFY(storage);
        const auto repository = storage->fileRepository();
        iiFileProvider::FileMetadata firstMetadata;
        firstMetadata.title = "First model";
        iiFileProvider::FileMetadata secondMetadata;
        secondMetadata.title = "Second model";
        repository->observe(storage->managedFilePath(iiSocietyContainer::StoreSection::Models, "Checkpoint/same.safetensors"), firstMetadata);
        repository->observe(storage->managedFilePath(iiSocietyContainer::StoreSection::Models, "LoRA/same.safetensors"), secondMetadata);
        const auto secondId = storage->fileTracking(iiSocietyContainer::StoreSection::Models, "LoRA/same.safetensors").value("objectKey").toString();
        QVERIFY(!secondId.isEmpty());
        ModelImporter importer;
        importer.setContainerPath(target.path());
        QVERIFY(importer.recategorize(first, "LLM"));
        QTRY_VERIFY_WITH_TIMEOUT(!importer.busy(), 30000);
        QVERIFY2(importer.errorString().isEmpty(), qPrintable(importer.errorString()));
        QVERIFY(QFileInfo::exists(firstDestination));
        QTRY_VERIFY_WITH_TIMEOUT(!importer.busy(), 30000);
        QVERIFY(importer.recategorize(second, "Checkpoint"));
        QTRY_VERIFY_WITH_TIMEOUT(!importer.busy(), 30000);
        QVERIFY2(importer.errorString().isEmpty(), qPrintable(importer.errorString()));
        QCOMPARE(readFile(first), secondBytes);
        QCOMPARE(storage->fileTracking(iiSocietyContainer::StoreSection::Models, "Checkpoint/same.safetensors").value("objectKey").toString(), secondId);
        const auto metadata = QJsonDocument::fromJson(readFile(modelClassification(target.path(), first))).object().value("metadata").toObject();
        QCOMPARE(metadata.value("title").toString(), QStringLiteral("Second model"));
    }

    void categoryMoveCanBeCancelledWhileWaitingForIndex()
    {
        QTemporaryDir target(SOCIETY_TEST_DIRECTORY "/category-cancel-XXXXXX.societycontainer");
        QVERIFY(target.isValid());
        const auto drive = iiSocietyContainer::SocietyDrive::create(target.path()); QVERIFY(drive);
        QVERIFY(QDir().mkpath(target.filePath("Models/Checkpoint")));
        const auto source = target.filePath("Models/Checkpoint/cancel.safetensors");
        const auto destination = target.filePath("Models/LLM/cancel.safetensors");
        const auto bytes = validModel("cancel safely");
        writeFile(source, bytes);
        const auto storage = iiSocietyContainer::SharedStorage::open(target.path()); QVERIFY(storage);
        QString error;
        QVERIFY2(storage->recordFile(iiSocietyContainer::StoreSection::Models, "Checkpoint/cancel.safetensors", &error), qPrintable(error));
        const auto id = drive->storageKind() == iiSocietyContainer::SocietyDrive::StorageKind::LogicalDisk
            ? drive->storageIdentifier() : drive->identifier();
        const auto operations = iiSocietyContainer::FileRepository::packagePath(
            std::filesystem::path(target.path().toStdU16String()), id.toUtf8().toStdString()) / "operations";
        std::optional<iiFileProvider::TemporaryLease> lock;
        lock.emplace(operations, iiFileProvider::TemporaryLease::Mode::TryExclusive);
        QVERIFY(lock->locked());
        ModelImporter importer;
        importer.setContainerPath(target.path());
        QSignalSpy moved(&importer, &ModelImporter::recategorized);
        QVERIFY(importer.recategorize(source, "LLM"));
        QTRY_VERIFY_WITH_TIMEOUT(importer.status().contains("Waiting for") || QFileInfo::exists(destination), 15000);
        importer.cancel();
        for (int i = 0; i < 50 && importer.busy(); ++i) QTest::qWait(100);
        const bool cancelledWhileLocked = !importer.busy();
        lock.reset();
        QTRY_VERIFY_WITH_TIMEOUT(!importer.busy(), 30000);
        QVERIFY2(cancelledWhileLocked, "Cancellation waited for the unrelated repository operation.");
        QCOMPARE(moved.size(), 0);
        QVERIFY(importer.errorString().isEmpty());
        QVERIFY2(importer.status().contains("cancelled"), qPrintable(importer.status()));
        QCOMPARE(readFile(source), bytes);
        QVERIFY(!QFileInfo::exists(destination));
        QCOMPARE(storage->fileTracking(iiSocietyContainer::StoreSection::Models, "Checkpoint/cancel.safetensors").value("status").toString(), QString("ready"));
    }

    void categoryMoveRetriesAfterLongRepositoryContention()
    {
        QTemporaryDir target(SOCIETY_TEST_DIRECTORY "/category-retry-XXXXXX.societycontainer");
        QVERIFY(target.isValid());
        const auto drive = iiSocietyContainer::SocietyDrive::create(target.path()); QVERIFY(drive);
        QVERIFY(QDir().mkpath(target.filePath("Models/Checkpoint")));
        const auto source = target.filePath("Models/Checkpoint/wait.safetensors");
        const auto destination = target.filePath("Models/LLM/wait.safetensors");
        const auto bytes = validModel("wait safely");
        writeFile(source, bytes);
        const auto storage = iiSocietyContainer::SharedStorage::open(target.path()); QVERIFY(storage);
        QString error;
        QVERIFY2(storage->recordFile(iiSocietyContainer::StoreSection::Models, "Checkpoint/wait.safetensors", &error), qPrintable(error));
        const auto original = storage->fileTracking(iiSocietyContainer::StoreSection::Models, "Checkpoint/wait.safetensors").value("objectKey").toString();
        const auto id = drive->storageKind() == iiSocietyContainer::SocietyDrive::StorageKind::LogicalDisk
            ? drive->storageIdentifier() : drive->identifier();
        const auto operations = iiSocietyContainer::FileRepository::packagePath(
            std::filesystem::path(target.path().toStdU16String()), id.toUtf8().toStdString()) / "operations";
        std::optional<iiFileProvider::TemporaryLease> lock;
        lock.emplace(operations, iiFileProvider::TemporaryLease::Mode::TryExclusive);
        QVERIFY(lock->locked());
        ModelImporter importer;
        importer.setContainerPath(target.path());
        QSignalSpy moved(&importer, &ModelImporter::recategorized);
        QVERIFY(importer.recategorize(source, "LLM"));
        QTRY_VERIFY_WITH_TIMEOUT(importer.status().contains("Waiting to retry") || !importer.busy(), 50000);
        const bool remainedPending = importer.busy() && QFileInfo::exists(source) && !QFileInfo::exists(destination);
        lock.reset();
        QTRY_VERIFY_WITH_TIMEOUT(!importer.busy(), 30000);
        QVERIFY2(remainedPending, "The category change failed or moved the file after a lock timeout.");
        QVERIFY2(importer.errorString().isEmpty(), qPrintable(importer.errorString()));
        QCOMPARE(moved.size(), 1);
        QCOMPARE(readFile(destination), bytes);
        QCOMPARE(storage->fileTracking(iiSocietyContainer::StoreSection::Models, "LLM/wait.safetensors").value("objectKey").toString(), original);
    }

    void categoryMoveRecordsChangedContentBeforeMoving()
    {
        QTemporaryDir target(SOCIETY_TEST_DIRECTORY "/category-modified-XXXXXX.societycontainer");
        QVERIFY(target.isValid());
        QVERIFY(iiSocietyContainer::SocietyDrive::create(target.path()));
        QVERIFY(QDir().mkpath(target.filePath("Models/Checkpoint")));
        const auto source = target.filePath("Models/Checkpoint/modified.safetensors");
        const auto destination = target.filePath("Models/LLM/modified.safetensors");
        writeFile(source, validModel("original version"));
        const auto storage = iiSocietyContainer::SharedStorage::open(target.path()); QVERIFY(storage);
        QString error;
        QVERIFY2(storage->recordFile(iiSocietyContainer::StoreSection::Models, "Checkpoint/modified.safetensors", &error), qPrintable(error));
        const auto before = storage->fileTracking(iiSocietyContainer::StoreSection::Models, "Checkpoint/modified.safetensors");
        const auto updated = validModel("changed model contents");
        writeFile(source, updated);
        ModelImporter importer;
        importer.setContainerPath(target.path());
        QSignalSpy moved(&importer, &ModelImporter::recategorized);
        QVERIFY(importer.recategorize(source, "LLM"));
        QTRY_VERIFY_WITH_TIMEOUT(!importer.busy(), 30000);
        QVERIFY2(importer.errorString().isEmpty(), qPrintable(importer.errorString()));
        QCOMPARE(moved.size(), 1);
        QCOMPARE(readFile(destination), updated);
        const auto after = storage->fileTracking(iiSocietyContainer::StoreSection::Models, "LLM/modified.safetensors", &error);
        QVERIFY2(!after.isEmpty(), qPrintable(error));
        QCOMPARE(after.value("objectKey"), before.value("objectKey"));
        QCOMPARE(after.value("rawKey"), before.value("rawKey"));
        QVERIFY(after.value("resource").toObject().value("contentRevision") !=
                before.value("resource").toObject().value("contentRevision"));
    }

    void reconcilesAnAlreadyMovedModelWithoutLosingIdentity()
    {
        QTemporaryDir target(SOCIETY_TEST_DIRECTORY "/category-recovery-XXXXXX.societycontainer");
        QVERIFY(target.isValid());
        QVERIFY(iiSocietyContainer::SocietyDrive::create(target.path()));
        QVERIFY(QDir().mkpath(target.filePath("Models/Checkpoint")));
        const auto source = target.filePath("Models/Checkpoint/recover.safetensors");
        const auto destination = target.filePath("Models/LLM/recover.safetensors");
        const auto bytes = validModel("recover the same model");
        writeFile(source, bytes);
        const auto storage = iiSocietyContainer::SharedStorage::open(target.path()); QVERIFY(storage);
        QString error;
        QVERIFY2(storage->recordFile(iiSocietyContainer::StoreSection::Models, "Checkpoint/recover.safetensors", &error), qPrintable(error));
        const auto before = storage->fileTracking(iiSocietyContainer::StoreSection::Models, "Checkpoint/recover.safetensors");
        QVERIFY(!before.value("objectKey").toString().isEmpty());
        const auto store = iiSocietyContainer::ModelStore::open(target.path(), &error); QVERIFY2(store, qPrintable(error));
        QCOMPARE(store->place(source, iiSocietyContainer::ModelType::LLM, &error), destination);
        QVERIFY2(storage->reconcileFiles(&error), qPrintable(error));
        const auto after = storage->fileTracking(iiSocietyContainer::StoreSection::Models, "LLM/recover.safetensors", &error);
        QVERIFY2(!after.isEmpty(), qPrintable(error));
        QCOMPARE(after.value("status").toString(), QString("ready"));
        QCOMPARE(after.value("objectKey"), before.value("objectKey"));
        QCOMPARE(after.value("resource").toObject().value("contentRevision"),
                 before.value("resource").toObject().value("contentRevision"));
        QCOMPARE(readFile(destination), bytes);
        QVERIFY(!QFileInfo::exists(source));
    }

    void importsWhileObjectRepositoryIsContended()
    {
        QTemporaryDir source(SOCIETY_TEST_DIRECTORY "/model-source-XXXXXX.societycontainer");
        QTemporaryDir target(SOCIETY_TEST_DIRECTORY "/model-drive-XXXXXX.societycontainer");
        const auto drive = iiSocietyContainer::SocietyDrive::create(target.path());
        QVERIFY(drive);
        const auto storage = iiSocietyContainer::SharedStorage::open(target.path());
        QVERIFY(storage);
        const auto repository = storage->fileRepository();
        QVERIFY(repository);
        const auto id = drive->storageKind() == iiSocietyContainer::SocietyDrive::StorageKind::LogicalDisk
            ? drive->storageIdentifier() : drive->identifier();
        const auto directory = iiSocietyContainer::FileRepository::packagePath(
            std::filesystem::path(target.path().toStdU16String()), id.toUtf8().toStdString()) / "operations";
        std::optional<iiFileProvider::TemporaryLease> lock;
        lock.emplace(directory, iiFileProvider::TemporaryLease::Mode::TryExclusive);
        QVERIFY(lock->locked());
        const auto path = source.filePath("contended.safetensors");
        const auto bytes = validModel("content remains intact");
        writeFile(path, bytes);
        ModelImporter importer;
        importer.setContainerPath(target.path());
        QSignalSpy done(&importer, &ModelImporter::finished);
        bool sawIndexing = false;
        connect(&importer, &ModelImporter::stateChanged, &importer, [&] {
            sawIndexing |= importer.status().contains("Indexing");
        });
        QVERIFY(importer.importFiles({QUrl::fromLocalFile(path)}));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 30000);
        QVERIFY2(importer.errorString().isEmpty(), qPrintable(importer.errorString()));
        QVERIFY(sawIndexing);
        QVERIFY(lock->locked());
        lock.reset();
        QCOMPARE(importer.progress(), 1.0);
        QCOMPARE(readFile(target.filePath("Models/Other/contended.safetensors")), bytes);
        QCOMPARE(readFile(path), bytes);
        QCOMPARE(storage->fileTracking(iiSocietyContainer::StoreSection::Models, "Other/contended.safetensors").value("status").toString(), "ready");
    }

    void cancelsWhileWaitingToPublishWithoutRemovingSource()
    {
        QTemporaryDir source(SOCIETY_TEST_DIRECTORY "/model-source-XXXXXX.societycontainer");
        QTemporaryDir target(SOCIETY_TEST_DIRECTORY "/model-drive-XXXXXX.societycontainer");
        const auto drive = iiSocietyContainer::SocietyDrive::create(target.path());
        QVERIFY(drive);
        QVERIFY(QDir().mkpath(target.filePath(".society-sync")));
        QLockFile lock(target.filePath(".society-sync/operation.lock"));
        lock.setStaleLockTime(0); QVERIFY(lock.tryLock());
        const auto path = source.filePath("cancel.safetensors");
        const auto bytes = validModel("keep this model");
        writeFile(path, bytes);
        ModelImporter importer;
        importer.setContainerPath(target.path());
        QSignalSpy done(&importer, &ModelImporter::finished);
        QVERIFY(importer.importFiles({QUrl::fromLocalFile(path)}));
        QTRY_VERIFY_WITH_TIMEOUT(importer.status().contains("Waiting for another container operation"), 15000);
        importer.cancel();
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 5000);
        QVERIFY(importer.status().contains("cancelled"));
        QVERIFY(importer.errorString().isEmpty());
        QVERIFY(!QFileInfo::exists(target.filePath("Models/Other/cancel.safetensors")));
        QCOMPARE(readFile(path), bytes);
        QVERIFY(lock.isLocked());
    }

    void rejectsRedirectedTemporaryLeaseWithoutCopying()
    {
        QTemporaryDir source(SOCIETY_TEST_DIRECTORY "/model-source-XXXXXX.societycontainer");
        QTemporaryDir target(SOCIETY_TEST_DIRECTORY "/model-drive-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(target.path()));
        const auto path = source.filePath("weights.safetensor");
        const auto bytes = validModel("preserved model");
        writeFile(path, bytes);
        const auto sentinel = target.filePath("Models/.iisacc-temporary.lock");
        QVERIFY(createNativeTestLink(path, sentinel));
        ModelImporter importer;
        importer.setContainerPath(target.path());
        QSignalSpy done(&importer, &ModelImporter::finished);
        QVERIFY(importer.importFiles({QUrl::fromLocalFile(path)}));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 30000);
        QVERIFY(!importer.errorString().isEmpty());
        QCOMPARE(readFile(path), bytes);
        QCOMPARE(entries(target.filePath("Models/Other")), QStringList{});
        QCOMPARE(QDir(target.filePath("Models")).entryList({".society-import-*"}, QDir::Files | QDir::Hidden), QStringList{});
    }

    void copiesModelsWithoutPublishingOrRemovingSources()
    {
        QTemporaryDir source(SOCIETY_TEST_DIRECTORY "/model-source-XXXXXX.societycontainer");
        QTemporaryDir target(SOCIETY_TEST_DIRECTORY "/model-drive-XXXXXX.societycontainer");
        QVERIFY(source.isValid() && target.isValid());
        QVERIFY(iiSocietyContainer::SocietyDrive::create(target.path()));
        const QByteArray bytes = validModel(QByteArray("model\0data", 10).repeated(350000));
        const QStringList names = {"모델 # 100%.safetensor", "weights.v2.SAFETENSORS"};
        QList<QUrl> urls;
        for (const auto &name : names) {
            writeFile(source.filePath(name), bytes);
            urls.append(QUrl::fromLocalFile(source.filePath(name)));
        }
        ModelImporter importer;
        importer.setContainerPath(target.path());
        QVERIFY(importer.accepts(urls));
        QSignalSpy done(&importer, &ModelImporter::finished);
        QVERIFY(importer.importFiles(urls));
        QVERIFY(importer.busy());
        QVERIFY(!importer.importFiles(urls));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 120000);
        QVERIFY(!importer.busy());
        QVERIFY2(importer.errorString().isEmpty(), qPrintable(importer.errorString()));
        QCOMPARE(importer.progress(), 1.0);
        QCOMPARE(done.first().at(1).toStringList().size(), 2);
        for (const auto &name : names) {
            QCOMPARE(readFile(target.filePath("Models/Other/" + name)), bytes);
            QCOMPARE(readFile(source.filePath(name)), bytes);
            const auto storage = iiSocietyContainer::SharedStorage::open(target.path()); QVERIFY(storage);
            const auto tracking = storage->fileTracking(iiSocietyContainer::StoreSection::Models, "Other/" + name);
            QCOMPARE(tracking.value("status").toString(), "ready");
            QCOMPARE(tracking.value("resourceStatus").toString(), "working-copy-only");
            QVERIFY(!tracking.value("resource").toObject().value("id").toString().isEmpty());
            QVERIFY(tracking.value("rawKey").toString().isEmpty());
            QVERIFY(tracking.value("sessionKey").toString().isEmpty());
        }
        QCOMPARE(entries(target.filePath("Models/Other")).size(), 2);
        QCOMPARE(entries(target.filePath("Files")), QStringList{});
    }

    void keepsConflictingFilesAndReusesModelsAlreadyInTheDrive()
    {
        QTemporaryDir source(SOCIETY_TEST_DIRECTORY "/model-source-XXXXXX.societycontainer");
        QTemporaryDir target(SOCIETY_TEST_DIRECTORY "/model-drive-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(target.path()));
        const auto path = source.filePath("weights.safetensor");
        writeFile(path, validModel("new weights"));
        QVERIFY(QDir().mkdir(target.filePath("Models/Other")));
        writeFile(target.filePath("Models/Other/weights.safetensor"), "existing weights");
        QVERIFY(QDir().mkdir(target.filePath("Models/Other/weights (1).safetensor")));
        ModelImporter importer;
        importer.setContainerPath(target.path());
        QSignalSpy done(&importer, &ModelImporter::finished);
        QVERIFY(importer.importFiles({QUrl::fromLocalFile(path), QUrl::fromLocalFile(path)}));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 120000);
        QVERIFY2(importer.errorString().isEmpty(), qPrintable(importer.errorString()));
        QCOMPARE(readFile(target.filePath("Models/Other/weights.safetensor")), QByteArray("existing weights"));
        QCOMPARE(readFile(target.filePath("Models/Other/weights (2).safetensor")), validModel("new weights"));
        QCOMPARE(entries(target.filePath("Models/Other")).size(), 3);
        QVERIFY(importer.importFiles({QUrl::fromLocalFile(target.filePath("Models/Other/weights (2).safetensor"))}));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 2, 30000);
        QCOMPARE(entries(target.filePath("Models/Other")).size(), 3);
        QVERIFY(importer.status().contains("already"));
    }

    void rejectsUnsupportedDropsAsAWhole()
    {
        QTemporaryDir target(SOCIETY_TEST_DIRECTORY "/model-drive-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(target.path()));
        writeFile(target.filePath("Files/weights.safetensors"), "preserve source");
        ModelImporter importer;
        const auto valid = QUrl::fromLocalFile(target.filePath("Files/weights.safetensors"));
        QVERIFY(!importer.importFiles({valid}));
        importer.setContainerPath(target.path());
        for (const auto &urls : QList<QList<QUrl>>{{}, {QUrl("https://example.com/model.safetensor")},
                  {valid, QUrl::fromLocalFile(target.filePath("image.png"))}, {QUrl("relative.safetensor")}}) {
            QVERIFY(!importer.accepts(urls));
            QVERIFY(!importer.importFiles(urls));
            QVERIFY(!importer.errorString().isEmpty());
            QVERIFY(!importer.busy());
        }
        QVERIFY(entries(target.filePath("Models/Other")).isEmpty());
    }

    void concurrentImportsNeverReplaceEachOther()
    {
        QTemporaryDir source(SOCIETY_TEST_DIRECTORY "/model-source-XXXXXX.societycontainer");
        QTemporaryDir target(SOCIETY_TEST_DIRECTORY "/model-drive-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(target.path()));
        const auto path = source.filePath("shared.safetensors");
        const auto bytes = validModel(QByteArray(4 * 1024 * 1024, 'x'));
        writeFile(path, bytes);
        ModelImporter first, second;
        first.setContainerPath(target.path());
        second.setContainerPath(target.path());
        QSignalSpy firstDone(&first, &ModelImporter::finished);
        QSignalSpy secondDone(&second, &ModelImporter::finished);
        QVERIFY(first.importFiles({QUrl::fromLocalFile(path)}));
        QVERIFY(second.importFiles({QUrl::fromLocalFile(path)}));
        QTRY_COMPARE_WITH_TIMEOUT(firstDone.size(), 1, 30000);
        QTRY_COMPARE_WITH_TIMEOUT(secondDone.size(), 1, 30000);
        QVERIFY2(first.errorString().isEmpty(), qPrintable(first.errorString()));
        QVERIFY2(second.errorString().isEmpty(), qPrintable(second.errorString()));
        const auto firstPath = firstDone.first().at(1).toStringList().first();
        const auto secondPath = secondDone.first().at(1).toStringList().first();
        QVERIFY(firstPath != secondPath);
        QCOMPARE(readFile(firstPath), bytes);
        QCOMPARE(readFile(secondPath), bytes);
        QCOMPARE(entries(target.filePath("Models/Other")).size(), 2);
    }

    void rejectsTruncatedModelsBeforeCopying()
    {
        QTemporaryDir source(SOCIETY_TEST_DIRECTORY "/truncated-source-XXXXXX.societycontainer");
        QTemporaryDir target(SOCIETY_TEST_DIRECTORY "/truncated-drive-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(target.path()));
        const auto path = source.filePath("incomplete.safetensors");
        const auto bytes = validModel(QByteArray(1024, 'x')).chopped(512);
        writeFile(path, bytes);
        ModelImporter importer;
        importer.setContainerPath(target.path());
        QSignalSpy done(&importer, &ModelImporter::finished);
        QVERIFY(importer.importFiles({QUrl::fromLocalFile(path)}));
        QTRY_COMPARE(done.size(), 1);
        QVERIFY(importer.errorString().contains("Download the complete file"));
        QVERIFY(done.first().at(1).toStringList().isEmpty());
        QVERIFY(entries(target.filePath("Models/Other")).isEmpty());
        QCOMPARE(readFile(path), bytes);
    }
    void rejectsMissingSourcesAndRedirectedModels()
    {
        QTemporaryDir source(SOCIETY_TEST_DIRECTORY "/model-source-XXXXXX.societycontainer");
        QTemporaryDir target(SOCIETY_TEST_DIRECTORY "/model-drive-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(target.path()));
        ModelImporter importer;
        importer.setContainerPath(target.path());
        QSignalSpy done(&importer, &ModelImporter::finished);
        QVERIFY(importer.importFiles({QUrl::fromLocalFile(source.filePath("missing.safetensor"))}));
        QTRY_COMPARE(done.size(), 1);
        QVERIFY(!importer.errorString().isEmpty());
        QVERIFY(entries(target.filePath("Models/Other")).isEmpty());
        const auto path = source.filePath("weights.safetensor");
        writeFile(path, "private model");
        QVERIFY(QDir().rename(target.filePath("Models"), target.filePath("Models-original")));
        QVERIFY(createNativeTestLink(target.filePath("Files"), target.filePath("Models")));
        QVERIFY(importer.importFiles({QUrl::fromLocalFile(path)}));
        QTRY_COMPARE(done.size(), 2);
        QVERIFY(!importer.errorString().isEmpty());
        QCOMPARE(entries(target.filePath("Files")), QStringList{});
        QCOMPARE(readFile(path), QByteArray("private model"));
    }

    void providerNamesCannotEscapeModelsAndReadFailuresAreReported()
    {
        QTemporaryDir target(SOCIETY_TEST_DIRECTORY "/model-drive-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(target.path()));
        ModelImporter importer;
        importer.setContainerPath(target.path());
        bool read = false;
        QVERIFY(!importer.importSources({{"../outside.safetensor", [&](const auto &, const auto &) {
            read = true;
            return QString();
        }}}));
        QVERIFY(!read);
        QSignalSpy done(&importer, &ModelImporter::finished);
        QVERIFY(importer.importSources({{"unavailable.safetensor", [](const auto &, const auto &) {
            return QString("Provider is unavailable");
        }}}));
        QTRY_COMPARE(done.size(), 1);
        QVERIFY(importer.errorString().contains("Provider is unavailable"));
        QVERIFY(done.first().at(1).toStringList().isEmpty());
        QVERIFY(entries(target.filePath("Models/Other")).isEmpty());
    }

    void cancellationRemovesPartialCopiesAndRetainsTheCapturedDrive()
    {
        QTemporaryDir source(SOCIETY_TEST_DIRECTORY "/model-source-XXXXXX.societycontainer");
        QTemporaryDir target(SOCIETY_TEST_DIRECTORY "/model-drive-XXXXXX.societycontainer");
        QTemporaryDir other(SOCIETY_TEST_DIRECTORY "/model-other-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(target.path()));
        QVERIFY(iiSocietyContainer::SocietyDrive::create(other.path()));
        const auto path = source.filePath("large.safetensor");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.resize(256 * 1024 * 1024));
        file.close();
        ModelImporter importer;
        importer.setContainerPath(target.path());
        QSignalSpy done(&importer, &ModelImporter::finished);
        QVERIFY(importer.importFiles({QUrl::fromLocalFile(path)}));
        importer.setContainerPath(other.path());
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(done.first().at(0).toString(), target.path());
        QTRY_VERIFY(!importer.busy());
        QVERIFY(entries(target.filePath("Models/Other")).isEmpty());
        QVERIFY(entries(other.filePath("Models/Other")).isEmpty());
        QCOMPARE(QFileInfo(path).size(), qint64(256 * 1024 * 1024));
    }

    void automaticallyOrganizesOnOpenAndClassifiesNewImports()
    {
        QTemporaryDir source(SOCIETY_TEST_DIRECTORY "/typed-source-XXXXXX.societycontainer");
        QTemporaryDir target(SOCIETY_TEST_DIRECTORY "/typed-drive-XXXXXX.societycontainer");
        QVERIFY(source.isValid() && target.isValid());
        QVERIFY(iiSocietyContainer::SocietyDrive::create(target.path()));
        const auto bytesFor = [](const QString &type) {
            const auto header = QJsonDocument(QJsonObject{{"__metadata__", QJsonObject{{"society.model_type", type}}},
                {"tensor", QJsonObject{{"dtype", "F32"}, {"shape", QJsonArray{1}}, {"data_offsets", QJsonArray{0, 4}}}}})
                    .toJson(QJsonDocument::Compact);
            QByteArray bytes(8, '\0'); qToLittleEndian(quint64(header.size()), bytes.data()); return bytes + header + QByteArray(4, '\0');
        };
        writeFile(target.filePath("Models/old.safetensors"), bytesFor("Checkpoint"));
        ModelImporter importer;
        QSignalSpy organized(&importer, &ModelImporter::organized);
        QSignalSpy imported(&importer, &ModelImporter::finished);
        importer.setContainerPath(target.path());
        QTRY_COMPARE_WITH_TIMEOUT(organized.size(), 1, 120000);
        QVERIFY2(importer.errorString().isEmpty(), qPrintable(importer.errorString()));
        QCOMPARE(imported.size(), 0); // Startup organization must not navigate away from the current tab.
        QVERIFY(QFileInfo::exists(target.filePath("Models/Checkpoint/old.safetensors")));
        QVERIFY(!QFileInfo::exists(target.filePath("Models/old.safetensors")));
        writeFile(source.filePath("new.safetensors"), bytesFor("LoRA"));
        QVERIFY(importer.importFiles({QUrl::fromLocalFile(source.filePath("new.safetensors"))}));
        QTRY_COMPARE_WITH_TIMEOUT(imported.size(), 1, 30000);
        QCOMPARE(imported.first().at(1).toStringList(), QStringList{target.filePath("Models/LoRA/new.safetensors")});
        QCOMPARE(readFile(target.filePath("Models/LoRA/new.safetensors")), readFile(source.filePath("new.safetensors")));
        QVERIFY(importer.organizeModels());
        QTRY_COMPARE_WITH_TIMEOUT(organized.size(), 2, 120000);
        QVERIFY(organized.last().at(1).toStringList().isEmpty());
        QVERIFY2(importer.errorString().isEmpty(), qPrintable(importer.errorString()));
    }

    void recoversAnimaFromOtherAndImportsRenamedCheckpoints()
    {
        QTemporaryDir source(SOCIETY_TEST_DIRECTORY "/anima-source-XXXXXX.societycontainer");
        QTemporaryDir target(SOCIETY_TEST_DIRECTORY "/anima-drive-XXXXXX.societycontainer");
        QVERIFY(source.isValid() && target.isValid());
        QVERIFY(iiSocietyContainer::SocietyDrive::create(target.path()));
        QJsonObject tensors;
        int offset = 0;
        const auto add = [&](const QString &name, const QJsonArray &shape) {
            int size = 4;
            for (const auto &dimension : shape) size *= dimension.toInt();
            tensors.insert(name, QJsonObject{{"dtype", "F32"}, {"shape", shape}, {"data_offsets", QJsonArray{offset, offset + size}}});
            offset += size;
        };
        add("net.blocks.0.mlp.layer1.weight", {32, 8});
        add("net.llm_adapter.blocks.0.cross_attn.q_proj.weight", {4, 4});
        add("net.x_embedder.proj.1.weight", {8, 68});
        add("net.final_layer.linear.weight", {64, 8});
        const auto header = QJsonDocument(tensors).toJson(QJsonDocument::Compact);
        QByteArray bytes(8, '\0'); qToLittleEndian(quint64(header.size()), bytes.data());
        bytes += header + QByteArray(offset, '\0');
        QVERIFY(QDir().mkdir(target.filePath("Models/Other")));
        writeFile(target.filePath("Models/Other/old.safetensors"), bytes);
        ModelImporter importer;
        QSignalSpy organized(&importer, &ModelImporter::organized);
        QSignalSpy imported(&importer, &ModelImporter::finished);
        importer.setContainerPath(target.path());
        QTRY_COMPARE_WITH_TIMEOUT(organized.size(), 1, 120000);
        QVERIFY2(importer.errorString().isEmpty(), qPrintable(importer.errorString()));
        QCOMPARE(imported.size(), 0);
        QCOMPARE(readFile(target.filePath("Models/Checkpoint/old.safetensors")), bytes);
        QVERIFY(!QFileInfo::exists(target.filePath("Models/Other/old.safetensors")));
        writeFile(source.filePath("no-model-name.safetensors"), bytes);
        QVERIFY(importer.importFiles({QUrl::fromLocalFile(source.filePath("no-model-name.safetensors"))}));
        QTRY_COMPARE_WITH_TIMEOUT(imported.size(), 1, 30000);
        QCOMPARE(imported.first().at(1).toStringList(), QStringList{target.filePath("Models/Checkpoint/no-model-name.safetensors")});
        QCOMPARE(readFile(source.filePath("no-model-name.safetensors")), bytes);
        QCOMPARE(readFile(target.filePath("Models/Checkpoint/no-model-name.safetensors")), bytes);
        QVERIFY(importer.organizeModels());
        QTRY_COMPARE_WITH_TIMEOUT(organized.size(), 2, 120000);
        QVERIFY(organized.last().at(1).toStringList().isEmpty());
    }
};

QTEST_GUILESS_MAIN(ModelImporterTest)
#include "tst_modelimporter.moc"

#include "App/Files/ModelImporter.h"
#include <SocietyDrive.h>
#include <ModelStore.h>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtEndian>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace {
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
QStringList entries(const QString &path)
{
    return QDir(path).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);
}
}

class ModelImporterTest : public QObject
{
    Q_OBJECT
private slots:
    void copiesModelsWithoutPublishingOrRemovingSources()
    {
        QTemporaryDir source(SOCIETY_TEST_DIRECTORY "/model-source-XXXXXX");
        QTemporaryDir target(SOCIETY_TEST_DIRECTORY "/model-drive-XXXXXX");
        QVERIFY(source.isValid() && target.isValid());
        QVERIFY(iiSocietyContainer::SocietyDrive::create(target.path()));
        const QByteArray bytes = QByteArray("model\0data", 10).repeated(350000);
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
        QTRY_COMPARE(done.size(), 1);
        QVERIFY(!importer.busy());
        QVERIFY2(importer.errorString().isEmpty(), qPrintable(importer.errorString()));
        QCOMPARE(importer.progress(), 1.0);
        QCOMPARE(done.first().at(1).toStringList().size(), 2);
        for (const auto &name : names) {
            QCOMPARE(readFile(target.filePath("Models/Other/" + name)), bytes);
            QCOMPARE(readFile(source.filePath(name)), bytes);
        }
        QCOMPARE(entries(target.filePath("Models/Other")).size(), 2);
        QCOMPARE(entries(target.filePath("Files")), (QStringList{"3D objects", "Audios", "Documents"}));
    }

    void keepsConflictingFilesAndReusesModelsAlreadyInTheDrive()
    {
        QTemporaryDir source(SOCIETY_TEST_DIRECTORY "/model-source-XXXXXX");
        QTemporaryDir target(SOCIETY_TEST_DIRECTORY "/model-drive-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(target.path()));
        const auto path = source.filePath("weights.safetensor");
        writeFile(path, "new weights");
        writeFile(target.filePath("Models/Other/weights.safetensor"), "existing weights");
        QVERIFY(QDir().mkdir(target.filePath("Models/Other/weights (1).safetensor")));
        ModelImporter importer;
        importer.setContainerPath(target.path());
        QSignalSpy done(&importer, &ModelImporter::finished);
        QVERIFY(importer.importFiles({QUrl::fromLocalFile(path), QUrl::fromLocalFile(path)}));
        QTRY_COMPARE(done.size(), 1);
        QVERIFY2(importer.errorString().isEmpty(), qPrintable(importer.errorString()));
        QCOMPARE(readFile(target.filePath("Models/Other/weights.safetensor")), QByteArray("existing weights"));
        QCOMPARE(readFile(target.filePath("Models/Other/weights (2).safetensor")), QByteArray("new weights"));
        QCOMPARE(entries(target.filePath("Models/Other")).size(), 3);
        QVERIFY(importer.importFiles({QUrl::fromLocalFile(target.filePath("Models/Other/weights (2).safetensor"))}));
        QTRY_COMPARE(done.size(), 2);
        QCOMPARE(entries(target.filePath("Models/Other")).size(), 3);
        QVERIFY(importer.status().contains("already"));
    }

    void rejectsUnsupportedDropsAsAWhole()
    {
        QTemporaryDir target(SOCIETY_TEST_DIRECTORY "/model-drive-XXXXXX");
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
        QTemporaryDir source(SOCIETY_TEST_DIRECTORY "/model-source-XXXXXX");
        QTemporaryDir target(SOCIETY_TEST_DIRECTORY "/model-drive-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(target.path()));
        const auto path = source.filePath("shared.safetensors");
        const auto bytes = QByteArray(4 * 1024 * 1024, 'x');
        writeFile(path, bytes);
        ModelImporter first, second;
        first.setContainerPath(target.path());
        second.setContainerPath(target.path());
        QSignalSpy firstDone(&first, &ModelImporter::finished);
        QSignalSpy secondDone(&second, &ModelImporter::finished);
        QVERIFY(first.importFiles({QUrl::fromLocalFile(path)}));
        QVERIFY(second.importFiles({QUrl::fromLocalFile(path)}));
        QTRY_COMPARE(firstDone.size(), 1);
        QTRY_COMPARE(secondDone.size(), 1);
        QVERIFY2(first.errorString().isEmpty(), qPrintable(first.errorString()));
        QVERIFY2(second.errorString().isEmpty(), qPrintable(second.errorString()));
        const auto firstPath = firstDone.first().at(1).toStringList().first();
        const auto secondPath = secondDone.first().at(1).toStringList().first();
        QVERIFY(firstPath != secondPath);
        QCOMPARE(readFile(firstPath), bytes);
        QCOMPARE(readFile(secondPath), bytes);
        QCOMPARE(entries(target.filePath("Models/Other")).size(), 2);
    }

    void rejectsMissingSourcesAndRedirectedModels()
    {
        QTemporaryDir source(SOCIETY_TEST_DIRECTORY "/model-source-XXXXXX");
        QTemporaryDir target(SOCIETY_TEST_DIRECTORY "/model-drive-XXXXXX");
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
        QVERIFY(QFile::link(target.filePath("Files"), target.filePath("Models")));
        QVERIFY(importer.importFiles({QUrl::fromLocalFile(path)}));
        QTRY_COMPARE(done.size(), 2);
        QVERIFY(!importer.errorString().isEmpty());
        QCOMPARE(entries(target.filePath("Files")), (QStringList{"3D objects", "Audios", "Documents"}));
        QCOMPARE(readFile(path), QByteArray("private model"));
    }

    void providerNamesCannotEscapeModelsAndReadFailuresAreReported()
    {
        QTemporaryDir target(SOCIETY_TEST_DIRECTORY "/model-drive-XXXXXX");
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
        QTemporaryDir source(SOCIETY_TEST_DIRECTORY "/model-source-XXXXXX");
        QTemporaryDir target(SOCIETY_TEST_DIRECTORY "/model-drive-XXXXXX");
        QTemporaryDir other(SOCIETY_TEST_DIRECTORY "/model-other-XXXXXX");
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
        QTemporaryDir source(SOCIETY_TEST_DIRECTORY "/typed-source-XXXXXX");
        QTemporaryDir target(SOCIETY_TEST_DIRECTORY "/typed-drive-XXXXXX");
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
        QTRY_COMPARE(organized.size(), 1);
        QVERIFY2(importer.errorString().isEmpty(), qPrintable(importer.errorString()));
        QCOMPARE(imported.size(), 0); // Startup organization must not navigate away from the current tab.
        QVERIFY(QFileInfo::exists(target.filePath("Models/Checkpoint/old.safetensors")));
        QVERIFY(!QFileInfo::exists(target.filePath("Models/old.safetensors")));
        writeFile(source.filePath("new.safetensors"), bytesFor("LoRA"));
        QVERIFY(importer.importFiles({QUrl::fromLocalFile(source.filePath("new.safetensors"))}));
        QTRY_COMPARE(imported.size(), 1);
        QCOMPARE(imported.first().at(1).toStringList(), QStringList{target.filePath("Models/LoRA/new.safetensors")});
        QCOMPARE(readFile(target.filePath("Models/LoRA/new.safetensors")), readFile(source.filePath("new.safetensors")));
        QVERIFY(importer.organizeModels());
        QTRY_COMPARE(organized.size(), 2);
        QVERIFY(organized.last().at(1).toStringList().isEmpty());
        QVERIFY2(importer.errorString().isEmpty(), qPrintable(importer.errorString()));
    }

    void recoversAnimaFromOtherAndImportsRenamedCheckpoints()
    {
        QTemporaryDir source(SOCIETY_TEST_DIRECTORY "/anima-source-XXXXXX");
        QTemporaryDir target(SOCIETY_TEST_DIRECTORY "/anima-drive-XXXXXX");
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
        writeFile(target.filePath("Models/Other/old.safetensors"), bytes);
        ModelImporter importer;
        QSignalSpy organized(&importer, &ModelImporter::organized);
        QSignalSpy imported(&importer, &ModelImporter::finished);
        importer.setContainerPath(target.path());
        QTRY_COMPARE(organized.size(), 1);
        QVERIFY2(importer.errorString().isEmpty(), qPrintable(importer.errorString()));
        QCOMPARE(imported.size(), 0);
        QCOMPARE(readFile(target.filePath("Models/Checkpoint/old.safetensors")), bytes);
        QVERIFY(!QFileInfo::exists(target.filePath("Models/Other/old.safetensors")));
        writeFile(source.filePath("no-model-name.safetensors"), bytes);
        QVERIFY(importer.importFiles({QUrl::fromLocalFile(source.filePath("no-model-name.safetensors"))}));
        QTRY_COMPARE(imported.size(), 1);
        QCOMPARE(imported.first().at(1).toStringList(), QStringList{target.filePath("Models/Checkpoint/no-model-name.safetensors")});
        QCOMPARE(readFile(source.filePath("no-model-name.safetensors")), bytes);
        QCOMPARE(readFile(target.filePath("Models/Checkpoint/no-model-name.safetensors")), bytes);
        QVERIFY(importer.organizeModels());
        QTRY_COMPARE(organized.size(), 2);
        QVERIFY(organized.last().at(1).toStringList().isEmpty());
    }
};

QTEST_GUILESS_MAIN(ModelImporterTest)
#include "tst_modelimporter.moc"

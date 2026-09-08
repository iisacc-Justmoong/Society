#include "App/Files/ModelImporter.h"
#include <SocietyDrive.h>
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
            QCOMPARE(readFile(target.filePath("Models/" + name)), bytes);
            QCOMPARE(readFile(source.filePath(name)), bytes);
        }
        QCOMPARE(entries(target.filePath("Models")).size(), 2);
        QVERIFY(entries(target.filePath("Files")).isEmpty());
    }

    void keepsConflictingFilesAndReusesModelsAlreadyInTheDrive()
    {
        QTemporaryDir source(SOCIETY_TEST_DIRECTORY "/model-source-XXXXXX");
        QTemporaryDir target(SOCIETY_TEST_DIRECTORY "/model-drive-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(target.path()));
        const auto path = source.filePath("weights.safetensor");
        writeFile(path, "new weights");
        writeFile(target.filePath("Models/weights.safetensor"), "existing weights");
        QVERIFY(QDir().mkdir(target.filePath("Models/weights (1).safetensor")));
        ModelImporter importer;
        importer.setContainerPath(target.path());
        QSignalSpy done(&importer, &ModelImporter::finished);
        QVERIFY(importer.importFiles({QUrl::fromLocalFile(path), QUrl::fromLocalFile(path)}));
        QTRY_COMPARE(done.size(), 1);
        QVERIFY2(importer.errorString().isEmpty(), qPrintable(importer.errorString()));
        QCOMPARE(readFile(target.filePath("Models/weights.safetensor")), QByteArray("existing weights"));
        QCOMPARE(readFile(target.filePath("Models/weights (2).safetensor")), QByteArray("new weights"));
        QCOMPARE(entries(target.filePath("Models")).size(), 3);
        QVERIFY(importer.importFiles({QUrl::fromLocalFile(target.filePath("Models/weights (2).safetensor"))}));
        QTRY_COMPARE(done.size(), 2);
        QCOMPARE(entries(target.filePath("Models")).size(), 3);
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
        QVERIFY(entries(target.filePath("Models")).isEmpty());
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
        QCOMPARE(entries(target.filePath("Models")).size(), 2);
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
        QVERIFY(entries(target.filePath("Models")).isEmpty());
        const auto path = source.filePath("weights.safetensor");
        writeFile(path, "private model");
        QVERIFY(QDir().rmdir(target.filePath("Models")));
        QVERIFY(QFile::link(target.filePath("Files"), target.filePath("Models")));
        QVERIFY(importer.importFiles({QUrl::fromLocalFile(path)}));
        QTRY_COMPARE(done.size(), 2);
        QVERIFY(!importer.errorString().isEmpty());
        QVERIFY(entries(target.filePath("Files")).isEmpty());
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
        QVERIFY(entries(target.filePath("Models")).isEmpty());
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
        importer.cancel();
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(done.first().at(0).toString(), target.path());
        QVERIFY(!importer.busy());
        QVERIFY(importer.status().contains("cancel", Qt::CaseInsensitive));
        QVERIFY(entries(target.filePath("Models")).isEmpty());
        QVERIFY(entries(other.filePath("Models")).isEmpty());
        QCOMPARE(QFileInfo(path).size(), qint64(256 * 1024 * 1024));
    }
};

QTEST_GUILESS_MAIN(ModelImporterTest)
#include "tst_modelimporter.moc"

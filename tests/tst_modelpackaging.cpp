#include "Society/Tools/ModelPackagingController.h"
#include "Society/Tools/MergeModelCatalog.h"
#include "backend/runtime/appbootstrap.h"
#include <QDir>
#include <SharedStorage.h>
#include <FileOperations.h>
#include <QScopeGuard>
#include <QClipboard>
#include <QGuiApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJSValue>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>
#include <algorithm>

static bool write(const QString &path, const QByteArray &data)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}
static QByteArray safetensors(const QStringList &keys, const QJsonObject &metadata = {}, int bytes = 2)
{
    QJsonObject header;
    QByteArray payload;
    for (const auto &key : keys) {
        header[key] = QJsonObject{{"dtype", "BF16"}, {"shape", QJsonArray{bytes / 2}},
            {"data_offsets", QJsonArray{payload.size(), payload.size() + bytes}}};
        payload += QByteArray(bytes, '\x11');
    }
    if (!metadata.isEmpty()) header["__metadata__"] = metadata;
    auto json = QJsonDocument(header).toJson(QJsonDocument::Compact);
    json += QByteArray((8 - json.size() % 8) % 8, ' ');
    QByteArray prefix(8, 0); qToLittleEndian<quint64>(quint64(json.size()), prefix.data());
    return prefix + json + payload;
}
static QByteArray integer(quint64 value, int bytes)
{
    QByteArray data(bytes, 0);
    for (int i = 0; i < bytes; ++i) data[i] = char((value >> (i * 8)) & 255);
    return data;
}
static QByteArray ggufString(const QByteArray &text) { return integer(text.size(), 8) + text; }
static QByteArray gguf()
{
    auto data = QByteArray("GGUF") + integer(3,4) + integer(1,8) + integer(1,8);
    data += ggufString("general.architecture") + integer(8,4) + ggufString("gemma3");
    data += ggufString("token_embd.weight") + integer(1,4) + integer(1,8) + integer(0,4) + integer(0,8);
    data += QByteArray((32 - data.size() % 32) % 32, 0) + QByteArray(4, '\x11');
    return data;
}
static QQuickItem *visual(QQuickItem *parent, const QString &name)
{
    if (parent->objectName() == name) return parent;
    for (auto *child : parent->childItems()) if (auto *found = visual(child, name)) return found;
    return nullptr;
}

class ModelPackagingTest final : public QObject {
    Q_OBJECT
    QTemporaryDir m_root{SOCIETY_TEST_DIRECTORY "/model-packaging-XXXXXX"};
    QString folder, packageRoot;
    QByteArray previousContainer;
    QStringList warnings;
    void load(QQmlApplicationEngine &engine) {
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        connect(&engine, &QQmlApplicationEngine::warnings, this, [this](const QList<QQmlError> &errors) {
            for (const auto &error : errors) warnings.append(error.toString());
        });
        engine.load(QUrl::fromLocalFile(QString::fromUtf8(SOCIETY_PACKAGING_QML_FILE)));
    }
private slots:
    void initTestCase() {
        QVERIFY(m_root.isValid());
        previousContainer = qgetenv("SOCIETY_CONTAINER_PATH");
        const auto drive = iiSocietyContainer::SocietyDrive::createAt(m_root.path()); QVERIFY(drive);
        packageRoot = drive->sectionPath(iiSocietyContainer::StoreSection::Models);
        QVERIFY(QDir().mkdir(packageRoot + "/Checkpoint")); packageRoot += "/Checkpoint";
        qputenv("SOCIETY_CONTAINER_PATH", drive->rootPath().toUtf8());
        qmlRegisterType<ModelPackagingController>("Society", 1, 0, "ModelPackagingController");
        qmlRegisterType<MergeModelCatalog>("Society", 1, 0, "MergeModelCatalog");
        ModelPackagingController controller;
        QVERIFY(controller.supported());
        folder = m_root.filePath("LTX folder"); QVERIFY(QDir().mkpath(folder));
        const auto metadata = QJsonObject{{"model_version", "2.3.0"}, {"license", "test-license"}, {"config", "{\"_class_name\":\"LTXModel\"}"}};
        const QStringList keys{"model.diffusion_model.weight", "vae.decoder.weight", "audio_vae.decoder.weight", "vocoder.vocoder.weight", "text_embedding_projection.audio_aggregate_embed.weight"};
        QVERIFY(write(folder + "/ltxv23.safetensors", safetensors(keys, metadata)));
        QVERIFY(write(folder + "/video_vae.safetensors", safetensors({"decoder.weight"})));
        QVERIFY(write(folder + "/audio_vae.safetensors", safetensors(keys.mid(2,2))));
        QVERIFY(write(folder + "/projections.safetensors", safetensors(keys.mid(4,1))));
        QVERIFY(write(folder + "/gemma-3.gguf", gguf()));
        QVERIFY(write(folder + "/spatial_upscaler_x2.safetensors", safetensors({"initial_conv.weight"}, {{"config", "{\"_class_name\":\"LatentUpsampler\"}"}})));
        QVERIFY(write(folder + "/failed.safetensors", "Auth failed: credentials expired"));
    }
    void selectsLibraryModelsAndPackagesOnlyThoseInputs() {
        const auto models = QFileInfo(packageRoot).absolutePath();
        const auto main = packageRoot + "/select-main.safetensors";
        const auto vae = models + "/VAE/select-decoder.safetensors";
        const auto encoder = models + "/LLM/select-encoder.gguf";
        QVERIFY(QDir().mkpath(QFileInfo(vae).absolutePath()));
        QVERIFY(QDir().mkpath(QFileInfo(encoder).absolutePath()));
        QVERIFY(write(main, safetensors({"model.diffusion_model.weight"})));
        QVERIFY(write(vae, safetensors({"decoder.weight"})));
        QVERIFY(write(encoder, gguf()));
        QVERIFY(write(main + ".iisacc.json", "{\"internal\":true}"));
        ModelPackagingController controller;
        QVERIFY(!controller.scanSelection(models, {folder + "/ltxv23.safetensors"}));
        QVERIFY(!controller.scanSelection(models, {main + ".iisacc.json"}));
        QSignalSpy done(&controller, &ModelPackagingController::finished);
        QVERIFY(controller.scanSelection(models, {main, vae, encoder}));
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 60000);
        QVERIFY2(done.first().first().toBool(), qPrintable(controller.errorString()));
        const auto files = controller.report().value("files").toList();
        QCOMPARE(files.size(), 3);
        QStringList listed;
        for (const auto &file : files) listed.append(file.toMap().value("path").toString());
        QVERIFY(listed.contains(main)); QVERIFY(listed.contains(vae)); QVERIFY(listed.contains(encoder));
        const auto output = packageRoot + "/selected-package.safetensors";
        QVERIFY2(controller.createPackage(output), qPrintable(controller.errorString()));
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 2, 60000);
        QVERIFY2(done.last().first().toBool(), qPrintable(controller.errorString()));
        QVERIFY(controller.report().value("verified").toBool());
        QVERIFY(QFileInfo::exists(main)); QVERIFY(QFileInfo::exists(vae)); QVERIFY(QFileInfo::exists(encoder));
        QVERIFY(QFileInfo::exists(main + ".iisacc.json"));
    }
    void manualAssignmentsChangeTheWrittenPackage() {
        const auto input = m_root.filePath("manual-assignments");
        QVERIFY(QDir().mkpath(input));
        QVERIFY(write(input + "/large.safetensors", safetensors({"large.weight"}, {}, 200)));
        QVERIFY(write(input + "/small.safetensors", safetensors({"small.weight"})));
        QVERIFY(write(input + "/encoder.safetensors", safetensors({"encoder.weight"})));
        ModelPackagingController controller;
        QSignalSpy done(&controller, &ModelPackagingController::finished);
        QVERIFY(controller.scanFolder(input)); QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 15000);
        QVERIFY2(done.last().first().toBool(), qPrintable(controller.errorString()));
        QVERIFY(controller.setFilePosition("small.safetensors", "main"));
        QVERIFY(controller.setFileRole("encoder.safetensors", "text_encoder"));
        QVERIFY(controller.assignmentsDirty());
        const auto output = packageRoot + "/manual-assignments.safetensors";
        QVERIFY(!controller.createPackage(output));
        QVERIFY(!QFileInfo::exists(output));
        QVERIFY(controller.applyAssignments()); QTRY_COMPARE_WITH_TIMEOUT(done.count(), 2, 15000);
        QVERIFY2(done.last().first().toBool(), qPrintable(controller.errorString()));
        QVERIFY(!controller.assignmentsDirty());
        QVERIFY(controller.createPackage(output)); QTRY_COMPARE_WITH_TIMEOUT(done.count(), 3, 15000);
        QVERIFY2(done.last().first().toBool(), qPrintable(controller.errorString()));
        QFile file(output); QVERIFY(file.open(QIODevice::ReadOnly));
        const auto prefix = file.read(8); QCOMPARE(prefix.size(), 8);
        const auto size = qFromLittleEndian<quint64>(prefix.constData());
        const auto header = QJsonDocument::fromJson(file.read(qint64(size))).object();
        QVERIFY(header.contains("small.weight"));
        QVERIFY(header.contains("components/model/large.safetensors/large.weight"));
        QVERIFY(header.contains("components/text_encoder/encoder.safetensors/encoder.weight"));
        QVERIFY(controller.setFileRole("encoder.safetensors", ""));
        QVERIFY(controller.setFilePosition("small.safetensors", ""));
        QVERIFY(controller.assignmentsDirty()); QVERIFY(controller.applyAssignments());
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 4, 15000);
        QVERIFY2(done.last().first().toBool(), qPrintable(controller.errorString()));
        for (const auto &value : controller.report().value("files").toList()) {
            const auto row = value.toMap();
            if (row.value("id").toString() == "encoder.safetensors") QCOMPARE(row.value("role").toString(), QString("model"));
            if (row.value("id").toString() == "large.safetensors") QCOMPARE(row.value("position").toString(), QString("main"));
        }
    }
    void qmlManualAssignmentsGatePackagingUntilApplied() {
        const auto input = m_root.filePath("manual-qml"); QVERIFY(QDir().mkpath(input));
        QVERIFY(write(input + "/main.safetensors", safetensors({"main.weight"}, {}, 200)));
        QVERIFY(write(input + "/secondary.safetensors", safetensors({"secondary.weight"})));
        QVERIFY(write(input + "/encoder.safetensors", safetensors({"encoder.weight"})));
        warnings.clear(); QQmlApplicationEngine engine; load(engine);
        QVERIFY2(!engine.rootObjects().isEmpty(), qPrintable(warnings.join('\n')));
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        auto *tool = window->findChild<QQuickItem *>("modelPackagingTool"); QVERIFY(tool);
        auto *controller = window->findChild<ModelPackagingController *>("modelPackagingController"); QVERIFY(controller);
        tool->setProperty("outputDirectory", packageRoot);
        QSignalSpy done(controller, &ModelPackagingController::finished);
        QVERIFY(controller->scanFolder(input)); QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 15000);
        QVERIFY2(done.last().first().toBool(), qPrintable(controller->errorString()));
        auto *submit = visual(window->contentItem(), "packagingSubmit"); QVERIFY(submit);
        QTRY_VERIFY(submit->isEnabled());
        auto *role = visual(window->contentItem(), "packagingRole-encoder.safetensors"); QVERIFY(role);
        QVERIFY(QMetaObject::invokeMethod(role, "chosen", Q_ARG(QVariant, QVariant("text_encoder"))));
        QVERIFY(controller->assignmentsDirty()); QVERIFY(!submit->isEnabled());
        auto *position = visual(window->contentItem(), "packagingPosition-secondary.safetensors"); QVERIFY(position);
        QVERIFY(QMetaObject::invokeMethod(position, "chosen", Q_ARG(QVariant, QVariant("main"))));
        auto *apply = visual(window->contentItem(), "packagingApplyAssignments"); QVERIFY(apply);
        QVERIFY(apply->isEnabled()); QVERIFY(QMetaObject::invokeMethod(apply, "clicked"));
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 2, 15000);
        QVERIFY2(done.last().first().toBool(), qPrintable(controller->errorString()));
        QVERIFY(!controller->assignmentsDirty()); QTRY_VERIFY(submit->isEnabled());
        window->resize(360, 800); QTRY_COMPARE(window->width(), 360); QTest::qWait(150);
        role = visual(window->contentItem(), "packagingRole-encoder.safetensors"); QVERIFY(role);
        QVERIFY(role->mapToScene(QPointF(role->width(), 0)).x() <= 360);
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        window->close();
    }
    void roleChangesReevaluateOptionalSelections() {
        ModelPackagingController controller;
        QSignalSpy done(&controller, &ModelPackagingController::finished);
        QVERIFY(controller.scanFolder(folder)); QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 15000);
        QVERIFY2(done.last().first().toBool(), qPrintable(controller.errorString()));
        QVERIFY(controller.setComponentIncluded("spatial_upscaler_x2.safetensors", false));
        QVERIFY(controller.setFileRole("spatial_upscaler_x2.safetensors", "other"));
        QVERIFY(controller.applyAssignments()); QTRY_COMPARE_WITH_TIMEOUT(done.count(), 2, 15000);
        QVERIFY2(done.last().first().toBool(), qPrintable(controller.errorString()));
        const auto files = controller.report().value("files").toList();
        const auto found = std::find_if(files.cbegin(), files.cend(), [](const QVariant &v) {
            return v.toMap().value("id").toString() == "spatial_upscaler_x2.safetensors";
        });
        QVERIFY(found != files.cend());
        QCOMPARE(found->toMap().value("role").toString(), QString("other"));
        QCOMPARE(found->toMap().value("status").toString(), QString("included"));
        QVERIFY(!found->toMap().value("optional").toBool());
    }
    void optionalShardsAreSelectedTogether() {
        const auto input = m_root.filePath("optional-shards"); QVERIFY(QDir().mkpath(input));
        QVERIFY(write(input + "/a.safetensors", safetensors({"a"})));
        QVERIFY(write(input + "/b.safetensors", safetensors({"b"})));
        QVERIFY(write(input + "/main.safetensors", safetensors({"main"})));
        QVERIFY(write(input + "/model.safetensors.index.json", "{\"weight_map\":{\"a\":\"a.safetensors\",\"b\":\"b.safetensors\"}}"));
        ModelPackagingController controller; QSignalSpy done(&controller, &ModelPackagingController::finished);
        QVERIFY(controller.scanFolder(input)); QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 15000);
        QVERIFY(controller.setFileRole("a.safetensors", "lora")); QVERIFY(controller.applyAssignments());
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 2, 15000);
        QVERIFY2(done.last().first().toBool(), qPrintable(controller.errorString()));
        QVERIFY(controller.setComponentIncluded("a.safetensors", false));
        for (const auto &value : controller.report().value("files").toList()) {
            const auto file = value.toMap(); const auto name = file.value("id").toString();
            if (name == "a.safetensors" || name == "b.safetensors" || name == "model.safetensors.index.json")
                QCOMPARE(file.value("status").toString(), QString("excluded"));
        }
        for (const auto &value : controller.report().value("components").toList())
            QVERIFY(value.toMap().value("id").toString() != "model.safetensors.index.json");
        QVERIFY(controller.setComponentIncluded("b.safetensors", true));
        for (const auto &value : controller.report().value("files").toList())
            if (value.toMap().value("id").toString() == "a.safetensors")
                QCOMPARE(value.toMap().value("status").toString(), QString("included"));
    }
    void automaticRoleClearsAnIncompatibleMainPosition() {
        const auto input = m_root.filePath("automatic-role"); QVERIFY(QDir().mkpath(input));
        QVERIFY(write(input + "/main.safetensors", safetensors({"main.weight"})));
        QVERIFY(write(input + "/encoder.gguf", gguf()));
        ModelPackagingController controller; QSignalSpy done(&controller, &ModelPackagingController::finished);
        QVERIFY(controller.scanFolder(input)); QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 15000);
        QVERIFY(controller.setFileRole("encoder.gguf", "model"));
        QVERIFY(controller.setFilePosition("encoder.gguf", "main"));
        QVERIFY(controller.applyAssignments()); QTRY_COMPARE_WITH_TIMEOUT(done.count(), 2, 15000);
        QVERIFY2(done.last().first().toBool(), qPrintable(controller.errorString()));
        QVERIFY(controller.setFileRole("encoder.gguf", ""));
        const auto rows = controller.assignableFiles();
        const auto row = std::find_if(rows.cbegin(), rows.cend(), [](const QVariant &v) { return v.toMap().value("id").toString() == "encoder.gguf"; });
        QVERIFY(row != rows.cend());
        QCOMPARE(row->toMap().value("effectiveRole").toString(), QString("text_encoder"));
        QVERIFY(row->toMap().value("positionChoice").toString().isEmpty());
        QVERIFY(controller.applyAssignments()); QTRY_COMPARE_WITH_TIMEOUT(done.count(), 3, 15000);
        QVERIFY2(done.last().first().toBool(), qPrintable(controller.errorString()));
    }
    void automaticallyPromotedGgufCanBePinnedAsMain() {
        const auto input = m_root.filePath("standalone-gguf"); QVERIFY(QDir().mkpath(input));
        QVERIFY(write(input + "/main.gguf", gguf()));
        ModelPackagingController controller; QSignalSpy done(&controller, &ModelPackagingController::finished);
        QVERIFY(controller.scanFolder(input)); QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 15000);
        QVERIFY2(done.last().first().toBool(), qPrintable(controller.errorString()));
        QVERIFY(controller.setFilePosition("main.gguf", "main"));
        QVERIFY(controller.applyAssignments()); QTRY_COMPARE_WITH_TIMEOUT(done.count(), 2, 15000);
        QVERIFY2(done.last().first().toBool(), qPrintable(controller.errorString()));
    }
    void nativeSelectionIsInspectedAndInvalidatedWhenDriveChanges() {
        warnings.clear(); QQmlApplicationEngine engine; load(engine);
        QVERIFY2(!engine.rootObjects().isEmpty(), qPrintable(warnings.join('\n')));
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        auto *tool = window->findChild<QQuickItem *>("modelPackagingTool"); QVERIFY(tool);
        auto *catalog = window->findChild<MergeModelCatalog *>("packagingModelCatalog"); QVERIFY(catalog);
        auto *controller = window->findChild<ModelPackagingController *>("modelPackagingController"); QVERIFY(controller);
        const auto models = QFileInfo(packageRoot).absolutePath();
        QVERIFY(tool->setProperty("modelsDirectory", models));
        QTRY_VERIFY_WITH_TIMEOUT(!catalog->loading(), 60000);
        const auto main = packageRoot + "/select-main.safetensors";
        const auto encoder = models + "/LLM/select-encoder.gguf";
        QSignalSpy done(controller, &ModelPackagingController::finished);
        emit controller->modelsChosen({main, encoder});
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 60000);
        QVERIFY2(done.first().first().toBool(), qPrintable(controller->errorString()));
        QVERIFY(controller->report().value("ready").toBool());
        QVERIFY(tool->setProperty("modelsDirectory", QString()));
        QVERIFY(!controller->report().value("ready").toBool());
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        window->close();
    }
    void asynchronousScanSelectionAndVerifiedOutput() {
        ModelPackagingController controller;
        QSignalSpy done(&controller, &ModelPackagingController::finished);
        QVERIFY(controller.scanFolder(folder)); QVERIFY(controller.busy());
        QVERIFY(!controller.scanFolder(folder));
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 15000);
        QVERIFY2(done.first().at(0).toBool(), qPrintable(controller.errorString()));
        QCOMPARE(controller.report().value("included_file_count").toInt(), 3);
        QCOMPARE(controller.report().value("duplicate_file_count").toInt(), 3);
        QCOMPARE(controller.report().value("invalid_file_count").toInt(), 1);
        QCOMPARE(controller.report().value("component_count").toInt(), 6);
        QVERIFY(!controller.setComponentIncluded("model", false));
        QVERIFY(controller.setComponentIncluded("spatial_upscaler_x2.safetensors", false));
        QCOMPARE(controller.report().value("included_file_count").toInt(), 2);
        QCOMPARE(controller.report().value("component_count").toInt(), 5);
        const auto output = controller.outputPathForName("LTX core", packageRoot);
        QVERIFY(controller.createPackage(output));
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 2, 15000);
        QVERIFY2(done.last().at(0).toBool(), qPrintable(controller.errorString()));
        QVERIFY(done.last().at(1).toBool()); QVERIFY(QFileInfo(output).isFile());
        QCOMPARE(controller.completedOutput(), output);
        QVERIFY(controller.report().value("verified").toBool());
        QCOMPARE(controller.report().value("sha256").toString().size(), 64);
        QCOMPARE(controller.report().value("included_file_count").toInt(), 2);
        controller.copyDetails();
        const auto copied = QJsonDocument::fromJson(QGuiApplication::clipboard()->text().toUtf8()).object();
        QCOMPARE(copied.value("output").toString(), output);
        QCOMPARE(copied.value("sha256").toString(), controller.report().value("sha256").toString());
        QVERIFY(!controller.createPackage(output));
        QVERIFY(controller.errorString().contains("already exists"));
    }
    void cleanupTestCase() { qputenv("SOCIETY_CONTAINER_PATH", previousContainer); }
    void externalOutputsAreRejectedBeforeDirectoryCreation() {
        ModelPackagingController controller; QSignalSpy done(&controller, &ModelPackagingController::finished);
        QVERIFY(controller.scanFolder(folder)); QTRY_COMPARE(done.count(), 1);
        const auto outside = m_root.filePath("external.safetensors");
        QVERIFY(!controller.createPackage(outside)); QVERIFY(!QFileInfo::exists(outside));
        QVERIFY(controller.errorString().contains("selected .societycontainer"));
        const auto missing = m_root.filePath("outside/Checkpoint/result.safetensors");
        QVERIFY(!controller.createPackage(missing)); QVERIFY(!QFileInfo::exists(m_root.filePath("outside")));
    }
    void invalidNamesRemoteUrlsAndMissingComponentsAreBlocked() {
        ModelPackagingController controller;
        for (const auto &name : {"../bad", "CON", "bad/name", "bad:name", "..", ""})
            QVERIFY(controller.outputPathForName(name, m_root.path()).isEmpty());
        QCOMPARE(controller.localPath(QUrl::fromLocalFile(folder)), folder);
        QVERIFY(controller.localPath(QUrl("https://example.com/model")).isEmpty());
        QCOMPARE(controller.outputPathForName("한국어.safetensors", m_root.path()), m_root.filePath("한국어.safetensors"));
        const auto incomplete = m_root.filePath("incomplete"); QVERIFY(QDir().mkpath(incomplete));
        QFile::copy(folder + "/ltxv23.safetensors", incomplete + "/ltxv23.safetensors");
        QSignalSpy done(&controller, &ModelPackagingController::finished);
        QVERIFY(controller.scanFolder(incomplete));
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 15000);
        QVERIFY(!done.first().first().toBool()); QVERIFY(!controller.report().value("ready").toBool());
        QVERIFY(controller.errorString().contains("encoder", Qt::CaseInsensitive));
        QVERIFY(!controller.createPackage(m_root.filePath("incomplete.safetensors")));
    }
    void cancellationPreservesOriginals() {
        const auto large = m_root.filePath("large"); QVERIFY(QDir().mkpath(large));
        QVERIFY(write(large + "/model.safetensors", safetensors({"model.diffusion_model.weight"}, {}, 32 * 1024 * 1024)));
        ModelPackagingController controller;
        QSignalSpy done(&controller, &ModelPackagingController::finished);
        QVERIFY(controller.scanFolder(large)); QTRY_COMPARE(done.count(), 1);
        const auto output = QDir(packageRoot).filePath("cancelled.safetensors");
        QVERIFY(controller.createPackage(output)); controller.cancel();
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 2, 15000);
        QVERIFY(!done.last().first().toBool()); QCOMPARE(controller.phase(), QString("cancelled"));
        QVERIFY(!QFileInfo::exists(output)); QVERIFY(QFileInfo::exists(large + "/model.safetensors"));
        QVERIFY(QDir(m_root.path()).entryList({"*.partial-*"}, QDir::Files | QDir::Hidden).isEmpty());
    }
    void qmlOutputFolderStaysInsideModelsAndContainerChangeCancelsWork() {
        warnings.clear(); QQmlApplicationEngine engine; load(engine);
        QVERIFY2(!engine.rootObjects().isEmpty(), qPrintable(warnings.join('\n')));
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        auto *tool = window->findChild<QQuickItem *>("modelPackagingTool"); QVERIFY(tool);
        auto *controller = window->findChild<ModelPackagingController *>("modelPackagingController"); QVERIFY(controller);
        const auto models = QFileInfo(packageRoot).absolutePath();
        QVERIFY(tool->setProperty("modelsDirectory", models));
        auto *row = visual(window->contentItem(), "packagingOutputFolder"); QVERIFY(row);
        QVERIFY(QMetaObject::invokeMethod(row, "clicked"));
        auto *dialog = window->findChild<QObject *>("packagingOutputDialog"); QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("opened").toBool());
        auto *input = visual(window->contentItem(), "packagingOutputRelativeFolder"); QVERIFY(input);
        auto *confirm = visual(window->contentItem(), "packagingOutputConfirm"); QVERIFY(confirm);
        QCOMPARE(input->property("text").toString(), QString("Checkpoint"));
        for (const auto &invalid : {"/outside", "../escaped", "C:/outside", "Checkpoint//Invalid", "Checkpoint\\Invalid"}) {
            QVERIFY(input->setProperty("text", QString::fromLatin1(invalid)));
            QVERIFY(QMetaObject::invokeMethod(confirm, "clicked"));
            QVERIFY(dialog->property("visible").toBool()); QVERIFY(tool->property("outputDirectory").toString().isEmpty());
            QVERIFY(!dialog->property("errorText").toString().isEmpty());
        }
        QVERIFY(input->setProperty("text", "Checkpoint/Packages"));
        QVERIFY(QMetaObject::invokeMethod(confirm, "clicked"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        const auto relative = models + "/Checkpoint/Packages";
        QCOMPARE(tool->property("outputDirectory").toString(), relative);
        QVERIFY(!QFileInfo::exists(relative)); // Choosing a relative output creates no folders.
        const auto large = m_root.filePath("switch source"); QVERIFY(QDir().mkpath(large));
        QVERIFY(write(large + "/model.safetensors", safetensors({"model.diffusion_model.weight"}, {}, 32 * 1024 * 1024)));
        QTemporaryDir next(SOCIETY_TEST_DIRECTORY "/packaging-next-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(next.path()));
        QSignalSpy done(controller, &ModelPackagingController::finished);
        QVERIFY(controller->scanFolder(large)); QTRY_COMPARE(done.size(), 1);
        const auto output = QDir(relative).filePath("cancelled-on-switch.safetensors");
        QVERIFY2(controller->createPackage(output), qPrintable(controller->errorString())); QVERIFY(controller->busy());
        QVERIFY(QFileInfo(relative).isDir()); // Only starting the package prepares its requested Models folder.
        QVERIFY(tool->setProperty("modelsDirectory", next.filePath("Models")));
        QTRY_COMPARE(done.size(), 2); QVERIFY(!done.last().first().toBool());
        QVERIFY(!controller->busy()); QCOMPARE(controller->phase(), QString("cancelled"));
        QVERIFY(tool->property("outputDirectory").toString().isEmpty());
        QCOMPARE(tool->property("effectiveOutputDirectory").toString(), next.filePath("Models/Checkpoint"));
        QVERIFY(!QFileInfo::exists(output)); QVERIFY(QFileInfo::exists(large + "/model.safetensors"));
        const auto runtime = QFileInfo(models).absolutePath() + "/.society-runtime/tmp";
        QVERIFY(QDir(runtime).entryList({".society-model-package-*"}, QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty());
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        window->close();
    }
    void modalSurfacesFollowTheApplicationWindowColor() {
        warnings.clear(); QQmlApplicationEngine engine; load(engine);
        QVERIFY2(!engine.rootObjects().isEmpty(), qPrintable(warnings.join('\n')));
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        auto *surface = window->findChild<QQuickItem *>("applicationWindowMaterial"); QVERIFY(surface);
        const auto backgroundFor = [](QObject *panel) {
            return panel ? qvariant_cast<QQuickItem *>(panel->property("background")) : nullptr;
        };
        QList<QQuickItem *> backgrounds;
        for (const auto &name : {"packagingDetails", "packagingProgress", "packagingCompletion"}) {
            auto *popup = window->findChild<QObject *>(QString::fromLatin1(name)); QVERIFY2(popup, name);
            auto *panel = qvariant_cast<QQuickItem *>(popup->property("contentItem")); QVERIFY2(panel, name);
            auto *background = backgroundFor(panel); QVERIFY2(background, name);
            backgrounds.append(background);
        }
        const auto initial = surface->property("color").value<QColor>();
        QVERIFY(initial.isValid());
        for (auto *background : backgrounds) {
            const auto color = background->property("color").value<QColor>();
            QCOMPARE(color.alpha(), 255);
            QCOMPARE(color, initial);
        }
        auto *ordinary = window->findChild<QQuickItem *>("packagingSourcePanel"); QVERIFY(ordinary);
        auto *ordinaryBackground = backgroundFor(ordinary); QVERIFY(ordinaryBackground);
        QCOMPARE(ordinaryBackground->property("color").value<QColor>().alpha(), 0);
        QVERIFY(window->setProperty("primaryColor", QColor(Qt::red)));
        QTRY_VERIFY(surface->property("color").value<QColor>() != initial);
        const auto changed = surface->property("color").value<QColor>();
        for (auto *background : backgrounds)
            QTRY_COMPARE(background->property("color").value<QColor>(), changed);
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        window->close();
    }
    void realQmlControlsMatchContractAndRemainResponsive() {
        warnings.clear(); QQmlApplicationEngine engine; load(engine);
        QVERIFY2(!engine.rootObjects().isEmpty(), qPrintable(warnings.join('\n')));
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        auto *tool = window->findChild<QQuickItem *>("modelPackagingTool"); QVERIFY(tool);
        auto *controller = window->findChild<ModelPackagingController *>("modelPackagingController"); QVERIFY(controller);
        tool->setProperty("outputDirectory", packageRoot);
        QSignalSpy done(controller, &ModelPackagingController::finished);
        QVERIFY(controller->scanFolder(folder)); QTRY_COMPARE(done.count(), 1);
        auto *submit = visual(window->contentItem(), "packagingSubmit"); QVERIFY(submit);
        QTRY_VERIFY(submit->isEnabled());
        QVERIFY(tool->setProperty("modelsBusy", true));
        QVERIFY(submit->isEnabled());
        QVERIFY(tool->setProperty("modelsBusy", false));
        auto *detailsButton = visual(window->contentItem(), "packagingDetailsButton"); QVERIFY(detailsButton);
        QVERIFY(QMetaObject::invokeMethod(detailsButton, "clicked"));
        auto *details = window->findChild<QObject *>("packagingDetails"); QVERIFY(details);
        QTRY_VERIFY(details->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(details, "close"));
        auto *toggle = visual(window->contentItem(), "packagingToggle-spatial_upscaler_x2.safetensors"); QVERIFY(toggle);
        QVERIFY(QMetaObject::invokeMethod(toggle, "toggle"));
        QVERIFY(QMetaObject::invokeMethod(toggle, "toggled"));
        QTRY_COMPARE(controller->report().value("component_count").toInt(), 5);
        const auto screenshot = qEnvironmentVariable("SOCIETY_PACKAGING_SCREENSHOT_PATH");
        if (!screenshot.isEmpty()) { QTest::qWait(300); QVERIFY(window->grabWindow().save(screenshot)); }
        window->resize(360, 800); QTRY_COMPARE(window->width(), 360); QTest::qWait(150);
        QVERIFY2(tool->property("outputPath").toString().endsWith(".safetensors"), qPrintable(warnings.join('\n')));
        const auto *name = visual(window->contentItem(), "packagingOutputName"); QVERIFY(name);
        QVERIFY(name->mapToScene(QPointF(name->width(),0)).x() <= 360);
        submit->forceActiveFocus(Qt::TabFocusReason);
        QVERIFY(QMetaObject::invokeMethod(tool, "revealFocusedControl", Q_ARG(QVariant, QVariant::fromValue(submit))));
        QTRY_VERIFY(submit->mapToScene(QPointF(0, submit->height())).y() <= 800);
        window->resize(1440, 900); QTest::qWait(100);
        QVERIFY(QMetaObject::invokeMethod(submit, "clicked"));
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 2, 15000);
        QVERIFY2(done.last().first().toBool(), qPrintable(controller->errorString()));
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        window->close();
    }
};
int main(int argc, char **argv) {
    lvrs::AppBootstrapOptions options;
    options.applicationName = "SocietyModelPackagingTests";
    options.quickStyleName = "Basic";
    options.bootstrapGraphicsBackend = false; options.configureRenderQualityDefaults = false;
    options.logBootstrapDiagnostics = false; options.logGraphicsBackend = false;
    if (!lvrs::preApplicationBootstrap(options).ok) return 1;
    QGuiApplication app(argc, argv); lvrs::postApplicationBootstrap(app, options);
    ModelPackagingTest test; return QTest::qExec(&test, argc, argv);
}
#include "tst_modelpackaging.moc"

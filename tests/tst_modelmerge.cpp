#include "App/Tools/ModelMergeController.h"
#include "App/Tools/MergeModelCatalog.h"
#include "backend/runtime/appbootstrap.h"

#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJSValue>
#include <QQmlApplicationEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

static QQuickItem *visualItem(QQuickItem *parent, const QString &name)
{
    if (parent->objectName() == name) return parent;
    for (auto *child : parent->childItems())
        if (auto *found = visualItem(child, name)) return found;
    return nullptr;
}

static QVariantList listProperty(QObject *object, const char *name)
{
    const auto value = object->property(name);
    return value.metaType() == QMetaType::fromType<QJSValue>()
        ? value.value<QJSValue>().toVariant().toList() : value.toList();
}

static bool writeFixture(const QString &path, const QByteArray &contents = "model fixture")
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}

class SocietyModelMergeTest final : public QObject
{
    Q_OBJECT
    QTemporaryDir m_fixture{SOCIETY_TEST_DIRECTORY "/model merge-XXXXXX"};
    bool m_hasRuntime = false;
    QString m_pythonError;

    QByteArray bytes(const QString &path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return {};
        return file.readAll();
    }
    bool python(const QStringList &arguments)
    {
        QProcess process;
        process.start(QString::fromUtf8(SOCIETY_MERGE_TEST_PYTHON),
            QStringList{QString::fromUtf8(SOCIETY_MERGE_FIXTURE)} + arguments);
        // The staged Torch runtime can spend well over 30 seconds loading from an
        // external volume on its first invocation.  Killing it here replaces the
        // real safetensors fixtures with text placeholders and makes structure
        // inspection test the fallback rather than the production contract.
        const bool done = process.waitForFinished(120000);
        m_pythonError = QString::fromUtf8(process.readAllStandardError());
        return done && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    }
    QVariantMap options(const QString &name = "merged.safetensors")
    {
        return {{"baseModel", m_fixture.filePath("base.safetensors")},
            {"mode", "weighted-sum"}, {"weightMode", "automatic"}, {"sharedWeight", "0.25"},
            {"materials", QVariantList{
                QVariantMap{{"path", m_fixture.filePath("extra.safetensors")}, {"weight", "0.25"}},
                QVariantMap{{"path", m_fixture.filePath("style.safetensors")}, {"weight", "1.5"}}}},
            {"output", m_fixture.filePath(name)}, {"cacheDirectory", m_fixture.filePath("conversion cache")},
            {"pythonExecutable", QString::fromUtf8(SOCIETY_MERGE_TEST_PYTHON)}};
    }
private slots:
    void initTestCase()
    {
        QVERIFY(m_fixture.isValid());
        qmlRegisterType<ModelMergeController>("Society", 1, 0, "ModelMergeController");
        qmlRegisterType<MergeModelCatalog>("Society", 1, 0, "MergeModelCatalog");
        ModelMergeController controller;
        QVERIFY2(!controller.defaultExecutable().isEmpty(), "Installed iild-merge is required for the integration contract.");
        m_hasRuntime = python({"create", m_fixture.path()});
        if (!m_hasRuntime) {
            for (const auto &name : {"base.safetensors", "extra.safetensors", "style.safetensors"}) {
                QFile file(m_fixture.filePath(name)); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("argument fixture");
            }
            qWarning().noquote() << "Tensor tests require the installed Torch/safetensors runtime:" << m_pythonError;
        }
    }

    void validationMapsAllParametersWithoutWritingModels()
    {
        ModelMergeController controller;
        auto request = options("output ' $(literal).safetensors");
        request["mode"] = "weighted-difference";
        request["weightMode"] = "per-model";
        QSignalSpy done(&controller, &ModelMergeController::finished);
        QVERIFY(controller.run(request, true));
        QVERIFY(controller.busy());
        QVERIFY(!controller.run(options(), false));
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 15000);
        QVERIFY2(done.first().first().toBool(), qPrintable(controller.errorString()));
        QVERIFY(done.first().at(1).toBool());
        const auto result = QJsonDocument::fromJson(controller.details().toUtf8()).object();
        QCOMPARE(result["base_model"].toString(), request["baseModel"].toString());
        QCOMPARE(result["mode"].toString(), QString("weighted-difference"));
        QCOMPARE(result["weights"].toArray(), QJsonArray({0.25, 1.5}));
        QCOMPARE(result["additional_models"].toArray().size(), 2);
        QCOMPARE(result["output"].toString(), request["output"].toString());
        QCOMPARE(result["cache_dir"].toString(), request["cacheDirectory"].toString());
        QCOMPARE(result["base_weight"].toDouble(), 1.0);
        QCOMPARE(result["inspection"].toString(), QString("structure-only"));
        QVERIFY(!QFileInfo::exists(request["output"].toString()));
        QVERIFY(!QFileInfo::exists(request["cacheDirectory"].toString()));
    }

    void catalogListsOnlyContainerModelsAndKeepsPackagesWhole()
    {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/model catalog-XXXXXX");
        QVERIFY(fixture.isValid());
        const auto models = fixture.filePath("Models");
        const QStringList files{"base.SAFETENSORS", "packed.iildmodel", "legacy.ckpt", "legacy.pt", "legacy.pth", "legacy.bin",
            "Group A/shared.safetensor", "Group B/shared.safetensor", "Loras/style.safetensors"};
        for (const auto &path : files) QVERIFY(writeFixture(QDir(models).filePath(path)));
        QVERIFY(writeFixture(QDir(models).filePath("Pipeline/model_index.json"), "{}"));
        QVERIFY(writeFixture(QDir(models).filePath("Pipeline/unet/model.safetensors")));
        QVERIFY(writeFixture(QDir(models).filePath("Adapter/adapter_config.json"), "{}"));
        QVERIFY(writeFixture(QDir(models).filePath("Adapter/adapter_model.safetensors")));
        QVERIFY(writeFixture(QDir(models).filePath("notes.txt")));
        QVERIFY(writeFixture(QDir(models).filePath("llm.gguf")));
        QVERIFY(writeFixture(QDir(models).filePath("empty.safetensors"), {}));
        QVERIFY(writeFixture(QDir(models).filePath(".staging/incomplete.safetensors")));
        QVERIFY(writeFixture(fixture.filePath("Files/outside.safetensors")));
        QVERIFY(QFile::link(fixture.filePath("Files/outside.safetensors"), QDir(models).filePath("linked.safetensors")));
        QVERIFY(QFile::link(fixture.filePath("Files"), QDir(models).filePath("linked-folder")));
        MergeModelCatalog catalog;
        catalog.setDirectory(models);
        QTRY_VERIFY(!catalog.loading());
        QVERIFY2(catalog.errorString().isEmpty(), qPrintable(catalog.errorString()));
        QCOMPARE(catalog.models().size(), files.size() + 1);
        QStringList paths;
        for (const auto &entry : catalog.models()) {
            const auto model = entry.toMap();
            const auto path = model.value("path").toString();
            QVERIFY(path.startsWith(models + '/'));
            paths.append(model.value("relativePath").toString());
            QVERIFY(catalog.contains(path));
            QCOMPARE(catalog.contains(path, true), model.value("kind") != "adapter");
        }
        for (const auto &path : files) QVERIFY(paths.contains(path));
        const auto packaged = QDir(models).filePath("packed.iildmodel");
        QCOMPARE(MergeModelCatalog::checkpointPath(packaged), packaged);
        QVERIFY(!paths.contains("Pipeline"));
        QVERIFY(paths.contains("Adapter"));
        QVERIFY(!catalog.contains(fixture.filePath("Files/outside.safetensors")));

        QSignalSpy changes(&catalog, &MergeModelCatalog::modelsChanged);
        const auto snapshot = catalog.models();
        catalog.refresh();
        QTRY_VERIFY(!catalog.loading());
        QCOMPARE(catalog.models(), snapshot);
        QCOMPARE(changes.count(), 0); // Do not reset an open menu for an unchanged snapshot.
        QVERIFY(QFile::remove(QDir(models).filePath("legacy.pt")));
        QVERIFY(writeFixture(QDir(models).filePath("new model.safetensors")));
        catalog.refresh();
        QTRY_VERIFY(!catalog.loading());
        QVERIFY(!catalog.contains(QDir(models).filePath("legacy.pt")));
        QVERIFY(catalog.contains(QDir(models).filePath("new model.safetensors")));

        const auto otherModels = fixture.filePath("Other/Models");
        QVERIFY(writeFixture(QDir(otherModels).filePath("other.safetensors")));
        catalog.refresh();
        catalog.setDirectory(otherModels); // Supersede the in-flight scan from the previous container.
        QVERIFY(catalog.models().isEmpty());
        QTRY_VERIFY(!catalog.loading());
        QCOMPARE(catalog.models().size(), 1);
        QVERIFY(catalog.contains(QDir(otherModels).filePath("other.safetensors")));
        catalog.setDirectory({});
        QVERIFY(!catalog.loading());
        QVERIFY(catalog.models().isEmpty());
        catalog.setDirectory("Models");
        QVERIFY(!catalog.loading());
        QVERIFY(catalog.models().isEmpty());
        QVERIFY(!catalog.errorString().isEmpty());
        catalog.setDirectory(fixture.filePath("missing/Models"));
        QTRY_VERIFY(!catalog.loading());
        QVERIFY(!catalog.errorString().isEmpty());
        QVERIFY(QFile::link(models, fixture.filePath("Models-alias")));
        catalog.setDirectory(fixture.filePath("Models-alias"));
        QTRY_VERIFY(!catalog.loading());
        QVERIFY(catalog.models().isEmpty());
        QVERIFY(!catalog.errorString().isEmpty());
    }

    void wrappedCheckpointsAppearOnceAndResolveForExecution()
    {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/wrapped merge-XXXXXX");
        const auto models = fixture.filePath("Models");
        const auto package = QDir(models).filePath("Checkpoint/Wrapped.iildmodel");
        const auto payload = QDir(package).filePath("model.safetensors");
        QVERIFY(writeFixture(payload, bytes(m_fixture.filePath("base.safetensors"))));
        const auto manifest = QDir(package).filePath("model_index.json");
        const auto document = [](const QString &path, int count = 1, bool lora = false) {
            QJsonObject stage{{"model", path}, {"strength", 1.0}};
            if (lora) stage["loras"] = QJsonArray{QJsonObject{{"model", "style.safetensors"}}};
            QJsonArray stages; for (int i = 0; i < count; ++i) stages.append(stage);
            return QJsonDocument(QJsonObject{{"schema", "iild-unified-model-v1"}, {"stages", stages}}).toJson();
        };
        QVERIFY(writeFixture(manifest, document("model.safetensors")));
        MergeModelCatalog catalog; catalog.setDirectory(models);
        QTRY_VERIFY(!catalog.loading());
        QCOMPARE(catalog.models().size(), 1);
        QVERIFY(catalog.contains(package, true));
        QVERIFY(!catalog.contains(payload));
        QCOMPARE(catalog.outputDirectory(package), QDir(models).filePath("Checkpoint"));
        QCOMPARE(MergeModelCatalog::checkpointPath(package), payload);
        ModelMergeController controller;
        auto request = options("result.safetensors");
        request["output"] = fixture.filePath("result.safetensors");
        request["baseModel"] = package;
        request["materials"] = QVariantList{QVariantMap{{"path", package}}};
        QSignalSpy done(&controller, &ModelMergeController::finished);
        QVERIFY(controller.run(request, true));
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 15000);
        QVERIFY2(done.first().first().toBool(), qPrintable(controller.errorString()));
        const auto report = QJsonDocument::fromJson(controller.details().toUtf8()).object();
        QCOMPARE(report["base_model"].toString(), payload);
        QCOMPARE(report["additional_models"].toArray(), QJsonArray{payload});
        QVERIFY(!QFileInfo::exists(request["output"].toString()));
        for (const auto bad : {document("model.safetensors", 2), document("model.safetensors", 1, true),
                 document("missing.safetensors"), document(m_fixture.filePath("base.safetensors")), QByteArray("{}")}) {
            QVERIFY(writeFixture(manifest, bad));
            QVERIFY(MergeModelCatalog::checkpointPath(package).isEmpty());
            QVERIFY(!controller.run(request, true));
            catalog.refresh(); QTRY_VERIFY(!catalog.loading());
            QVERIFY(catalog.models().isEmpty());
        }
    }

    void unifiedInspectionPreservesArchitecturesWithoutWritingOutput()
    {
        ModelMergeController controller;
        auto request = options("inspect-only.iildmodel");
        request["mode"] = "unified";
        request["compatibilityModels"] = QStringList{m_fixture.filePath("base.safetensors")};
        QCOMPARE(controller.outputPathForName("Unified.iildmodel", m_fixture.path(), "unified"),
                 m_fixture.filePath("Unified.iildmodel"));
        QSignalSpy done(&controller, &ModelMergeController::finished);
        QVERIFY(controller.run(request, true));
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 15000);
        QVERIFY2(done.first().first().toBool(), qPrintable(controller.errorString()));
        const auto report = QJsonDocument::fromJson(controller.details().toUtf8()).object();
        QCOMPARE(report["mode"].toString(), QString("unified"));
        QCOMPARE(report["stages"].toArray().size(), 2);
        QCOMPARE(report["stages"].toArray().last().toObject()["loras"].toArray().size(), 1);
        QCOMPARE(report["compatibility_models"].toArray(), QJsonArray{m_fixture.filePath("base.safetensors")});
        QVERIFY(!QFileInfo::exists(request["output"].toString()));
        QVERIFY(!QFileInfo::exists(request["cacheDirectory"].toString()));
    }

    void packagedModelIsPassedWholeToTheSdkAndCanBeMerged()
    {
        if (!m_hasRuntime) QSKIP("Install the SDK Torch/safetensors environment for real package checks.");
        const auto package = m_fixture.filePath("base.iildmodel");
        QVERIFY(QFileInfo(package).isFile());
        QCOMPARE(MergeModelCatalog::checkpointPath(package), package);
        auto request = options("packaged-base.safetensors");
        request["baseModel"] = package;
        ModelMergeController controller;
        QSignalSpy done(&controller, &ModelMergeController::finished);
        QVERIFY(controller.run(request));
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 20000);
        QVERIFY2(done.first().first().toBool(), qPrintable(controller.errorString()));
        QVERIFY(QFileInfo(request["output"].toString()).isFile());
        const auto report = QJsonDocument::fromJson(controller.details().toUtf8()).object();
        QCOMPARE(report["sources"].toArray().first().toObject()["path"].toString(), package);
        QCOMPARE(report["sources"].toArray().first().toObject()["format"].toString(), QString("iildmodel"));
        QVERIFY2(python({"verify", m_fixture.path(), request["output"].toString(),
                         "weighted-sum", "automatic"}), qPrintable(m_pythonError));
    }

    void requiresExplicitSafetensorsOutputBeforeStarting()
    {
        ModelMergeController controller;
        QSignalSpy done(&controller, &ModelMergeController::finished);
        for (const auto &output : {QString(), QString("  "), m_fixture.filePath("combined.iildmodel"),
                 m_fixture.filePath("unnamed-output"), m_fixture.filePath("output.ckpt")}) {
            auto request = options();
            request["output"] = output;
            request["outputDirectory"] = m_fixture.path();
            QVERIFY(!controller.run(request));
            QVERIFY(!controller.busy());
            QVERIFY(!controller.errorString().isEmpty());
            QVERIFY(!controller.run(request, true));
        }
        auto request = options("combined.safetensors");
        request["mode"] = "unified";
        QVERIFY(!controller.run(request));
        QCOMPARE(done.count(), 0);
        QVERIFY(!QFileInfo::exists(m_fixture.filePath("base-sum.safetensors")));
        QVERIFY(!QFileInfo::exists(m_fixture.filePath("combined.iildmodel")));
    }

    void outputNamesResolveToOneSafetensorsFile()
    {
        ModelMergeController controller;
        const auto directory = m_fixture.filePath("Named outputs");
        for (const auto &name : {QString("My model"), QString(" My model "), QString("My model.safetensors"),
                 QString("My model.SAFETENSORS"), QString("My model.safetensor")})
            QCOMPARE(controller.outputPathForName(name, directory), QDir(directory).filePath("My model.safetensors"));
        QCOMPARE(controller.outputPathForName(QString::fromUtf8("새 모델 v1.2"), directory),
            QDir(directory).filePath(QString::fromUtf8("새 모델 v1.2.safetensors")));
        for (const auto &name : {QString(), QString("  "), QString(".safetensors"), QString("../base"),
                 QString("folder/model"), QString("folder\\model"), QString("model\nname"), QString(".hidden")})
            QVERIFY(controller.outputPathForName(name, directory).isEmpty());
        QVERIFY(controller.outputPathForName("model", "relative/path").isEmpty());
        QVERIFY(controller.outputPathForName("model", {}).isEmpty());
        QVERIFY(!QFileInfo::exists(directory)); // Resolving a name does not create any output.
    }

    void catalogUsesModelTypesForSelectionAndOutput()
    {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/typed catalog-XXXXXX");
        QVERIFY(fixture.isValid());
        const auto models = fixture.filePath("Models");
        const QStringList categories{"Checkpoint", "LoRA", "LyCORIS", "DoRA", "VAE", "Other"};
        for (const auto &type : categories)
            QVERIFY(writeFixture(QDir(models).filePath(type + "/model.safetensors")));
        MergeModelCatalog catalog;
        catalog.setDirectory(models);
        QTRY_VERIFY(!catalog.loading());
        QCOMPARE(catalog.models().size(), categories.size());
        for (const auto &entry : catalog.models()) {
            const auto model = entry.toMap();
            const auto type = model.value("modelType").toString();
            QVERIFY(categories.contains(type));
            const auto path = model.value("path").toString();
            QCOMPARE(catalog.contains(path, true), type != "LoRA" && type != "LyCORIS" && type != "DoRA");
            QCOMPARE(catalog.outputDirectory(path), QDir(models).filePath(type));
        }
        QCOMPARE(catalog.outputDirectory(fixture.filePath("outside.safetensors")), models);
    }

    void rejectsInvalidInputsBeforeStarting()
    {
        ModelMergeController controller;
        QSignalSpy done(&controller, &ModelMergeController::finished);
        for (const auto &value : {QString(), QString("-0.1"), QString("nan"), QString("inf"), QString("1,000"), QString("true")}) {
            auto request = options(); request["weightMode"] = "shared"; request["sharedWeight"] = value;
            QVERIFY(!controller.run(request)); QVERIFY(!controller.busy()); QVERIFY(!controller.errorString().isEmpty());
        }
        auto request = options(); request["materials"] = QVariantList{};
        QVERIFY(!controller.run(request));
        request = options(); request["output"] = request["baseModel"];
        const auto before = bytes(request["baseModel"].toString());
        QVERIFY(!controller.run(request));
        QCOMPARE(bytes(request["baseModel"].toString()), before);
        request = options(); request["output"] = m_fixture.filePath("extra.safetensors");
        const auto materialBefore = bytes(request["output"].toString());
        QVERIFY(!controller.run(request));
        QCOMPARE(bytes(request["output"].toString()), materialBefore);
        request = options(); request["pythonExecutable"] = m_fixture.filePath("missing-python");
        QVERIFY(!controller.run(request));
        request = options(); request["executable"] = m_fixture.filePath("missing-merge");
        QVERIFY(!controller.run(request));
        QCOMPARE(done.count(), 0);
    }

    void checkpointAndLoraArithmetic_data()
    {
        QTest::addColumn<QString>("mode");
        QTest::addColumn<QString>("weightMode");
        for (const auto &mode : {"weighted-sum", "weighted-difference"})
            for (const auto &weights : {"automatic", "shared", "per-model"})
                QTest::newRow(qPrintable(QString("%1-%2").arg(mode, weights))) << QString(mode) << QString(weights);
    }
    void checkpointAndLoraArithmetic()
    {
        if (!m_hasRuntime) QSKIP("Install the SDK Torch/safetensors environment for real tensor checks.");
        QFETCH(QString, mode); QFETCH(QString, weightMode);
        auto request = options(mode + "-" + weightMode + ".safetensors");
        request["mode"] = mode; request["weightMode"] = weightMode;
        const auto baseBefore = bytes(request["baseModel"].toString());
        const auto extraBefore = bytes(m_fixture.filePath("extra.safetensors"));
        const auto loraBefore = bytes(m_fixture.filePath("style.safetensors"));
        ModelMergeController controller; QSignalSpy done(&controller, &ModelMergeController::finished);
        QVERIFY(controller.run(request));
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 20000);
        QVERIFY2(done.first().first().toBool(), qPrintable(controller.errorString()));
        QCOMPARE(controller.completedOutput(), request["output"].toString());
        QVERIFY(QFileInfo(controller.completedOutput()).isFile());
        QVERIFY2(python({"verify", m_fixture.path(), controller.completedOutput(), mode, weightMode}), qPrintable(m_pythonError));
        QCOMPARE(bytes(request["baseModel"].toString()), baseBefore);
        QCOMPARE(bytes(m_fixture.filePath("extra.safetensors")), extraBefore);
        QCOMPARE(bytes(m_fixture.filePath("style.safetensors")), loraBefore);
        const auto report = QJsonDocument::fromJson(controller.details().toUtf8()).object();
        QCOMPARE(report["sources"].toArray().at(2).toObject()["kind"].toString(), QString("lora"));
        QCOMPARE(report["output_dtype"].toString(), QString("base"));
        QVERIFY(!controller.run(request)); // A successful output cannot be overwritten on rerun.
    }

    void rejectsDirectoryOutputsAndProjectsIncompatibleInputs()
    {
        if (!m_hasRuntime) QSKIP("Install the SDK Torch/safetensors environment for real tensor checks.");
        auto request = options("merged-pipeline.safetensors");
        request["baseModel"] = m_fixture.filePath("base-pipeline");
        request["materials"] = QVariantList{QVariantMap{{"path", m_fixture.filePath("extra-pipeline")}}};
        ModelMergeController controller; QSignalSpy done(&controller, &ModelMergeController::finished);
        QVERIFY(!controller.run(request));
        QVERIFY(!QFileInfo::exists(request["output"].toString()));
        QVERIFY(controller.errorString().contains("single-checkpoint .iildmodel"));
        QCOMPARE(done.count(), 0);
        request = options("incompatible.safetensors");
        request["materials"] = QVariantList{QVariantMap{{"path", m_fixture.filePath("wrong.safetensors")}}};
        const auto baseBefore = bytes(request["baseModel"].toString());
        const auto materialBefore = bytes(m_fixture.filePath("wrong.safetensors"));
        done.clear(); QVERIFY(controller.run(request, true));
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 20000);
        QVERIFY2(done.first().first().toBool(), qPrintable(controller.errorString()));
        auto report = QJsonDocument::fromJson(controller.details().toUtf8()).object();
        QCOMPARE(report["checkpoint_policy"].toString(), QString("common-layer"));
        QVERIFY(report["common_layers"].toObject()["1"].toObject()["projected_tensors"].toInt() > 0);
        QVERIFY(!QFileInfo::exists(request["output"].toString()));
        done.clear(); QVERIFY(controller.run(request, false));
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 20000);
        QVERIFY2(done.first().first().toBool(), qPrintable(controller.errorString()));
        QVERIFY(QFileInfo(request["output"].toString()).isFile());
        QVERIFY2(python({"verify-base", m_fixture.path(), request["output"].toString()}), qPrintable(m_pythonError));
        QCOMPARE(bytes(request["baseModel"].toString()), baseBefore);
        QCOMPARE(bytes(m_fixture.filePath("wrong.safetensors")), materialBefore);
    }

    void cancellationStopsOnlyTheRunningProcess()
    {
        const auto script = m_fixture.filePath("slow merge");
        QFile file(script); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("#!/usr/bin/env python3\nimport signal,time\nsignal.signal(signal.SIGINT, lambda *args: exit(130))\nwhile True: time.sleep(0.02)\n");
        file.close(); QVERIFY(file.setPermissions(file.permissions() | QFileDevice::ExeOwner));
        auto request = options("cancelled.safetensors"); request["executable"] = script;
        const auto baseBefore = bytes(request["baseModel"].toString());
        const auto extraBefore = bytes(m_fixture.filePath("extra.safetensors"));
        const auto loraBefore = bytes(m_fixture.filePath("style.safetensors"));
        ModelMergeController controller; QSignalSpy done(&controller, &ModelMergeController::finished);
        QVERIFY(controller.run(request));
        QTest::qWait(200);
        controller.cancel();
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 10000);
        QVERIFY(!done.first().first().toBool());
        QCOMPARE(controller.status(), QString("Cancelled"));
        QVERIFY(!controller.busy());
        QVERIFY(!QFileInfo::exists(request["output"].toString()));
        QCOMPARE(bytes(request["baseModel"].toString()), baseBefore);
        QCOMPARE(bytes(m_fixture.filePath("extra.safetensors")), extraBefore);
        QCOMPARE(bytes(m_fixture.filePath("style.safetensors")), loraBefore);
    }

    void qmlModelMenusRefreshScrollAndResetWithContainer()
    {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/model menu-XXXXXX");
        QVERIFY(fixture.isValid());
        const auto models = fixture.filePath("Models");
        for (int index = 0; index < 40; ++index)
            QVERIFY(writeFixture(QDir(models).filePath(QString("Model %1.safetensors").arg(index, 2, 10, QChar('0')))));
        QVERIFY(writeFixture(QDir(models).filePath("A Pipeline/model_index.json"), "{}"));
        QVERIFY(writeFixture(QDir(models).filePath("A Pipeline/unet/model.safetensors")));
        QVERIFY(writeFixture(QDir(models).filePath("Adapter/adapter_config.json"), "{}"));
        QVERIFY(writeFixture(QDir(models).filePath("Adapter/adapter_model.safetensors")));
        QVERIFY(writeFixture(QDir(models).filePath("Group A/shared.safetensor")));
        const auto lastPath = QDir(models).filePath("Z/shared.safetensor");
        QVERIFY(writeFixture(lastPath));
        QQmlApplicationEngine engine;
        QStringList warnings;
        connect(&engine, &QQmlApplicationEngine::warnings, this, [&](const QList<QQmlError> &errors) {
            for (const auto &error : errors) warnings.append(error.toString());
        });
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        engine.load(QUrl::fromLocalFile(QString::fromUtf8(SOCIETY_MERGE_QML_FILE)));
        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *tool = window->findChild<QQuickItem *>("modelMergeTool"); QVERIFY(tool);
        auto *catalog = window->findChild<MergeModelCatalog *>("mergeModelCatalog"); QVERIFY(catalog);
        auto *base = window->findChild<QQuickItem *>("mergeBaseField"); QVERIFY(base);
        auto *button = window->findChild<QQuickItem *>("mergeBaseFieldButton"); QVERIFY(button);
        auto *menu = base->findChild<QObject *>("mergeBaseFieldMenu"); QVERIFY(menu);
        auto *list = base->findChild<QQuickItem *>("mergeBaseFieldList"); QVERIFY(list);
        auto *run = window->findChild<QQuickItem *>("mergeRun"); QVERIFY(run);
        auto *refresh = window->findChild<QObject *>("mergeRefreshModels"); QVERIFY(refresh);
        QVERIFY(!button->isEnabled());
        QVERIFY(!run->isEnabled());
        QVERIFY(tool->setProperty("modelsDirectory", models));
        QTRY_VERIFY(!catalog->loading() && catalog->models().size() == 43);
        QCOMPARE(listProperty(base, "choices").size(), 42); // Single-file bases; adapter folders are material-only.
        QVERIFY(!run->isEnabled());
        const auto click = [window](QQuickItem *item, Qt::MouseButton mouseButton = Qt::LeftButton) {
            QTest::qWait(100);
            QTest::mouseClick(window, mouseButton, Qt::NoModifier,
                item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
        };
        click(button, Qt::RightButton);
        QTRY_VERIFY(menu->property("opened").toBool() && !catalog->loading());
        QVERIFY(tool->setProperty("modelsBusy", true));
        QTRY_VERIFY(!menu->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(menu, "triggerEntry", Q_ARG(QVariant, 0)));
        QVERIFY(tool->property("baseModel").toString().isEmpty());
        QVERIFY(tool->setProperty("modelsBusy", false));
        QTRY_VERIFY(!catalog->loading());
        click(button, Qt::RightButton);
        QTRY_VERIFY(menu->property("opened").toBool() && !catalog->loading());
        QVERIFY(list->clip());
        QVERIFY(list->property("contentHeight").toReal() > list->height());
        QTest::keyClick(window, Qt::Key_End);
        QTRY_COMPARE(list->property("currentIndex").toInt(), 41);
        QVERIFY(list->property("contentY").toReal() > 0);
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_COMPARE(tool->property("baseModel").toString(), lastPath);
        QTRY_VERIFY(!menu->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(refresh, "clicked"));
        QTRY_VERIFY(!catalog->loading());
        QCOMPARE(tool->property("baseModel").toString(), lastPath);

        // Refreshing a menu discovers models added while the form remains open.
        QVERIFY(writeFixture(QDir(models).filePath("Newly imported.safetensors")));
        click(button);
        QTRY_VERIFY(menu->property("opened").toBool() && !catalog->loading());
        QTRY_COMPARE(listProperty(base, "choices").size(), 43);
        const auto screenshot = qEnvironmentVariable("SOCIETY_MERGE_MENU_SCREENSHOT_PATH");
        if (!screenshot.isEmpty()) { QTest::qWait(300); QVERIFY(window->grabWindow().save(screenshot)); }
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!menu->property("visible").toBool());
        QVERIFY(QFile::remove(lastPath));
        QVERIFY(QMetaObject::invokeMethod(refresh, "clicked"));
        QTRY_VERIFY(!catalog->loading());
        QTRY_VERIFY(tool->property("baseModel").toString().isEmpty());
        QVERIFY(!run->isEnabled());

        window->resize(360, 640);
        QTRY_COMPARE(window->width(), 360);
        QTRY_VERIFY(button->mapToScene(QPointF(button->width(), 0)).x() <= 336);
        click(button);
        QTRY_VERIFY(menu->property("opened").toBool() && !catalog->loading());
        QVERIFY(menu->property("width").toReal() <= 352);
        QVERIFY(menu->property("height").toReal() <= 320);
        QTest::keyClick(window, Qt::Key_Home);
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_COMPARE(tool->property("baseModel").toString(), QDir(models).filePath("Group A/shared.safetensor"));
        QTRY_VERIFY(!menu->property("visible").toBool());

        window->resize(1440, 1000);
        QTRY_COMPARE(window->width(), 1440);
        auto *material = visualItem(window->contentItem(), "mergeMaterial0"); QVERIFY(material);
        auto *materialButton = visualItem(window->contentItem(), "mergeMaterial0Button"); QVERIFY(materialButton);
        auto *materialMenu = material->findChild<QObject *>("mergeMaterial0Menu"); QVERIFY(materialMenu);
        click(materialButton);
        QTRY_VERIFY(materialMenu->property("opened").toBool() && !catalog->loading());
        const auto choices = listProperty(material, "choices");
        int adapterIndex = -1;
        for (qsizetype i = 0; i < choices.size(); ++i)
            if (choices[i].toMap().value("kind").toString() == "adapter") adapterIndex = int(i);
        QVERIFY(adapterIndex >= 0);
        auto *option = visualItem(window->contentItem(), "mergeMaterial0Option" + QString::number(adapterIndex)); QVERIFY(option);
        click(option);
        QTRY_COMPARE(material->property("path").toString(), QDir(models).filePath("Adapter"));
        QTRY_VERIFY(!materialMenu->property("visible").toBool());
        QTRY_VERIFY(tool->property("inputModelsReady").toBool());
        QVERIFY(!run->isEnabled());
        QVERIFY(tool->setProperty("outputName", "custom"));
        QTRY_VERIFY(run->isEnabled());

        const auto otherModels = fixture.filePath("Other/Models");
        QVERIFY(writeFixture(QDir(otherModels).filePath("other.safetensors")));
        QVERIFY(tool->setProperty("outputDirectory", models));
        QCOMPARE(tool->property("outputPath").toString(), QDir(models).filePath("custom.iildmodel"));
        QVERIFY(tool->setProperty("sharedWeight", "0.25"));
        catalog->refresh();
        QVERIFY(tool->setProperty("modelsDirectory", otherModels));
        QVERIFY(tool->property("baseModel").toString().isEmpty());
        QVERIFY(material->property("path").toString().isEmpty());
        QVERIFY(tool->property("outputPath").toString().isEmpty());
        QVERIFY(tool->property("outputName").toString().isEmpty());
        QVERIFY(tool->property("outputDirectory").toString().isEmpty());
        QCOMPARE(tool->property("sharedWeight").toString(), QString("0.25"));
        QTRY_VERIFY(!catalog->loading());
        QCOMPARE(catalog->models().size(), 1);
        QVERIFY(!run->isEnabled());
        const auto emptyModels = fixture.filePath("Empty/Models");
        QVERIFY(QDir().mkpath(emptyModels));
        QVERIFY(tool->setProperty("modelsDirectory", emptyModels));
        QTRY_VERIFY(!catalog->loading());
        QVERIFY(catalog->models().isEmpty());
        auto *message = window->findChild<QQuickItem *>("mergeModelsMessage"); QVERIFY(message);
        QVERIFY(message->isVisible());
        click(button);
        QTRY_VERIFY(menu->property("opened").toBool() && !catalog->loading());
        QCOMPARE(listProperty(menu, "items").size(), 1);
        QVERIFY(!listProperty(menu, "items").first().toMap().value("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(menu, "triggerEntry", Q_ARG(QVariant, 0)));
        QVERIFY(tool->property("baseModel").toString().isEmpty());
        QTest::keyClick(window, Qt::Key_Escape);
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        window->close();
    }

    void qmlFigmaPanelsKeepEditingAndResponsiveNavigation()
    {
        QQmlApplicationEngine engine;
        QStringList warnings;
        connect(&engine, &QQmlApplicationEngine::warnings, this, [&](const QList<QQmlError> &errors) {
            for (const auto &error : errors) warnings.append(error.toString());
        });
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        engine.load(QUrl::fromLocalFile(QString::fromUtf8(SOCIETY_MERGE_QML_FILE)));
        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        window->resize(1212, 876); // Figma 121:752 is 1212 x 844 below the native title bar.
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *tool = window->findChild<QQuickItem *>("modelMergeTool"); QVERIFY(tool);
        auto *catalog = window->findChild<MergeModelCatalog *>("mergeModelCatalog"); QVERIFY(catalog);
        QVERIFY(tool->setProperty("modelsDirectory", m_fixture.path()));
        QTRY_VERIFY(!catalog->loading() && !catalog->models().isEmpty());
        auto *inputs = visualItem(tool, "mergeInputPanel"); QVERIFY(inputs);
        auto *settings = visualItem(tool, "mergeSettingsPanel"); QVERIFY(settings);
        auto *output = visualItem(tool, "mergeOutputPanel"); QVERIFY(output);
        auto *review = visualItem(tool, "mergeReviewPanel"); QVERIFY(review);
        auto *advanced = visualItem(tool, "mergeAdvancedPanel"); QVERIFY(advanced);
        const auto origin = [tool](QQuickItem *item) { return item->mapToItem(tool, QPointF()); };
        QTRY_COMPARE(output->width(), 348.0);
        QCOMPARE(review->width(), output->width());
        QCOMPARE(origin(inputs).x(), 32.0);
        QCOMPARE(origin(output).x() - origin(inputs).x() - inputs->width(), 24.0);
        QCOMPARE(origin(output).y(), origin(inputs).y());
        QVERIFY(origin(settings).y() >= origin(inputs).y() + inputs->height() + 20);
        QVERIFY(origin(review).y() >= origin(output).y() + output->height() + 20);
        QVERIFY(!advanced->isVisible());
        auto *base = visualItem(tool, "mergeBaseFieldButton"); QVERIFY(base);
        QCOMPARE(base->height(), 56.0);
        QVERIFY(tool->setProperty("baseModel", m_fixture.filePath("base.safetensors")));
        QVERIFY(QMetaObject::invokeMethod(tool, "setMaterial", Q_ARG(QVariant, 0),
            Q_ARG(QVariant, m_fixture.filePath("style.safetensors")), Q_ARG(QVariant, "1.5")));
        QVERIFY(tool->setProperty("outputName", "Studio Blend v3"));
        QTRY_VERIFY(tool->property("inputModelsReady").toBool());
        const auto click = [window, tool](const QString &name) {
            auto *item = visualItem(tool, name); QVERIFY(item);
            QTest::qWait(50);
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
        };
        click("mergeSharedMode");
        QTRY_COMPARE(tool->property("weightMode").toString(), QString("shared"));
        auto *shared = visualItem(tool, "mergeSharedWeight"); QVERIFY(shared && shared->isVisible());
        QVERIFY(QMetaObject::invokeMethod(shared, "textEdited", Q_ARG(QString, QString("0.375"))));
        click("mergePerModelMode");
        QTRY_COMPARE(tool->property("weightMode").toString(), QString("per-model"));
        auto *materialWeight = visualItem(tool, "mergeMaterialWeight0"); QVERIFY(materialWeight);
        QTRY_VERIFY(materialWeight->isVisible());
        QCOMPARE(materialWeight->property("text").toString(), QString("1.5"));
        click("mergeAutomaticMode");
        QTRY_COMPARE(tool->property("weightMode").toString(), QString("automatic"));
        QCOMPARE(tool->property("sharedWeight").toString(), QString("0.375"));
        click("mergeAdvancedToggle");
        QTRY_VERIFY(advanced->isVisible());
        QVERIFY(tool->setProperty("cacheDirectory", m_fixture.filePath("preserved cache")));
        click("mergeAdvancedToggle");
        QTRY_VERIFY(!advanced->isVisible());
        QCOMPARE(tool->property("cacheDirectory").toString(), m_fixture.filePath("preserved cache"));
        QSignalSpy back(tool, SIGNAL(backRequested()));
        click("toolsBack");
        QCOMPARE(back.count(), 1);
        const auto screenshot = qEnvironmentVariable("SOCIETY_MERGE_DESIGN_SCREENSHOT_PATH");
        if (!screenshot.isEmpty()) { QTest::qWait(250); QVERIFY(window->grabWindow().save(screenshot)); }

        // The compact review panel must keep Cancel reachable while editors are locked.
        const auto slow = m_fixture.filePath("slow UI merge");
        QVERIFY(writeFixture(slow, "#!/usr/bin/env python3\nimport signal,time\nsignal.signal(signal.SIGINT, lambda *args: exit(130))\nwhile True: time.sleep(0.02)\n"));
        QVERIFY(QFile::setPermissions(slow, QFile::permissions(slow) | QFileDevice::ExeOwner));
        QVERIFY(tool->setProperty("mergeExecutable", slow));
        auto *controller = window->findChild<ModelMergeController *>("modelMergeController"); QVERIFY(controller);
        click("mergeRun");
        QTRY_VERIFY(controller->busy());
        QVERIFY(!inputs->isEnabled() && !settings->isEnabled() && !output->isEnabled());
        auto *cancel = visualItem(tool, "mergeCancel"); QVERIFY(cancel && cancel->isVisible() && cancel->isEnabled());
        QTest::qWait(200);
        click("mergeCancel");
        QTRY_VERIFY_WITH_TIMEOUT(!controller->busy(), 10000);
        QCOMPARE(controller->status(), QString("Cancelled"));
        QCOMPARE(visualItem(tool, "mergeStatus")->property("text").toString(), QString("Cancelled"));
        QVERIFY(inputs->isEnabled() && settings->isEnabled() && output->isEnabled());
        QVERIFY(tool->setProperty("mergeExecutable", ""));

        window->resize(360, 640);
        QVERIFY(tool->setProperty("touchNavigation", true));
        QTRY_COMPARE(window->width(), 360);
        QTRY_COMPARE(origin(output).x(), origin(inputs).x());
        QVERIFY(origin(output).y() > origin(settings).y());
        auto *run = visualItem(tool, "mergeRun"); QVERIFY(run);
        QTRY_VERIFY(run->height() >= 44);
        QVERIFY(run->mapToItem(tool, QPointF(run->width(), 0)).x() <= tool->width() - 16);
        run->forceActiveFocus(Qt::TabFocusReason);
        QTRY_VERIFY(run->mapToScene(QPointF(0, run->height())).y() <= window->height());
        QVERIFY(run->mapToScene(QPointF()).y() >= 32);
        QVERIFY(tool->property("outputName").toString() == "Studio Blend v3");
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        window->close();
    }

    void qmlFormSubmitsAllInputsAndFitsSmallWindows()
    {
        QQmlApplicationEngine engine;
        QStringList warnings;
        connect(&engine, &QQmlApplicationEngine::warnings, this, [&](const QList<QQmlError> &errors) {
            for (const auto &error : errors) warnings.append(error.toString());
        });
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        engine.load(QUrl::fromLocalFile(QString::fromUtf8(SOCIETY_MERGE_QML_FILE)));
        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        auto *tool = window->findChild<QQuickItem *>("modelMergeTool"); QVERIFY(tool);
        auto *controller = window->findChild<ModelMergeController *>("modelMergeController"); QVERIFY(controller);
        auto *catalog = window->findChild<MergeModelCatalog *>("mergeModelCatalog"); QVERIFY(catalog);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QVERIFY(tool->setProperty("modelsDirectory", m_fixture.path()));
        QTRY_VERIFY(!catalog->loading() && !catalog->models().isEmpty());
        const auto select = [window, catalog](const QString &name, const QString &path) {
            auto *picker = visualItem(window->contentItem(), name); QVERIFY(picker);
            auto *menu = picker->findChild<QObject *>(name + "Menu"); QVERIFY(menu);
            const auto choices = listProperty(picker, "choices");
            int index = -1;
            for (qsizetype i = 0; i < choices.size(); ++i)
                if (choices[i].toMap().value("path").toString() == path) index = int(i);
            QVERIFY(index >= 0);
            QVERIFY(QMetaObject::invokeMethod(picker, "openMenu"));
            QTRY_VERIFY(!catalog->loading());
            QTRY_VERIFY(menu->property("opened").toBool());
            QVERIFY(QMetaObject::invokeMethod(menu, "triggerEntry", Q_ARG(QVariant, index)));
            QTRY_VERIFY(!menu->property("visible").toBool());
        };
        QVERIFY(!window->findChild<QObject *>("mergeBaseFieldInput"));
        QVERIFY(!window->findChild<QObject *>("mergeBaseFieldFileButton"));
        const auto base = m_fixture.filePath("base.safetensors");
        select("mergeBaseField", base);
        QCOMPARE(tool->property("baseModel").toString(), base);
        QCOMPARE(tool->property("mode").toString(), QString("unified"));
        QVERIFY(window->findChild<QObject *>("mergeUnifiedMode"));
        QVERIFY(tool->setProperty("mode", "weighted-sum"));
        QVERIFY(tool->property("outputName").toString().isEmpty());
        QVERIFY(tool->property("outputPath").toString().isEmpty());
        QVERIFY(tool->setProperty("weightMode", "per-model"));
        QVERIFY(tool->setProperty("cacheDirectory", m_fixture.filePath("form cache")));
        QVERIFY(tool->setProperty("outputDirectory", m_fixture.path()));
        QVERIFY(tool->setProperty("mergeExecutable", controller->defaultExecutable()));
        QVERIFY(tool->setProperty("pythonExecutable", QString::fromUtf8(SOCIETY_MERGE_TEST_PYTHON)));
        QVERIFY(QMetaObject::invokeMethod(tool, "setMaterial", Q_ARG(QVariant, 0),
            Q_ARG(QVariant, m_fixture.filePath("extra.safetensors")), Q_ARG(QVariant, "0.25")));
        QVERIFY(QMetaObject::invokeMethod(tool, "addMaterial", Q_ARG(QVariant, m_fixture.filePath("style.safetensors"))));
        QVERIFY(QMetaObject::invokeMethod(tool, "setMaterial", Q_ARG(QVariant, 1),
            Q_ARG(QVariant, m_fixture.filePath("style.safetensors")), Q_ARG(QVariant, "1.5")));
        select("mergeMaterial0", m_fixture.filePath("extra.safetensors"));
        select("mergeMaterial1", m_fixture.filePath("style.safetensors"));
        QCOMPARE(tool->property("materialCount").toInt(), 2);
        QTRY_VERIFY(tool->property("inputModelsReady").toBool());
        auto *runButton = window->findChild<QQuickItem *>("mergeRun"); QVERIFY(runButton);
        auto *validateButton = window->findChild<QQuickItem *>("mergeValidate"); QVERIFY(validateButton);
        auto *nameInput = window->findChild<QObject *>("mergeOutputName"); QVERIFY(nameInput);
        for (const auto &name : {QString(), QString("   "), QString("../escape"), QString("nested/model"), QString(".safetensors")}) {
            QVERIFY(QMetaObject::invokeMethod(nameInput, "textEdited", Q_ARG(QString, name)));
            QVERIFY(tool->property("outputPath").toString().isEmpty());
            QVERIFY(!runButton->isEnabled());
            QVERIFY(!validateButton->isEnabled());
        }
        QVERIFY(QMetaObject::invokeMethod(nameInput, "textEdited", Q_ARG(QString, QString("form output.safetensors"))));
        QCOMPARE(tool->property("outputPath").toString(), m_fixture.filePath("form output.safetensors"));
        QVERIFY(runButton->isEnabled());
        QVERIFY(validateButton->isEnabled());
        auto *preview = window->findChild<QObject *>("mergeOutputPreview"); QVERIFY(preview);
        QCOMPARE(preview->property("text").toString(), QString("form output.safetensors"));
        auto *pathTooltip = window->findChild<QObject *>("mergeOutputPathTooltip"); QVERIFY(pathTooltip);
        QCOMPARE(pathTooltip->property("text").toString(), m_fixture.filePath("form output.safetensors"));
        QVERIFY(tool->setProperty("modelsBusy", true));
        QVERIFY(!tool->property("inputModelsReady").toBool());
        QVERIFY(!window->findChild<QQuickItem *>("mergeValidate")->isEnabled());
        QVERIFY(tool->setProperty("modelsBusy", false));
        QTRY_VERIFY(tool->property("inputModelsReady").toBool());
        QCOMPARE(tool->property("baseModel").toString(), base);
        const auto click = [window](QQuickItem *item) {
            // Row edits schedule Qt Quick layout polish before pointer coordinates settle.
            QTest::qWait(100);
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
        };
        auto *difference = window->findChild<QQuickItem *>("mergeDifferenceMode"); QVERIFY(difference); click(difference);
        QCOMPARE(tool->property("mode").toString(), QString("weighted-difference"));
        auto *validate = window->findChild<QQuickItem *>("mergeValidate"); QVERIFY(validate);
        QSignalSpy done(controller, &ModelMergeController::finished);
        click(validate);
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 15000);
        QVERIFY2(done.first().first().toBool(), qPrintable(controller->errorString()));
        auto *status = visualItem(tool, "mergeStatus"); QVERIFY(status);
        QCOMPARE(status->property("text").toString(), controller->status());
        QVERIFY(tool->setProperty("mode", "weighted-sum"));
        QVERIFY(status->property("text").toString().startsWith("Not checked yet."));
        QVERIFY(tool->setProperty("mode", "weighted-difference"));
        const auto result = QJsonDocument::fromJson(controller->details().toUtf8()).object();
        QCOMPARE(result["base_model"].toString(), base);
        QCOMPARE(result["additional_models"].toArray(), QJsonArray({m_fixture.filePath("extra.safetensors"), m_fixture.filePath("style.safetensors")}));
        QCOMPARE(result["weights"].toArray(), QJsonArray({0.25, 1.5}));
        QCOMPARE(result["additional_models"].toArray().size(), 2);
        QCOMPARE(result["cache_dir"].toString(), m_fixture.filePath("form cache"));
        QCOMPARE(result["output"].toString(), m_fixture.filePath("form output.safetensors"));
        if (m_hasRuntime) {
            auto *run = window->findChild<QQuickItem *>("mergeRun"); QVERIFY(run); click(run);
            QTRY_COMPARE_WITH_TIMEOUT(done.count(), 2, 20000);
            QVERIFY2(done.last().first().toBool(), qPrintable(controller->errorString()));
            QVERIFY(!done.last().at(1).toBool());
            QVERIFY2(python({"verify", m_fixture.path(), controller->completedOutput(), "weighted-difference", "per-model"}), qPrintable(m_pythonError));
            QTRY_VERIFY(!catalog->loading());
            QCOMPARE(tool->property("baseModel").toString(), base);
            QVERIFY(catalog->contains(base, true));
            QVERIFY(catalog->contains(m_fixture.filePath("extra.safetensors")));
            QVERIFY(catalog->contains(m_fixture.filePath("style.safetensors")));
        }
        for (const auto size : {QSize(1440, 1000), QSize(760, 720), QSize(360, 640), QSize(360, 320)}) {
            window->resize(size); QTRY_COMPARE(window->size(), size);
            QTest::qWait(100); // Let wrapped rows and the one-column layout settle before focusing.
            auto *scroll = window->findChild<QQuickItem *>("mergeScroll"); QVERIFY(scroll);
            QTRY_VERIFY(scroll->width() > 0 && scroll->height() > 0);
            validate->forceActiveFocus(Qt::TabFocusReason);
            QVERIFY(QMetaObject::invokeMethod(tool, "revealFocusedControl", Q_ARG(QVariant, QVariant::fromValue(validate))));
            QTRY_VERIFY(validate->mapToScene(QPointF(validate->width(), validate->height())).y() <= size.height());
            QVERIFY(difference->mapToScene(QPointF(difference->width(), 0)).x() <= size.width() - 24);
            auto *run = window->findChild<QQuickItem *>("mergeRun"); QVERIFY(run);
            run->forceActiveFocus(Qt::TabFocusReason);
            QVERIFY(QMetaObject::invokeMethod(tool, "revealFocusedControl", Q_ARG(QVariant, QVariant::fromValue(run))));
            QTRY_VERIFY2(run->mapToScene(QPointF(0, run->height())).y() <= size.height(),
                qPrintable(QString("Run bottom %1 exceeds %2 at width %3")
                    .arg(run->mapToScene(QPointF(0, run->height())).y()).arg(size.height()).arg(size.width())));
            QTRY_VERIFY(run->mapToScene(QPointF(run->width(), 0)).x() <= size.width() - 24);
        }
        window->resize(1440, 1000); QTRY_COMPARE(window->width(), 1440);
        QVERIFY(QMetaObject::invokeMethod(tool, "focusBackButton"));
        const auto screenshot = qEnvironmentVariable("SOCIETY_MERGE_SCREENSHOT_PATH");
        if (!screenshot.isEmpty()) { QTest::qWait(300); QVERIFY(window->grabWindow().save(screenshot)); }
        auto *remove = visualItem(window->contentItem(), "mergeRemoveMaterial1"); QVERIFY(remove);
        QVERIFY(QMetaObject::invokeMethod(remove, "clicked"));
        QCOMPARE(tool->property("materialCount").toInt(), 1);
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        window->close();
    }
};

int main(int argc, char **argv)
{
    lvrs::AppBootstrapOptions options;
    options.applicationName = "SocietyModelMergeTests";
    options.quickStyleName = "Basic";
    options.bootstrapGraphicsBackend = false;
    options.configureRenderQualityDefaults = false;
    options.logBootstrapDiagnostics = false;
    options.logGraphicsBackend = false;
    if (!lvrs::preApplicationBootstrap(options).ok) return 1;
    QGuiApplication app(argc, argv);
    lvrs::postApplicationBootstrap(app, options);
    SocietyModelMergeTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_modelmerge.moc"

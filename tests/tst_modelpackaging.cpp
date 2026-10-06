#include "App/Tools/ModelPackagingController.h"
#include "backend/runtime/appbootstrap.h"
#include <QDir>
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
    QString folder;
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
        qmlRegisterType<ModelPackagingController>("Society", 1, 0, "ModelPackagingController");
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
        const auto output = controller.outputPathForName("LTX core", m_root.path());
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
        const auto output = m_root.filePath("cancelled.safetensors");
        QVERIFY(controller.createPackage(output)); controller.cancel();
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 2, 15000);
        QVERIFY(!done.last().first().toBool()); QCOMPARE(controller.phase(), QString("cancelled"));
        QVERIFY(!QFileInfo::exists(output)); QVERIFY(QFileInfo::exists(large + "/model.safetensors"));
        QVERIFY(QDir(m_root.path()).entryList({"*.partial-*"}, QDir::Files | QDir::Hidden).isEmpty());
    }
    void realQmlControlsMatchContractAndRemainResponsive() {
        warnings.clear(); QQmlApplicationEngine engine; load(engine);
        QVERIFY2(!engine.rootObjects().isEmpty(), qPrintable(warnings.join('\n')));
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        auto *tool = window->findChild<QQuickItem *>("modelPackagingTool"); QVERIFY(tool);
        auto *controller = window->findChild<ModelPackagingController *>("modelPackagingController"); QVERIFY(controller);
        tool->setProperty("outputDirectory", m_root.path());
        QSignalSpy done(controller, &ModelPackagingController::finished);
        QVERIFY(controller->scanFolder(folder)); QTRY_COMPARE(done.count(), 1);
        auto *submit = visual(window->contentItem(), "packagingSubmit"); QVERIFY(submit);
        QTRY_VERIFY(submit->isEnabled());
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

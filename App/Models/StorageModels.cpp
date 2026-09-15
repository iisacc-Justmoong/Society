#include "StorageModels.h"
#include <ModelStore.h>

#include <QDirIterator>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QLocale>
#include <QSet>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>

namespace {
using namespace iiSocietyContainer;
QVariantMap emptyGroups()
{
    return {{"image", QVariantList()}, {"video", QVariantList()}, {"audio", QVariantList()}, {"language", QVariantList()}};
}
QString firstString(const QJsonObject &metadata, const QStringList &keys)
{
    for (const auto &key : keys) {
        const auto value = metadata.value(key).toString().trimmed();
        if (!value.isEmpty()) return value.left(256);
    }
    return {};
}
QString architecture(const QJsonObject &metadata)
{
    auto value = firstString(metadata, {"modelspec.architecture", "architecture", "general.architecture", "_class_name"});
    if (value.isEmpty()) value = metadata.value("architectures").toArray().first().toString().left(256);
    if (value.isEmpty()) value = metadata.value("model_type").toString().left(256);
    if (value.contains("stable-diffusion-xl", Qt::CaseInsensitive) || value.contains("StableDiffusionXL")) return "SDXL";
    return value;
}
QString modality(const ModelEntry &entry, const QJsonObject &metadata)
{
    const auto explicitValue = firstString(metadata, {"society.modality", "modelspec.modality", "modality"}).toLower();
    if (emptyGroups().contains(explicitValue)) return explicitValue;
    const auto hint = (architecture(metadata) + ' ' + metadata.value("pipeline_tag").toString()).toLower();
    const auto has = [&](const QStringList &words) {
        return std::any_of(words.cbegin(), words.cend(), [&](const QString &word) { return hint.contains(word); });
    };
    if (has({"video", "animatediff", "cogvideo", "wanpipeline", "wantransformer", "hunyuanvideo"})) return "video";
    if (has({"audio", "speech", "whisper", "bark", "musicgen", "vits", "tacotron"})) return "audio";
    if (has({"causallm", "text-generation", "llama", "mistral", "gemma", "qwen", "llava"})) return "language";
    if (has({"diffusion", "sdxl", "flux", "text-to-image", "image-to-image"})) return "image";
    switch (entry.classification.type) {
    case ModelType::LLM: case ModelType::VLM: return "language";
    case ModelType::Motion: return "video";
    case ModelType::Other: return {};
    default: return "image";
    }
}
QString precision(const QJsonObject &metadata)
{
    auto value = firstString(metadata, {"modelspec.precision", "precision", "torch_dtype", "dtype"});
    if (value.isEmpty()) {
        QStringList types;
        for (const auto &dtype : metadata.value("tensor_dtypes").toArray()) types.append(dtype.toString());
        value = types.join(" / ");
    }
    value.replace("bfloat16", "BF16", Qt::CaseInsensitive);
    value.replace("float16", "FP16", Qt::CaseInsensitive);
    value.replace("float32", "FP32", Qt::CaseInsensitive);
    if (value == "F16") value = "FP16";
    if (value == "F32") value = "FP32";
    if (value.isEmpty() && metadata.contains("general.file_type")) {
        const auto type = metadata.value("general.file_type").toInt(-1);
        value = type == 0 ? "FP32" : type == 1 ? "FP16" : QObject::tr("Quantized");
    }
    return value;
}
struct Snapshot {
    QVariantMap groups = emptyGroups();
    int count = 0, uncategorized = 0;
    QString error;
    QStringList watches;
};
Snapshot scan(const QString &directory, const std::shared_ptr<std::atomic_bool> &cancel)
{
    Snapshot result;
    const auto entries = ModelStore::scanDirectory(directory, &result.error, cancel.get());
    if (!result.error.isEmpty() || cancel->load()) return result;
    const auto root = QFileInfo(directory).canonicalFilePath();
    result.watches.append(root);
    QDirIterator directories(root, QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (directories.hasNext()) {
        if (cancel->load()) return {};
        directories.next();
        const auto info = directories.fileInfo();
        if (!info.isSymLink() && info.canonicalFilePath().startsWith(root + '/')) result.watches.append(info.canonicalFilePath());
    }
    const QStringList extensions{"safetensors", "safetensor", "ckpt", "pt", "pth", "bin", "gguf", "onnx"};
    for (const auto &entry : entries) {
        if (cancel->load()) return {};
        if (entry.kind == "file" && !extensions.contains(entry.format)) continue;
        const auto metadata = ModelClassifier::metadata(entry.path);
        const auto group = modality(entry, metadata);
        result.watches.append(entry.path);
        result.watches.append(ModelClassifier::companionFiles(entry.path));
        if (entry.kind != "file") {
            for (const auto *name : {"config.json", "model_index.json", "adapter_config.json", "society.model.json"}) {
                const QFileInfo info(QDir(entry.path).filePath(QLatin1String(name)));
                if (info.isFile() && !info.isSymLink()) result.watches.append(info.absoluteFilePath());
            }
        }
        if (group.isEmpty()) { ++result.uncategorized; continue; }
        qint64 bytes = 0;
        if (entry.kind == "file") bytes = QFileInfo(entry.path).size();
        else {
            QDirIterator files(entry.path, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
            while (files.hasNext()) {
                if (cancel->load()) return {};
                files.next();
                const auto info = files.fileInfo();
                if (!info.isSymLink() && info.canonicalFilePath().startsWith(entry.path + '/')) bytes += info.size();
            }
        }
        auto name = firstString(metadata, {"modelspec.title", "general.name"});
        if (name.isEmpty()) name = entry.kind == "file" ? QFileInfo(entry.name).completeBaseName() : entry.name;
        const auto modelArchitecture = architecture(metadata);
        const auto modelPrecision = precision(metadata);
        auto rows = result.groups.value(group).toList();
        rows.append(QVariantMap{{"path", entry.path}, {"folderPath", QFileInfo(entry.path).absolutePath()},
            {"name", name}, {"relativePath", entry.relativePath}, {"directory", entry.kind != "file"},
            {"architecture", modelArchitecture.isEmpty() ? QObject::tr("Unknown") : modelArchitecture},
            {"precision", modelPrecision.isEmpty() ? QObject::tr("Unknown") : modelPrecision},
            {"format", entry.format == "safetensors" || entry.format == "safetensor" ? QString("Safetensors")
                : entry.kind == "diffusers" ? QString("Diffusers") : entry.kind == "adapter" ? QString("PEFT")
                : entry.kind == "package" ? QString("Transformers") : entry.format.toUpper()},
            {"bytes", bytes}, {"sizeText", QObject::tr("%1 on device").arg(QLocale().formattedDataSize(bytes))}});
        result.groups.insert(group, rows);
        ++result.count;
    }
    for (auto it = result.groups.begin(); it != result.groups.end(); ++it) {
        auto rows = it.value().toList();
        std::sort(rows.begin(), rows.end(), [](const QVariant &a, const QVariant &b) {
            const auto left = a.toMap(), right = b.toMap();
            const auto compared = left.value("name").toString().compare(right.value("name").toString(), Qt::CaseInsensitive);
            return compared == 0 ? left.value("path").toString() < right.value("path").toString() : compared < 0;
        });
        it.value() = rows;
    }
    result.watches.removeDuplicates();
    return result;
}
}

StorageModels::StorageModels(QObject *parent) : QObject(parent), m_groups(emptyGroups())
{
    m_debounce.setSingleShot(true); m_debounce.setInterval(150);
    connect(&m_files, &QFileSystemWatcher::directoryChanged, &m_debounce, qOverload<>(&QTimer::start));
    connect(&m_files, &QFileSystemWatcher::fileChanged, &m_debounce, qOverload<>(&QTimer::start));
    connect(&m_debounce, &QTimer::timeout, this, &StorageModels::refresh);
}
StorageModels::~StorageModels() { if (m_cancel) m_cancel->store(true); }
void StorageModels::setDirectory(const QString &directory)
{
    if (m_directory == directory) return;
    if (m_cancel) m_cancel->store(true);
    ++m_revision; m_refreshPending = false; m_debounce.stop();
    if (m_loading) { m_loading = false; emit loadingChanged(); }
    const auto watches = m_files.files() + m_files.directories();
    if (!watches.isEmpty()) m_files.removePaths(watches);
    emit modelsAboutToChange();
    m_directory = directory; m_groups = emptyGroups(); m_error.clear(); m_count = m_uncategorized = 0;
    emit directoryChanged(); emit modelsChanged();
    refresh();
}
void StorageModels::refresh()
{
    if (m_loading) { m_refreshPending = true; return; }
    if (m_directory.isEmpty()) return;
    const auto revision = ++m_revision;
    m_cancel = std::make_shared<std::atomic_bool>(false);
    m_loading = true; emit loadingChanged();
    auto *watcher = new QFutureWatcher<Snapshot>(this);
    connect(watcher, &QFutureWatcher<Snapshot>::finished, this, [this, watcher, revision] {
        const auto snapshot = watcher->result(); watcher->deleteLater();
        if (revision != m_revision) return;
        const auto watches = m_files.files() + m_files.directories();
        const QSet<QString> previous(watches.cbegin(), watches.cend());
        const QSet<QString> next(snapshot.watches.cbegin(), snapshot.watches.cend());
        const auto removed = (previous - next).values(), added = (next - previous).values();
        if (!removed.isEmpty()) m_files.removePaths(removed);
        if (!added.isEmpty()) m_files.addPaths(added);
        if (m_groups != snapshot.groups || m_error != snapshot.error || m_uncategorized != snapshot.uncategorized) {
            emit modelsAboutToChange();
            m_groups = snapshot.groups; m_error = snapshot.error;
            m_count = snapshot.count; m_uncategorized = snapshot.uncategorized;
            emit modelsChanged();
        }
        m_loading = false; emit loadingChanged();
        if (m_refreshPending) { m_refreshPending = false; m_debounce.start(); }
    });
    watcher->setFuture(QtConcurrent::run(scan, m_directory, m_cancel));
}

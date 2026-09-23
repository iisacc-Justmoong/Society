#include "MergeModelCatalog.h"
#include <ModelStore.h>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>

namespace {
struct Snapshot { QVariantList models; QString error; };

Snapshot scan(const QString &path, const std::shared_ptr<std::atomic_bool> &cancel)
{
    Snapshot result;
    const auto entries = iiSocietyContainer::ModelStore::scanDirectory(path, &result.error, cancel.get());
    const QStringList suffixes{"safetensors", "safetensor", "ckpt", "pt", "pth", "bin", "iildmodel"};
    for (const auto &entry : entries) {
        if (cancel->load()) return {};
        if (entry.kind == "file" && (!suffixes.contains(entry.format) || QFileInfo(entry.path).size() == 0)) continue;
        const bool wrapped = !MergeModelCatalog::checkpointPath(entry.path).isEmpty()
            && QFileInfo(entry.path).suffix().compare("iildmodel", Qt::CaseInsensitive) == 0;
        if (entry.kind != "file" && entry.kind != "adapter" && !wrapped) continue;
        using iiSocietyContainer::ModelType;
        const auto type = entry.classification.type;
        const bool adapter = entry.kind == "adapter" || type == ModelType::LoRA || type == ModelType::DoRA || type == ModelType::LyCORIS;
        result.models.append(QVariantMap{{"path", entry.path}, {"name", entry.name},
            {"relativePath", entry.relativePath}, {"kind", wrapped ? QString("checkpoint-package") : entry.kind}, {"format", wrapped ? QString("IILDMODEL") : entry.format.toUpper()},
            {"modelType", iiSocietyContainer::modelTypeName(type)}, {"baseEligible", !adapter}});
    }
    return result;
}
}

QString MergeModelCatalog::checkpointPath(const QString &path)
{
    const QFileInfo info(path);
    const QStringList suffixes{"safetensors", "safetensor", "ckpt", "pt", "pth", "bin", "iildmodel"};
    if (info.isFile()) return !info.isSymLink() && suffixes.contains(info.suffix().toLower()) && info.size() > 0
        ? path : QString();
    if (!info.isDir() || info.isSymLink() || info.suffix().compare("iildmodel", Qt::CaseInsensitive)) return {};
    QFile manifest(QDir(path).filePath("model_index.json"));
    if (!manifest.open(QIODevice::ReadOnly) || manifest.size() > 1024 * 1024) return {};
    const auto object = QJsonDocument::fromJson(manifest.readAll()).object();
    const auto stages = object.value("stages").toArray();
    if (object.value("schema").toString() != "iild-unified-model-v1" || stages.size() != 1) return {};
    const auto stage = stages.first().toObject();
    if (stage.value("strength").toDouble(-1) != 1.0 || !stage.value("loras").toArray().isEmpty()) return {};
    const auto relative = stage.value("model").toString();
    if (relative.isEmpty() || QDir::isAbsolutePath(relative)) return {};
    const QFileInfo payload(QDir(path).filePath(relative));
    const auto canonical = payload.canonicalFilePath();
    if (!canonical.startsWith(info.canonicalFilePath() + '/') || !payload.isFile()
        || payload.size() == 0 || !suffixes.contains(payload.suffix().toLower())) return {};
    return canonical;
}

MergeModelCatalog::MergeModelCatalog(QObject *parent) : QObject(parent) {}
MergeModelCatalog::~MergeModelCatalog() { if (m_cancel) m_cancel->store(true); }

void MergeModelCatalog::setDirectory(const QString &directory)
{
    if (m_directory == directory) return;
    m_directory = directory;
    m_models.clear();
    m_error.clear();
    emit directoryChanged();
    refresh();
    emit modelsChanged();
}

bool MergeModelCatalog::contains(const QString &path, bool baseOnly) const
{
    return std::any_of(m_models.cbegin(), m_models.cend(), [&](const QVariant &entry) {
        const auto model = entry.toMap();
        return model.value("path").toString() == path && (!baseOnly || model.value("baseEligible").toBool());
    });
}

QString MergeModelCatalog::outputDirectory(const QString &basePath) const
{
    for (const auto &entry : m_models) {
        const auto model = entry.toMap();
        if (model.value("path").toString() == basePath) {
            const auto type = iiSocietyContainer::modelTypeFromName(model.value("modelType").toString());
            if (type) return QDir(m_directory).filePath(iiSocietyContainer::modelTypeName(*type));
        }
    }
    return m_directory;
}

void MergeModelCatalog::refresh()
{
    if (m_cancel) m_cancel->store(true);
    const auto revision = ++m_revision;
    m_cancel = std::make_shared<std::atomic_bool>(false);
    // An absent container must never fall back to enumerating the working directory.
    if (m_directory.isEmpty() || !QDir::isAbsolutePath(m_directory)) {
        const auto error = m_directory.isEmpty() ? QString() : tr("Open a Society container to choose models.");
        const bool changed = !m_models.isEmpty() || m_error != error;
        m_models.clear(); m_error = error;
        if (changed) emit modelsChanged();
        if (m_loading) { m_loading = false; emit loadingChanged(); }
        return;
    }
    if (!m_loading) { m_loading = true; emit loadingChanged(); }
    auto *watcher = new QFutureWatcher<Snapshot>(this);
    connect(watcher, &QFutureWatcher<Snapshot>::finished, this, [this, watcher, revision] {
        const auto result = watcher->result();
        watcher->deleteLater();
        if (revision != m_revision) return;
        const bool changed = m_models != result.models || m_error != result.error;
        m_models = result.models; m_error = result.error; m_loading = false;
        if (changed) emit modelsChanged();
        emit loadingChanged();
    });
    watcher->setFuture(QtConcurrent::run(scan, m_directory, m_cancel));
}

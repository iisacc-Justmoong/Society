#include "MergeModelCatalog.h"
#include <ModelStore.h>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QSet>
#include <QtEndian>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <cstring>

namespace {
struct Snapshot { QVariantList models; QString error; };

QString familyFromHint(QString value)
{
    value = value.toLower().replace('_', '-');
    if (value.contains("krea2") || value.contains("krea-2") || value.contains("flux2") || value.contains("flux-2")) return "flux2";
    if (value.contains("anima") || value.contains("cosmos-predict2")) return "anima";
    for (const auto &name : {"illustrious", "pony", "noobai", "noob-ai", "stable-diffusion-xl", "sdxl"})
        if (value.contains(name)) return "sdxl";
    if (value.contains("stable-diffusion-3") || value.contains("sd3")) return "sd3";
    if (value.contains("stable-diffusion-2") || value.contains("sd2")) return "sd2";
    for (const auto &name : {"stable-diffusion-1", "sd-1.5", "sd15", "sd1"})
        if (value.contains(name)) return "sd1";
    if (value.contains("flux")) return "flux1";
    return {};
}

QStringList ecosystemsFromSafetensors(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const auto prefix = file.read(8);
    if (prefix.size() != 8) return {};
    const quint64 headerSize = qFromLittleEndian<quint64>(reinterpret_cast<const uchar *>(prefix.constData()));
    constexpr quint64 maximumHeaderSize = 100ULL * 1024ULL * 1024ULL;
    if (!headerSize || headerSize > maximumHeaderSize || headerSize > quint64(file.size() - 8)) return {};
    const auto header = QJsonDocument::fromJson(file.read(qint64(headerSize))).object();
    if (header.isEmpty()) return {};
    QSet<QString> families;
    const auto metadata = header.value("__metadata__").toObject();
    for (const auto &name : {"modelspec.architecture", "modelspec.implementation", "iild.model_family",
                             "ss_base_model_version", "base_model", "base_model_version", "architecture"}) {
        const auto family = familyFromHint(metadata.value(name).toVariant().toString());
        if (!family.isEmpty()) families.insert(family);
    }
    int fluxWidth = 0;
    QSet<int> crossWidths;
    bool kreaBlocks = false, kreaProjection = false;
    for (auto it = header.begin(); it != header.end(); ++it) {
        if (it.key() == "__metadata__" || !it.value().isObject()) continue;
        QString key = it.key();
        for (const auto &prefixName : {"model.diffusion_model.", "diffusion_model.", "unet.", "transformer."}) {
            if (key.startsWith(prefixName)) { key.remove(0, int(strlen(prefixName))); break; }
        }
        const auto lowered = key.toLower().replace('_', '.');
        const auto shape = it.value().toObject().value("shape").toArray();
        if (lowered.contains("llm.adapter.blocks.0.cross.attn") || lowered.contains("anima")) families.insert("anima");
        if (key.startsWith("joint_blocks.") || key.contains("transformer_blocks.0.attn.add_q_proj")) families.insert("sd3");
        if ((key.startsWith("double_blocks.") || key.startsWith("single_blocks.")) && shape.size() == 2)
            fluxWidth = std::max(fluxWidth, std::min(shape.at(0).toInt(), shape.at(1).toInt()));
        kreaBlocks = kreaBlocks || key.startsWith("blocks.0.");
        kreaProjection = kreaProjection || key.startsWith("tproj.") || key.startsWith("txtfusion.");
        if (key.endsWith("attn2.to_k.weight") && shape.size() == 2) crossWidths.insert(shape.at(1).toInt());
    }
    if (fluxWidth) families.insert(fluxWidth >= 4096 ? "flux2" : "flux1");
    if (kreaBlocks && kreaProjection) families.insert("flux2");
    if (crossWidths.size() == 1) {
        const int width = *crossWidths.constBegin();
        if (width == 768) families.insert("sd1");
        else if (width == 1024) families.insert("sd2");
        else if (width == 1280 || width == 2048) families.insert("sdxl");
    }
    auto result = families.values();
    std::sort(result.begin(), result.end());
    return result;
}

QStringList ecosystemsForPath(const QString &path)
{
    const QFileInfo info(path);
    QSet<QString> families;
    const auto collect = [&](const QString &file) {
        for (const auto &family : ecosystemsFromSafetensors(file)) families.insert(family);
    };
    if (info.isFile() && (info.suffix().compare("safetensors", Qt::CaseInsensitive) == 0
                          || info.suffix().compare("safetensor", Qt::CaseInsensitive) == 0)) {
        collect(path);
    } else if (info.isDir()) {
        QDirIterator iterator(path, {"*.safetensors", "*.safetensor"}, QDir::Files, QDirIterator::Subdirectories);
        int count = 0;
        while (iterator.hasNext() && count++ < 64) collect(iterator.next());
    }
    if (families.isEmpty()) {
        const auto hinted = familyFromHint(info.fileName());
        if (!hinted.isEmpty()) families.insert(hinted);
    }
    auto result = families.values();
    std::sort(result.begin(), result.end());
    return result;
}

QString familyLabel(const QString &family)
{
    if (family == "flux2") return QStringLiteral("FLUX.2 / Krea 2");
    if (family == "flux1") return QStringLiteral("FLUX.1");
    if (family == "sdxl") return QStringLiteral("SDXL / Illustrious / Pony");
    if (family == "sd3") return QStringLiteral("Stable Diffusion 3");
    if (family == "sd2") return QStringLiteral("Stable Diffusion 2");
    if (family == "sd1") return QStringLiteral("Stable Diffusion 1");
    if (family == "anima") return QStringLiteral("Anima / Cosmos Predict2");
    return family;
}

QString ecosystemLabel(const QStringList &families)
{
    QStringList labels;
    for (const auto &family : families) labels.append(familyLabel(family));
    return labels.join(QStringLiteral(", "));
}

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
        const auto ecosystems = ecosystemsForPath(entry.path);
        result.models.append(QVariantMap{{"path", entry.path}, {"name", entry.name},
            {"relativePath", entry.relativePath}, {"kind", wrapped ? QString("checkpoint-package") : entry.kind}, {"format", wrapped ? QString("IILDMODEL") : entry.format.toUpper()},
            {"modelType", iiSocietyContainer::modelTypeName(type)}, {"baseEligible", !adapter},
            {"ecosystems", ecosystems}, {"ecosystemLabel", ecosystemLabel(ecosystems)}});
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
    if (object.value("schema").toString() != "iild-unified-model-v1" || stages.isEmpty() || stages.size() > 1024) return {};
    QString singleCheckpoint;
    for (const auto &value : stages) {
        if (!value.isObject()) return {};
        const auto stage = value.toObject();
        const auto relative = stage.value("model").toString();
        if (relative.isEmpty() || QDir::isAbsolutePath(relative)) return {};
        const QFileInfo payload(QDir(path).filePath(relative));
        const auto canonical = payload.canonicalFilePath();
        if (!canonical.startsWith(info.canonicalFilePath() + '/') || !payload.isFile()
            || payload.size() == 0 || !suffixes.contains(payload.suffix().toLower())) return {};
        singleCheckpoint = canonical;
    }
    const auto first = stages.first().toObject();
    // Preserve the historical single-checkpoint wrapper behaviour. A real cascade
    // remains one selectable package and is passed whole to the SDK for inspection.
    return stages.size() == 1 && first.value("strength").toDouble(-1) == 1.0
            && first.value("loras").toArray().isEmpty()
        ? singleCheckpoint : info.canonicalFilePath();
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

QString MergeModelCatalog::ecosystemDescription(const QString &path) const
{
    for (const auto &entry : m_models) {
        const auto model = entry.toMap();
        if (model.value("path").toString() != path) continue;
        const auto label = model.value("ecosystemLabel").toString();
        return label.isEmpty() ? tr("Base ecosystem could not be identified from metadata or tensor structure.")
                               : tr("Base ecosystem: %1.").arg(label);
    }
    return {};
}

QString MergeModelCatalog::compatibilityDescription(const QString &basePath, const QString &materialPath) const
{
    QVariantMap base, material;
    for (const auto &entry : m_models) {
        const auto model = entry.toMap();
        if (model.value("path").toString() == basePath) base = model;
        if (model.value("path").toString() == materialPath) material = model;
    }
    if (base.isEmpty() || material.isEmpty()) return {};
    const auto baseFamilies = base.value("ecosystems").toStringList();
    const auto materialFamilies = material.value("ecosystems").toStringList();
    if (baseFamilies.isEmpty() || materialFamilies.isEmpty())
        return tr("Compatibility is not yet conclusive. The merge engine will include this resource only if its tensor layout or LoRA targets match the base.");
    for (const auto &family : materialFamilies) {
        if (baseFamilies.contains(family))
            return tr("Conditional: shared %1 ecosystem. Tensor structure and LoRA targets are being checked before inclusion.").arg(familyLabel(family));
    }
    return tr("Incompatible: %1 resource for a %2 base. This resource will be excluded from the merge.")
        .arg(ecosystemLabel(materialFamilies), ecosystemLabel(baseFamilies));
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

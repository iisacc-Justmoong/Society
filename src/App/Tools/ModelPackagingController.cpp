#include "ModelPackagingController.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QtConcurrent/QtConcurrentRun>
#include <chrono>
#include <algorithm>
#ifdef SOCIETY_HAS_MODEL_PACKAGING
#include <ModelPackaging/ModelPackaging.hpp>
#endif

ModelPackagingController::ModelPackagingController(QObject *parent) : QObject(parent)
{
    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        m_seconds = int(m_elapsed.elapsed() / 1000);
        emit elapsedChanged();
    });
    connect(&m_watcher, &QFutureWatcher<QVariantMap>::finished, this, &ModelPackagingController::finish);
}

ModelPackagingController::~ModelPackagingController()
{
    if (m_stop) m_stop->request_stop();
    m_watcher.waitForFinished();
}

bool ModelPackagingController::supported() const
{
#ifdef SOCIETY_HAS_MODEL_PACKAGING
    return true;
#else
    return false;
#endif
}

QString ModelPackagingController::status() const
{
    if (cancelling() && m_busy) return tr("Cancelling safely…");
    if (m_phase == "scan") return tr("Inspecting model files");
    if (m_phase == "deduplicate") return tr("Checking duplicate tensors");
    if (m_phase == "write") return tr("Writing the model package");
    if (m_phase == "verify") return tr("Verifying the written package");
    if (m_phase == "save") return tr("Saving the verified package");
    if (m_phase == "complete") return tr("Model package ready");
    if (m_phase == "cancelled") return tr("Cancelled · Original files preserved");
    if (m_phase == "error") return tr("Packaging needs attention");
    return m_report.value("ready").toBool() ? tr("Ready to package") : tr("Choose a model folder");
}

bool ModelPackagingController::fail(const QString &error)
{
    m_error = error;
    emit changed();
    return false;
}

QString ModelPackagingController::localPath(const QUrl &url) const
{
    return url.isLocalFile() ? QDir::cleanPath(url.toLocalFile()) : QString{};
}

QString ModelPackagingController::outputPathForName(QString name, const QString &directory) const
{
    name = name.trimmed();
    if (name.endsWith(".safetensors", Qt::CaseInsensitive)) name.chop(12);
    // Keep the filename portable, including folders shared with Windows devices.
    if (directory.trimmed().isEmpty() || !QDir::isAbsolutePath(directory) || name.isEmpty()
        || name.size() > 120 || name == "." || name == ".." || name.endsWith('.')
        || name.contains(QRegularExpression("[\\\\/:*?\"<>|\\x00-\\x1f]"))
        || QRegularExpression("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\\.|$)",
            QRegularExpression::CaseInsensitiveOption).match(name).hasMatch()) return {};
    return QDir(directory).absoluteFilePath(name + ".safetensors");
}

QString ModelPackagingController::formatSize(double bytes) const
{
    if (bytes >= 1e9) return QString::number(bytes / 1e9, 'f', 2) + " GB";
    if (bytes >= 1e6) return QString::number(bytes / 1e6, 'f', 2) + " MB";
    if (bytes >= 1e3) return QString::number(bytes / 1e3, 'f', 1) + " KB";
    return QString::number(bytes, 'f', 0) + " B";
}

bool ModelPackagingController::scanFolder(const QString &directory)
{
    if (m_busy) return false;
    if (!QDir::isAbsolutePath(directory) || !QFileInfo(directory).isDir())
        return fail(tr("Choose an existing local model folder."));
    m_input = QDir::cleanPath(directory);
    m_scanned.clear(); m_report.clear(); m_excluded.clear();
    return start(false);
}

bool ModelPackagingController::setComponentIncluded(const QString &id, bool included)
{
    if (m_busy) return false;
    const auto files = m_scanned.value("files").toList();
    const auto found = std::find_if(files.begin(), files.end(), [&id](const QVariant &value) {
        const auto file = value.toMap();
        return file.value("id").toString() == id && file.value("optional").toBool()
            && file.value("status").toString() == "included";
    });
    if (found == files.end()) return false;
    m_excluded.removeAll(id);
    if (!included) m_excluded.append(id);
    rebuildSelection(); emit changed();
    return true;
}

void ModelPackagingController::rebuildSelection()
{
    m_report = m_scanned;
    auto files = m_report.value("files").toList();
    auto components = m_report.value("components").toList();
    double removed = 0; int included = 0, componentCount = 0;
    for (auto &value : files) {
        auto file = value.toMap();
        if (m_excluded.contains(file.value("id").toString())) {
            file["status"] = "excluded"; file["reason"] = tr("Optional component excluded");
            removed += file.value("size").toDouble();
        }
        if (file.value("status").toString() == "included") ++included;
        value = file;
    }
    for (auto &value : components) {
        auto component = value.toMap();
        if (m_excluded.contains(component.value("id").toString())) component["included"] = false;
        if (component.value("included").toBool()) ++componentCount;
        value = component;
    }
    m_report["files"] = files; m_report["components"] = components;
    m_report["included_file_count"] = included; m_report["component_count"] = componentCount;
    m_report["estimated_size_bytes"] = std::max(0.0, m_scanned.value("estimated_size_bytes").toDouble() - removed);
}

bool ModelPackagingController::createPackage(const QString &output)
{
    if (m_busy) return false;
    if (!m_report.value("ready").toBool()) return fail(tr("Inspect a complete model folder before packaging."));
    const QFileInfo target(output);
    if (output.isEmpty() || outputPathForName(target.fileName(), target.absolutePath()) != output)
        return fail(tr("Enter a valid model name and choose an output folder."));
    // The standard container destination can be created once; arbitrary missing
    // user-selected destinations are errors rather than silently created paths.
    if (!target.dir().exists() && target.dir().dirName() == "Checkpoint"
        && QFileInfo(target.absolutePath() + "/..").isDir()) QDir().mkpath(target.absolutePath());
    if (!target.dir().exists()) return fail(tr("The output folder does not exist."));
    if (target.exists() || target.isSymLink()) return fail(tr("A file with this name already exists. Choose another name."));
    return start(true, output);
}

bool ModelPackagingController::start(bool packaging, const QString &output)
{
    if (!supported()) return fail(tr("Model Packaging is not installed for this platform."));
    m_busy = true; m_packaging = packaging; m_phase = "scan";
    m_error.clear(); m_output.clear(); m_detail.clear(); m_requestedOutput = output;
    m_progress = 0; m_seconds = 0; m_elapsed.restart(); m_timer.start();
    m_stop = std::make_shared<std::stop_source>();
    const auto stop = m_stop;
    const auto input = m_input;
    const auto excluded = m_excluded;
    const auto revision = ++m_revision;
    emit changed(); emit elapsedChanged();
    m_watcher.setFuture(QtConcurrent::run([this, stop, input, excluded, output, packaging, revision] {
        QVariantMap result;
#ifdef SOCIETY_HAS_MODEL_PACKAGING
        try {
            std::vector<std::string> exclusions;
            for (const auto &file : excluded) exclusions.push_back(file.toStdString());
            auto last = std::chrono::steady_clock::time_point{};
            std::string previousPhase;
            const auto observer = [this, revision, &last, &previousPhase](const iild::ModelPackagingProgress &value) {
                const auto now = std::chrono::steady_clock::now();
                if (value.phase == previousPhase && now - last < std::chrono::milliseconds(150)) return;
                last = now; previousPhase = value.phase;
                const auto phase = QString::fromStdString(value.phase);
                const auto detail = QString::fromStdString(value.detail);
                const double fraction = value.totalBytes ? double(value.completedBytes) / double(value.totalBytes) : 0;
                QMetaObject::invokeMethod(this, [this, revision, phase, detail, fraction] {
                    if (!m_busy || m_revision != revision) return;
                    m_phase = phase; m_detail = detail; m_progress = std::clamp(fraction, 0.0, 1.0);
                    emit changed();
                }, Qt::QueuedConnection);
            };
            const auto json = packaging
                ? iild::createModelPackage(std::filesystem::path(input.toStdString()), std::filesystem::path(output.toStdString()), exclusions, stop->get_token(), observer)
                : iild::scanModelFolder(std::filesystem::path(input.toStdString()), exclusions, stop->get_token(), observer);
            QJsonParseError error;
            const auto document = QJsonDocument::fromJson(QByteArray::fromStdString(json), &error);
            if (error.error != QJsonParseError::NoError || !document.isObject())
                throw std::runtime_error("The SDK returned an invalid packaging report");
            result = document.object().toVariantMap();
        } catch (const std::exception &error) {
            result["error"] = QString::fromUtf8(error.what());
            result["cancelled"] = stop->stop_requested();
        }
#endif
        return result;
    }));
    return true;
}

void ModelPackagingController::finish()
{
    const auto result = m_watcher.result();
    m_timer.stop(); m_seconds = int(m_elapsed.elapsed() / 1000); m_busy = false;
    bool success = result.value("schema").toString() == "iild-model-package-report-v1"
        && result.value("ready").toBool();
    if (result.contains("error")) {
        const bool cancelled = result.value("cancelled").toBool();
        m_phase = cancelled ? "cancelled" : "error";
        m_error = cancelled ? QString{} : result.value("error").toString();
        success = false;
    } else if (m_packaging) {
        success = success && result.value("verified").toBool()
            && result.value("output").toString() == m_requestedOutput
            && QFileInfo(m_requestedOutput).isFile();
        if (success) { m_report = result; m_output = m_requestedOutput; m_phase = "complete"; m_progress = 1; }
        else { m_phase = "error"; m_error = tr("The SDK did not confirm a saved and verified model package."); }
    } else {
        m_scanned = result; rebuildSelection(); m_phase = success ? "ready" : "error";
        QStringList errors;
        for (const auto &error : result.value("errors").toList()) errors.append(error.toString());
        m_error = errors.join('\n');
    }
    m_stop.reset(); emit changed(); emit elapsedChanged(); emit finished(success, m_packaging);
}

void ModelPackagingController::cancel()
{
    if (m_busy && m_stop) { m_stop->request_stop(); emit changed(); }
}

void ModelPackagingController::copyDetails() const
{
    QGuiApplication::clipboard()->setText(QJsonDocument::fromVariant(m_report).toJson(QJsonDocument::Indented));
}

void ModelPackagingController::openOutputFolder() const
{
    if (!m_output.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_output).absolutePath()));
}

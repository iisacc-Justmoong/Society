#include "ModelPackagingController.h"
#include <SharedStorage.h>
#include <FileOperations.h>
#include <TemporaryLease.h>

#include <QClipboard>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QScopeGuard>
#include <QUuid>
#include <QFile>
#include <QtConcurrent/QtConcurrentRun>
#include <chrono>
#include <algorithm>
#ifdef SOCIETY_HAS_MODEL_PACKAGING
#include "ModelPackaging/ModelPackaging.hpp"
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
    if (assignmentsDirty()) return tr("Apply assignments to update the package preview");
    return m_report.value("ready").toBool() ? tr("Ready to package") : tr("Choose models or a model folder");
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
    m_input.clear(); m_selected.clear(); m_scanned.clear(); m_report.clear(); m_excluded.clear();
    m_assignments.clear(); m_appliedAssignments.clear();
    emit assignmentsChanged();
    if (!QDir::isAbsolutePath(directory) || !QFileInfo(directory).isDir())
        return fail(tr("Choose an existing local model folder."));
    m_input = QDir::cleanPath(directory);
    return start(false);
}

bool ModelPackagingController::scanSelection(const QString &modelsDirectory, const QStringList &paths)
{
    if (m_busy) return false;
    m_input.clear(); m_selected.clear(); m_scanned.clear(); m_report.clear(); m_excluded.clear();
    m_assignments.clear(); m_appliedAssignments.clear();
    emit assignmentsChanged();
    const auto storage = iiSocietyContainer::SharedStorage::open();
    const auto root = QDir::cleanPath(modelsDirectory);
    if (!storage || paths.isEmpty() || root != storage->drive().sectionPath(iiSocietyContainer::StoreSection::Models))
        return fail(tr("Choose models from the currently open Society drive."));
    const iiSocietyContainer::FileOperations files(storage->drive());
    QStringList selected;
    for (const auto &path : paths) {
        const auto clean = QDir::cleanPath(path);
        const QFileInfo info(clean);
        if (!QDir::isAbsolutePath(path) || !clean.startsWith(root + '/') || !files.editable(clean)
            || (!info.isFile() && !info.isDir()) || info.isSymLink() || info.isJunction())
            return fail(tr("A selected model is unavailable or outside this Society drive."));
        const auto relative = QDir(root).relativeFilePath(clean);
        if (!selected.contains(relative)) selected.append(relative);
    }
    m_input = root; m_selected = selected;
    return start(false);
}

bool ModelPackagingController::chooseModels(const QString &modelsDirectory, const QVariantList &catalog,
                                             const QStringList &selected)
{
    if (m_busy || m_chooser) return false;
    const auto storage = iiSocietyContainer::SharedStorage::open();
    const auto root = QDir::cleanPath(modelsDirectory);
    if (!storage || root != storage->drive().sectionPath(iiSocietyContainer::StoreSection::Models))
        return fail(tr("Open a Society drive to choose models."));
    const QString executable = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(
#if defined(Q_OS_WIN)
        "SocietyModelChooser.exe");
#else
        "SocietyModelChooser");
#endif
    if (!QFileInfo(executable).isExecutable())
        return fail(tr("The model chooser is unavailable in this installation."));

    QJsonArray models, current;
    QSet<QString> offered;
    for (const auto &value : catalog) {
        const auto model = value.toMap();
        const auto path = QDir::cleanPath(model.value("path").toString());
        const auto name = model.value("name").toString();
        if (!model.value("packagingEligible").toBool() || name.isEmpty()
            || !path.startsWith(root + '/') || offered.contains(path)) continue;
        offered.insert(path);
        const auto category = QDir(root).relativeFilePath(path).section('/', 0, 0);
        models.append(QJsonObject{{"path", path}, {"name", name}, {"category", category}});
    }
    for (const auto &path : selected) if (offered.contains(path)) current.append(path);
    const auto request = QJsonDocument(QJsonObject{{"models", models}, {"selected", current}}).toJson(QJsonDocument::Compact);
    if (request.size() > 16 * 1024 * 1024 || models.size() > 65536)
        return fail(tr("The model catalogue is too large to open."));

    auto *process = new QProcess(this);
    m_chooser = process;
    const auto revision = ++m_chooserRevision;
    m_error.clear();
    connect(process, &QProcess::started, process, [process, request] {
        process->write(request);
        process->closeWriteChannel();
    });
    connect(process, &QProcess::errorOccurred, this, [this, process, revision](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || revision != m_chooserRevision) return;
        m_chooser.clear();
        process->deleteLater();
        fail(tr("Could not open the model chooser."));
    });
    connect(process, &QProcess::finished, this,
            [this, process, revision, root, offered](int exitCode, QProcess::ExitStatus status) {
        const auto response = process->readAllStandardOutput();
        process->deleteLater();
        if (revision != m_chooserRevision) return;
        m_chooser.clear();
        emit changed();
        if (status != QProcess::NormalExit || exitCode != 0 || response.size() > 16 * 1024 * 1024) {
            fail(tr("The model chooser did not finish successfully."));
            return;
        }
        const auto storage = iiSocietyContainer::SharedStorage::open();
        if (!storage || root != storage->drive().sectionPath(iiSocietyContainer::StoreSection::Models)) return;
        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(response, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            fail(tr("The model chooser returned an invalid selection."));
            return;
        }
        const auto result = document.object();
        const auto action = result.value("action").toString();
        if (action == "cancel") return;
        if (action == "folder") {
            const auto folder = result.value("path").toString();
            if (QDir::isAbsolutePath(folder) && QFileInfo(folder).isDir()) emit folderChosen(folder);
            else fail(tr("The selected folder is no longer available."));
            return;
        }
        if (action == "selected" && result.value("paths").isArray()) {
            QStringList paths;
            QSet<QString> seen;
            for (const auto &value : result.value("paths").toArray()) {
                const auto path = value.toString();
                if (!value.isString() || !offered.contains(path) || seen.contains(path)) {
                    fail(tr("The model chooser returned an invalid selection."));
                    return;
                }
                seen.insert(path); paths.append(path);
            }
            if (!paths.isEmpty()) emit modelsChosen(paths);
            return;
        }
        fail(tr("The model chooser returned an invalid selection."));
    });
    process->start(executable);
    emit changed();
    return true;
}

void ModelPackagingController::clearInput()
{
    ++m_chooserRevision;
    if (m_chooser) { m_chooser->kill(); m_chooser.clear(); }
    if (m_busy) { m_clearPending = true; cancel(); return; }
    m_input.clear(); m_selected.clear(); m_scanned.clear(); m_report.clear(); m_excluded.clear();
    m_assignments.clear(); m_appliedAssignments.clear();
    m_error.clear(); m_output.clear(); m_phase.clear(); m_detail.clear(); m_progress = 0;
    emit assignmentsChanged(); emit changed();
}

QVariantList ModelPackagingController::roleOptions() const
{
    return {QVariantMap{{"value", ""}, {"label", tr("Automatic")}},
        QVariantMap{{"value", "model"}, {"label", tr("Model")}},
        QVariantMap{{"value", "text_encoder"}, {"label", tr("Text encoder")}},
        QVariantMap{{"value", "video_vae"}, {"label", tr("VAE / decoder")}},
        QVariantMap{{"value", "audio_vae"}, {"label", tr("Audio decoder")}},
        QVariantMap{{"value", "projections"}, {"label", tr("Text projections")}},
        QVariantMap{{"value", "upscaler"}, {"label", tr("Upscaler")}},
        QVariantMap{{"value", "lora"}, {"label", tr("LoRA")}},
        QVariantMap{{"value", "other"}, {"label", tr("Other component")}}};
}

QVariantList ModelPackagingController::positionOptions() const
{
    return {QVariantMap{{"value", ""}, {"label", tr("Automatic")}},
        QVariantMap{{"value", "main"}, {"label", tr("Main")}},
        QVariantMap{{"value", "component"}, {"label", tr("Component")}}};
}

QVariantList ModelPackagingController::assignableFiles() const
{
    QVariantList result;
    const auto files = m_scanned.value("files").toList();
    QHash<QString, QString> groups;
    for (const auto &value : files) {
        const auto file = value.toMap();
        groups.insert(file.value("id").toString(), file.value("group").toString());
    }
    QHash<QString, QVariantMap> choices;
    for (auto it = m_assignments.cbegin(); it != m_assignments.cend(); ++it) {
        if (!groups.contains(it.key())) continue;
        auto &choice = choices[groups.value(it.key())];
        const auto fields = it.value().toMap();
        if (fields.contains("role")) choice["role"] = fields.value("role");
        if (fields.contains("position")) choice["position"] = fields.value("position");
    }
    for (const auto &value : files) {
        auto file = value.toMap();
        const auto format = file.value("format").toString();
        const auto status = file.value("status").toString();
        if ((format != "safetensors" && format != "gguf") || status == "invalid" || status == "ignored") continue;
        const auto choice = choices.value(file.value("group").toString());
        file["roleChoice"] = choice.value("role");
        file["positionChoice"] = choice.value("position");
        file["effectiveRole"] = choice.value("role", file.value("detected_role"));
        result.append(file);
    }
    return result;
}

bool ModelPackagingController::setFileRole(const QString &id, const QString &role)
{
    if (m_busy) return false;
    const auto options = roleOptions();
    if (std::none_of(options.cbegin(), options.cend(), [&](const QVariant &v) { return v.toMap().value("value").toString() == role; })) return false;
    const auto files = assignableFiles();
    const auto found = std::find_if(files.cbegin(), files.cend(), [&](const QVariant &v) { return v.toMap().value("id").toString() == id; });
    if (found == files.cend()) return false;
    const auto group = found->toMap().value("group").toString();
    const auto effectiveRole = role.isEmpty() ? found->toMap().value("detected_role").toString() : role;
    for (const auto &value : files) {
        const auto file = value.toMap();
        if (file.value("group").toString() != group) continue;
        const auto key = file.value("id").toString(); auto choice = m_assignments.value(key).toMap();
        choice.remove("role");
        if (effectiveRole != "model") choice.remove("position");
        if (choice.isEmpty()) m_assignments.remove(key); else m_assignments[key] = choice;
        if (!role.isEmpty() && role != "upscaler" && role != "lora") m_excluded.removeAll(key);
    }
    if (!role.isEmpty()) { auto choice = m_assignments.value(id).toMap(); choice["role"] = role; m_assignments[id] = choice; }
    rebuildSelection(); m_error.clear(); emit assignmentsChanged(); emit changed(); return true;
}

bool ModelPackagingController::setFilePosition(const QString &id, const QString &position)
{
    if (m_busy || (position != "" && position != "main" && position != "component")) return false;
    const auto files = assignableFiles();
    const auto found = std::find_if(files.cbegin(), files.cend(), [&](const QVariant &v) { return v.toMap().value("id").toString() == id; });
    if (found == files.cend() || (position == "main" && found->toMap().value("effectiveRole").toString() != "model")) return false;
    const auto group = found->toMap().value("group").toString();
    for (const auto &value : files) {
        const auto file = value.toMap(); const auto key = file.value("id").toString();
        auto choice = m_assignments.value(key).toMap();
        if (file.value("group").toString() == group) choice.remove("position");
        else if (position == "main" && choice.value("position").toString() == "main") choice["position"] = "component";
        if (choice.isEmpty()) m_assignments.remove(key); else m_assignments[key] = choice;
    }
    if (!position.isEmpty()) {
        auto choice = m_assignments.value(id).toMap(); choice["position"] = position;
        const auto file = found->toMap();
        // A standalone GGUF can be promoted automatically to the main model.
        // Preserve that effective role when the user explicitly pins it.
        if (position == "main" && file.value("format").toString() == "gguf"
            && file.value("role_source").toString() == "detected" && file.value("role").toString() == "model")
            choice["role"] = "model";
        m_assignments[id] = choice;
    }
    m_error.clear(); emit assignmentsChanged(); emit changed(); return true;
}

bool ModelPackagingController::applyAssignments()
{
    if (m_busy || !assignmentsDirty() || m_input.isEmpty()) return false;
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
    const auto group = found->toMap().value("group").toString();
    for (const auto &value : files) {
        const auto file = value.toMap();
        if (!file.value("optional").toBool() || file.value("group").toString() != group) continue;
        const auto key = file.value("id").toString();
        m_excluded.removeAll(key);
        if (!included) m_excluded.append(key);
    }
    rebuildSelection(); emit changed();
    return true;
}

void ModelPackagingController::rebuildSelection()
{
    m_report = m_scanned;
    auto files = m_report.value("files").toList();
    auto components = m_report.value("components").toList();
    QSet<QString> excludedGroups, excludedFiles;
    for (const auto &value : files) {
        const auto file = value.toMap();
        if (file.value("optional").toBool() && m_excluded.contains(file.value("id").toString()))
            excludedGroups.insert(file.value("group").toString());
    }
    double removed = 0; int included = 0, componentCount = 0;
    for (auto &value : files) {
        auto file = value.toMap();
        const auto id = file.value("id").toString();
        const auto group = file.value("group").toString();
        const bool shardIndex = id == group && id.endsWith(".safetensors.index.json") && file.value("format").toString() == "asset";
        if (excludedGroups.contains(group) && (file.value("optional").toBool() || shardIndex)) {
            file["status"] = "excluded"; file["reason"] = tr("Optional component excluded");
            removed += file.value("size").toDouble();
            excludedFiles.insert(id);
        }
        if (file.value("status").toString() == "included") ++included;
        value = file;
    }
    QVariantList visibleComponents;
    for (auto &value : components) {
        auto component = value.toMap();
        if (excludedFiles.contains(component.value("id").toString())
            && !component.value("optional").toBool()
            && component.value("id").toString().endsWith(".safetensors.index.json")) continue;
        if (excludedFiles.contains(component.value("id").toString())) component["included"] = false;
        if (component.value("included").toBool()) ++componentCount;
        visibleComponents.append(component);
    }
    m_report["files"] = files; m_report["components"] = visibleComponents;
    m_report["included_file_count"] = included; m_report["component_count"] = componentCount;
    m_report["estimated_size_bytes"] = std::max(0.0, m_scanned.value("estimated_size_bytes").toDouble() - removed);
}

bool ModelPackagingController::createPackage(const QString &output)
{
    if (m_busy) return false;
    if (assignmentsDirty()) return fail(tr("Apply model assignments before packaging."));
    if (!m_report.value("ready").toBool()) return fail(tr("Inspect the selected models before packaging."));
    const QFileInfo target(output);
    if (output.isEmpty() || outputPathForName(target.fileName(), target.absolutePath()) != output)
        return fail(tr("Enter a valid model name and choose an output folder."));
    const auto storage = iiSocietyContainer::SharedStorage::open();
    if (!storage || !iiSocietyContainer::FileOperations(storage->drive()).editable(output))
        return fail(tr("Choose an output folder inside the selected .societycontainer package."));
    // Relative output folders selected in the app are prepared lazily inside
    // Models, after the public package boundary has been checked.
    const auto models = storage->drive().sectionPath(iiSocietyContainer::StoreSection::Models);
    if (!target.dir().exists() && target.absolutePath().startsWith(models + '/')) {
        const auto relative = QDir(models).relativeFilePath(target.absolutePath());
        QString error;
        if (storage->ensureDirectory(iiSocietyContainer::StoreSection::Models, relative, &error).isEmpty())
            return fail(error);
    }
    if (!target.dir().exists()) return fail(tr("The output folder does not exist."));
    if (!iiSocietyContainer::FileOperations(storage->drive()).editable(output))
        return fail(tr("The container output folder is unavailable."));
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
    const auto selected = m_selected;
    const auto excluded = m_excluded;
    const auto assignments = m_assignments;
    if (!packaging) m_inflightAssignments = assignments;
    const auto revision = ++m_revision;
    emit changed(); emit elapsedChanged();
    m_watcher.setFuture(QtConcurrent::run([this, stop, input, selected, excluded, assignments, output, packaging, revision] {
        QVariantMap result;
#ifdef SOCIETY_HAS_MODEL_PACKAGING
        try {
            const auto storage = (packaging || !selected.isEmpty()) ? iiSocietyContainer::SharedStorage::open() : std::nullopt;
            if (!selected.isEmpty() && (!storage || input != storage->drive().sectionPath(iiSocietyContainer::StoreSection::Models)))
                throw std::runtime_error("The selected Society model library is no longer open.");
            QString staged = output, workspace;
            std::unique_ptr<iiFileProvider::TemporaryLease> lease;
            if (packaging) {
                if (!storage || !iiSocietyContainer::FileOperations(storage->drive()).editable(output))
                    throw std::runtime_error("Choose an output inside the selected .societycontainer.");
                const auto parent = storage->ensureRuntimeDirectory("tmp");
                if (parent.isEmpty()) throw std::runtime_error("The container temporary area is unavailable.");
                lease = std::make_unique<iiFileProvider::TemporaryLease>(std::filesystem::path(parent.toStdString()), iiFileProvider::TemporaryLease::Mode::Shared);
                if (!lease->locked()) throw std::runtime_error("The container temporary area is busy.");
                workspace = storage->ensureRuntimeDirectory("tmp/.society-model-package-" + QUuid::createUuid().toString(QUuid::WithoutBraces));
                if (workspace.isEmpty()) throw std::runtime_error("The packaging workspace is unavailable.");
                staged = workspace + "/output.safetensors";
            }
            const auto cleanup = qScopeGuard([&] {
                if (!workspace.isEmpty() && storage && storage->runtimePath(QDir(storage->runtimePath()).relativeFilePath(workspace)) == workspace)
                    QDir(workspace).removeRecursively();
            });
            std::vector<std::string> exclusions;
            if (packaging) for (const auto &file : excluded) exclusions.push_back(file.toStdString());
            std::vector<std::filesystem::path> selection;
            for (const auto &file : selected) selection.emplace_back(file.toStdU16String());
            std::vector<iild::ModelPackagingAssignment> choices;
            for (auto it = assignments.cbegin(); it != assignments.cend(); ++it) {
                const auto fields = it.value().toMap();
                choices.push_back({std::filesystem::path(it.key().toStdU16String()),
                    fields.value("role").toString().toStdString(), fields.value("position").toString().toStdString()});
            }
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
            const auto source = std::filesystem::path(input.toStdU16String());
            const auto target = std::filesystem::path(staged.toStdU16String());
            const auto json = selected.isEmpty()
                ? (packaging ? iild::createModelPackageWithAssignments(source, target, choices, exclusions, stop->get_token(), observer)
                             : iild::scanModelFolderWithAssignments(source, choices, exclusions, stop->get_token(), observer))
                : (packaging ? iild::createModelPackageFromSelectionWithAssignments(source, selection, target, choices, exclusions, stop->get_token(), observer)
                             : iild::scanModelSelectionWithAssignments(source, selection, choices, exclusions, stop->get_token(), observer));
            QJsonParseError error;
            const auto document = QJsonDocument::fromJson(QByteArray::fromStdString(json), &error);
            if (error.error != QJsonParseError::NoError || !document.isObject())
                throw std::runtime_error("The SDK returned an invalid packaging report");
            result = document.object().toVariantMap();
            if (packaging) {
                if (!result.value("verified").toBool() || stop->stop_requested()
                    || !iiSocietyContainer::FileOperations(storage->drive()).editable(output)
                    || !QFile::rename(staged, output))
                    throw std::runtime_error("The verified package could not be saved without overwriting an existing file.");
                result["output"] = output;
            }
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
            && QDir::cleanPath(QDir::fromNativeSeparators(result.value("output").toString()))
                == QDir::cleanPath(QDir::fromNativeSeparators(m_requestedOutput))
            && QFileInfo(m_requestedOutput).isFile();
        if (success) { m_report = result; m_report["output"] = m_requestedOutput; m_output = m_requestedOutput; m_phase = "complete"; m_progress = 1; }
        else { m_phase = "error"; m_error = tr("The SDK did not confirm a saved and verified model package."); }
    } else {
        m_scanned = result; m_appliedAssignments = m_inflightAssignments;
        for (const auto &value : m_scanned.value("files").toList()) {
            const auto file = value.toMap();
            if (!file.value("optional").toBool()) m_excluded.removeAll(file.value("id").toString());
        }
        rebuildSelection(); m_phase = success ? "ready" : "error";
        QStringList errors;
        for (const auto &error : result.value("errors").toList()) errors.append(error.toString());
        m_error = errors.join('\n');
    }
    if (m_clearPending) {
        m_clearPending = false; m_input.clear(); m_selected.clear(); m_scanned.clear(); m_report.clear(); m_excluded.clear();
        m_assignments.clear(); m_appliedAssignments.clear();
        success = false;
    }
    m_stop.reset(); emit assignmentsChanged(); emit changed(); emit elapsedChanged(); emit finished(success, m_packaging);
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

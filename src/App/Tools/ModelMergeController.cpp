#include "ModelMergeController.h"
#include "MergeModelCatalog.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QStandardPaths>
#include <QUuid>
#include <cmath>
#if defined(SOCIETY_MODEL_MERGE_PROCESS) && defined(Q_OS_UNIX)
#include <csignal>
#endif

namespace {
QString pathFromText(QString text)
{
    text = text.trimmed();
    if (text.startsWith("file:")) text = QUrl(text).toLocalFile();
    if (text == "~") text = QDir::homePath();
    else if (text.startsWith("~/")) text = QDir::home().filePath(text.mid(2));
    if (text.isEmpty() || !QDir::isAbsolutePath(text)) return {};
    return QDir::cleanPath(text);
}

QString executable(QString value)
{
    value = value.trimmed();
    if (value.isEmpty()) return {};
    const auto path = pathFromText(value);
    if (!path.isEmpty()) return QFileInfo(path).isExecutable() && QFileInfo(path).isFile() ? path : QString();
    return QStandardPaths::findExecutable(value);
}

bool weightValue(const QVariant &value, QString &argument)
{
    if (value.metaType().id() == QMetaType::Bool) return false;
    bool ok = false;
    auto locale = QLocale::c();
    locale.setNumberOptions(QLocale::RejectGroupSeparator);
    const double number = locale.toDouble(value.toString().trimmed(), &ok);
    if (!ok || !std::isfinite(number) || number < 0) return false;
    argument = QString::number(number, 'g', 17);
    return true;
}
}

ModelMergeController::ModelMergeController(QObject *parent) : QObject(parent), m_status(tr("Ready"))
{
    m_tick.setInterval(1000);
    connect(&m_tick, &QTimer::timeout, this, [this] {
        m_elapsedSeconds = int(m_elapsed.elapsed() / 1000);
        emit elapsedChanged();
    });
#ifdef SOCIETY_MODEL_MERGE_PROCESS
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &ModelMergeController::readOutput);
    connect(&m_process, &QProcess::readyReadStandardError, this, &ModelMergeController::readOutput);
    connect(&m_process, &QProcess::started, this, [this] { if (m_cancelled) cancel(); });
    connect(&m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus state) {
        finish(code, state == QProcess::CrashExit);
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            m_error = tr("Could not start iild-merge: %1").arg(m_process.errorString());
            finish(-1, true);
        }
    });
#endif
}

ModelMergeController::~ModelMergeController()
{
#ifdef SOCIETY_MODEL_MERGE_PROCESS
    disconnect(&m_process, nullptr, this, nullptr);
    if (m_process.state() != QProcess::NotRunning) {
        cancel();
        if (!m_process.waitForFinished(2000)) {
            m_process.kill();
            m_process.waitForFinished(2000);
        }
    }
#endif
}

bool ModelMergeController::supported() const
{
#ifdef SOCIETY_MODEL_MERGE_PROCESS
    return true;
#else
    return false;
#endif
}

QString ModelMergeController::defaultExecutable() const
{
    const auto override = executable(qEnvironmentVariable("SOCIETY_MODEL_MERGE_EXECUTABLE"));
    if (!override.isEmpty()) return override;
    QStringList candidates{QDir(QCoreApplication::applicationDirPath()).filePath("iild-merge")};
#ifdef SOCIETY_MODEL_MERGE_EXECUTABLE
    candidates.append(QString::fromUtf8(SOCIETY_MODEL_MERGE_EXECUTABLE));
#endif
    candidates.append(QDir::home().filePath(".local/SDK/iiLocalDiffusion/bin/iild-merge"));
    candidates.append("iild-merge");
    for (const auto &candidate : candidates) {
        const auto result = executable(candidate);
        if (!result.isEmpty()) return result;
    }
    return {};
}

QString ModelMergeController::defaultPython() const
{
    return qEnvironmentVariable("IILD_PYTHON_EXECUTABLE");
}

QString ModelMergeController::localPath(const QUrl &url) const { return url.isLocalFile() ? url.toLocalFile() : QString(); }
QString ModelMergeController::outputPathForName(const QString &name, const QString &directory,
                                                const QString &mode, const QString &baseModel) const
{
    auto stem = name.trimmed();
    const auto parent = pathFromText(directory);
    if (parent.isEmpty() || stem.isEmpty() || stem.startsWith('.') || stem.contains('/') || stem.contains('\\')) return {};
    for (const auto character : stem)
        if (character.category() == QChar::Other_Control) return {};
    if (stem.endsWith(".iildmodel", Qt::CaseInsensitive)) stem.chop(10);
    else if (stem.endsWith(".safetensors", Qt::CaseInsensitive)) stem.chop(12);
    else if (stem.endsWith(".safetensor", Qt::CaseInsensitive)) stem.chop(11);
    if (stem.trimmed().isEmpty()) return {};
    const auto base = pathFromText(baseModel);
    const bool packageOutput = mode == "unified" || (QFileInfo(base).isDir()
        && QFileInfo(base).suffix().compare("iildmodel", Qt::CaseInsensitive) == 0);
    return QDir(parent).filePath(stem + (packageOutput ? ".iildmodel" : ".safetensors"));
}

bool ModelMergeController::fail(const QString &message)
{
    m_error = message;
    m_status = tr("Check the inputs");
    emit changed();
    return false;
}

bool ModelMergeController::run(const QVariantMap &options, bool validateOnly)
{
    if (m_busy) return false;
    m_error.clear(); m_details.clear(); m_report.clear(); m_completedOutput.clear(); m_cancelled = false;
    m_elapsedSeconds = 0;
    emit elapsedChanged();
    if (!supported()) return fail(tr("Model merging requires the desktop iiLocalDiffusion Python runtime."));
    const auto program = executable(options.value("executable", defaultExecutable()).toString());
    if (program.isEmpty()) return fail(tr("Select an installed iild-merge executable in Runtime."));
    const auto pythonText = options.value("pythonExecutable").toString().trimmed();
    const auto python = executable(pythonText);
    if (!pythonText.isEmpty() && python.isEmpty()) return fail(tr("The selected Python executable could not be found."));
    const auto base = MergeModelCatalog::checkpointPath(pathFromText(options.value("baseModel").toString()));
    if (base.isEmpty() || !QFileInfo::exists(base)) return fail(tr("Choose an existing checkpoint file or .iildmodel package."));
    const auto mode = options.value("mode", "weighted-sum").toString();
    if (mode != "weighted-sum" && mode != "weighted-difference" && mode != "unified")
        return fail(tr("Choose Unified, weighted sum, or weighted difference."));
    const auto materials = options.value("materials").toList();
    if (materials.isEmpty()) return fail(tr("Add at least one checkpoint or LoRA material."));
    QStringList arguments{"--base-model", base, "--mode", mode,
                          "--checkpoint-policy", "common-layer",
                          "--lora-policy", "strict"};
    for (qsizetype i = 0; i < materials.size(); ++i) {
        auto path = pathFromText(materials[i].toMap().value("path").toString());
        if (QFileInfo(path).isDir() && QFileInfo(path).suffix().compare("iildmodel", Qt::CaseInsensitive) == 0)
            path = MergeModelCatalog::checkpointPath(path);
        if (path.isEmpty() || (mode == "unified" && !QFileInfo::exists(path)))
            return fail(tr("Choose a local model for material %1.").arg(i + 1));
        arguments << "--additional-model" << path;
    }
    const auto candidates = options.value("compatibilityModels").toStringList();
    if (mode == "unified") {
        for (const auto &candidate : candidates) {
            const auto path = MergeModelCatalog::checkpointPath(pathFromText(candidate));
            if (path.isEmpty()) return fail(tr("Choose an existing compatibility checkpoint."));
            arguments << "--compatibility-model" << path;
        }
    }
    const auto weightMode = options.value("weightMode", "automatic").toString();
    if (weightMode == "shared") {
        QString weight;
        if (!weightValue(options.value("sharedWeight"), weight)) return fail(tr("The shared weight must be a finite nonnegative number."));
        arguments << "--weights" << weight;
    } else if (weightMode == "per-model") {
        arguments << "--weights";
        for (qsizetype i = 0; i < materials.size(); ++i) {
            QString weight;
            if (!weightValue(materials[i].toMap().value("weight"), weight)) return fail(tr("Enter a finite nonnegative weight for material %1.").arg(i + 1));
            arguments << weight;
        }
    } else if (weightMode != "automatic") return fail(tr("Choose automatic, shared, or per-model weights."));
    const auto requested = options.value("output").toString().trimmed();
    if (requested.isEmpty()) return fail(tr("Enter an output model name before checking or merging models."));
    const auto output = pathFromText(requested);
    if (output.isEmpty()) return fail(tr("Enter an absolute local output path."));
    const auto suffix = QFileInfo(output).suffix().toLower();
    const bool packageOutput = mode == "unified" || (QFileInfo(base).isDir()
        && QFileInfo(base).suffix().compare("iildmodel", Qt::CaseInsensitive) == 0);
    if (packageOutput ? suffix != "iildmodel" : (suffix != "safetensors" && suffix != "safetensor"))
        return fail(packageOutput ? tr("This package merge requires an .iildmodel output.")
                                  : tr("Weighted checkpoint arithmetic requires a .safetensors output."));
    if (QFileInfo::exists(output) || QFileInfo(output).isSymLink()) return fail(tr("The output already exists. Choose a new model name. Original models are always kept."));
    const auto cacheText = options.value("cacheDirectory").toString().trimmed();
    const auto cache = cacheText.isEmpty() ? QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).filePath("model-merge") : pathFromText(cacheText);
    if (cache.isEmpty()) return fail(tr("Enter an absolute local conversion cache path."));
    arguments << "--output" << output << "--cache-dir" << cache;
    if (validateOnly) arguments << "--inspect";
#ifdef SOCIETY_MODEL_MERGE_PROCESS
    auto environment = QProcessEnvironment::systemEnvironment();
    if (!python.isEmpty()) environment.insert("IILD_PYTHON_EXECUTABLE", python);
    else environment.remove("IILD_PYTHON_EXECUTABLE");
    environment.insert("PYTHONUNBUFFERED", "1");
    m_process.setProcessEnvironment(environment);
    m_process.setProgram(program);
    m_process.setArguments(arguments);
    m_process.setWorkingDirectory(QFileInfo(base).absolutePath());
    m_stdout.clear(); m_stderr.clear();
    m_requestedOutput = output;
    m_validationOnly = validateOnly;
    m_busy = true;
    m_status = validateOnly ? tr("Inspecting model inputs and planned tensor projections…")
        : tr("Merging model tensors…");
    m_elapsed.start(); m_tick.start();
    emit changed();
    m_process.start();
    return true;
#else
    return false;
#endif
}

bool ModelMergeController::inspectInputs(QVariantMap options)
{
    // Preflight does not require a final name and never publishes this path.
    // A unique prospective name also avoids rejecting inspection after a merge.
    const auto base = options.value("baseModel").toString();
    const auto mode = options.value("mode", "weighted-sum").toString();
    options["output"] = outputPathForName("inspection-" + QUuid::createUuid().toString(QUuid::WithoutBraces),
        QFileInfo(base).absolutePath(), mode, base);
    return run(options, true);
}

void ModelMergeController::readOutput()
{
#ifdef SOCIETY_MODEL_MERGE_PROCESS
    m_stdout += m_process.readAllStandardOutput();
    m_stderr += m_process.readAllStandardError();
    if (m_stderr.size() > 65536) m_stderr = m_stderr.right(65536);
    if (m_stdout.size() > 64 * 1024 * 1024) {
        m_error = tr("The SDK report exceeded 64 MiB. Check the output folder and reduce the number of materials.");
        m_stdout.clear();
        m_process.kill();
    }
#endif
}

void ModelMergeController::finish(int exitCode, bool crashed)
{
    if (!m_busy) return;
    readOutput();
    m_tick.stop(); m_elapsedSeconds = int(m_elapsed.elapsed() / 1000);
    m_busy = false;
    const auto document = QJsonDocument::fromJson(m_stdout);
    bool success = exitCode == 0 && !crashed && m_error.isEmpty() && document.isObject();
    if (success && !m_validationOnly)
        success = document.object().value("schema").toString() == "iild-model-merge-v1"
            && document.object().value("output").toString() == m_requestedOutput
            && (QFileInfo(m_requestedOutput).isFile() || (QFileInfo(m_requestedOutput).isDir()
                && QFileInfo(m_requestedOutput).suffix().compare("iildmodel", Qt::CaseInsensitive) == 0));
    if (success && !m_validationOnly
            && document.object().value("output_verification").toObject().value("status").toString() != "passed") {
        success = false;
        m_error = tr("The SDK created an output without saved-output verification. Update the merge runtime and inspect the file before using it.");
    }
    if (success) {
        m_report = document.object().toVariantMap();
        m_details = QString::fromUtf8(document.toJson(QJsonDocument::Indented));
        if (m_validationOnly) {
            const auto report = document.object();
            m_status = tr("Inspection complete. %1 compatible material(s) will be merged; %2 incompatible material(s) will be excluded.")
                .arg(report.value("included_material_count").toInt())
                .arg(report.value("excluded_material_count").toInt());
        } else {
            m_completedOutput = m_requestedOutput;
            const auto report = document.object();
            const int changed = report.value("changed_tensor_count").toInt(report.value("merged_tensor_count").toInt());
            m_status = tr("Changed %1 of %2 merged tensors from %3 models; excluded %4 incompatible material(s). Base weight: %5. Equal file size is expected when tensor layouts match.")
                .arg(changed)
                .arg(report.value("merged_tensor_count").toInt())
                .arg(report.value("sources").toArray().size())
                .arg(report.value("excluded_material_count").toInt())
                .arg(report.value("base_weight").toDouble(), 0, 'g', 8);
            if (report.value("result_kind").toString() == "base-fallback")
                m_status = tr("Output created from the base model. No effective material contribution; %1 material(s) excluded.")
                    .arg(report.value("excluded_material_count").toInt());
            else if (report.value("result_kind").toString() == "repaired-base")
                m_status = tr("Output created from the repaired base model. No material weights were applied.");
            if (report.value("mode").toString() == "unified")
                m_status = tr("Created %1 independent refinement stages. This is a model package, not a single-network weight conversion.")
                    .arg(report.value("stages").toArray().size());
            const auto verification = report.value("output_verification").toObject();
            if (verification.value("status").toString() == "passed")
                m_status += report.value("mode").toString() == "unified"
                    ? tr(" All saved package members were re-read and verified.")
                    : tr(" Re-read and verified all %1 saved tensors against computed values.").arg(verification.value("tensor_count").toInt());
            const auto repairs = report.value("numeric_normalization").toObject();
            if (!repairs.value("repair_events").toArray().isEmpty()
                    || repairs.value("nonfinite_material_values").toDouble() > 0
                    || report.value("weight_normalization").isObject())
                m_status += tr(" Automatic corrections were applied; see the full report.");
        }
    } else if (m_cancelled && m_error.isEmpty()) {
        m_status = tr("Cancelled");
        if (QFileInfo::exists(m_requestedOutput))
            m_error = tr("The SDK stopped after an output appeared. Inspect it before using it.");
    } else {
        if (m_error.isEmpty()) m_error = QString::fromUtf8(m_stderr).trimmed();
        if (m_error.isEmpty()) m_error = tr("The SDK stopped without a valid result (exit %1). Check the Python runtime and inputs.").arg(exitCode);
        m_status = tr("Model merge failed");
    }
    if (!m_stderr.isEmpty()) m_details += "\n" + QString::fromUtf8(m_stderr);
    m_cancelled = false;
    emit elapsedChanged(); emit changed(); emit finished(success, m_validationOnly);
}

void ModelMergeController::cancel()
{
    if (!m_busy) return;
    m_cancelled = true;
    m_status = tr("Cancelling… waiting for the current operation to stop.");
    emit changed();
#ifdef SOCIETY_MODEL_MERGE_PROCESS
    if (m_process.state() != QProcess::Running) return;
#ifdef Q_OS_UNIX
    ::kill(pid_t(m_process.processId()), SIGINT);
#else
    m_process.terminate();
    const auto pid = m_process.processId();
    QTimer::singleShot(3000, this, [this, pid] {
        if (m_cancelled && m_process.processId() == pid) m_process.kill();
    });
#endif
#endif
}

void ModelMergeController::copyDetails() const
{
    if (auto *clipboard = QGuiApplication::clipboard()) clipboard->setText(m_details);
}

void ModelMergeController::openOutputFolder() const
{
    if (!m_completedOutput.isEmpty() && QFileInfo::exists(m_completedOutput))
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_completedOutput).absolutePath()));
}

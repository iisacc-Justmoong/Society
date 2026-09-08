#include "ModelImporter.h"

#include <SocietyDrive.h>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTemporaryFile>
#include <algorithm>
#ifdef Q_OS_ANDROID
#include <AndroidStorage.h>
#endif
#ifdef Q_OS_IOS
#include "IosModelDrop.h"
#include "IosFileAccess.h"
#endif

namespace {
using iiSocietyContainer::SocietyDrive;
using iiSocietyContainer::StoreSection;

bool isModelUrl(const QUrl &url)
{
#ifdef Q_OS_ANDROID
    if (url.scheme() == "content") {
        const auto suffix = QFileInfo(iiSocietyContainer::androidDocumentName(url.toString())).suffix();
        return suffix.compare("safetensor", Qt::CaseInsensitive) == 0 || suffix.compare("safetensors", Qt::CaseInsensitive) == 0;
    }
#endif
    const auto path = url.toLocalFile();
    const auto suffix = QFileInfo(path).suffix();
    return url.isLocalFile() && url.host().isEmpty() && !url.hasQuery() && !url.hasFragment()
        && QDir::isAbsolutePath(path)
        && (suffix.compare("safetensor", Qt::CaseInsensitive) == 0
            || suffix.compare("safetensors", Qt::CaseInsensitive) == 0);
}

struct ImportResult {
    QStringList paths;
    QStringList errors;
    QString lastCopiedPath;
    int copied = 0;
    int existing = 0;
};

QString publishCopy(QTemporaryFile &copy, const QString &directory, const QString &name)
{
    const QFileInfo original(name);
    for (int index = 0; index < 10000; ++index) {
        const auto candidate = QDir(directory).filePath(index == 0 ? name
            : QString("%1 (%2).%3").arg(original.completeBaseName()).arg(index).arg(original.suffix()));
        const QFileInfo destination(candidate);
        if (destination.exists() || destination.isSymLink())
            continue;
        // QTemporaryFile only renames on the same filesystem and never overwrites.
        // The final filename becomes visible only after the complete copy is flushed.
        if (copy.rename(candidate)) {
            copy.setAutoRemove(false);
            return candidate;
        }
        const QFileInfo racedDestination(candidate);
        if (!racedDestination.exists() && !racedDestination.isSymLink())
            return {};
    }
    return {};
}
}

ModelImporter::ModelImporter(QObject *parent) : QObject(parent)
{
#ifdef Q_OS_IOS
    observeIosImportLifecycle(this);
#endif
}
ModelImporter::~ModelImporter()
{
    cancel();
    if (m_worker) {
        m_worker->wait();
        delete m_worker;
    }
}
QString ModelImporter::containerPath() const { return m_containerPath; }
void ModelImporter::setContainerPath(const QString &path)
{
    if (m_containerPath == path)
        return;
    m_containerPath = path;
    if (!busy()) {
        m_status.clear();
        m_error.clear();
        emit stateChanged();
    }
    emit containerPathChanged();
}
bool ModelImporter::busy() const { return m_worker != nullptr; }
double ModelImporter::progress() const { return m_progress; }
QString ModelImporter::status() const { return m_status; }
QString ModelImporter::errorString() const { return m_error; }
bool ModelImporter::nativeDragActive() const { return m_nativeDragActive; }
void ModelImporter::setNativeDragActive(bool active)
{
    if (m_nativeDragActive == active)
        return;
    m_nativeDragActive = active;
    emit nativeDragChanged();
}
void ModelImporter::attachWindow(QWindow *window)
{
    m_window = window;
#ifdef Q_OS_IOS
    attachIosModelDrop(window, this);
#else
    Q_UNUSED(window)
#endif
}

void ModelImporter::setChoosingFiles(bool choosing)
{
    if (m_choosingFiles == choosing) return;
    m_choosingFiles = choosing;
    emit stateChanged();
}

bool ModelImporter::chooseFiles()
{
    if (busy() || choosingFiles() || m_containerPath.isEmpty()) return false;
#ifdef Q_OS_IOS
    if (presentIosModelPicker(m_window, this)) return true;
    m_error = tr("Open Society's window to choose model files.");
    emit stateChanged();
#endif
    return false;
}

bool ModelImporter::accepts(const QList<QUrl> &urls) const
{
    // Drag hover only classifies names. Filesystem validation runs off the GUI thread.
    return !busy() && !choosingFiles() && !m_containerPath.isEmpty() && !urls.isEmpty()
        && std::all_of(urls.cbegin(), urls.cend(), isModelUrl);
}

bool ModelImporter::importFiles(const QList<QUrl> &urls)
{
    if (busy())
        return false;
    if (!accepts(urls)) {
        m_error = m_containerPath.isEmpty() ? tr("Open a Society Container before importing models.")
            : tr("Drop local .safetensor or .safetensors files. Other file types are not supported yet.");
        m_status.clear();
        emit stateChanged();
        return false;
    }

    QList<ModelImportSource> sources;
    for (const auto &url : urls) {
#ifdef Q_OS_ANDROID
        if (url.scheme() == "content") {
            sources.append({iiSocietyContainer::androidDocumentName(url.toString()), [url](const auto &consume, const auto &) {
                consume(url.toString());
                return QString();
            }});
            continue;
        }
#endif
        const auto path = url.toLocalFile();
        sources.append({QFileInfo(path).fileName(), [path](const auto &consume, const auto &) {
            consume(path);
            return QString();
        }});
    }
    return importSources(sources);
}

bool ModelImporter::importSources(const QList<ModelImportSource> &sources)
{
    if (busy() || choosingFiles() || m_containerPath.isEmpty() || sources.isEmpty())
        return false;
    for (const auto &source : sources) {
        if (!source.read || QFileInfo(source.name).fileName() != source.name
            || !isModelUrl(QUrl::fromLocalFile(QDir(m_containerPath).filePath(source.name)))) {
            m_error = tr("The file provider must supply a .safetensor or .safetensors filename.");
            emit stateChanged();
            return false;
        }
    }

    const auto root = m_containerPath;
    const auto result = std::make_shared<ImportResult>();
    const auto cancelled = std::make_shared<std::atomic_bool>(false);
    m_cancelled = cancelled;
    m_progress = 0;
    m_error.clear();
    m_status = tr("Preparing models…");
    m_worker = QThread::create([this, root, sources, result, cancelled] {
        QString driveError;
        const auto drive = SocietyDrive::open(root, &driveError);
        if (!drive) {
            result->errors.append(driveError);
            return;
        }
        const auto models = drive->sectionPath(StoreSection::Models);
        QSet<QString> seen;
        qsizetype completed = 0;
        QElapsedTimer throttle;
        throttle.start();
        const auto reportProgress = [&](const QString &name, double fileProgress, bool force = false) {
            if (!force && throttle.elapsed() < 80)
                return;
            throttle.restart();
            const double fraction = std::clamp((completed + fileProgress) / sources.size(), 0.0, 1.0);
            QMetaObject::invokeMethod(this, [this, name, fraction] {
                m_progress = fraction;
                m_status = tr("Copying %1 to Models… %2%").arg(name).arg(qRound(fraction * 100));
                emit stateChanged();
            }, Qt::QueuedConnection);
        };
        QByteArray buffer(1024 * 1024, Qt::Uninitialized);
        for (const auto &entry : sources) {
            if (cancelled->load())
                break;
            // Native providers may need to download or materialize a file first.
            QMetaObject::invokeMethod(this, [this, name = entry.name] {
                m_status = tr("Preparing %1…").arg(name);
                emit stateChanged();
            }, Qt::QueuedConnection);
            const auto readError = entry.read([&](const QString &path) {
                const QFileInfo input(path);
                const QString name = entry.name;
                bool content = false;
#ifdef Q_OS_ANDROID
                content = QUrl(path).scheme() == "content";
#endif
                if (!isModelUrl(QUrl::fromLocalFile(QDir(models).filePath(name)))
                    || QFileInfo(name).fileName() != name || (!content && (!input.isFile()
                    || input.isSymLink() || !input.isReadable()))) {
                    result->errors.append(tr("%1: the source must be a readable .safetensor or .safetensors file.").arg(name));
                    return;
                }
                if (cancelled->load())
                    return;
                const auto canonical = content ? path : input.canonicalFilePath();
                if (seen.contains(canonical))
                    return;
                seen.insert(canonical);
                // Revalidate the original drive identity and section paths before each write.
                if (!drive->isValid()) {
                    result->errors.append(tr("The Society Container changed or Models is unavailable."));
                    return;
                }
                if (canonical.startsWith(models + '/')) {
                    result->paths.append(canonical);
                    ++result->existing;
                    return;
                }
                QFile source(content ? path : input.absoluteFilePath());
                QTemporaryFile copy(QDir(models).filePath(".society-import-XXXXXX"));
                if (!source.open(QIODevice::ReadOnly) || !copy.open()) {
                    result->errors.append(tr("%1: %2").arg(name, source.isOpen() ? copy.errorString() : source.errorString()));
                    return;
                }
                QString error;
                qint64 copied = 0;
                reportProgress(name, 0, true);
                while (!cancelled->load()) {
                    const auto count = source.read(buffer.data(), buffer.size());
                    if (count < 0) {
                        error = source.errorString();
                        break;
                    }
                    if (count == 0)
                        break;
                    qint64 written = 0;
                    while (written < count) {
                        const auto chunk = copy.write(buffer.constData() + written, count - written);
                        if (chunk <= 0) {
                            error = copy.errorString();
                            break;
                        }
                        written += chunk;
                    }
                    if (!error.isEmpty())
                        break;
                    copied += count;
                    reportProgress(name, input.size() > 0 ? double(copied) / input.size() : 0);
                }
                if (cancelled->load())
                    return;
                if (error.isEmpty() && (copied != input.size()
                    || QFileInfo(source).lastModified() != input.lastModified()))
                    error = tr("The source changed while it was being copied. Try importing it again.");
                if (error.isEmpty() && !copy.flush())
                    error = copy.errorString();
                if (error.isEmpty() && !drive->isValid())
                    error = tr("The Society Container changed while the model was being copied.");
                if (error.isEmpty()) {
                    const auto destination = publishCopy(copy, models, name);
                    if (destination.isEmpty())
                        error = tr("Could not save a complete model without replacing an existing item: %1").arg(copy.errorString());
                    else {
                        result->paths.append(destination);
                        result->lastCopiedPath = destination;
                        ++result->copied;
                    }
                }
                if (!error.isEmpty())
                    result->errors.append(tr("%1: %2").arg(name, error));
            }, cancelled);
            if (!readError.isEmpty() && !cancelled->load())
                result->errors.append(tr("%1: %2").arg(entry.name, readError));
            ++completed;
        }
    });
    connect(m_worker, &QThread::finished, this, [this, root, result, cancelled] {
        m_worker->deleteLater();
        m_worker = nullptr;
        m_cancelled.reset();
        m_error = result->errors.join('\n');
        if (cancelled->load())
            m_status = tr("Import cancelled. %1 model(s) copied to Models.").arg(result->copied);
        else if (result->copied == 0 && result->existing > 0)
            m_status = tr("%1 model(s) already in Models.").arg(result->existing);
        else if (result->copied == 1)
            m_status = tr("Imported %1 to Models.").arg(QFileInfo(result->lastCopiedPath).fileName());
        else
            m_status = tr("%1 model(s) imported to Models.").arg(result->copied);
        if (!cancelled->load() && m_error.isEmpty())
            m_progress = 1;
        emit stateChanged();
        emit finished(root, result->paths);
    });
    m_worker->start();
    emit stateChanged();
    return true;
}

void ModelImporter::cancel()
{
    if (m_cancelled)
        m_cancelled->store(true);
}

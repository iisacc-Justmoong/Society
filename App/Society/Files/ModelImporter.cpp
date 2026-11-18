#include "ModelImporter.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <algorithm>
#ifdef Q_OS_ANDROID
#include <iiSocietyContainer>
#endif
#ifdef Q_OS_IOS
#include "IosModelDrop.h"
#include "IosFileAccess.h"
#endif

namespace {
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

}

ModelImporter::ModelImporter(QObject *parent) : iiSocietyContainer::ModelImportService(parent)
{
    connect(&m_backend, &ModelBackendClient::connectedChanged, this, [this] {
        if (m_backendManaged && !m_backend.connected()) {
            m_backendState = {};
            if (m_backendError.isEmpty()) {
                m_backendError = tr("The Society background service disconnected. Reconnecting…");
                m_connectionError = true;
            }
            emit stateChanged(); emit jobsChanged();
        }
        emit backendConnectedChanged();
    });
    connect(&m_backend, &ModelBackendClient::pendingChanged, this, &ModelImporter::stateChanged);
    connect(&m_backend, &ModelBackendClient::requestError, this, [this](const QString &error) {
        m_connectionError = false; m_backendError = error; emit stateChanged();
    });
    connect(&m_backend, &ModelBackendClient::stateReceived, this, [this](const QJsonObject &state) {
        if (m_connectionError) { m_connectionError = false; m_backendError.clear(); }
        m_backendState = state; emit stateChanged(); emit jobsChanged();
    });
    connect(&m_backend, &ModelBackendClient::eventReceived, this, [this](const QJsonObject &event) {
        const auto type = event.value("type").toString();
        const auto root = event.value("container").toString();
        if (type == "finished" || type == "organized") {
            QStringList paths;
            for (const auto &path : event.value("paths").toArray()) paths.append(path.toString());
            if (type == "finished") emit finished(root, paths); else emit organized(root, paths);
        } else if (type == "recategorized") {
            emit recategorized(root, event.value("previousPath").toString(), event.value("path").toString());
        }
    });
#ifdef Q_OS_IOS
    observeIosImportLifecycle(this);
#endif
}

void ModelImporter::setBackendManaged(bool managed)
{
    if (m_backendManaged == managed) return;
    if (busy() || organizing()) {
        inputError(tr("Wait for the current model operation before changing its owner.")); return;
    }
    const auto path = containerPath();
    if (managed) iiSocietyContainer::ModelImportService::setContainerPath({});
    else m_backend.stop();
    m_backendManaged = managed; m_containerPath = path; m_backendState = {}; m_backendError.clear(); m_connectionError = false;
    if (managed) { m_backend.setContainerPath(path); m_backend.start(); }
    else iiSocietyContainer::ModelImportService::setContainerPath(path);
    emit backendManagedChanged(); emit backendConnectedChanged(); emit stateChanged(); emit jobsChanged();
}
QString ModelImporter::containerPath() const { return m_backendManaged ? m_containerPath : iiSocietyContainer::ModelImportService::containerPath(); }
void ModelImporter::setContainerPath(const QString &path)
{
    if (!m_backendManaged) { iiSocietyContainer::ModelImportService::setContainerPath(path); return; }
    if (m_containerPath == path) return;
    m_containerPath = path; m_backendState = {}; m_backendError.clear(); m_connectionError = false;
    m_backend.setContainerPath(path);
    emit containerPathChanged(); emit stateChanged(); emit jobsChanged();
}
bool ModelImporter::busy() const { return m_backendManaged ? m_backend.pendingMutation() || m_backendState.value("busy").toBool() : iiSocietyContainer::ModelImportService::busy(); }
bool ModelImporter::organizing() const { return m_backendManaged ? m_backendState.value("organizing").toBool() : iiSocietyContainer::ModelImportService::organizing(); }
double ModelImporter::progress() const { return m_backendManaged ? m_backendState.value("progress").toDouble() : iiSocietyContainer::ModelImportService::progress(); }
QString ModelImporter::status() const { return m_backendManaged ? m_backendState.value("status").toString() : iiSocietyContainer::ModelImportService::status(); }
QString ModelImporter::errorString() const { return m_backendManaged ? (m_backendError.isEmpty() ? m_backendState.value("errorString").toString() : m_backendError) : iiSocietyContainer::ModelImportService::errorString(); }
bool ModelImporter::choosingFiles() const { return m_backendManaged ? false : iiSocietyContainer::ModelImportService::choosingFiles(); }
QString ModelImporter::currentJobId() const { return m_backendManaged ? m_backendState.value("currentJobId").toString() : iiSocietyContainer::ModelImportService::currentJobId(); }
QString ModelImporter::selectedJobId() const { return m_backendManaged ? m_backendState.value("selectedJobId").toString() : iiSocietyContainer::ModelImportService::selectedJobId(); }
QVariantList ModelImporter::jobs() const { return m_backendManaged ? m_backendState.value("jobs").toArray().toVariantList() : iiSocietyContainer::ModelImportService::jobs(); }
QVariantList ModelImporter::jobItems() const { return m_backendManaged ? m_backendState.value("jobItems").toArray().toVariantList() : iiSocietyContainer::ModelImportService::jobItems(); }
bool ModelImporter::hasMoreJobItems() const { return m_backendManaged ? m_backendState.value("hasMoreJobItems").toBool() : iiSocietyContainer::ModelImportService::hasMoreJobItems(); }
QString ModelImporter::jobsError() const { return m_backendManaged ? m_backendState.value("jobsError").toString() : iiSocietyContainer::ModelImportService::jobsError(); }
void ModelImporter::inputError(const QString &error)
{
    if (m_backendManaged) { m_connectionError = false; m_backendError = error; emit stateChanged(); }
    else reportInputError(error);
}
bool ModelImporter::backendRequest(const QString &operation, QJsonObject arguments, bool mutation)
{
    if (mutation) { m_connectionError = false; m_backendError.clear(); }
    return m_backend.request(operation, std::move(arguments), mutation);
}
void ModelImporter::cancel() { if (m_backendManaged) backendRequest("cancel", {}, true); else iiSocietyContainer::ModelImportService::cancel(); }
bool ModelImporter::organizeModels() { return m_backendManaged ? backendRequest("organize", {}, true) : iiSocietyContainer::ModelImportService::organizeModels(); }
bool ModelImporter::recategorize(const QString &path, const QString &category) { return m_backendManaged ? backendRequest("recategorize", {{"path", path}, {"category", category}}, true) : iiSocietyContainer::ModelImportService::recategorize(path, category); }
void ModelImporter::refreshJobs() { if (m_backendManaged) backendRequest("refreshJobs"); else iiSocietyContainer::ModelImportService::refreshJobs(); }
void ModelImporter::selectJob(const QString &jobId) { if (m_backendManaged) backendRequest("selectJob", {{"jobId", jobId}}); else iiSocietyContainer::ModelImportService::selectJob(jobId); }
void ModelImporter::loadMoreJobItems() { if (m_backendManaged) backendRequest("loadMoreJobItems"); else iiSocietyContainer::ModelImportService::loadMoreJobItems(); }
bool ModelImporter::retryFailed(const QString &jobId) { return m_backendManaged ? backendRequest("retryFailed", {{"jobId", jobId}}, true) : iiSocietyContainer::ModelImportService::retryFailed(jobId); }

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

bool ModelImporter::chooseFiles()
{
    if (busy() || choosingFiles() || containerPath().isEmpty()) return false;
#ifdef Q_OS_IOS
    if (presentIosModelPicker(m_window, this)) return true;
    inputError(tr("Open Society's window to choose model files."));
#endif
    return false;
}

bool ModelImporter::accepts(const QList<QUrl> &urls) const
{
    // Drag hover only classifies names. Filesystem validation runs off the GUI thread.
    return !busy() && !choosingFiles() && !containerPath().isEmpty() && !urls.isEmpty()
        && std::all_of(urls.cbegin(), urls.cend(), isModelUrl);
}

bool ModelImporter::importFiles(const QList<QUrl> &urls)
{
    if (busy()) {
        inputError(tr("Another model operation is running. Try again when it finishes."));
        return false;
    }
    if (!accepts(urls)) {
        inputError(containerPath().isEmpty() ? tr("Open a Society drive before importing models.")
            : tr("Drop local .safetensor or .safetensors files. Other file types are not supported yet."));
        return false;
    }

    if (m_backendManaged) {
        QJsonArray paths;
        for (const auto &url : urls) paths.append(url.toLocalFile());
        return backendRequest("import", {{"sources", paths}}, true);
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
        }, path});
    }
    return importSources(sources);
}

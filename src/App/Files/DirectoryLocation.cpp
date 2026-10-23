#include "DirectoryLocation.h"

#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QImageReader>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <iiFileProviderPresentation.h>
#include <FileCatalog.h>
#include <SharedStorage.h>
#include <FileOperations.h>
#include <QJsonDocument>
#include <QJsonArray>

DirectoryLocation::DirectoryLocation(QObject *parent) : QObject(parent) {}
DirectoryLocation::~DirectoryLocation() { if (m_inspectionWatcher) m_inspectionWatcher->waitForFinished(); }

QString DirectoryLocation::path() const { return m_path; }
QUrl DirectoryLocation::folderUrl() const { return m_folderUrl; }
QString DirectoryLocation::errorString() const { return m_errorString; }

namespace {
QVariantMap describe(const QString &folder, const QString &path, bool prepare)
{
    const QFileInfo info(path);
    // Only describe a selected child of the validated visible folder. Remote
    // originals need not exist yet; no metadata lookup should download them.
    if (folder.isEmpty() || !info.exists() || info.isSymLink()
        || info.absolutePath() != folder || info.canonicalFilePath() != QDir::cleanPath(path))
        return {};
    QVariantMap result{{"created", info.birthTime()}};
    if (info.isFile()) {
        using iiFileProvider::native::Presentation;
        const auto native = std::filesystem::path(path.toStdU16String());
        QJsonObject document;
        const auto drive = iiSocietyContainer::FileOperations::containingDrive(path);
        if (prepare && drive) {
            QString error;
            const auto storage = iiSocietyContainer::SharedStorage::open(drive->rootPath(), &error);
            const auto section = drive->sectionForPath(path);
            if (storage && section) document = storage->fileInformation(*section, QDir(drive->sectionPath(*section)).relativeFilePath(path), &error);
            if (document.isEmpty()) result.insert("previewError", error.isEmpty() ? DirectoryLocation::tr("This file is unavailable.") : error);
        } else {
            auto presentation = Presentation::load(native);
            if (prepare) {
                try { presentation = Presentation::prepare(native); }
                catch (const std::exception &e) { result.insert("previewError", QString::fromUtf8(e.what())); }
            }
            if (presentation) {
                document = QJsonDocument::fromJson(QByteArray::fromStdString(presentation->document)).object();
                document.insert("embedded", presentation->embedded);
                QJsonObject classification{{"status", "absent"}};
                try {
                    const auto stored = iiFileProvider::FileCatalog::classificationDocument(native);
                    if (!stored.empty()) { classification = QJsonDocument::fromJson(QByteArray::fromStdString(stored)).object(); classification.insert("status", "ready"); }
                } catch (...) { classification = {{"status", "invalid"}}; }
                document.insert("classification", classification);
            }
        }
        if (!document.isEmpty()) {
            const auto details = document.value("details").toObject(), preview = document.value("preview").toObject();
            result.insert("name", details.value("name").toString());
            result.insert("mimeType", details.value("mimeType").toString());
            result.insert("bytes", QVariant::fromValue<qulonglong>(details.value("size").toInteger()));
            result.insert("previewKind", preview.value("kind").toString());
            result.insert("embedded", document.value("embedded").toBool());
            result.insert("sourceVersion", document.value("sourceVersion").toString());
            const auto managed = document.value("managed").toObject();
            result.insert("managedStatus", managed.value("status").toString());
            for (const auto *key : {"objectKey", "rawKey", "version", "rawVersion", "sessionKey", "sessionDescription", "sessionStartedAtNs", "application", "originalName", "originalSha256", "ontologyRevision"})
                result.insert(QString::fromLatin1(key), managed.value(QString::fromLatin1(key)).toVariant());
            result.insert("previewSource", QUrl("data:" + preview.value("mimeType").toString() + ";base64," + preview.value("data").toString()));
            const auto properties = details.value("properties").toObject().toVariantMap();
            result.insert("properties", properties);
            const auto width = properties.value("width").toString(), height = properties.value("height").toString();
            if (!width.isEmpty() && !height.isEmpty()) result.insert("contents", DirectoryLocation::tr("%1 × %2 pixels").arg(width, height));
            else result.insert("contents", details.value("mimeType").toString());
            const auto classification = document.value("classification").toObject();
            result.insert("classificationStatus", classification.value("status").toString());
            const auto metadata = classification.value("metadata").toObject();
            result.insert("title", metadata.value("title").toString()); result.insert("description", metadata.value("description").toString());
            result.insert("keywords", metadata.value("keywords").toArray().toVariantList());
            result.insert("rating", metadata.value("rating").toInt()); result.insert("colorLabel", metadata.value("color").toString());
            result.insert("fields", metadata.value("fields").toObject().toVariantMap());
            QStringList collections;
            for (const auto &value : classification.value("collections").toArray()) collections.append(value.toObject().value("name").toString());
            result.insert("collections", collections);
            return result;
        }
        QImageReader reader(path);
        const auto size = reader.size();
        if (size.isValid())
            result.insert("contents", DirectoryLocation::tr("%1 × %2 pixels").arg(size.width()).arg(size.height()));
    }
    return result;
}
}
QVariantMap DirectoryLocation::fileDetails(const QString &path) const { return describe(m_folderUrl.toLocalFile(), path, false); }
void DirectoryLocation::inspectFile(const QString &path, const QString &revision)
{
    if (!revision.isEmpty() && path == m_requestedPath && revision == m_requestedRevision)
        return;
    ++m_inspection;
    m_details.clear(); m_inspecting = false;
    m_requestedPath.clear();
    m_requestedRevision.clear();
    const auto folder = m_folderUrl.toLocalFile();
    if (folder.isEmpty() || path.isEmpty() || QFileInfo(path).absolutePath() != folder) { emit detailsChanged(); return; }
    m_requestedPath = path; m_requestedRevision = revision; m_inspecting = true; emit detailsChanged();
    if (!m_inspectionWatcher) startInspection();
}
void DirectoryLocation::startInspection()
{
    if (m_requestedPath.isEmpty()) return;
    const auto revision = m_inspection;
    const auto folder = m_folderUrl.toLocalFile(), path = m_requestedPath;
    auto *watcher = new QFutureWatcher<QVariantMap>(this);
    m_inspectionWatcher = watcher;
    connect(watcher, &QFutureWatcher<QVariantMap>::finished, this, [this, watcher, revision] {
        const auto result = watcher->result(); m_inspectionWatcher = nullptr; watcher->deleteLater();
        if (revision != m_inspection) { startInspection(); return; }
        m_details = result; m_inspecting = false; emit detailsChanged();
    });
    watcher->setFuture(QtConcurrent::run([folder, path] { return describe(folder, path, true); }));
}

void DirectoryLocation::setPath(const QString &path)
{
    if (m_path == path)
        return;

    m_path = path;
    m_folderUrl.clear();
    m_errorString.clear();
    ++m_inspection; m_requestedPath.clear(); m_requestedRevision.clear(); m_details.clear(); m_inspecting = false; emit detailsChanged();

    if (!path.isEmpty()) {
        const QFileInfo info(path);
        if (!QDir::isAbsolutePath(path))
            m_errorString = tr("Use an absolute folder path.");
        else if (!info.exists())
            m_errorString = tr("This folder could not be found.");
        else if (!info.isDir())
            m_errorString = tr("This path is not a folder.");
        else if (!info.isReadable())
            m_errorString = tr("This folder cannot be read.");
        else
            m_folderUrl = QUrl::fromLocalFile(QDir::cleanPath(path));
    }

    emit pathChanged();
}

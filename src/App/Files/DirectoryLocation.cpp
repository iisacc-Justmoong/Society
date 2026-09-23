#include "DirectoryLocation.h"

#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QImageReader>

DirectoryLocation::DirectoryLocation(QObject *parent) : QObject(parent) {}

QString DirectoryLocation::path() const { return m_path; }
QUrl DirectoryLocation::folderUrl() const { return m_folderUrl; }
QString DirectoryLocation::errorString() const { return m_errorString; }

QVariantMap DirectoryLocation::fileDetails(const QString &path) const
{
    const QFileInfo info(path);
    // Only describe a selected child of the validated visible folder. Remote
    // originals need not exist yet; no metadata lookup should download them.
    if (m_folderUrl.isEmpty() || !info.exists()
        || info.absolutePath() != m_folderUrl.toLocalFile())
        return {};
    QVariantMap result{{"created", info.birthTime()}};
    if (info.isFile()) {
        QImageReader reader(path);
        const auto size = reader.size();
        if (size.isValid())
            result.insert("contents", tr("%1 × %2 pixels").arg(size.width()).arg(size.height()));
    }
    return result;
}

void DirectoryLocation::setPath(const QString &path)
{
    if (m_path == path)
        return;

    m_path = path;
    m_folderUrl.clear();
    m_errorString.clear();

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

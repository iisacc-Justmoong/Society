#include "DashboardFiles.h"
#include <SocietyDrive.h>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QLocale>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>

namespace {
struct Snapshot { QVariantList files; QString error; };

bool isImage(const QFileInfo &file)
{
    static const QStringList extensions{"png", "jpg", "jpeg", "webp", "bmp", "gif", "tif", "tiff", "avif", "heic", "heif"};
    return extensions.contains(file.suffix().toLower());
}

Snapshot scan(const QString &path, const std::shared_ptr<std::atomic_bool> &cancel)
{
    Snapshot result;
    const auto drive = iiSocietyContainer::SocietyDrive::open(path, &result.error);
    if (!drive) return result;
    for (const auto section : drive->sections()) {
        const bool history = section == iiSocietyContainer::StoreSection::GenerationHistory;
        QDirIterator iterator(drive->sectionPath(section), QDir::Files | QDir::NoSymLinks | QDir::Readable,
                              history ? QDirIterator::NoIteratorFlags : QDirIterator::Subdirectories);
        while (!cancel->load() && iterator.hasNext()) {
            iterator.next();
            const auto file = iterator.fileInfo();
            if (file.isSymLink() || (history && !isImage(file))) continue;
            // Recheck SDK boundaries in case an entry changed during the walk.
            const auto actualSection = drive->sectionForPath(file.canonicalFilePath());
            if (!actualSection || *actualSection != section) continue;
            const auto suffix = file.suffix().toLower();
            const auto kind = isImage(file) ? QStringLiteral("Image")
                : suffix == "iisc" ? QStringLiteral("Canvas") : QStringLiteral("Document");
            const auto icon = isImage(file) ? QStringLiteral("fileTypesimage")
                : suffix == "iisc" ? QStringLiteral("application") : QStringLiteral("fileTypestext");
            const auto folder = QDir(path).relativeFilePath(file.absolutePath());
            result.files.append(QVariantMap{
                {"path", file.canonicalFilePath()}, {"folderPath", file.absolutePath()},
                {"name", file.fileName()}, {"description", iiSocietyContainer::storeSectionName(section) + " · " + kind},
                {"modified", file.lastModified().toUTC()}, {"history", history}, {"iconName", icon},
                {"metadata1", (suffix.isEmpty() ? kind : suffix.toUpper()) + " · " + QLocale().formattedDataSize(file.size())},
                {"metadata2", QStringLiteral("Society / ") + folder}
            });
        }
        if (cancel->load()) return {};
    }
    if (!drive->isValid()) {
        result.files.clear();
        result.error = DashboardFiles::tr("The Society drive changed while its files were being read.");
        return result;
    }
    std::sort(result.files.begin(), result.files.end(), [](const QVariant &left, const QVariant &right) {
        const auto a = left.toMap(), b = right.toMap();
        const auto at = a.value("modified").toDateTime(), bt = b.value("modified").toDateTime();
        return at == bt ? a.value("path").toString() < b.value("path").toString() : at > bt;
    });
    return result;
}

QString relativeTime(const QDateTime &modified)
{
    const auto seconds = std::max<qint64>(0, modified.secsTo(QDateTime::currentDateTimeUtc()));
    if (seconds < 60) return DashboardFiles::tr("Just now");
    if (seconds < 3600) return DashboardFiles::tr("%1 min ago").arg(seconds / 60);
    if (seconds < 86400) return DashboardFiles::tr("%1 h ago").arg(seconds / 3600);
    return QLocale().toString(modified.toLocalTime().date(), QLocale::ShortFormat);
}
}

DashboardFiles::DashboardFiles(QObject *parent) : QObject(parent) {}
DashboardFiles::~DashboardFiles() { if (m_cancel) m_cancel->store(true); }

void DashboardFiles::setContainerPath(const QString &path)
{
    if (m_path == path) return;
    m_path = path;
    m_files.clear(); m_error.clear();
    emit containerPathChanged(); emit filesChanged();
    refresh();
}

void DashboardFiles::setQuery(const QString &query)
{
    if (m_query == query) return;
    m_query = query;
    emit queryChanged(); emit filesChanged();
}

QVariantList DashboardFiles::filtered(bool historyOnly) const
{
    QVariantList result;
    for (const auto &entry : m_files) {
        auto file = entry.toMap();
        if (historyOnly && !file.value("history").toBool()) continue;
        if (!m_query.trimmed().isEmpty() && !file.value("name").toString().contains(m_query.trimmed(), Qt::CaseInsensitive)
            && !file.value("metadata2").toString().contains(m_query.trimmed(), Qt::CaseInsensitive)) continue;
        file.insert("dateText", relativeTime(file.value("modified").toDateTime()));
        result.append(file);
        if (result.size() == 3) break;
    }
    return result;
}

QVariantList DashboardFiles::recentFiles() const { return filtered(false); }
QVariantList DashboardFiles::generationHistory() const { return filtered(true); }

void DashboardFiles::refresh()
{
    if (m_cancel) m_cancel->store(true);
    const auto revision = ++m_revision;
    m_cancel = std::make_shared<std::atomic_bool>(false);
    m_error.clear();
    // Empty or relative paths must never enumerate the process working directory.
    if (m_path.isEmpty() || !QFileInfo(m_path).isAbsolute()) {
        m_files.clear();
        if (!m_path.isEmpty()) m_error = tr("Choose an absolute Society drive path.");
        m_loading = false; emit loadingChanged(); emit filesChanged();
        return;
    }
    m_loading = true; emit loadingChanged();
    auto *watcher = new QFutureWatcher<Snapshot>(this);
    connect(watcher, &QFutureWatcher<Snapshot>::finished, this, [this, watcher, revision] {
        const auto result = watcher->result();
        watcher->deleteLater();
        if (revision != m_revision) return;
        m_files = result.files; m_error = result.error; m_loading = false;
        emit filesChanged(); emit loadingChanged();
    });
    watcher->setFuture(QtConcurrent::run(scan, m_path, m_cancel));
}

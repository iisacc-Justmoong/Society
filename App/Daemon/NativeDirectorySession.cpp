#include "NativeDirectorySession.h"
#include <QDateTime>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>
#include <QUuid>
#include <cmath>

namespace {
QString token() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
void fail(QString *output, const QString &value) { if (output) *output = value; }
bool integer(const QJsonValue &value, int minimum, int maximum, int *output)
{
    if (!value.isDouble()) return false;
    const auto n = value.toDouble();
    if (!std::isfinite(n) || std::floor(n) != n || n < minimum || n > maximum) return false;
    *output = static_cast<int>(n); return true;
}
}

NativeDirectorySession::NativeDirectorySession(QObject *parent) : QObject(parent)
{
    connect(&m_model, &iiSocietyContainer::StorageDirectoryModel::contentsChanged, this, [this] {
        if (!valid()) return;
        m_revision = token(); scheduleChanged();
    });
    connect(&m_model, &iiSocietyContainer::StorageDirectoryModel::statusChanged,
            this, [this] { if (valid()) scheduleChanged(); });
    connect(&m_model,&iiSocietyContainer::StorageDirectoryModel::activated,this,[this](const QString &path,bool directory) {
        if (!m_activation) return;
        const auto pending=*m_activation; m_activation.reset();
        if (!valid() || pending.generation!=m_generation) return;
        const auto error=directory || path!=pending.absolute || pending.revision!=m_revision
            ? tr("The selected file changed before it could be opened.") : QString();
        emit openResult({{"id",pending.id},{"generation",pending.generation},{"relative",pending.relative},
                         {"path",error.isEmpty()?path:QString()},{"error",error}});
    });
    connect(&m_model,&iiSocietyContainer::StorageDirectoryModel::downloadFailed,this,[this](const QString &error) {
        if (!m_activation) return;
        const auto pending=*m_activation; m_activation.reset();
        if (valid() && pending.generation==m_generation)
            emit openResult({{"id",pending.id},{"generation",pending.generation},{"relative",pending.relative},
                             {"path",QString()},{"error",error}});
    });
}

bool NativeDirectorySession::open(const QString &container, const QJsonObject &arguments, QString *error)
{
    const auto value = arguments.value("path");
    if (!value.isString()) { fail(error, tr("A directory path is required.")); return false; }
    const auto relative = value.toString();
    if (relative.isEmpty() || relative.size() > 4096) { fail(error, tr("Invalid directory path.")); return false; }
    for (const auto &part : relative.split('/')) {
        if (part.startsWith('.')) { fail(error, tr("This directory is not available.")); return false; }
    }
    const auto queryValue=arguments.value("query"), sortValue=arguments.value("sort"), descendingValue=arguments.value("descending");
    if ((!queryValue.isUndefined() && !queryValue.isString()) || (!sortValue.isUndefined() && !sortValue.isString())
        || (!descendingValue.isUndefined() && !descendingValue.isBool())) {
        fail(error,tr("Invalid directory query.")); return false;
    }
    const auto query=queryValue.toString().trimmed();
    const auto sort=sortValue.isUndefined()?QStringLiteral("name"):sortValue.toString();
    const auto descending=descendingValue.toBool(false);
    if (query.size()>256 || query.contains(QChar::Null) || (sort!="name" && sort!="modified" && sort!="kind" && sort!="size")
        || m_model.metaObject()->indexOfProperty("searchText")<0) {
        fail(error,tr("Invalid or unsupported directory query.")); return false;
    }
    const auto drive = iiSocietyContainer::SocietyDrive::open(container);
    if (!drive || !drive->isReady()) { fail(error, tr("The selected Society drive is unavailable.")); return false; }
    const auto absolute = drive->resolvePath(relative);
    const QFileInfo info(absolute);
    if (absolute.isEmpty() || (info.exists() && !info.isDir())) {
        fail(error, tr("This directory is not available.")); return false;
    }
    m_activation.reset();
    m_drive = drive; m_container = container; m_path = relative; m_query=query; m_sort=sort; m_descending=descending;
    m_generation = token(); m_revision = token();
    // Clearing even the same folder ensures an old filter is never published
    // as a ready result for this new generation while the worker is running.
    m_model.setFolder({});
    const auto field=sort=="modified"?iiSocietyContainer::StorageDirectoryModel::Time:
        sort=="kind"?iiSocietyContainer::StorageDirectoryModel::Type:
        sort=="size"?iiSocietyContainer::StorageDirectoryModel::Size:iiSocietyContainer::StorageDirectoryModel::Name;
    const bool naturalDescending=field==iiSocietyContainer::StorageDirectoryModel::Time || field==iiSocietyContainer::StorageDirectoryModel::Size;
    m_model.setProperty("sortField",int(field));
    m_model.setProperty("sortReversed",naturalDescending?!descending:descending);
    m_model.setProperty("searchText",query);
    m_model.setFolder(QUrl::fromLocalFile(absolute));
    scheduleChanged();
    return true;
}

void NativeDirectorySession::invalidate()
{
    m_replies.clear(); m_replySizes.clear(); m_replyOrder.clear(); m_replyBytes = 0;
    if (!valid()) return;
    m_activation.reset(); m_drive.reset(); m_container.clear(); m_path.clear(); m_generation.clear(); m_revision.clear(); m_query.clear(); m_sort="name"; m_descending=false;
    m_model.setFolder({});
    scheduleChanged();
}

QJsonObject NativeDirectorySession::state() const
{
    const auto status = !valid() ? QStringLiteral("closed")
        : m_model.status() == iiSocietyContainer::StorageDirectoryModel::Ready ? QStringLiteral("ready")
        : m_model.status() == iiSocietyContainer::StorageDirectoryModel::Loading ? QStringLiteral("loading")
        : QStringLiteral("closed");
    return {{"path", m_path}, {"generation", m_generation}, {"revision", m_revision},
            {"query",m_query},{"sort",m_sort},{"descending",m_descending},
            {"status", status}, {"count", valid() ? m_model.rowCount() : 0}};
}

QJsonObject NativeDirectorySession::page(const QJsonObject &arguments, QString *error) const
{
    if (!valid() || arguments.value("generation").toString() != m_generation
        || arguments.value("revision").toString() != m_revision) {
        fail(error, tr("The directory changed; request its current state.")); return {};
    }
    const auto absolute = m_drive->resolvePath(m_path);
    if (absolute.isEmpty() || m_model.folder().toLocalFile() != absolute) {
        fail(error, tr("The directory is no longer available.")); return {};
    }
    int offset = 0, limit = 0;
    if (!integer(arguments.value("offset"), 0, m_model.rowCount(), &offset)
        || !integer(arguments.value("limit"), 1, 64, &limit)) {
        fail(error, tr("Invalid directory page.")); return {};
    }
    QJsonArray entries;
    const int end = offset + qMin(limit, m_model.rowCount() - offset);
    for (int i = offset; i < end; ++i) {
        const auto row = m_model.entry(i);
        const auto path = m_drive->relativePath(row.value("filePath").toString());
        if (path.isEmpty() || path.size() > 4096 || m_drive->resolvePath(path).isEmpty()) {
            fail(error, tr("A directory entry is no longer available.")); return {};
        }
        const auto date = row.value("fileModified").toDateTime();
        entries.append(QJsonObject{{"name", row.value("fileName").toString()}, {"path", path},
            {"directory", row.value("fileIsDir").toBool()}, {"suffix", row.value("fileSuffix").toString()},
            {"size", QString::number(row.value("fileSize").toLongLong())},
            {"modified", date.isValid() ? date.toUTC().toString(Qt::ISODateWithMs) : QString()},
            {"resident", row.value("fileResident").toBool()},
            {"informationKey", row.value("fileInformationKey").toString()},
            {"informationProvider", row.value("fileInformationProvider").toString()}});
    }
    return {{"generation", m_generation}, {"revision", m_revision}, {"offset", offset},
            {"count", m_model.rowCount()}, {"entries", entries}};
}

bool NativeDirectorySession::activate(const QJsonObject &arguments, QString *error)
{
    if (!valid() || m_model.status()!=iiSocietyContainer::StorageDirectoryModel::Ready
        || arguments.value("generation").toString()!=m_generation || arguments.value("revision").toString()!=m_revision
        || !arguments.value("path").isString() || !arguments.value("id").isString() || m_activation) {
        fail(error,tr("The selected directory changed or another file is being prepared.")); return false;
    }
    const auto relative=arguments.value("path").toString();
    const auto absolute=m_drive->resolvePath(relative);
    if (absolute.isEmpty()) { fail(error,tr("The selected file is unavailable.")); return false; }
    for (int index=0;index<m_model.rowCount();++index) {
        const auto row=m_model.entry(index);
        if (row.value("filePath").toString()!=absolute || row.value("fileIsDir").toBool()) continue;
        m_activation=Activation{arguments.value("id").toString(),m_generation,m_revision,relative,absolute};
        m_model.activate(index); return true;
    }
    fail(error,tr("The selected file is no longer in this directory.")); return false;
}

std::optional<QPair<QByteArray, QJsonObject>> NativeDirectorySession::cachedReply(const QString &id) const
{
    const auto found = m_replies.constFind(id);
    return found == m_replies.cend() ? std::nullopt : std::optional(*found);
}

void NativeDirectorySession::rememberReply(const QString &id, const QByteArray &request, const QJsonObject &reply)
{
    constexpr qsizetype maximumBytes = 4 * 1024 * 1024;
    const auto size = request.size() + QJsonDocument(reply).toJson(QJsonDocument::Compact).size();
    if (m_replies.contains(id)) {
        m_replyBytes -= m_replySizes.take(id);
        m_replies.remove(id); m_replyOrder.removeAll(id);
    }
    if (size > maximumBytes) return;
    while (!m_replyOrder.isEmpty() && (m_replyOrder.size() >= 256 || m_replyBytes > maximumBytes - size)) {
        const auto oldest = m_replyOrder.dequeue();
        m_replyBytes -= m_replySizes.take(oldest); m_replies.remove(oldest);
    }
    m_replies.insert(id, {request, reply}); m_replySizes.insert(id, size);
    m_replyOrder.enqueue(id); m_replyBytes += size;
}

void NativeDirectorySession::scheduleChanged()
{
    if (m_notificationPending) return;
    m_notificationPending = true;
    QTimer::singleShot(0, this, [this] { m_notificationPending = false; emit changed(state()); });
}

#include "RecentCatalog.h"
#include "Society/Semantics/PhysicalIdentity.h"
#include <SharedStorage.h>
#include <iiFileProviderPresentation.h>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <algorithm>
#include <filesystem>
#include <memory>
#include <queue>

namespace society::recent {
namespace {
using Storage=iiSocietyContainer::SharedStorage;
using Section=iiSocietyContainer::StoreSection;
namespace fs=std::filesystem;
constexpr int maxHistory=4096;
constexpr qint64 maxHistoryBytes=1024*1024;
bool cancelled(const std::atomic_bool *cancel) { return cancel && cancel->load(); }
bool privatePath(const QString &relative) {
    const auto parts=relative.split('/');
    for (qsizetype i=0;i<parts.size();++i) {
        const auto &part=parts[i];
        if (part.isEmpty() || part=="." || part==".." || part.startsWith('.') ||
            (i+1<parts.size() && part.endsWith(".iiproject",Qt::CaseInsensitive)) ||
            iiFileProvider::native::Presentation::isControlFile(fs::path(part.toStdU16String()))) return true;
    }
    return false;
}
bool invalidHint(const QString &relative) {
    if (relative.size()>4096 || QDir::isAbsolutePath(relative) || QDir::cleanPath(relative)!=relative ||
        relative.contains(QChar::Null) || privatePath(relative)) return true;
#ifdef Q_OS_WIN
    if (relative.contains('\\')) return true;
#endif
    return false;
}
bool within(const QString &root,const QString &path) {
    return QDir::isAbsolutePath(path) && QDir::cleanPath(path)==path && path.startsWith(root+'/') &&
           !privatePath(QDir(root).relativeFilePath(path));
}
bool package(const QFileInfo &info) {
    if (!info.isDir() || info.isSymLink() || info.isJunction() || info.suffix().compare("iiproject",Qt::CaseInsensitive)!=0) return false;
    const QFileInfo manifest(QDir(info.filePath()).filePath("manifest.json"));
    return manifest.isFile() && !manifest.isSymLink() && !manifest.isJunction();
}
std::optional<QString> uniqueIdentity(const QFileInfo &file) {
    if (file.isFile()) {
        std::error_code error;
        const auto links=fs::hard_link_count(fs::path(file.absoluteFilePath().toStdU16String()),error);
        if (error || links!=1) return {};
    }
    return semantics::physicalIdentity(file.absoluteFilePath());
}
bool canonicalMatch(const QFileInfo &file) {
#ifdef Q_OS_DARWIN
    return file.canonicalFilePath().normalized(QString::NormalizationForm_C)==file.absoluteFilePath().normalized(QString::NormalizationForm_C);
#else
    return file.canonicalFilePath()==file.absoluteFilePath();
#endif
}
bool newer(const Item &a,const Item &b) {
    return a.activityMs==b.activityMs ? a.path<b.path : a.activityMs>b.activityMs;
}
struct Better { bool operator()(const Item &a,const Item &b) const { return newer(a,b); } };
struct HistoryEntry { qint64 atMs=0; QString relative; };
using History=QHash<QString,HistoryEntry>;

History readHistory(const Storage &storage,QString &error) {
    History values;
    const auto path=storage.runtimePath("recent/activity-v1.json",&error);
    if (path.isEmpty() || !QFileInfo::exists(path)) return values;
    QFile file(path);
    if (QFileInfo(path).isSymLink() || !file.open(QIODevice::ReadOnly) || file.size()>maxHistoryBytes) {
        error="Recent activity history cannot be read safely."; return {};
    }
    QJsonParseError parse;
    const auto doc=QJsonDocument::fromJson(file.readAll(),&parse);
    if (parse.error!=QJsonParseError::NoError || !doc.isObject() || doc.object().value("version").toInt()!=1 ||
        !doc.object().value("items").isArray() || doc.object().value("items").toArray().size()>maxHistory) {
        error="Recent activity history is invalid."; return {};
    }
    for (const auto &value:doc.object().value("items").toArray()) {
        const auto row=value.toObject(); const auto id=row.value("identity").toString();
        bool ok=false; const auto at=row.value("atMs").toString().toLongLong(&ok);
        const auto hint=row.value("path"); const auto relative=hint.toString();
        if (id.isEmpty() || id.size()>512 || !ok || at<=0 || (!hint.isUndefined() && (!hint.isString() || invalidHint(relative)))) {
            error="Recent activity history is invalid."; return {};
        }
        if (at>=values.value(id).atMs) values[id]={at,relative};
    }
    return values;
}
bool writeHistory(const Storage &storage,const History &values,QString &error) {
    const auto directory=storage.ensureRuntimeDirectory("recent",&error); if (directory.isEmpty()) return false;
    const auto path=storage.runtimePath("recent/activity-v1.json",&error); if (path.isEmpty()) return false;
    QVector<QPair<QString,HistoryEntry>> entries; entries.reserve(values.size());
    for (auto it=values.cbegin();it!=values.cend();++it) entries.append({it.key(),it.value()});
    std::sort(entries.begin(),entries.end(),[](const auto &a,const auto &b) {
        return a.second.atMs==b.second.atMs ? a.first<b.first : a.second.atMs>b.second.atMs;
    });
    QJsonArray rows;
    qint64 used=32;
    for (int i=0;i<qMin(maxHistory,int(entries.size()));++i) {
        QJsonObject row{{"identity",entries[i].first},{"atMs",QString::number(entries[i].second.atMs)}};
        if (!entries[i].second.relative.isEmpty()) row.insert("path",entries[i].second.relative);
        const auto bytes=QJsonDocument(row).toJson(QJsonDocument::Compact).size()+1;
        if (used+bytes>maxHistoryBytes) break;
        rows.append(row); used+=bytes;
    }
    QSaveFile file(path); file.setDirectWriteFallback(false);
    const auto bytes=QJsonDocument(QJsonObject{{"version",1},{"items",rows}}).toJson(QJsonDocument::Compact);
    if (bytes.size()>maxHistoryBytes || !file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size() || !file.commit()) {
        error="Recent activity history could not be saved."; return false;
    }
    return true;
}
}

Snapshot RecentCatalog::update(const QString &container,const QVector<Activity> &activities,int limit,const std::atomic_bool *cancel) {
    Snapshot result;
    if (limit<1 || limit>500) { result.error="Invalid Recent result limit."; return result; }
    try {
        auto storage=Storage::open(container,&result.error); if (!storage) return result;
        if (!storage->drive().isReady()) { result.error="The Society drive is not ready."; return result; }
        const auto root=storage->drive().sectionPath(Section::Files);
        if (root.isEmpty() || QFileInfo(root).canonicalFilePath()!=root) { result.error="The Files directory is unavailable."; return result; }
        auto history=readHistory(*storage,result.error); if (!result.error.isEmpty()) return result;
        bool changed=false;
        for (const auto &event:activities) {
            if (cancelled(cancel)) return {};
            const QFileInfo file(event.path);
            if (event.atMs<=0 || !within(root,event.path) || (!file.isFile() && !package(file))) continue;
            if (file.birthTime().isValid() && file.birthTime().toMSecsSinceEpoch()>event.atMs+1000) { result.partial=true; continue; }
            const auto id=uniqueIdentity(file);
            if (!id) { result.partial=true; continue; }
            const auto previous=history.value(*id); const auto relative=QDir(root).relativeFilePath(event.path);
            if (invalidHint(relative)) { result.partial=true; continue; }
            if (event.atMs>=previous.atMs && (event.atMs!=previous.atMs || relative!=previous.relative)) {
                history[*id]={event.atMs,relative}; changed=true;
            }
        }
        if (changed && !writeHistory(*storage,history,result.error)) return result;
        std::priority_queue<Item,std::vector<Item>,Better> newest;
        QSet<QString> activityPaths,seenActivities;
        for (const auto &event:activities) activityPaths.insert(event.path);
        for (auto it=history.cbegin();it!=history.cend();++it)
            if (!it->relative.isEmpty()) activityPaths.insert(QDir(root).filePath(it->relative));
        const auto add=[&](const QString &path) {
            if (!within(root,path) || seenActivities.contains(path)) return;
            const QFileInfo file(path); const bool isPackage=package(file);
            if ((!file.isFile() && !isPackage) || file.isSymLink() || file.isJunction()) return;
            if (!canonicalMatch(file)) { result.partial=true; return; }
            const auto id=uniqueIdentity(file); if (!id) result.partial=true;
            auto modified=file.lastModified().toMSecsSinceEpoch();
            if (isPackage) modified=qMax(modified,QFileInfo(QDir(path).filePath("manifest.json")).lastModified().toMSecsSinceEpoch());
            const auto at=qMax(modified,id?history.value(*id).atMs:qint64(0));
            Item item{path,file.fileName(),at,isPackage?-1:file.size()}; if (activityPaths.contains(path)) seenActivities.insert(path);
            if (int(newest.size())<limit) newest.push(std::move(item));
            else if (newer(item,newest.top())) { newest.pop(); newest.push(std::move(item)); }
        };
        // A provider index may contain only part of the Files tree. Enumerate
        // metadata once in the worker so a partial index cannot hide files.
        // Keep only the best 500 rows, do not follow links or read payloads.
        std::vector<std::unique_ptr<QDirIterator>> stack;
        const auto descend=[&](const QString &directory) {
            stack.push_back(std::make_unique<QDirIterator>(directory,QDir::AllEntries|QDir::NoDotAndDotDot|QDir::NoSymLinks));
        };
        descend(root);
        while (!cancelled(cancel) && !stack.empty()) {
            auto &it=*stack.back();
            if (!it.hasNext()) { stack.pop_back(); continue; }
            it.next(); const auto info=it.fileInfo(); const auto path=info.absoluteFilePath();
            if (!within(root,path)) continue;
            if (info.isDir() && !package(info)) { if (info.isReadable()) descend(path); else result.partial=true; }
            else add(path);
        }
        if (cancelled(cancel)) return {};
        // A hint is only a locator: recheck the native identity before using it.
        // This keeps a newly moved file visible while the index catches up.
        for (auto it=history.cbegin();it!=history.cend();++it) {
            if (it->relative.isEmpty()) continue;
            const auto path=QDir(root).filePath(it->relative); if (!within(root,path)) continue;
            const auto identity=uniqueIdentity(QFileInfo(path));
            if (identity && *identity==it.key()) add(path);
        }
        for (const auto &event:activities) add(event.path);
        result.items.reserve(int(newest.size()));
        while (!newest.empty()) { result.items.append(newest.top()); newest.pop(); }
        std::sort(result.items.begin(),result.items.end(),newer);
        if (!storage->drive().isValid()) { result.items.clear(); result.error="The Society drive changed during the Recent scan."; }
    } catch (const std::exception &e) { result.error=QString::fromUtf8(e.what()); }
    return result;
}
}

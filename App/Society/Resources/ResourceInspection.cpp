#include "ResourceInspection.h"
#include "ContainerResourceAdapter.h"
#include "Shared/NativeObservedPath.h"
#include <iiSocietyContainer>
#include <iiFileProvider>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>

namespace society::resources {
namespace {
QString text(const std::string &value) { return QString::fromUtf8(value.data(),qsizetype(value.size())); }
QJsonValue id(const std::optional<ii::resource::LocationId> &value) { return value ? QJsonValue(text(value->value)) : QJsonValue(QJsonValue::Null); }
// The identity owning the managed index may differ from the visible drive ID.
QString storageId(const iiSocietyContainer::SharedStorage &storage) {
    const auto &drive=storage.drive();
    return drive.storageKind()==iiSocietyContainer::SocietyDrive::StorageKind::LogicalDisk
        ? drive.storageIdentifier() : drive.identifier();
}
struct Selection { iiSocietyContainer::StoreSection section; QString relative; };

// Validate a visible container-relative path before opening the underlying file.
std::optional<Selection> selection(const QString &logicalPath,QString &error) {
    const bool files=logicalPath.startsWith("Files/"),models=logicalPath.startsWith("Models/");
    if ((!files && !models) || logicalPath.size()>4096 || QDir::cleanPath(logicalPath)!=logicalPath ||
        logicalPath.contains('\\') || logicalPath.contains(QChar::Null)) {
        error="Use a file path within the selected Society Files or Models section."; return {};
    }
    const auto section=files ? iiSocietyContainer::StoreSection::Files : iiSocietyContainer::StoreSection::Models;
    const auto relative=logicalPath.mid(files ? 6 : 7);
    for (const auto &part:relative.split('/')) {
        if (part.isEmpty() || part=="." || part==".." || part.startsWith('.') ||
            iiFileProvider::native::Presentation::isControlFile(std::filesystem::path(part.toStdU16String()))) {
            error="The requested path is not a visible Society file."; return {};
        }
    }
    return Selection{section,relative};
}

// Reuse a read-only repository during paged searches while retaining the same
// path, native identity and record checks used for a standalone inspection.
Inspection inspectSelection(const iiSocietyContainer::SharedStorage &storage,
                            iiSocietyContainer::FileRepository &repository,const Selection &chosen) {
    Inspection out;
    try {
        QString error; const auto path=storage.filePath(chosen.section,chosen.relative,&error);
        if (path.isEmpty() || !QFileInfo::exists(path)) { out.error=error.isEmpty()?QStringLiteral("The file is unavailable."):error; return out; }
        const auto managedPath=storage.managedFilePath(chosen.section,chosen.relative);
        const auto nativePath=societyNativeObservedPath(path);
        const auto snapshot=projectCurrent(repository,managedPath,nativePath);
        if (!snapshot) { out.error="The file has no current resource observation; reindex it before inspecting."; return out; }
        QJsonObject resource{{"id",text(snapshot->resource.id.value)},{"displayName",text(snapshot->resource.displayName)},
                             {"displayPath",text(snapshot->resource.displayPath)}};
        resource.insert("observedRevisionId",snapshot->resource.observedRevision ? QJsonValue(text(snapshot->resource.observedRevision->value)) : QJsonValue(QJsonValue::Null));
        QJsonValue revision(QJsonValue::Null);
        if (snapshot->revision) {
            QJsonObject row{{"id",text(snapshot->revision->id.value)},{"resourceId",text(snapshot->revision->resourceId.value)},
                            {"baseRevisionId",QJsonValue(QJsonValue::Null)}};
            if (snapshot->revision->baseRevision) row.insert("baseRevisionId",text(snapshot->revision->baseRevision->value));
            if (snapshot->revision->digest) row.insert("digest",QJsonObject{{"algorithm",text(snapshot->revision->digest->algorithm)},
                {"value",text(snapshot->revision->digest->value)},{"evidence","observed"}});
            else row.insert("digest",QJsonValue(QJsonValue::Null));
            revision=row;
        }
        QJsonArray locations,replicas;
        for (const auto &location:snapshot->locations)
            locations.append(QJsonObject{{"id",id(location.id)},{"storageId",text(location.storageId)},
                {"nodeId",location.nodeId ? QJsonValue(text(*location.nodeId)) : QJsonValue(QJsonValue::Null)},
                {"locator",text(location.locator)},{"availability","unknown"}});
        for (const auto &replica:snapshot->replicas)
            replicas.append(QJsonObject{{"id",text(replica.id.value)},{"resourceId",text(replica.resourceId.value)},
                {"revisionId",text(replica.revisionId.value)},{"locationId",id(replica.locationId)},
                {"locatorHint",text(replica.locatorHint)},{"state","unknown"}});
        out.snapshot={{"resource",resource},{"contentRevision",revision},{"locations",locations},{"replicas",replicas},
                      {"provenance","last_observed"}};
    } catch (const std::exception &error) { out.error=QString::fromUtf8(error.what()); }
    return out;
}
}

Inspection inspect(const QString &container,const QString &logicalPath) {
    Inspection out; const auto chosen=selection(logicalPath,out.error);
    if (!chosen) return out;
    try {
        QString error; const auto storage=iiSocietyContainer::SharedStorage::inspect(container,&error);
        if (!storage || !storage->drive().isReady()) { out.error=error.isEmpty()?QStringLiteral("The Society drive is unavailable."):error; return out; }
        // New directory-owned objects carry their hash and metadata beside the
        // file. They do not have an ObjectStore revision or RAW copy, and
        // inspection must not create or require the legacy repository.
        const auto root=societyNativeObservedPath(storage->drive().rootPath());
        const auto indexPath=root/".iisacc-file-provider"/"metadata-paths.sqlite3";
        std::error_code statusError;
        const auto status=std::filesystem::symlink_status(indexPath,statusError);
        if (statusError && statusError!=std::errc::no_such_file_or_directory)
            throw std::runtime_error(statusError.message());
        if (std::filesystem::exists(status) || std::filesystem::is_symlink(status)) {
            const auto tracked=storage->fileTracking(chosen->section,chosen->relative,&error);
            if (!error.isEmpty()) throw std::runtime_error(error.toStdString());
            if (tracked.value("resourceStatus")=="directory-metadata-only") {
                const auto observed=tracked.value("resource").toObject();
                const auto key=observed.value("id").toString();
                const auto path=storage->filePath(chosen->section,chosen->relative,&error);
                if (key.isEmpty() || path.isEmpty() || !storage->drive().isReady())
                    throw std::runtime_error("The directory-owned file is unavailable.");
                const auto hash=observed.value("hashes").toObject().value("sha256").toString();
                const auto hashStatus=tracked.value("hashStatus").toString();
                QJsonValue digest(QJsonValue::Null);
                if (hashStatus=="current" && !hash.isEmpty())
                    digest=QJsonObject{{"algorithm","sha256"},{"value",hash},{"evidence","current_local_observation"}};
                out.snapshot={{"resource",QJsonObject{{"id",key},{"displayName",QFileInfo(path).fileName()},
                    {"displayPath",tracked.value("path")},{"observedRevisionId",QJsonValue(QJsonValue::Null)}}},
                    {"contentRevision",QJsonValue(QJsonValue::Null)},{"contentDigest",digest},{"hashStatus",hashStatus},
                    {"metadata",tracked.value("metadata").toObject()},{"collections",tracked.value("collections").toArray()},
                    {"locations",QJsonArray{QJsonObject{{"id",QJsonValue(QJsonValue::Null)},{"storageId",storageId(*storage)},
                        {"nodeId",QJsonValue(QJsonValue::Null)},{"locator",path},{"availability","unknown"}}}},
                    {"replicas",QJsonArray{}},{"provenance","directory_metadata"}};
                return out;
            }
        }
        auto repository=storage->fileRepository({},iiSocietyContainer::FileRepository::Access::ReadOnly,
                                                 iiSocietyContainer::FileRepository::Intent::Background);
        return inspectSelection(*storage,*repository,*chosen);
    } catch (const std::exception &error) { out.error=QString::fromUtf8(error.what()); }
    return out;
}

Inspection compareBytes(const QString &container,const QString &firstPath,const QString &secondPath,
                        const QString &temporaryRoot) {
    Inspection out;
    const auto first=selection(firstPath,out.error);
    if (!first) return out;
    const auto second=selection(secondPath,out.error);
    if (!second) return out;
    if (firstPath==secondPath) { out.error="Choose two distinct resources to compare."; return out; }
    try {
        constexpr std::uint64_t maxBytes=64ULL*1024*1024;
        QString error;
        const auto storage=iiSocietyContainer::SharedStorage::inspect(container,&error);
        if (!storage || !storage->drive().isReady()) {
            out.error=error.isEmpty()?QStringLiteral("The Society drive is unavailable."):error; return out;
        }
        const auto containerId=storageId(*storage);
        if (containerId.isEmpty()) { out.error="The Society storage identity is unavailable."; return out; }
        const auto firstName=storage->filePath(first->section,first->relative,&error);
        if (firstName.isEmpty()) { out.error=error.isEmpty()?QStringLiteral("The first file is unavailable."):error; return out; }
        const auto secondName=storage->filePath(second->section,second->relative,&error);
        if (secondName.isEmpty()) { out.error=error.isEmpty()?QStringLiteral("The second file is unavailable."):error; return out; }
        const auto firstNative=societyNativeObservedPath(firstName);
        const auto secondNative=societyNativeObservedPath(secondName);
        // This SDK inspection rejects redirected paths and binds subsequent
        // captures to the same file identity, size and timestamps.
        const auto firstIdentity=iiFileProvider::ObjectSource::inspect(firstNative);
        const auto secondIdentity=iiFileProvider::ObjectSource::inspect(secondNative);
        if (firstIdentity.size>maxBytes || secondIdentity.size>maxBytes) {
            out.error="Byte comparison is limited to 64 MiB per file."; return out;
        }
        // Match the managed record to the actual visible spelling returned by
        // the filesystem. On macOS this may be a decomposed Unicode name even
        // when the caller supplied its canonically equivalent composed form.
        const auto recordedSelection=[&](const Selection &chosen,const QString &absolute) {
            const auto root=QFileInfo(storage->drive().sectionPath(chosen.section)).canonicalFilePath();
            if (root.isEmpty()) throw std::runtime_error("The Society section is unavailable.");
            auto relative=QDir(root).relativeFilePath(absolute);
#ifdef Q_OS_MACOS
            relative=relative.normalized(QString::NormalizationForm_D);
#endif
            const auto prefix=chosen.section==iiSocietyContainer::StoreSection::Files?QStringLiteral("Files/"):QStringLiteral("Models/");
            QString selectionError;
            const auto selected=selection(prefix+relative,selectionError);
            if (!selected) throw std::runtime_error(selectionError.toStdString());
            return *selected;
        };
        const auto recordedFirst=recordedSelection(*first,firstName);
        const auto recordedSecond=recordedSelection(*second,secondName);
        auto repository=storage->fileRepository({},iiSocietyContainer::FileRepository::Access::ReadOnly,
                                                 iiSocietyContainer::FileRepository::Intent::Background);
        const auto beforeFirst=inspectSelection(*storage,*repository,recordedFirst);
        const auto beforeSecond=inspectSelection(*storage,*repository,recordedSecond);
        if (!beforeFirst.error.isEmpty() || !beforeSecond.error.isEmpty()) {
            out.error=!beforeFirst.error.isEmpty()?beforeFirst.error:beforeSecond.error; return out;
        }
        const auto firstResource=beforeFirst.snapshot.value("resource").toObject();
        const auto secondResource=beforeSecond.snapshot.value("resource").toObject();
        const auto firstRevision=beforeFirst.snapshot.value("contentRevision").toObject();
        const auto secondRevision=beforeSecond.snapshot.value("contentRevision").toObject();
        if (firstResource.value("id").toString().isEmpty() || secondResource.value("id").toString().isEmpty() ||
            firstResource.value("id")==secondResource.value("id") || firstRevision.value("id").toString().isEmpty() ||
            secondRevision.value("id").toString().isEmpty()) {
            out.error="Two distinct observed resource revisions are required."; return out;
        }
        const auto base=temporaryRoot.isEmpty()?QDir::tempPath():temporaryRoot;
        const auto canonicalBase=QFileInfo(base).canonicalFilePath();
        if (canonicalBase.isEmpty()) { out.error="The temporary comparison directory is unavailable."; return out; }
        QTemporaryDir stage(QDir(canonicalBase).filePath("society-byte-compare-XXXXXX"));
        if (!stage.isValid()) { out.error="Could not create a private temporary comparison directory."; return out; }
        const auto stagePath=societyNativeObservedPath(stage.path());
        iiFileProvider::ObjectSource firstCopy(firstNative,stagePath,{},firstIdentity);
        iiFileProvider::ObjectSource secondCopy(secondNative,stagePath,{},secondIdentity);
        bool equal=firstIdentity.size==secondIdentity.size;
        if (equal) {
            std::ifstream a(firstCopy.path(),std::ios::binary), b(secondCopy.path(),std::ios::binary);
            if (!a || !b) throw std::runtime_error("Could not read temporary file snapshots.");
            std::array<char,64*1024> left{},right{};
            auto remaining=firstIdentity.size;
            while (remaining>0) {
                const auto count=std::streamsize(std::min<std::uint64_t>(remaining,left.size()));
                a.read(left.data(),count); b.read(right.data(),count);
                if (a.gcount()!=count || b.gcount()!=count) throw std::runtime_error("Could not read complete file snapshots.");
                if (std::memcmp(left.data(),right.data(),std::size_t(count))!=0) { equal=false; break; }
                remaining-=std::uint64_t(count);
            }
        }
        const auto afterFirst=inspectSelection(*storage,*repository,recordedFirst);
        const auto afterSecond=inspectSelection(*storage,*repository,recordedSecond);
        const auto same=[](const Inspection &before,const Inspection &after) {
            return after.error.isEmpty() && before.snapshot.value("resource").toObject().value("id")
                ==after.snapshot.value("resource").toObject().value("id") &&
                before.snapshot.value("contentRevision")==after.snapshot.value("contentRevision");
        };
        const auto checked=iiSocietyContainer::SharedStorage::inspect(container,&error);
        if (!same(beforeFirst,afterFirst) || !same(beforeSecond,afterSecond) || !checked ||
            !checked->drive().isReady() || storageId(*checked)!=containerId ||
            iiFileProvider::ObjectSource::inspect(firstNative)!=firstIdentity ||
            iiFileProvider::ObjectSource::inspect(secondNative)!=secondIdentity) {
            out.error="A file or the selected Society storage changed during comparison."; return out;
        }
        const auto row=[](const QString &path,const QJsonObject &resource,const QJsonObject &revision,std::uint64_t size) {
            return QJsonObject{{"path",path},{"resourceId",resource.value("id")},{"revisionId",revision.value("id")},
                               {"bytes",int(size)}};
        };
        out.snapshot={{"containerId",containerId},{"first",row(firstPath,firstResource,firstRevision,firstIdentity.size)},
            {"second",row(secondPath,secondResource,secondRevision,secondIdentity.size)},
            {"comparison",equal?QStringLiteral("equal"):QStringLiteral("different")},
            {"evidence","separate_temporary_snapshots"},{"snapshot","non_atomic"},
            {"deduplicationAuthorized",false}};
    } catch (const std::exception &error) {
        out.error=QString::fromUtf8(error.what());
        if (out.error.isEmpty()) out.error=QStringLiteral("Could not compare the file snapshots.");
    }
      catch (...) { out.error=QStringLiteral("Could not compare the file snapshots."); }
    return out;
}

Inspection duplicateCandidates(const QString &container,const QString &logicalPath,int limit,const QJsonObject &cursor) {
    Inspection out;
    if (limit<1 || limit>20 || QJsonDocument(cursor).toJson(QJsonDocument::Compact).size()>4096) {
        out.error="Invalid duplicate candidate page or cursor."; return out;
    }
    // Inspect supplies the path, source-stamp, current repository record and
    // section checks shared with ordinary resource inspection.
    const auto reference=inspect(container,logicalPath);
    if (!reference.error.isEmpty()) return reference;
    const auto referenceResource=reference.snapshot.value("resource").toObject();
    const auto referenceRevision=reference.snapshot.value("contentRevision").toObject();
    const auto digest=referenceRevision.value("digest").toObject();
    const auto resourceId=referenceResource.value("id").toString();
    const auto revisionId=referenceRevision.value("id").toString();
    const auto sha=digest.value("value").toString();
    static const QRegularExpression sha256(QStringLiteral("^[0-9a-f]{64}$"));
    if (resourceId.isEmpty() || revisionId.isEmpty() || digest.value("algorithm")!="sha256" ||
        !sha256.match(sha).hasMatch()) {
        out.error="The file has no usable observed SHA-256; reindex it before comparing."; return out;
    }
    if (!cursor.isEmpty() && (cursor.size()!=4 || cursor.value("resourceId").toString()!=resourceId ||
        cursor.value("revisionId").toString()!=revisionId || !cursor.value("index").isObject() ||
        cursor.value("index").toObject().isEmpty())) {
        out.error="The duplicate candidate cursor is invalid or its reference changed."; return out;
    }
    try {
        QString error;
        const auto storage=iiSocietyContainer::SharedStorage::inspect(container,&error);
        if (!storage || !storage->drive().isReady()) {
            out.error=error.isEmpty()?QStringLiteral("The Society drive is unavailable."):error; return out;
        }
        const auto containerId=storageId(*storage);
        if (containerId.isEmpty() || (!cursor.isEmpty() && cursor.value("containerId").toString()!=containerId)) {
            out.error="The duplicate candidate cursor belongs to another container."; return out;
        }
        QJsonObject query{{"classKey","society:File"},{"limit",limit},
            {"conditions",QJsonArray{QJsonObject{{"property","society:contentSha256"},
                {"equals",QJsonObject{{"kind","text"},{"value",sha}}}}}}};
        if (!cursor.isEmpty()) query.insert("cursor",cursor.value("index"));
        const auto page=storage->searchFiles(query,&error);
        if (!error.isEmpty()) { out.error=error; return out; }
        if (page.value("pending").toBool()) { out.error="The file index is not available."; return out; }
        QJsonArray candidates; int skipped=0;
        for (const auto &value:page.value("files").toArray()) {
            const auto row=value.toObject(); const auto path=row.value("path").toString();
            QString visible;
            if (path.startsWith("files/")) visible="Files/"+path.mid(6);
            else if (path.startsWith("models/")) visible="Models/"+path.mid(7);
            else { ++skipped; continue; }
            const auto candidate=inspect(container,visible);
            if (!candidate.error.isEmpty()) { ++skipped; continue; }
            const auto resource=candidate.snapshot.value("resource").toObject();
            const auto revision=candidate.snapshot.value("contentRevision").toObject();
            const auto candidateId=resource.value("id").toString();
            if (candidateId.isEmpty() || candidateId!=row.value("objectKey").toString() || row.value("sha256")!=sha ||
                revision.value("id").toString().isEmpty() || revision.value("digest").toObject().value("value")!=sha) {
                ++skipped; continue;
            }
            if (candidateId==resourceId) continue;
            candidates.append(QJsonObject{{"path",visible},{"resourceId",candidateId},
                {"revisionId",revision.value("id")}});
        }
        // Do not publish a page for a reference that changed while the index
        // and candidate observations were being read.
        const auto checked=inspect(container,logicalPath);
        QString identityError;
        const auto checkedStorage=iiSocietyContainer::SharedStorage::inspect(container,&identityError);
        const auto checkedId=checkedStorage && checkedStorage->drive().isReady() ? storageId(*checkedStorage) : QString();
        if (!checked.error.isEmpty() || checked.snapshot.value("resource").toObject().value("id")!=resourceId ||
            checked.snapshot.value("contentRevision").toObject().value("id")!=revisionId ||
            checked.snapshot.value("contentRevision").toObject().value("digest").toObject().value("value")!=sha ||
            checkedId!=containerId) {
            out.error="The reference changed during duplicate candidate discovery."; return out;
        }
        QJsonObject next;
        const auto indexNext=page.value("next").toObject();
        if (!indexNext.isEmpty()) next={{"containerId",containerId},{"resourceId",resourceId},
                                        {"revisionId",revisionId},{"index",indexNext}};
        out.snapshot={{"containerId",containerId},{"reference",QJsonObject{{"path",logicalPath},{"resourceId",resourceId},
                          {"revisionId",revisionId},{"sha256",sha}}},
                      {"candidates",candidates},{"next",next},{"skipped",skipped},
                      {"evidence","matching_last_observed_sha256"},{"coverage","indexed_files_and_models"},
                      {"completeness","unknown"}};
    } catch (const std::exception &error) { out.error=QString::fromUtf8(error.what()); }
    return out;
}

Inspection searchIndexed(const QString &container,const QJsonObject &query,int limit,const QJsonObject &cursor) {
    Inspection out;
    if (limit<1 || limit>20 || QJsonDocument(query).toJson(QJsonDocument::Compact).size()>8192 ||
        QJsonDocument(cursor).toJson(QJsonDocument::Compact).size()>4096) {
        out.error="Invalid indexed search page, query or cursor."; return out;
    }
    QJsonObject normalized=query;
    const bool directory=query.contains("directory");
    if (directory) {
        const auto value=query.value("directory"); const auto path=value.toString();
        if (query.size()!=1 || !value.isString() || path.isEmpty() || path.size()>1024 ||
            QDir::isAbsolutePath(path) || QDir::cleanPath(path)!=path || path.contains('\\') || path.contains(QChar::Null)) {
            out.error="A saved virtual directory must be used by itself with a valid relative path."; return out;
        }
    } else {
        static const QSet<QString> allowed{"section","text","keywords","collections","minimumRating"};
        for (auto it=query.constBegin();it!=query.constEnd();++it) if (!allowed.contains(it.key())) {
            out.error="Unsupported indexed search condition."; return out;
        }
        if (!normalized.contains("section")) normalized.insert("section","files");
        const auto section=normalized.value("section");
        if (!section.isString() || (section!="files" && section!="models")) {
            out.error="Choose the files or models search section."; return out;
        }
        if (query.contains("text") && (!query.value("text").isString() || query.value("text").toString().size()>512 ||
                                       query.value("text").toString().contains(QChar::Null))) {
            out.error="Invalid indexed search text."; return out;
        }
        for (const auto *key:{"keywords","collections"}) if (query.contains(key)) {
            const auto value=query.value(key);
            if (!value.isArray() || value.toArray().size()>16) { out.error="Invalid indexed search terms."; return out; }
            for (const auto &term:value.toArray()) if (!term.isString() || term.toString().isEmpty() ||
                term.toString().size()>128 || term.toString().contains(QChar::Null)) {
                out.error="Invalid indexed search term."; return out;
            }
        }
        if (query.contains("minimumRating")) {
            const auto value=query.value("minimumRating"); const auto rating=value.toInt(-1);
            if (!value.isDouble() || rating<0 || rating>5 || value.toDouble()!=rating) {
                out.error="The minimum rating must be an integer from 0 to 5."; return out;
            }
        }
    }
    const auto filter=QString::fromLatin1(QCryptographicHash::hash(
        QJsonDocument(normalized).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex());
    if (!cursor.isEmpty() && (cursor.size()!=5 || cursor.value("filter").toString()!=filter ||
        !cursor.value("index").isObject() || cursor.value("index").toObject().isEmpty())) {
        out.error="The indexed search cursor is invalid or the query changed."; return out;
    }
    try {
        QString error; const auto storage=iiSocietyContainer::SharedStorage::inspect(container,&error);
        if (!storage || !storage->drive().isReady()) {
            out.error=error.isEmpty()?QStringLiteral("The Society drive is unavailable."):error; return out;
        }
        const auto containerId=storageId(*storage);
        if (containerId.isEmpty() || (!cursor.isEmpty() && cursor.value("containerId").toString()!=containerId)) {
            out.error="The indexed search cursor belongs to another container."; return out;
        }
        auto repository=storage->fileRepository({},iiSocietyContainer::FileRepository::Access::ReadOnly,
                                                  iiSocietyContainer::FileRepository::Intent::Background);
        const auto generation=repository->ontology().queryFiles({}, {}, 1);
        const auto ontology=QString::number(qulonglong(generation.revision));
        const auto sequence=QString::number(qlonglong(generation.fileSequence));
        if (!cursor.isEmpty() && (cursor.value("ontologyRevision").toString()!=ontology ||
                                  cursor.value("fileSequence").toString()!=sequence)) {
            out.error="The indexed search changed; start again from the first page."; return out;
        }
        auto request=normalized; request.insert("limit",limit);
        if (!cursor.isEmpty()) request.insert("cursor",cursor.value("index"));
        const auto page=storage->searchFiles(request,&error);
        if (!error.isEmpty()) { out.error=error; return out; }
        if (page.value("pending").toBool()) { out.error="The file index is not available."; return out; }
        QJsonArray items; int skipped=0;
        for (const auto &value:page.value("files").toArray()) {
            const auto row=value.toObject(); const auto path=row.value("path").toString(); QString visible;
            if (path.startsWith("files/")) visible="Files/"+path.mid(6);
            else if (path.startsWith("models/")) visible="Models/"+path.mid(7);
            else { ++skipped; continue; }
            QString selectionError; const auto chosen=selection(visible,selectionError);
            if (!chosen) { ++skipped; continue; }
            const auto observation=inspectSelection(*storage,*repository,*chosen);
            const auto resource=observation.snapshot.value("resource").toObject();
            const auto revision=observation.snapshot.value("contentRevision").toObject();
            const auto resourceId=resource.value("id").toString();
            if (!observation.error.isEmpty() || resourceId.isEmpty() || resourceId!=row.value("objectKey").toString() ||
                revision.value("id").toString().isEmpty()) { ++skipped; continue; }
            QJsonObject item{{"path",visible},{"resourceId",resourceId},{"revisionId",revision.value("id")}};
            if (row.value("title").isString()) item.insert("title",row.value("title"));
            if (row.value("rating").isDouble()) item.insert("rating",row.value("rating"));
            items.append(item);
        }
        const auto checked=repository->ontology().queryFiles({}, {}, 1);
        QString identityError; const auto checkedStorage=iiSocietyContainer::SharedStorage::inspect(container,&identityError);
        if (checked.revision!=generation.revision || checked.fileSequence!=generation.fileSequence ||
            !checkedStorage || !checkedStorage->drive().isReady() || storageId(*checkedStorage)!=containerId) {
            out.error="The indexed search changed while reading this page."; return out;
        }
        QJsonObject next; const auto indexNext=page.value("next").toObject();
        if (!indexNext.isEmpty()) next={{"containerId",containerId},{"filter",filter},
            {"ontologyRevision",ontology},{"fileSequence",sequence},{"index",indexNext}};
        out.snapshot={{"containerId",containerId},{"items",items},{"next",next},{"skipped",skipped},
                      {"evidence","indexed_metadata"},{"coverage",directory?QStringLiteral("indexed_files_and_models")
                          :QStringLiteral("indexed_")+normalized.value("section").toString()},
                      {"completeness","unknown"}};
    } catch (const std::exception &error) { out.error=QString::fromUtf8(error.what()); }
    return out;
}
}

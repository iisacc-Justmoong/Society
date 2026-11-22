#include "ResourceInspection.h"
#include "ContainerResourceAdapter.h"
#include <iiSocietyContainer>
#include <iiFileProvider>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <filesystem>

namespace society::resources {
namespace {
QString text(const std::string &value) { return QString::fromUtf8(value.data(),qsizetype(value.size())); }
QJsonValue id(const std::optional<ii::resource::LocationId> &value) { return value ? QJsonValue(text(value->value)) : QJsonValue(QJsonValue::Null); }
}
Inspection inspect(const QString &container,const QString &logicalPath) {
    Inspection out;
    const bool files=logicalPath.startsWith("Files/"),models=logicalPath.startsWith("Models/");
    if ((!files && !models) || logicalPath.size()>4096 || QDir::cleanPath(logicalPath)!=logicalPath ||
        logicalPath.contains('\\') || logicalPath.contains(QChar::Null)) {
        out.error="Use a file path within the selected Society Files or Models section."; return out;
    }
    const auto section=files ? iiSocietyContainer::StoreSection::Files : iiSocietyContainer::StoreSection::Models;
    const auto relative=logicalPath.mid(files ? 6 : 7);
    for (const auto &part:relative.split('/')) {
        if (part.isEmpty() || part=="." || part==".." || part.startsWith('.') ||
            iiFileProvider::native::Presentation::isControlFile(std::filesystem::path(part.toStdU16String()))) {
            out.error="The requested path is not a visible Society file."; return out;
        }
    }
    try {
        QString error; auto storage=iiSocietyContainer::SharedStorage::open(container,&error);
        if (!storage || !storage->drive().isReady()) { out.error=error.isEmpty()?QStringLiteral("The Society drive is unavailable."):error; return out; }
        const auto path=storage->filePath(section,relative,&error);
        if (path.isEmpty() || !QFileInfo::exists(path)) { out.error=error.isEmpty()?QStringLiteral("The file is unavailable."):error; return out; }
        auto repository=storage->fileRepository({},iiSocietyContainer::FileRepository::Access::ReadOnly,
                                                 iiSocietyContainer::FileRepository::Intent::Background);
        const auto managedPath=storage->managedFilePath(section,relative);
#ifdef _WIN32
        const auto nativePath=std::filesystem::path(path.toStdWString());
#else
        const auto nativePath=std::filesystem::path(path.toStdString());
#endif
        const auto snapshot=projectCurrent(*repository,managedPath,nativePath);
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
        const auto storage=iiSocietyContainer::SharedStorage::open(container,&error);
        if (!storage || !storage->drive().isReady()) {
            out.error=error.isEmpty()?QStringLiteral("The Society drive is unavailable."):error; return out;
        }
        const auto containerId=storage->drive().storageKind()==iiSocietyContainer::SocietyDrive::StorageKind::LogicalDisk
            ? storage->drive().storageIdentifier() : storage->drive().identifier();
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
        const auto checkedStorage=iiSocietyContainer::SharedStorage::open(container,&identityError);
        const auto checkedId=checkedStorage && checkedStorage->drive().isReady()
            ? (checkedStorage->drive().storageKind()==iiSocietyContainer::SocietyDrive::StorageKind::LogicalDisk
                ? checkedStorage->drive().storageIdentifier() : checkedStorage->drive().identifier()) : QString();
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
}

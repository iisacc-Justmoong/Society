#include "ResourceInspection.h"
#include "ContainerResourceAdapter.h"
#include <SharedStorage.h>
#include <iiFileProviderPresentation.h>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
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
}

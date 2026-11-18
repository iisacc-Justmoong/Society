#include "WorkspaceAddService.h"
#include "Society/Dashboard/ProjectRepository.h"
#include "Society/Semantics/SemanticRepository.h"
#include <iiSocietyContainer>
#include <iiSharedCanvas>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QSysInfo>
#include <QThread>
#include <QUrl>
#include <algorithm>
#include <stdexcept>

namespace society::workspace {
namespace {
[[noreturn]] void fail(const QString &value) { throw std::runtime_error(value.toUtf8().toStdString()); }
QString localActor(const QString &actor) {
    if (!actor.trimmed().isEmpty()) return actor.trimmed();
    auto id=QSysInfo::machineUniqueId();
    if (id.isEmpty()) id=QByteArrayLiteral("anonymous-local-device");
    return QStringLiteral("urn:iisacc:local-actor:")+QString::fromLatin1(QCryptographicHash::hash(id,QCryptographicHash::Sha256).toHex().left(24));
}
QString exactField(const QJsonArray &fields, int index) {
    if (index>=fields.size() || !fields[index].isString()) fail(QStringLiteral("A required field is missing."));
    const auto value=fields[index].toString();
    if (value.isEmpty() || value.size()>4096 || value.contains(QChar::Null)) fail(QStringLiteral("A required field is invalid."));
    return value;
}
QString field(const QJsonArray &fields, int index) {
    const auto value=exactField(fields,index).trimmed();
    if (value.isEmpty()) fail(QStringLiteral("A required field is invalid."));
    return value;
}
QString projectKey(ProjectRepository &repo, const QString &value) {
    const auto all=repo.load().projects;
    QStringList matches;
    for (const auto &item:all) {
        const auto project=item.toMap();
        if (project.value("key").toString()==value || project.value("projectId").toString()==value
            || project.value("name").toString().compare(value,Qt::CaseInsensitive)==0)
            matches.append(project.value("key").toString());
    }
    matches.removeDuplicates();
    if (matches.isEmpty()) fail(QStringLiteral("The project was not found."));
    if (matches.size()!=1) fail(QStringLiteral("More than one project has that name. Use its project ID."));
    return matches.first();
}
QString graphId(const ProjectRepository::Snapshot &state, const QString &value) {
    QStringList matches;
    for (const auto &items:{state.resources,state.entities}) for (const auto &item:items) {
        const auto map=item.toMap();
        if (map.value("id").toString()==value || map.value("name").toString().compare(value,Qt::CaseInsensitive)==0)
            matches.append(map.value("id").toString());
    }
    if (state.project.value("goalId").toString()==value || state.project.value("goal").toString()==value)
        matches.append(state.project.value("goalId").toString());
    matches.removeDuplicates();
    if (matches.size()!=1) fail(matches.isEmpty()?QStringLiteral("The project subject or object was not found.")
                                               :QStringLiteral("The project name is ambiguous. Use its ID."));
    return matches.first();
}
QString relationType(const ProjectRepository::Snapshot &state, const QString &value) {
    QStringList matches;
    for (const auto &item:state.schema.value("relations").toList()) {
        const auto map=item.toMap();
        if (map.value("id").toString()==value || map.value("label").toString().compare(value,Qt::CaseInsensitive)==0)
            matches.append(map.value("id").toString());
    }
    matches.removeDuplicates();
    if (matches.size()!=1) fail(matches.isEmpty()?QStringLiteral("The project relationship type was not found.")
                                               :QStringLiteral("The relationship type is ambiguous. Use its ID."));
    return matches.first();
}
QString managedPath(const iiSocietyContainer::SocietyDrive &drive, const QString &path) {
    if (!QDir::isAbsolutePath(path) || QDir::cleanPath(path)!=path || path.contains(QChar::Null)) fail(QStringLiteral("Choose an absolute managed file path."));
    const auto relative=drive.relativePath(path);
    if (!(relative.startsWith("Files/") || relative.startsWith("Models/")) || drive.resolvePath(relative)!=path
        || !QFileInfo(path).exists() || !iiSocietyContainer::FileOperations(drive).editable(path))
        fail(QStringLiteral("The selected resource is no longer available in Society Files or Models."));
    return path;
}
QString fileName(QString name) {
    if (name.isEmpty() || name.startsWith('.') || name.contains('/') || name.contains('\\') || name.contains(':') || name.contains(QChar::Null))
        fail(QStringLiteral("Enter a valid canvas name."));
    if (!name.endsWith(".iisc",Qt::CaseInsensitive)) name += ".iisc";
    return name;
}
}

QJsonObject executeAdd(const QString &container, const QString &actor, const QJsonObject &request) {
    int completed=0;
    try {
        QString error;
        auto drive=iiSocietyContainer::SocietyDrive::open(container,&error);
        if (!drive || !drive->isReady()) fail(error.isEmpty()?QStringLiteral("The Society drive is unavailable."):error);
        const auto action=request.value("action").toString();
        if (!request.value("fields").isArray() || !request.value("selection").isArray()) fail(QStringLiteral("Invalid Add request."));
        const auto values=request.value("fields").toArray(), selection=request.value("selection").toArray();
        if (values.size()>4 || selection.size()>64) fail(QStringLiteral("The Add request is too large."));
        const auto who=localActor(actor);
        if (action=="new-project") {
            ProjectRepository repo(container,who);
            const auto key=repo.create({{"name",field(values,0)},{"goal",field(values,1)}});
            return {{"accepted",true},{"message",QStringLiteral("Project created.")},{"projectKey",key}};
        }
        if (action=="new-canvas") {
            const auto name=fileName(field(values,0)); bool widthOk=false,heightOk=false;
            const int width=field(values,1).toInt(&widthOk),height=field(values,2).toInt(&heightOk);
            if (!widthOk || !heightOk || width<=0 || height<=0 || width>32768 || height>32768
                || qint64(width)*height>268435456) fail(QStringLiteral("Canvas dimensions are invalid or too large."));
            const auto relative=QStringLiteral("Files/")+name, path=drive->resolvePath(relative);
            if (path.isEmpty() || QFileInfo::exists(path)) fail(QStringLiteral("A file with that name already exists or the destination is unavailable."));
            iiSharedCanvas::Document document; document.extent={width,height};
            iiSharedCanvas::DocumentFile file; const auto created=file.create(path.toUtf8().toStdString(),document); file.close();
            if (!created.ok()) fail(QString::fromStdString(created.message));
            auto storage=iiSocietyContainer::SharedStorage::open(container,&error);
            const bool indexed=storage && storage->recordFile(iiSocietyContainer::StoreSection::Files,name,&error);
            return {{"accepted",true},{"message",indexed?QStringLiteral("Canvas created."):QStringLiteral("Canvas created; indexing is pending: ")+error},{"path",relative},{"indexed",indexed}};
        }
        if (action=="link-resource" || action=="link-external" || action=="link-url") {
            if (selection.size()!=1 || !selection[0].isString()) fail(QStringLiteral("Choose one source resource."));
            const auto source=managedPath(*drive,selection[0].toString());
            auto target=action=="link-url" ? field(values,0) : exactField(values,0); const auto predicate=field(values,1);
            if (action=="link-resource") target=managedPath(*drive,target);
            else if (action=="link-external") {
                const QFileInfo info(target);
                if (!QDir::isAbsolutePath(target) || QDir::cleanPath(target)!=target || !info.isFile() || info.isSymLink()
                    || info.isJunction() || info.canonicalFilePath()!=target)
                    fail(QStringLiteral("Choose an existing external file."));
                const auto root=QDir::cleanPath(drive->rootPath());
                if (target==root || target.startsWith(root+'/')) fail(QStringLiteral("Choose a file outside the Society drive, or use Link resource."));
            } else {
                const QUrl url(target,QUrl::StrictMode);
                if (!url.isValid() || (url.scheme()!="http" && url.scheme()!="https") || url.host().isEmpty() || !url.userInfo().isEmpty())
                    fail(QStringLiteral("Enter an HTTP or HTTPS URL without credentials."));
                target=url.toString(QUrl::FullyEncoded);
            }
            SemanticRepository repo(container,who); QJsonObject result;
            for (int attempt=0;attempt<12;++attempt) {
                try {
                    const auto current=repo.read(source);
                    result=repo.putRelationIfAbsent(source,predicate,target,{},current.value("revision").toString());
                    break;
                } catch (const std::exception &failure) {
                    if (!QString::fromUtf8(failure.what()).contains(QStringLiteral("revision conflict"),Qt::CaseInsensitive)) throw;
                    if (attempt==11) fail(QStringLiteral("The reference changed while it was being saved. Review it and try again."));
                    QThread::msleep(static_cast<unsigned long>(30*(attempt+1)));
                }
            }
            const auto message=result.value("alreadyExists").toBool() ? QStringLiteral("Reference already exists.")
                : result.value("catalogStatus").toString()=="pending"
                    ? QStringLiteral("Reference saved; project index update is pending: ")+result.value("catalogError").toString()
                    : QStringLiteral("Reference saved.");
            return {{"accepted",true},{"message",message},{"relation",result}};
        }
        if (action=="project-add-selection" || action=="project-add-resource" || action=="project-create-relation") {
            ProjectRepository repo(container,who); const auto key=projectKey(repo,field(values,0)); auto state=repo.load(key);
            if (action=="project-create-relation") {
                repo.addRelation(key,state.project.value("revision").toString(),{{"type",relationType(state,field(values,1))},
                    {"subject",graphId(state,field(values,2))},{"object",graphId(state,field(values,3))}});
                return {{"accepted",true},{"message",QStringLiteral("Project relation created.")}};
            }
            QStringList paths; const auto purpose=field(values,action=="project-add-selection"?1:2);
            if (action=="project-add-selection") {
                for (const auto &v:selection) { if (!v.isString()) fail(QStringLiteral("Invalid selection.")); paths.append(managedPath(*drive,v.toString())); }
                if (paths.isEmpty()) fail(QStringLiteral("Choose at least one resource."));
            } else {
                const auto path=exactField(values,1); const QFileInfo info(path);
                if (!QDir::isAbsolutePath(path) || QDir::cleanPath(path)!=path || !info.exists() || info.isSymLink()
                    || info.isJunction() || info.canonicalFilePath()!=path)
                    fail(QStringLiteral("Choose an existing local resource."));
                const auto root=QDir::cleanPath(drive->rootPath());
                if (path==root || path.startsWith(root+'/')) (void)managedPath(*drive,path);
                paths.append(path);
            }
            paths.removeDuplicates();
            for (const auto &path:paths) {
                repo.addResource(key,state.project.value("revision").toString(),{{"name",QFileInfo(path).fileName()},
                    {"purpose",purpose},{"locator",QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded)},{"location","reference"}});
                ++completed; state=repo.load(key);
            }
            return {{"accepted",true},{"message",QStringLiteral("Processed %1 resource(s) for the project.").arg(completed)}};
        }
        fail(QStringLiteral("Unknown Add operation."));
    } catch (const std::exception &e) {
        const auto message=QString::fromUtf8(e.what());
        return {{"accepted",false},{"error",completed?QStringLiteral("%1 resource(s) were processed before the error. Review the project before retrying: %2").arg(completed).arg(message):message},
                {"partial",completed>0},{"completed",completed}};
    } catch (...) {
        return {{"accepted",false},{"error",completed ? QStringLiteral("Some resources were processed before an unexpected error. Review the project before retrying.") : QStringLiteral("An unexpected Add operation error occurred.")},
                {"partial",completed>0},{"completed",completed}};
    }
}
}

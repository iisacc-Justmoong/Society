#include "LegacyWorkspaceBridge.h"
#include "NativeWorkspaceSurface.h"
#include "Society/Files/ModelBackendClient.h"
#include <QDateTime>
#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QLocale>
#include <QMimeData>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QVariantMap>
#include <algorithm>

namespace {
using Page = NativeWorkspaceViewModel::Page;
QString kind(const QString &suffix, bool directory) {
    if (directory) return QObject::tr("Folder");
    const auto s = suffix.toLower();
    if (s == "pdf") return QObject::tr("PDF document");
    if (s == "md") return QObject::tr("Markdown");
    if (s == "txt") return QObject::tr("Text document");
    if (s == "png") return QObject::tr("PNG image");
    if (s == "jpg" || s == "jpeg") return QObject::tr("JPEG image");
    if (s == "svg") return QObject::tr("SVG image");
    if (s == "csv") return QObject::tr("CSV document");
    return s.isEmpty() ? QObject::tr("File") : QObject::tr("%1 file").arg(s.toUpper());
}
std::vector<NativeWorkspaceGroup> groups(const QVariantList &values) {
    std::vector<NativeWorkspaceGroup> result;
    for (const auto &value : values) {
        const auto item = value.toMap();
        const auto id = item.value("id").toString(), name = item.value("name").toString();
        if (!id.isEmpty() && !name.isEmpty()) result.push_back({id.toStdString(), name.toStdString()});
    }
    return result;
}
bool invokeCalendar(QObject *dashboard, const QString &section) {
    if (QMetaObject::invokeMethod(dashboard, "showCalendar", Q_ARG(QString, section))) return true;
    return QMetaObject::invokeMethod(dashboard, "showCalendar", Q_ARG(QVariant, QVariant(section)));
}
bool insideFilesRoot(const QString &root, const QString &path) {
    if (root.isEmpty() || path.isEmpty() || !QDir::isAbsolutePath(root) || !QDir::isAbsolutePath(path)) return false;
    const auto cleanRoot = QDir::cleanPath(root);
    const auto cleanPath = QDir::cleanPath(path);
    return cleanPath.startsWith(cleanRoot == "/" ? cleanRoot : cleanRoot + '/');
}
std::string previewAddress(const QString &path, qint64 modified, qint64 size) {
    if (path.isEmpty() || modified<=0 || size<0) return {};
    auto url=QUrl::fromLocalFile(path);
    url.setQuery(QStringLiteral("version=%1-%2").arg(modified).arg(size));
    return url.toString(QUrl::FullyEncoded).toStdString();
}
}

NativeWorkspaceSurface *LegacyWorkspaceBridge::install(QQuickWindow *window) {
    if (!window) return nullptr;
    if (auto *existing = window->findChild<LegacyWorkspaceBridge *>(QStringLiteral("societyNativeWorkspaceBridge"))) return existing->m_surface;
    auto *dashboard = window->findChild<QQuickItem *>(QStringLiteral("dashboardView"));
    auto *sidebar = dashboard ? dashboard->findChild<QQuickItem *>(QStringLiteral("dashboardSidebar")) : nullptr;
    auto *scroll = dashboard ? dashboard->findChild<QQuickItem *>(QStringLiteral("dashboardScroll")) : nullptr;
    if (!dashboard || !dashboard->parentItem() || !sidebar || !scroll || !scroll->parentItem()) return nullptr;
    auto *surface = new NativeWorkspaceSurface(dashboard->parentItem());
    surface->setSize(dashboard->size()); surface->refresh();
    if (!surface->ready()) { qWarning("Unable to initialize Society LVUI Workspace: %s", qPrintable(surface->errorString())); delete surface; return nullptr; }
    new LegacyWorkspaceBridge(window, dashboard, sidebar, scroll->parentItem(), scroll, surface);
    return surface;
}

LegacyWorkspaceBridge::LegacyWorkspaceBridge(QQuickWindow *window, QQuickItem *dashboard, QQuickItem *sidebar,
    QQuickItem *content, QQuickItem *scroll, NativeWorkspaceSurface *surface)
    : QObject(window), m_dashboard(dashboard), m_sidebar(sidebar), m_content(content), m_scroll(scroll), m_surface(surface),
      m_agentClient(new ModelBackendClient(this)) {
    setObjectName(QStringLiteral("societyNativeWorkspaceBridge"));
    surface->setObjectName(QStringLiteral("nativeWorkspaceSurface")); surface->setZ(100);
    m_files = dashboard->property("viewModel").value<QObject *>();
    m_navigation = dashboard->property("navigation").value<QObject *>();
    const QPointer<LegacyWorkspaceBridge> guard(this);
    auto *agent = surface->model();
    agent->onSend = [guard](const std::string &prompt, const std::vector<NativeWorkspaceFile> &context) {
        if (!guard || !guard->m_surface || !guard->m_surface->model()) return false;
        QJsonArray attachments;
        for (const auto &file:context) attachments.append(QString::fromStdString(file.path));
        guard->m_agentDraft=QString::fromStdString(prompt);
        if (guard->m_agentClient->request("agentSend",{{"prompt",guard->m_agentDraft},{"attachments",attachments}},true)) return true;
        guard->m_agentDraft.clear(); return false;
    };
    agent->onSelectModel = [guard](const std::string &id) { if (guard) guard->m_agentClient->request("agentSelect",{{"model",QString::fromStdString(id)}},true); };
    agent->onNewConversation = [guard] { if (guard) guard->m_agentClient->request("agentNew",{},true); };
    agent->onStopAgent = [guard] { if (guard) guard->m_agentClient->request("agentCancel",{},true); };
    agent->onRefresh = [guard] {
        if (!guard || !guard->m_agentClient->connected()) return;
        guard->m_agentClient->request("suggestedRefresh"); guard->m_agentClient->request("recentRefresh");
        guard->pollFileJobs();
    };
    agent->onCopyPaths = [](const std::vector<std::string> &paths) {
        QStringList values; for (const auto &path:paths) values.push_back(QString::fromStdString(path));
        if (auto *clipboard=QGuiApplication::clipboard()) clipboard->setText(values.join('\n'));
    };
    agent->onCopyFiles = [guard](const std::vector<std::string> &paths) {
        auto *clipboard=QGuiApplication::clipboard(); if (!clipboard) return;
        if (guard) { guard->m_cut.clear(); guard->m_cutUncertain.clear(); }
        auto *mime=new QMimeData; QList<QUrl> urls;
        for (const auto &path:paths) urls.push_back(QUrl::fromLocalFile(QString::fromStdString(path)));
        mime->setUrls(urls); clipboard->setMimeData(mime);
    };
    agent->onCutFiles = [guard](const std::vector<std::string> &paths) {
        if (!guard || paths.empty() || guard->m_agentContainer.isEmpty()) return;
        auto *clipboard=QGuiApplication::clipboard(); if (!clipboard) return;
        const auto token=QUuid::createUuid().toString(QUuid::WithoutBraces);
        guard->m_cut.begin(token.toStdString(),guard->m_agentContainer.toStdString(),paths);
        guard->m_cutUncertain.clear();
        auto *mime=new QMimeData; QList<QUrl> urls;
        for (const auto &path:paths) urls.push_back(QUrl::fromLocalFile(QString::fromStdString(path)));
        mime->setUrls(urls); mime->setData("application/x-society-cut-token",token.toUtf8());
        clipboard->setMimeData(mime);
        if (guard->m_surface && guard->m_surface->model()) guard->m_surface->model()->operationStatus.set("Cut files; choose a destination and paste.");
    };
    agent->onPasteFiles = [guard](const std::string &destination) {
        if (!guard || !guard->m_surface || !guard->m_surface->model()) return false;
        auto *clipboard=QGuiApplication::clipboard(); const auto *mime=clipboard?clipboard->mimeData():nullptr;
        if (!mime || !mime->hasUrls()) { guard->m_surface->model()->recordOperationError("The clipboard has no files to paste."); return false; }
        std::vector<std::string> paths;
        for (const auto &url:mime->urls()) {
            if (!url.isLocalFile() || !QDir::isAbsolutePath(url.toLocalFile())) {
                guard->m_surface->model()->recordOperationError("A clipboard item is not a local file and was not submitted."); return false;
            }
            paths.push_back(url.toLocalFile().toStdString());
        }
        const auto marker=mime->data("application/x-society-cut-token").toStdString();
        const bool move=!marker.empty();
        if (move && !guard->m_cut.matches(marker,guard->m_agentContainer.toStdString(),paths)) {
            guard->m_surface->model()->recordOperationError("The cut files or Society drive changed. Cut the files again."); return false;
        }
        std::size_t submitted=0;
        for (const auto &path:paths) {
            const auto source=QString::fromStdString(path);
            if (move) {
                if (guard->m_cutUncertain.contains(source)) {
                    guard->m_surface->model()->recordOperationError("A cut operation has an uncertain outcome. Check the file and cut it again before retrying.");
                    continue;
                }
                const auto active=[&](const auto &transfers) {
                    return std::any_of(transfers.cbegin(),transfers.cend(),[&](const auto &transfer) {
                        return transfer.token==QString::fromStdString(marker) && transfer.path==source;
                    });
                };
                if (active(guard->m_cutRequests) || active(guard->m_cutJobs)) continue;
            }
            QString requestId;
            if (!guard->submitFileOperation(move?"move":"copy",source,QString::fromStdString(destination),!move,&requestId)) break;
            if (move) guard->m_cutRequests.insert(requestId,{QString::fromStdString(marker),guard->m_agentContainer,source});
            ++submitted;
        }
        if (move && !submitted && guard->m_cutUncertain.isEmpty())
            guard->m_surface->model()->recordOperationError("The cut files are already being moved.");
        return submitted>0;
    };
    agent->onImportFile = [guard](const std::string &path,const std::string &destination) {
        return guard && guard->submitFileOperation("copy",QString::fromStdString(path),QString::fromStdString(destination),true);
    };
    agent->onWorkspaceAdd = [guard](const NativeWorkspaceAddRequest &request) {
        if (!guard || !guard->m_surface || !guard->m_surface->model()) return false;
        auto *vm=guard->m_surface->model();
        if (!guard->m_agentClient->connected() || guard->m_agentContainer.isEmpty()) {
            vm->recordOperationError("The Society backend or drive is unavailable. Reconnect before submitting the operation."); return false;
        }
        const auto action=QString::fromStdString(request.action);
        if (action=="add-import-files" || action=="add-import-folder" || action=="add-import-models") {
            QStringList paths;
            for (const auto &line:QString::fromStdString(request.fields[0]).split('\n',Qt::SkipEmptyParts)) {
                auto path=line;
                if (path.endsWith('\r')) path.chop(1);
                if (!QDir::isAbsolutePath(path) || QDir::cleanPath(path)!=path || path.contains(QChar::Null)) {
                    vm->recordOperationError("Enter absolute local paths, one per line."); return false;
                }
                paths.append(path);
            }
            paths.removeDuplicates();
            if (paths.isEmpty() || paths.size()>64 || (action=="add-import-folder" && paths.size()!=1)) {
                vm->recordOperationError("Choose a valid number of source paths."); return false;
            }
            if (action=="add-import-models") {
                QString id;
                if (!guard->m_agentClient->request("workspaceAddModels",{{"sources",QJsonArray::fromStringList(paths)}},true,&id)) return false;
                guard->m_addRequests.insert(id,action); vm->operationStatus.set("Submitting model import…"); return true;
            }
            const auto destination=QString::fromStdString(request.fields[1]); int submitted=0;
            for (const auto &path:paths) {
                if (!guard->submitFileOperation("copy",path,destination,true)) {
                    if (submitted) vm->recordOperationError((QStringLiteral("%1 source(s) were submitted; check their jobs before retrying.").arg(submitted)).toStdString());
                    return submitted>0;
                }
                ++submitted;
            }
            return true;
        }
        QJsonArray fields,selection;
        for (const auto &value:request.fields) fields.append(QString::fromStdString(value));
        for (const auto &value:request.selection) selection.append(QString::fromStdString(value));
        QString actor;
        if (guard->m_dashboard) if (auto *projects=guard->m_dashboard->property("projectWorkspace").value<QObject *>())
            actor=projects->property("actorId").toString();
        QString id;
        if (!guard->m_agentClient->request("workspaceAdd",{{"action",action},{"fields",fields},{"selection",selection},{"actor",actor}},true,&id)) return false;
        guard->m_addRequests.insert(id,action); vm->operationStatus.set("Submitting Add operation…"); return true;
    };
    connect(m_agentClient,&ModelBackendClient::workspaceResultReceived,this,[this](const QString &operation,const QString &id,const QJsonObject &result) {
        if (!m_surface || !m_surface->model() || !m_addRequests.contains(id)) return;
        m_addRequests.remove(id); auto *vm=m_surface->model();
        if (!result.value("accepted").toBool()) vm->recordOperationError(result.value("error").toString(QStringLiteral("The Add operation failed.")).toStdString());
        else if (operation=="workspaceAddModels") {
            m_addModelJob=result.value("jobId").toString();
            vm->operationStatus.set((QStringLiteral("Model import queued · job ")+m_addModelJob).toStdString());
        } else vm->operationStatus.set(result.value("message").toString(QStringLiteral("Add operation completed.")).toStdString());
        m_surface->refresh();
    });
    connect(m_agentClient,&ModelBackendClient::stateReceived,this,[this](const QJsonObject &state) {
        if (m_addModelJob.isEmpty() || !m_surface || !m_surface->model()) return;
        auto *vm=m_surface->model();
        const auto currentJob=state.value("currentJobId").toString();
        if (currentJob!=m_addModelJob) {
            vm->recordOperationError((QStringLiteral("Model import status is unavailable for job ")+m_addModelJob+QStringLiteral(". Check the job before retrying.")).toStdString());
            m_addModelJob.clear(); m_surface->refresh(); return;
        }
        if (state.value("busy").toBool()) vm->operationStatus.set(state.value("status").toString(QStringLiteral("Importing models…")).toStdString());
        else {
            const auto error=state.value("errorString").toString();
            if (!error.isEmpty()) vm->recordOperationError(error.toStdString());
            else vm->operationStatus.set(state.value("status").toString(QStringLiteral("Model import finished.")).toStdString());
            m_addModelJob.clear();
        }
        m_surface->refresh();
    });
    agent->onInspectResource = [guard](const std::string &path) {
        if (!guard || !guard->m_dashboard || !guard->m_surface || !guard->m_surface->model()) return;
        const auto absolute=QString::fromStdString(path), root=guard->m_dashboard->property("filesRoot").toString();
        if (!guard->m_agentClient->connected() || !insideFilesRoot(root,absolute)) {
            guard->m_surface->model()->setDetailsInspection(path,"Resource information is unavailable."); return;
        }
        QString id;
        const auto logical=QStringLiteral("Files/")+QDir(root).relativeFilePath(absolute);
        if (!guard->m_agentClient->request("resourceInspect",{{"path",logical}},false,&id)) {
            guard->m_surface->model()->setDetailsInspection(path,"Resource information could not be requested."); return;
        }
        guard->m_inspectionId=id; guard->m_inspectionPath=absolute;
    };
    if (auto *clipboard=QGuiApplication::clipboard()) {
        const auto refreshClipboard=[this,clipboard] {
            if (!m_surface || !m_surface->model()) return;
            const auto *mime=clipboard->mimeData();
            if (!m_cut.token().empty() && (!mime || mime->data("application/x-society-cut-token").toStdString()!=m_cut.token())) {
                m_cut.clear(); m_cutUncertain.clear();
            }
            m_surface->model()->setClipboardHasFiles(mime && mime->hasUrls());
            m_surface->refresh();
        };
        connect(clipboard,&QClipboard::dataChanged,this,[this,refreshClipboard] { QTimer::singleShot(0,this,refreshClipboard); });
        refreshClipboard();
    }
    agent->onFileAction = [guard](const std::string &action, const std::string &locator, const std::string &argument) {
        return guard && guard->submitFileOperation(QString::fromStdString(action),QString::fromStdString(locator),QString::fromStdString(argument));
    };
    connect(m_agentClient,&ModelBackendClient::fileResultReceived,this,[this](const QString &operation,const QString &id,const QJsonObject &result) {
        if (!m_surface || !m_surface->model()) return;
        auto *vm=m_surface->model();
        if (operation=="fileOperation") {
            if (!m_fileRequests.contains(id)) return;
            const auto path=m_fileRequests.take(id), job=result.value("jobId").toString();
            const auto cut=m_cutRequests.take(id);
            if (!result.value("accepted").toBool() || job.isEmpty()) {
                const auto error=result.value("error").toString();
                if (!cut.token.isEmpty() && result.value("uncertain").toBool()
                    && m_cut.token()==cut.token.toStdString()) m_cutUncertain.insert(cut.path);
                vm->recordOperationError((path+": "+(error.isEmpty()?QStringLiteral("File operation was rejected."):error)).toStdString());
            } else {
                if (!cut.token.isEmpty()) m_cutJobs.insert(job,cut);
                m_fileJobs.insert(job,path); vm->operationStatus.set("File operation queued · job "+job.toStdString());
            }
        } else if (operation=="fileJobStatus") {
            const auto jobId=m_fileQueries.take(id);
            if (jobId.isEmpty() || !m_fileJobs.contains(jobId)) return;
            if (!result.value("accepted").toBool()) {
                if (!m_recordedFileJobs.contains(jobId))
                    vm->operationStatus.set((QStringLiteral("Job status is not available yet; check before retrying · ")+jobId).toStdString());
                else vm->recordOperationError((QStringLiteral("Could not check job ")+jobId+QStringLiteral(": ")+result.value("error").toString()).toStdString());
            } else {
                m_recordedFileJobs.insert(jobId);
                const auto job=result.value("job").toObject();
                const auto state=job.value("state").toString();
                if (state=="succeeded") { completeCutJob(jobId,true); vm->operationStatus.set("File operation completed · job "+jobId.toStdString()); m_fileJobs.remove(jobId); m_recordedFileJobs.remove(jobId); }
                else if (state=="failed" || state=="partial" || state=="cancelled" || state=="attention") {
                    completeCutJob(jobId,false);
                    const auto error=job.value("error").toString();
                    vm->recordOperationError((m_fileJobs.value(jobId)+QStringLiteral(": ")+(error.isEmpty()?state:error)+QStringLiteral(" · job ")+jobId).toStdString());
                    if (state!="attention") { m_fileJobs.remove(jobId); m_recordedFileJobs.remove(jobId); }
                } else vm->operationStatus.set((QStringLiteral("File job ")+state+QStringLiteral(" · ")+jobId).toStdString());
            }
        }
        m_surface->refresh();
    });
    connect(m_agentClient,&ModelBackendClient::resourceResultReceived,this,[this](const QString &id,const QJsonObject &result) {
        if (id!=m_inspectionId || !m_surface || !m_surface->model()) return;
        const auto path=m_inspectionPath; m_inspectionId.clear(); m_inspectionPath.clear();
        QString report;
        if (!result.value("accepted").toBool()) report=result.value("error").toString(QStringLiteral("Resource information is unavailable."));
        else {
            const auto snapshot=result.value("resource").toObject(), resource=snapshot.value("resource").toObject();
            const auto revision=snapshot.value("contentRevision").toObject();
            report=QStringLiteral("Resource ID: %1\nObserved revision: %2")
                .arg(resource.value("id").toString(QStringLiteral("unknown")),revision.value("id").toString(QStringLiteral("unknown")));
            const auto digest=revision.value("digest").toObject();
            if (!digest.isEmpty()) report+=QStringLiteral("\nObserved %1: %2").arg(digest.value("algorithm").toString(),digest.value("value").toString());
            const auto locations=snapshot.value("locations").toArray(), replicas=snapshot.value("replicas").toArray();
            report+=QStringLiteral("\nLocations: %1 · replicas: %2\nAvailability and replica state: unknown")
                .arg(locations.size()).arg(replicas.size());
            for (const auto &item:locations) report+=QStringLiteral("\n• ")+item.toObject().value("locator").toString();
            for (const auto &item:replicas) report+=QStringLiteral("\n• Replica: ")+item.toObject().value("locatorHint").toString();
        }
        m_surface->model()->setDetailsInspection(path.toStdString(),report.toStdString()); m_surface->refresh();
    });
    connect(m_agentClient,&ModelBackendClient::directoryResultReceived,this,[this](const QString &operation,const QString &id,const QJsonObject &result) {
        if (operation=="directoryOpen" && id==m_openDirectoryRequest) {
            m_openDirectoryRequest.clear();
            if (!result.value("accepted").toBool()) { failPendingOpen(result.value("error").toString()); return; }
            const auto state=result.value("directory").toObject();
            if (state.value("path").toString()!=m_openDirectory || state.value("generation").toString().isEmpty()) {
                failPendingOpen(tr("The background service returned an invalid directory.")); return;
            }
            m_openGeneration=state.value("generation").toString();
            advancePendingOpen(state);
        } else if (operation=="directoryActivate" && id==m_openActivateRequest && !result.value("accepted").toBool()) {
            failPendingOpen(result.value("error").toString());
        }
    });
    connect(m_agentClient,&ModelBackendClient::directoryStateReceived,this,&LegacyWorkspaceBridge::advancePendingOpen);
    connect(m_agentClient,&ModelBackendClient::directoryActivated,this,[this](const QJsonObject &event) {
        if (m_openActivateRequest.isEmpty() || event.value("id").toString()!=m_openActivateRequest
            || event.value("generation").toString()!=m_openGeneration) return;
        const auto error=event.value("error").toString(),path=event.value("path").toString();
        if (!error.isEmpty()) { failPendingOpen(error); return; }
        if (!m_dashboard || path!=m_pendingOpen || QDir::cleanPath(m_dashboard->property("filesRoot").toString())!=m_pendingRoot
            || !insideFilesRoot(m_pendingRoot,path)) { failPendingOpen(tr("The selected file or Society drive changed.")); return; }
        clearPendingOpen();
        if (m_surface && m_surface->model()) {
            auto *vm=m_surface->model();
            vm->status.set((vm->recentActive.get()?m_recentStatus:m_suggestedStatus).toStdString()); m_surface->refresh();
        }
        if (!QMetaObject::invokeMethod(m_dashboard,"fileRequested",Q_ARG(QString,path))
            && !QMetaObject::invokeMethod(m_dashboard,"fileRequested",Q_ARG(QVariant,QVariant(path))))
            failPendingOpen(tr("The selected file could not be opened."));
    });
    connect(m_agentClient,&ModelBackendClient::fileJobChanged,this,[this](const QJsonObject &event) {
        const auto job=event.value("jobId").toString();
        if (!m_fileJobs.contains(job) || !m_surface || !m_surface->model()) return;
        if (event.value("state").toString()=="recorded") {
            m_recordedFileJobs.insert(job);
            m_surface->model()->operationStatus.set("File job recorded · job "+job.toStdString()); m_surface->refresh(); return;
        }
        if (event.value("state").toString()!="finished") return;
        const auto error=event.value("error").toString();
        if (error.isEmpty()) {
            completeCutJob(job,true);
            m_surface->model()->operationStatus.set("File operation completed · job "+job.toStdString());
            m_fileJobs.remove(job); m_recordedFileJobs.remove(job);
        } else {
            completeCutJob(job,false);
            m_surface->model()->recordOperationError((m_fileJobs.value(job)+QStringLiteral(": ")+error+QStringLiteral(" · job ")+job).toStdString());
            m_fileJobs.remove(job); m_recordedFileJobs.remove(job);
        }
        m_surface->refresh();
    });
    connect(m_agentClient,&ModelBackendClient::connectedChanged,this,[this] {
        if (!m_surface || !m_surface->model()) return;
        m_surface->model()->setFileActionsAvailable(m_agentClient->connected() && !m_agentContainer.isEmpty());
        if (m_agentClient->connected() && !m_agentContainer.isEmpty()) {
            m_agentClient->request("agentSubscribe"); m_agentClient->request("suggestedSubscribe"); m_agentClient->request("recentSubscribe");
            pollFileJobs();
        } else {
            if (!m_pendingOpen.isEmpty()) failPendingOpen(tr("Connection interrupted while preparing the selected file."));
            if (!m_addRequests.isEmpty()) {
                m_surface->model()->recordOperationError("Connection interrupted during an Add operation. Inspect its destination before retrying.");
                m_addRequests.clear();
            }
            if (!m_inspectionId.isEmpty()) {
                m_surface->model()->setDetailsInspection(m_inspectionPath.toStdString(),"Connection interrupted while reading resource information.");
                m_inspectionId.clear(); m_inspectionPath.clear();
            }
            if (!m_fileRequests.isEmpty()) m_surface->model()->recordOperationError("Connection interrupted. Check submitted jobs before retrying.");
            for (const auto &cut:m_cutRequests)
                if (m_cut.token()==cut.token.toStdString()) m_cutUncertain.insert(cut.path);
            m_cutRequests.clear();
            m_fileRequests.clear(); m_fileQueries.clear();
            m_surface->model()->setAgentAvailable(false); m_surface->model()->setSuggestedFiles({}); m_surface->model()->setRecentFiles({});
            m_suggestedStatus=QStringLiteral("The background service is unavailable.");
            m_recentStatus=m_suggestedStatus;
            if (m_surface->model()->suggestedActive.get()) m_surface->model()->status.set(m_suggestedStatus.toStdString());
            if (m_surface->model()->recentActive.get()) m_surface->model()->status.set(m_recentStatus.toStdString());
            m_surface->refresh();
        }
    });
    connect(m_agentClient,&ModelBackendClient::recentStateReceived,this,[this](const QJsonObject &state) {
        if (!m_surface || !m_surface->model()) return;
        std::vector<NativeWorkspaceFile> files;
        for (const auto &value:state.value("items").toArray()) {
            const auto item=value.toObject(); const auto path=item.value("path").toString();
            const auto millis=item.value("activityMs").toString().toLongLong();
            const auto time=QDateTime::fromMSecsSinceEpoch(millis);
            const auto size=item.value("size").toString().toLongLong();
            NativeWorkspaceFile file{path.toStdString(),item.value("name").toString().toStdString(),
                millis>0?time.toLocalTime().toString("dd MMM yyyy, HH:mm").toStdString():std::string{},
                kind(QFileInfo(path).suffix(),false).toStdString(),size<0?std::string("—"):QLocale().formattedDataSize(size).toStdString(),false,millis,size};
            file.previewSource=previewAddress(path,item.value("modifiedMs").toString().toLongLong(),size);
            files.push_back(std::move(file));
        }
        auto *vm=m_surface->model(); vm->setRecentFiles(std::move(files));
        m_recentStatus=state.value("status").toString();
        if (vm->recentActive.get()) vm->status.set(m_recentStatus.toStdString());
        m_surface->refresh();
    });
    connect(m_agentClient,&ModelBackendClient::recentResultReceived,this,[this](const QString &,bool accepted,const QString &error) {
        if (accepted || !m_surface || !m_surface->model()) return;
        m_recentStatus=error.isEmpty()?QStringLiteral("Recent files are unavailable."):error;
        if (m_surface->model()->recentActive.get()) m_surface->model()->status.set(m_recentStatus.toStdString());
        m_surface->refresh();
    });
    connect(m_agentClient,&ModelBackendClient::suggestedStateReceived,this,[this](const QJsonObject &state) {
        if (!m_surface || !m_surface->model()) return;
        std::vector<NativeWorkspaceFile> files;
        for (const auto &value:state.value("items").toArray()) {
            const auto item=value.toObject(); const auto path=item.value("path").toString();
            const auto millis=item.value("modifiedMs").toString().toLongLong();
            const auto modified=QDateTime::fromMSecsSinceEpoch(millis);
            const auto size=item.value("size").toString().toLongLong();
            NativeWorkspaceFile file{path.toStdString(),item.value("name").toString().toStdString(),
                millis>0?modified.toLocalTime().toString("dd MMM yyyy, HH:mm").toStdString():std::string{},
                kind(QFileInfo(path).suffix(),false).toStdString(),QLocale().formattedDataSize(size).toStdString(),false,millis,size};
            file.reason=item.value("reason").toString().toStdString();
            file.recommendationSource=item.value("source").toString().toStdString();
            file.resourceId=item.value("resourceId").toString().toStdString();
            file.workingVersion=item.value("workingVersion").toString().toStdString();
            file.previewSource=previewAddress(path,millis,size);
            files.push_back(std::move(file));
        }
        auto *vm=m_surface->model(); vm->setSuggestedFiles(std::move(files));
        m_suggestedStatus=state.value("status").toString();
        if (vm->suggestedActive.get()) vm->status.set(m_suggestedStatus.toStdString());
        m_surface->refresh();
    });
    connect(m_agentClient,&ModelBackendClient::suggestedResultReceived,this,[this](const QString &,bool accepted,const QString &error) {
        if (accepted || !m_surface || !m_surface->model()) return;
        m_suggestedStatus=error.isEmpty()?QStringLiteral("Suggested files are unavailable."):error;
        if (m_surface->model()->suggestedActive.get()) m_surface->model()->status.set(m_suggestedStatus.toStdString());
        m_surface->refresh();
    });
    connect(m_agentClient,&ModelBackendClient::agentStateReceived,this,[this](const QJsonObject &state) {
        if (!m_surface || !m_surface->model()) return;
        std::vector<NativeAgentModel> models;
        for (const auto &value : state.value("models").toArray()) {
            const auto item=value.toObject(); models.push_back({item.value("id").toString().toStdString(),item.value("name").toString().toStdString()});
        }
        std::vector<NativeAgentMessage> messages;
        for (const auto &value : state.value("messages").toArray()) {
            const auto item=value.toObject(); messages.push_back({item.value("id").toString().toStdString(),item.value("role").toString().toStdString(),item.value("text").toString().toStdString()});
        }
        auto *vm=m_surface->model(); vm->setAgentModels(std::move(models));
        vm->agentRetryable.set(state.value("retryable").toBool());
        vm->setAgentState(state.value("model").toString().toStdString(),state.value("busy").toBool(),state.value("status").toString().toStdString(),std::move(messages));
        m_surface->refresh();
    });
    connect(m_agentClient,&ModelBackendClient::agentResultReceived,this,[this](const QString &operation,bool accepted,const QString &error) {
        if (!m_surface || !m_surface->model()) return;
        auto *vm=m_surface->model();
        if (operation=="agentSend") {
            if (!accepted && vm->prompt.get().empty()) vm->promptEdited(m_agentDraft.toStdString());
            m_agentDraft.clear();
        }
        if (operation=="agentSubscribe" && !accepted && m_agentClient->connected() && !m_agentContainer.isEmpty())
            QTimer::singleShot(1000,this,[this] { if (m_agentClient->connected() && !m_agentContainer.isEmpty()) m_agentClient->request("agentSubscribe"); });
        if (!accepted) vm->agentStatus.set(error.isEmpty()?"The agent request was not accepted.":error.toStdString());
        m_surface->refresh();
    });
    connect(m_agentClient,&ModelBackendClient::requestError,this,[this](const QString &error) {
        if (!m_surface || !m_surface->model()) return;
        auto *vm=m_surface->model();
        if (!m_agentDraft.isEmpty() && vm->prompt.get().empty()) vm->promptEdited(m_agentDraft.toStdString());
        if (!m_agentDraft.isEmpty()) vm->agentStatus.set(error.toStdString());
        else vm->recordOperationError(error.toStdString());
        m_agentDraft.clear(); m_surface->refresh();
    });
    m_agentClient->start();
    connect(dashboard, &QQuickItem::widthChanged, this, &LegacyWorkspaceBridge::synchronize);
    connect(dashboard, &QQuickItem::heightChanged, this, &LegacyWorkspaceBridge::synchronize);
    connect(dashboard, &QQuickItem::xChanged, this, &LegacyWorkspaceBridge::synchronize);
    connect(dashboard, &QQuickItem::yChanged, this, &LegacyWorkspaceBridge::synchronize);
    connect(dashboard, &QQuickItem::visibleChanged, this, &LegacyWorkspaceBridge::synchronize);
    connect(dashboard, SIGNAL(projectsPageVisibleChanged()), this, SLOT(synchronize()));
    connect(dashboard, SIGNAL(calendarPageVisibleChanged()), this, SLOT(synchronize()));
    connect(dashboard, SIGNAL(activityPageVisibleChanged()), this, SLOT(synchronize()));
    connect(dashboard, SIGNAL(filesRootChanged()), this, SLOT(synchronize()));
    connect(dashboard, SIGNAL(fileRequested(QString)), this, SLOT(recordRecentOpen(QString)));
    connect(dashboard, SIGNAL(dashboardPathChanged()), this, SLOT(synchronize()));
    if (m_files) {
        connect(m_files, SIGNAL(filesChanged()), this, SLOT(refreshRecent()));
        connect(m_files, SIGNAL(filesChanged()), this, SLOT(scheduleSuggestedRefresh()));
        connect(m_files, SIGNAL(loadingChanged()), this, SLOT(refreshRecent()));
        connect(m_files, SIGNAL(containerPathChanged()), this, SLOT(synchronize()));
    }
    if (m_navigation) connect(m_navigation, SIGNAL(membershipsChanged()), this, SLOT(refreshGroups()));
    connect(surface, &NativeWorkspaceSurface::pageRequested, this, &LegacyWorkspaceBridge::navigate);
    connect(surface, &NativeWorkspaceSurface::openRequested, this, [this](const QString &path) {
        if (!m_dashboard || !m_surface || !m_surface->model()) return;
        const auto root = m_dashboard->property("filesRoot").toString();
        if (!insideFilesRoot(root, path)) {
            if (m_surface && m_surface->model()) { m_surface->model()->status.set("The file is not in the current Society drive."); m_surface->refresh(); }
            return;
        }
        if (!m_agentClient->connected() || m_agentContainer.isEmpty()) {
            failPendingOpen(tr("The Society background service is unavailable.")); return;
        }
        clearPendingOpen();
        m_pendingRoot=QDir::cleanPath(root); m_pendingOpen=QDir::cleanPath(path);
        m_openRelative=QStringLiteral("Files/")+QDir(m_pendingRoot).relativeFilePath(m_pendingOpen);
        m_openDirectory=QFileInfo(m_openRelative).path();
        if (!m_agentClient->request("directoryOpen",{{"path",m_openDirectory}},false,&m_openDirectoryRequest)) {
            failPendingOpen(tr("The file preparation request could not be sent.")); return;
        }
        m_surface->model()->status.set("Preparing file…"); m_surface->refresh();
    });
    connect(surface, &NativeWorkspaceSurface::upRequested, this, [this] { if (m_dashboard) QMetaObject::invokeMethod(m_dashboard, "upDashboard"); });
    connect(surface, &NativeWorkspaceSurface::groupRequested, this, [this](const QString &kind, const QString &id) {
        if (m_navigation) QMetaObject::invokeMethod(m_navigation, "activate", Q_ARG(QString, kind), Q_ARG(QString, id));
    });
    refreshGroups(); refreshRecent(); synchronize();
}
LegacyWorkspaceBridge::~LegacyWorkspaceBridge() {
    if (m_sidebar) { m_sidebar->setOpacity(1); m_sidebar->setEnabled(true); }
    if (m_content) { m_content->setOpacity(1); m_content->setEnabled(true); }
    if (m_scroll && m_dashboard) m_scroll->setVisible(!m_dashboard->property("projectsPageVisible").toBool()
        && !m_dashboard->property("calendarPageVisible").toBool() && !m_dashboard->property("activityPageVisible").toBool());
}
void LegacyWorkspaceBridge::navigate(int value) {
    if (!m_dashboard || !m_surface || !m_surface->model()) return;
    const auto page = static_cast<Page>(value);
    switch (page) {
    case Page::Suggested: case Page::Recent:
        m_homePage = value; QMetaObject::invokeMethod(m_dashboard, "goHome"); break;
    case Page::Projects: QMetaObject::invokeMethod(m_dashboard, "showProjects"); break;
    case Page::Calendar: invokeCalendar(m_dashboard, {}); break;
    case Page::Activity: invokeCalendar(m_dashboard, QStringLiteral("activity")); break;
    }
    synchronize();
}
void LegacyWorkspaceBridge::synchronize() {
    if (!m_dashboard || !m_surface || !m_surface->model()) return;
    auto *vm = m_surface->model();
    const auto agentContainer=m_files ? m_files->property("containerPath").toString() : QString();
    if (agentContainer!=m_agentContainer) {
        vm->resetDriveContext();
        clearPendingOpen();
        m_cut.clear(); m_cutUncertain.clear(); m_cutRequests.clear(); m_cutJobs.clear();
        m_agentContainer=agentContainer; m_agentDraft.clear();
        m_inspectionId.clear(); m_inspectionPath.clear();
        if (!m_addRequests.isEmpty() || !m_addModelJob.isEmpty()) vm->operationStatus.set("Add operations may continue in the previous Society drive.");
        m_addRequests.clear(); m_addModelJob.clear();
        if (!m_fileRequests.isEmpty() || !m_fileJobs.isEmpty()) vm->operationStatus.set("File jobs continue in the previous Society drive.");
        m_fileRequests.clear(); m_fileQueries.clear(); m_fileJobs.clear(); m_recordedFileJobs.clear();
        vm->setFileActionsAvailable(m_agentClient->connected() && !agentContainer.isEmpty());
        vm->setSuggestedFiles({}); m_suggestedStatus=agentContainer.isEmpty()?QStringLiteral("Society drive is not available."):QStringLiteral("Finding unfinished work…");
        vm->setRecentFiles({}); m_recentStatus=agentContainer.isEmpty()?QStringLiteral("Society drive is not available."):QStringLiteral("Reading recent files…");
        vm->setAgentModels({}); vm->setAgentState("",false,agentContainer.isEmpty()?"Society drive is not available.":"Connecting agent…",{});
        m_agentClient->setContainerPath(agentContainer);
        if (m_agentClient->connected() && !agentContainer.isEmpty()) { m_agentClient->request("agentSubscribe"); m_agentClient->request("suggestedSubscribe"); m_agentClient->request("recentSubscribe"); }
    }
    const bool desktop = m_dashboard->width() >= 760;
    const bool projects = m_dashboard->property("projectsPageVisible").toBool();
    const bool calendar = m_dashboard->property("calendarPageVisible").toBool();
    const bool activity = m_dashboard->property("activityPageVisible").toBool();
    const bool home = !projects && !calendar && !activity;
    const auto page = projects ? Page::Projects : calendar ? Page::Calendar : activity ? Page::Activity : static_cast<Page>(m_homePage);
    vm->setPage(page);
    m_active = desktop && m_dashboard->isVisible();
    m_surface->setVisible(m_active);
    m_surface->setPosition(m_dashboard->position());
    m_surface->setSize(QSizeF(home ? m_dashboard->width() : m_sidebar->width(), m_dashboard->height()));
    if (m_sidebar) { m_sidebar->setOpacity(m_active ? 0 : 1); m_sidebar->setEnabled(!m_active); }
    if (m_content) { m_content->setOpacity(m_active && home ? 0 : 1); m_content->setEnabled(!(m_active && home)); }
    // The legacy browser would otherwise create a second view of the same directory.
    if (m_scroll) m_scroll->setVisible(home && !m_active);
    const auto root = m_dashboard->property("filesRoot").toString();
    if (!m_pendingRoot.isEmpty() && (root.isEmpty() || QDir::cleanPath(root) != m_pendingRoot)) {
        clearPendingOpen();
    }
    vm->canUp.set(false);
    if (root.isEmpty()) { vm->setSuggestedFiles({}); vm->status.set("Society drive is not available."); }
    else if (vm->suggestedActive.get()) vm->status.set(m_suggestedStatus.toStdString());
    if (vm->recentActive.get()) vm->status.set((root.isEmpty()?QStringLiteral("Society drive is not available."):m_recentStatus).toStdString());
    m_surface->refresh();
}
void LegacyWorkspaceBridge::scheduleSuggestedRefresh() {
    if (m_suggestedRefreshScheduled) return;
    m_suggestedRefreshScheduled=true;
    QTimer::singleShot(1500,this,[this] {
        m_suggestedRefreshScheduled=false;
        if (m_agentClient->connected() && !m_agentContainer.isEmpty() && m_surface && m_surface->model() &&
            m_surface->model()->suggestedActive.get()) m_agentClient->request("suggestedRefresh");
    });
}
void LegacyWorkspaceBridge::refreshRecent() {
    if (m_recentRefreshScheduled) return;
    m_recentRefreshScheduled=true;
    QTimer::singleShot(1500,this,[this] {
        m_recentRefreshScheduled=false;
        if (m_agentClient->connected() && !m_agentContainer.isEmpty()) m_agentClient->request("recentRefresh");
    });
}
void LegacyWorkspaceBridge::pollFileJobs() {
    if (!m_agentClient->connected() || m_agentContainer.isEmpty()) return;
    for (auto it=m_fileJobs.cbegin();it!=m_fileJobs.cend();++it) {
        if (m_fileQueries.values().contains(it.key())) continue;
        QString requestId;
        if (m_agentClient->request("fileJobStatus",{{"jobId",it.key()}},false,&requestId)) m_fileQueries.insert(requestId,it.key());
    }
}
bool LegacyWorkspaceBridge::submitFileOperation(const QString &action,const QString &path,const QString &argument,bool externalCopy,QString *requestId) {
    if (requestId) requestId->clear();
    if (!m_dashboard || !m_surface || !m_surface->model()) return false;
    auto *vm=m_surface->model();
    const auto root=m_dashboard->property("filesRoot").toString();
    const bool createRoot=action=="create-directory" && path.isEmpty();
    const bool managed=insideFilesRoot(root,path);
    const bool external=externalCopy && action=="copy" && QDir::isAbsolutePath(path);
    if (!m_agentClient->connected() || m_agentContainer.isEmpty() || root.isEmpty() || (!createRoot && !managed && !external)) {
        vm->recordOperationError("The selected file is unavailable in this Society drive."); return false;
    }
    if (m_fileRequests.size()+m_fileJobs.size()>=64) {
        vm->recordOperationError("The remaining files were not submitted: too many file jobs are pending."); return false;
    }
    // The container's copy job accepts an absolute source, including when the
    // source is already in this drive; other managed jobs use logical paths.
    const QString source=createRoot?QStringLiteral("Files"):
        (action=="copy"?QDir::cleanPath(path):QStringLiteral("Files/")+QDir(root).relativeFilePath(QDir::cleanPath(path)));
    if ((action=="move" || action=="copy") && argument!="Files" && !argument.startsWith("Files/")) {
        vm->recordOperationError("Choose a destination inside Society/Files."); return false;
    }
    QJsonObject args{{"action",action},{"path",source}};
    if (!argument.isEmpty()) args.insert("argument",argument);
    QString id;
    if (!m_agentClient->request("fileOperation",args,true,&id)) {
        vm->recordOperationError("The file operation could not be submitted."); return false;
    }
    if (requestId) *requestId=id;
    m_fileRequests.insert(id,createRoot?root:path); vm->operationStatus.set("Submitting file operation…"); return true;
}
void LegacyWorkspaceBridge::completeCutJob(const QString &jobId,bool succeeded) {
    const auto cut=m_cutJobs.take(jobId);
    if (cut.token.isEmpty() || m_cut.token()!=cut.token.toStdString() || m_cut.drive()!=cut.drive.toStdString()) return;
    if (!succeeded) { m_cutUncertain.insert(cut.path); return; }
    auto *clipboard=QGuiApplication::clipboard(); const auto *mime=clipboard?clipboard->mimeData():nullptr;
    std::vector<std::string> paths;
    if (mime) for (const auto &url:mime->urls()) {
        if (!url.isLocalFile()) { paths.clear(); break; }
        paths.push_back(url.toLocalFile().toStdString());
    }
    const bool ownsClipboard=mime && m_cut.matches(mime->data("application/x-society-cut-token").toStdString(),cut.drive.toStdString(),paths);
    m_cut.completed(cut.path.toStdString()); m_cutUncertain.remove(cut.path);
    if (!ownsClipboard || !clipboard) return;
    if (m_cut.paths().empty()) { clipboard->clear(); return; }
    auto *remaining=new QMimeData; QList<QUrl> urls;
    for (const auto &path:m_cut.paths()) urls.push_back(QUrl::fromLocalFile(QString::fromStdString(path)));
    remaining->setUrls(urls); remaining->setData("application/x-society-cut-token",cut.token.toUtf8());
    clipboard->setMimeData(remaining);
}
void LegacyWorkspaceBridge::recordRecentOpen(const QString &path) {
    if (m_dashboard && m_agentClient->connected() && insideFilesRoot(m_dashboard->property("filesRoot").toString(),path))
        m_agentClient->request("recentOpened",{{"path",QDir::cleanPath(path)}});
}
void LegacyWorkspaceBridge::refreshGroups() {
    if (!m_surface || !m_surface->model() || !m_navigation) return;
    m_surface->model()->setGroups(groups(m_navigation->property("guilds").toList()), groups(m_navigation->property("organizations").toList()));
    m_surface->refresh();
}
void LegacyWorkspaceBridge::clearPendingOpen() {
    m_pendingOpen.clear(); m_pendingRoot.clear(); m_openDirectory.clear(); m_openRelative.clear();
    m_openGeneration.clear(); m_openDirectoryRequest.clear(); m_openActivateRequest.clear();
}
void LegacyWorkspaceBridge::failPendingOpen(const QString &reason) {
    clearPendingOpen();
    if (m_surface && m_surface->model()) {
        const auto message=reason.isEmpty()?tr("The selected file could not be prepared."):reason;
        m_surface->model()->status.set(message.toStdString());
        m_surface->model()->recordOperationError(message.toStdString()); m_surface->refresh();
    }
}
void LegacyWorkspaceBridge::advancePendingOpen(const QJsonObject &state) {
    if (m_pendingOpen.isEmpty() || m_openGeneration.isEmpty() || !m_openActivateRequest.isEmpty()
        || state.value("path").toString()!=m_openDirectory || state.value("generation").toString()!=m_openGeneration) return;
    const auto status=state.value("status").toString();
    if (status=="loading") return;
    const auto revision=state.value("revision").toString();
    if (status!="ready" || revision.isEmpty()) { failPendingOpen(tr("The selected directory is unavailable.")); return; }
    if (!m_agentClient->request("directoryActivate",{{"path",m_openRelative},{"generation",m_openGeneration},{"revision",revision}},false,&m_openActivateRequest))
        failPendingOpen(tr("The file preparation request could not be sent."));
}

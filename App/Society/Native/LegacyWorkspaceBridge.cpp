#include "LegacyWorkspaceBridge.h"
#include "NativeWorkspaceSurface.h"
#include "Society/Files/ModelBackendClient.h"
#include <StorageDirectoryModel.h>
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
      m_directory(new iiSocietyContainer::StorageDirectoryModel(this)), m_agentClient(new ModelBackendClient(this)) {
    setObjectName(QStringLiteral("societyNativeWorkspaceBridge"));
    surface->setObjectName(QStringLiteral("nativeWorkspaceSurface")); surface->setZ(100);
    m_files = dashboard->property("viewModel").value<QObject *>();
    m_navigation = dashboard->property("navigation").value<QObject *>();
    const QPointer<LegacyWorkspaceBridge> guard(this);
    auto *agent = surface->model();
    agent->onSend = [guard](const std::string &prompt, const std::vector<NativeWorkspaceFile> &context) {
        if (!guard || !guard->m_surface || !guard->m_surface->model()) return false;
        if (!context.empty()) {
            guard->m_surface->model()->agentStatus.set("File context is not available yet. Remove attachments to send a text message.");
            guard->m_surface->refresh(); return false;
        }
        guard->m_agentDraft=QString::fromStdString(prompt);
        if (guard->m_agentClient->request("agentSend",{{"prompt",guard->m_agentDraft}},true)) return true;
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
    agent->onCopyFiles = [](const std::vector<std::string> &paths) {
        auto *clipboard=QGuiApplication::clipboard(); if (!clipboard) return;
        auto *mime=new QMimeData; QList<QUrl> urls;
        for (const auto &path:paths) urls.push_back(QUrl::fromLocalFile(QString::fromStdString(path)));
        mime->setUrls(urls); clipboard->setMimeData(mime);
    };
    agent->onPasteFiles = [guard](const std::string &destination) {
        if (!guard || !guard->m_surface || !guard->m_surface->model()) return false;
        const auto *clipboard=QGuiApplication::clipboard(); const auto *mime=clipboard?clipboard->mimeData():nullptr;
        if (!mime || !mime->hasUrls()) { guard->m_surface->model()->recordOperationError("The clipboard has no files to paste."); return false; }
        bool submitted=false;
        for (const auto &url:mime->urls()) {
            if (!url.isLocalFile() || !QDir::isAbsolutePath(url.toLocalFile())) {
                guard->m_surface->model()->recordOperationError("A clipboard item is not a local file and was not submitted."); continue;
            }
            if (!guard->submitFileOperation("copy",url.toLocalFile(),QString::fromStdString(destination),true)) break;
            submitted=true;
        }
        return submitted;
    };
    if (auto *clipboard=QGuiApplication::clipboard()) {
        const auto refreshClipboard=[this,clipboard] {
            if (!m_surface || !m_surface->model()) return;
            const auto *mime=clipboard->mimeData();
            m_surface->model()->setClipboardHasFiles(mime && mime->hasUrls());
            m_surface->refresh();
        };
        connect(clipboard,&QClipboard::dataChanged,this,[this,refreshClipboard] { QTimer::singleShot(0,this,refreshClipboard); });
        refreshClipboard();
    }
    agent->onFileAction = [guard](const std::string &action, const std::string &locator, const std::string &argument) {
        return guard && guard->submitFileOperation(QString::fromStdString(action),QString::fromStdString(locator),QString::fromStdString(argument));
    };
    m_fileJobTimer.setInterval(1200);
    connect(&m_fileJobTimer,&QTimer::timeout,this,&LegacyWorkspaceBridge::pollFileJobs);
    m_fileJobTimer.start();
    connect(m_agentClient,&ModelBackendClient::fileResultReceived,this,[this](const QString &operation,const QString &id,const QJsonObject &result) {
        if (!m_surface || !m_surface->model()) return;
        auto *vm=m_surface->model();
        if (operation=="fileOperation") {
            if (!m_fileRequests.contains(id)) return;
            const auto path=m_fileRequests.take(id), job=result.value("jobId").toString();
            if (!result.value("accepted").toBool() || job.isEmpty()) {
                const auto error=result.value("error").toString();
                vm->recordOperationError((path+": "+(error.isEmpty()?QStringLiteral("File operation was rejected."):error)).toStdString());
            } else {
                m_fileJobs.insert(job,path); vm->operationStatus.set("File operation queued · job "+job.toStdString());
                pollFileJobs();
            }
        } else if (operation=="fileJobStatus") {
            const auto jobId=m_fileQueries.take(id);
            if (jobId.isEmpty() || !m_fileJobs.contains(jobId)) return;
            if (!result.value("accepted").toBool()) {
                vm->recordOperationError((QStringLiteral("Could not check job ")+jobId+QStringLiteral(": ")+result.value("error").toString()).toStdString());
            } else {
                const auto job=result.value("job").toObject();
                const auto state=job.value("state").toString();
                if (state=="succeeded") { vm->operationStatus.set("File operation completed · job "+jobId.toStdString()); m_fileJobs.remove(jobId); }
                else if (state=="failed" || state=="partial" || state=="cancelled" || state=="attention") {
                    const auto error=job.value("error").toString();
                    vm->recordOperationError((m_fileJobs.value(jobId)+QStringLiteral(": ")+(error.isEmpty()?state:error)+QStringLiteral(" · job ")+jobId).toStdString());
                    if (state!="attention") m_fileJobs.remove(jobId);
                } else vm->operationStatus.set((QStringLiteral("File job ")+state+QStringLiteral(" · ")+jobId).toStdString());
            }
        }
        m_surface->refresh();
    });
    connect(m_agentClient,&ModelBackendClient::fileJobChanged,this,[this](const QJsonObject &event) {
        const auto job=event.value("jobId").toString();
        if (!m_fileJobs.contains(job) || event.value("state").toString()!="finished" || !m_surface || !m_surface->model()) return;
        const auto error=event.value("error").toString();
        if (error.isEmpty()) {
            m_surface->model()->operationStatus.set("File operation completed · job "+job.toStdString());
            m_fileJobs.remove(job);
        } else m_surface->model()->recordOperationError((m_fileJobs.value(job)+QStringLiteral(": ")+error+QStringLiteral(" · job ")+job).toStdString());
        m_surface->refresh();
    });
    connect(m_agentClient,&ModelBackendClient::connectedChanged,this,[this] {
        if (!m_surface || !m_surface->model()) return;
        m_surface->model()->setFileActionsAvailable(m_agentClient->connected() && !m_agentContainer.isEmpty());
        if (m_agentClient->connected() && !m_agentContainer.isEmpty()) {
            m_agentClient->request("agentSubscribe"); m_agentClient->request("suggestedSubscribe"); m_agentClient->request("recentSubscribe");
        } else {
            if (!m_fileRequests.isEmpty()) m_surface->model()->recordOperationError("Connection interrupted. Check submitted jobs before retrying.");
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
            files.push_back({path.toStdString(),item.value("name").toString().toStdString(),
                millis>0?time.toLocalTime().toString("dd MMM yyyy, HH:mm").toStdString():std::string{},
                kind(QFileInfo(path).suffix(),false).toStdString(),size<0?std::string("—"):QLocale().formattedDataSize(size).toStdString(),false,millis,size});
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
    connect(m_directory, &iiSocietyContainer::StorageDirectoryModel::activated, this, &LegacyWorkspaceBridge::directoryActivated);
    connect(m_directory, &iiSocietyContainer::StorageDirectoryModel::downloadFailed, this, [this](const QString &error) {
        m_pendingOpen.clear(); m_pendingRoot.clear();
        if (m_surface && m_surface->model()) { m_surface->model()->status.set(error.toStdString()); m_surface->refresh(); }
    });
    connect(surface, &NativeWorkspaceSurface::pageRequested, this, &LegacyWorkspaceBridge::navigate);
    connect(surface, &NativeWorkspaceSurface::openRequested, this, [this](const QString &path) {
        if (!m_directory || !m_dashboard) return;
        const auto root = m_dashboard->property("filesRoot").toString();
        if (!insideFilesRoot(root, path)) {
            if (m_surface && m_surface->model()) { m_surface->model()->status.set("The file is not in the current Society drive."); m_surface->refresh(); }
            return;
        }
        m_pendingRoot = QDir::cleanPath(root); m_pendingOpen = QDir::cleanPath(path);
        // Suggested can reference any indexed subdirectory. The container
        // validates the current path before opening it.
        m_directory->openPath(m_pendingOpen, false);
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
        m_agentContainer=agentContainer; m_agentDraft.clear();
        if (!m_fileRequests.isEmpty() || !m_fileJobs.isEmpty()) vm->operationStatus.set("File jobs continue in the previous Society drive.");
        m_fileRequests.clear(); m_fileQueries.clear(); m_fileJobs.clear();
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
        m_pendingOpen.clear(); m_pendingRoot.clear();
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
bool LegacyWorkspaceBridge::submitFileOperation(const QString &action,const QString &path,const QString &argument,bool externalCopy) {
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
    QString source=createRoot?QStringLiteral("Files"):(managed?QStringLiteral("Files/")+QDir(root).relativeFilePath(QDir::cleanPath(path)):QDir::cleanPath(path));
    if ((action=="move" || action=="copy") && argument!="Files" && !argument.startsWith("Files/")) {
        vm->recordOperationError("Choose a destination inside Society/Files."); return false;
    }
    QJsonObject args{{"action",action},{"path",source}};
    if (!argument.isEmpty()) args.insert("argument",argument);
    QString id;
    if (!m_agentClient->request("fileOperation",args,true,&id)) {
        vm->recordOperationError("The file operation could not be submitted."); return false;
    }
    m_fileRequests.insert(id,createRoot?root:path); vm->operationStatus.set("Submitting file operation…"); return true;
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
void LegacyWorkspaceBridge::directoryActivated(const QString &path, bool directory) {
    if (!m_dashboard || path != m_pendingOpen || m_pendingRoot.isEmpty()
        || QDir::cleanPath(m_dashboard->property("filesRoot").toString()) != m_pendingRoot) return;
    m_pendingOpen.clear(); m_pendingRoot.clear();
    if (directory) {
        if (!QMetaObject::invokeMethod(m_dashboard, "navigateDashboard", Q_ARG(QString, path)))
            QMetaObject::invokeMethod(m_dashboard, "navigateDashboard", Q_ARG(QVariant, QVariant(path)));
    } else QMetaObject::invokeMethod(m_dashboard, "fileRequested", Q_ARG(QString, path));
}

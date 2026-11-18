#pragma once

#include "NativeWorkspaceClipboard.h"
#include <QObject>
#include <QPointer>
#include <QHash>
#include <QSet>

class NativeWorkspaceSurface;
class ModelBackendClient;
class QJsonObject;
class QQuickItem;
class QQuickWindow;

// Temporary host adapter; all Workspace controls are LVUI/iiGUI widgets.
class LegacyWorkspaceBridge final : public QObject {
    Q_OBJECT
public:
    static NativeWorkspaceSurface *install(QQuickWindow *window);
    ~LegacyWorkspaceBridge() override;
private slots:
    void synchronize();
    void refreshRecent();
    void refreshGroups();
    void scheduleSuggestedRefresh();
    void recordRecentOpen(const QString &path);
    void pollFileJobs();
private:
    LegacyWorkspaceBridge(QQuickWindow *, QQuickItem *dashboard, QQuickItem *sidebar,
                          QQuickItem *content, QQuickItem *scroll, NativeWorkspaceSurface *);
    void navigate(int page);
    void clearPendingOpen();
    void failPendingOpen(const QString &reason);
    void advancePendingOpen(const QJsonObject &state);
    bool submitFileOperation(const QString &action, const QString &path, const QString &argument, bool externalCopy = false, QString *requestId = nullptr);
    void completeCutJob(const QString &jobId, bool succeeded);
    struct CutTransfer { QString token, drive, path; };
    QPointer<QQuickItem> m_dashboard, m_sidebar, m_content, m_scroll;
    QPointer<NativeWorkspaceSurface> m_surface;
    QPointer<QObject> m_files, m_navigation;
    ModelBackendClient *m_agentClient = nullptr;
    QString m_agentContainer, m_agentDraft, m_suggestedStatus, m_recentStatus;
    QString m_pendingOpen, m_pendingRoot;
    QString m_openDirectory, m_openRelative, m_openGeneration, m_openDirectoryRequest, m_openActivateRequest;
    QString m_inspectionId, m_inspectionPath;
    QHash<QString, QString> m_fileRequests, m_fileJobs, m_fileQueries;
    QHash<QString, QString> m_addRequests;
    QString m_addModelJob;
    QSet<QString> m_recordedFileJobs;
    QHash<QString, CutTransfer> m_cutRequests, m_cutJobs;
    QSet<QString> m_cutUncertain;
    NativeWorkspaceCutState m_cut;
    int m_homePage = 0;
    bool m_active = false, m_suggestedRefreshScheduled = false, m_recentRefreshScheduled = false;
};

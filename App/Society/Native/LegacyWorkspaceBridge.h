#pragma once

#include <QObject>
#include <QPointer>
#include <QHash>
#include <QTimer>

class NativeWorkspaceSurface;
class ModelBackendClient;
class QQuickItem;
class QQuickWindow;
namespace iiSocietyContainer { class StorageDirectoryModel; }

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
    void directoryActivated(const QString &path, bool directory);
    void recordRecentOpen(const QString &path);
    void pollFileJobs();
private:
    LegacyWorkspaceBridge(QQuickWindow *, QQuickItem *dashboard, QQuickItem *sidebar,
                          QQuickItem *content, QQuickItem *scroll, NativeWorkspaceSurface *);
    void navigate(int page);
    bool submitFileOperation(const QString &action, const QString &path, const QString &argument, bool externalCopy = false);
    QPointer<QQuickItem> m_dashboard, m_sidebar, m_content, m_scroll;
    QPointer<NativeWorkspaceSurface> m_surface;
    QPointer<QObject> m_files, m_navigation;
    iiSocietyContainer::StorageDirectoryModel *m_directory = nullptr;
    ModelBackendClient *m_agentClient = nullptr;
    QString m_agentContainer, m_agentDraft, m_suggestedStatus, m_recentStatus;
    QString m_pendingOpen, m_pendingRoot;
    QHash<QString, QString> m_fileRequests, m_fileJobs, m_fileQueries;
    QTimer m_fileJobTimer;
    int m_homePage = 0;
    bool m_active = false, m_suggestedRefreshScheduled = false, m_recentRefreshScheduled = false;
};

#pragma once

#include <QObject>
#include <QPointer>

class NativeNavigationSurface;
class NativeNoticeSurface;
class NativeErrorNotificationSurface;
class NativeSpaceSurface;
class NativeAppsSurface;
class QAbstractItemModel;
class QQuickItem;
class QQuickWindow;

// Connect the LVUI navigation to the existing shell until that shell is migrated.
class LegacyNavigationBridge final : public QObject {
    Q_OBJECT
public:
    static NativeNavigationSurface *install(QQuickWindow *window);

private:
    LegacyNavigationBridge(QQuickWindow *window, QQuickItem *view, QQuickItem *viewport,
                           QQuickItem *legacy, NativeNavigationSurface *surface);
    void navigate(const QString &destination);
    void positionSurface();
    void showOverview(bool scrollToHosting);
    /// Complete a pending Apps request once the Environment page is available.
    void showApps();
    /// Align the Apps surface with the shell's shared content viewport.
    void positionApps();
    /// Create and position the LVUI Space surface in the existing shell.
    bool showSpace();
    void positionSpace();
    // Append newly recorded errors without changing the notification center.
    void appendErrors(QAbstractItemModel *model, int first, int last);
    // Anchor the nonmodal notification surface inside the active page viewport.
    void positionErrorNotification();

private slots:
    void synchronize();
    void environmentLoaded();
    void environmentPageChanged();
    void synchronizeSpace();
    /// Leave Space even when a request selects the shell's already-current tab.
    void tabRequested(const QString &page);
    /// Present legacy shell notice requests with the LVUI alert when available.
    void presentNotice();
    /// Keep the native alert surface aligned to the window's content area.
    void positionNotice();

private:
    QPointer<QQuickItem> m_view;
    QPointer<QQuickItem> m_viewport;
    QPointer<NativeNavigationSurface> m_surface;
    QPointer<QQuickWindow> m_window;
    QPointer<QObject> m_legacyNotice;
    QPointer<NativeNoticeSurface> m_noticeSurface;
    QPointer<NativeErrorNotificationSurface> m_errorNotification;
    QPointer<QQuickItem> m_errorAnchor;
    QPointer<QQuickItem> m_onboardingAnchor;
    QPointer<QObject> m_environment;
    QPointer<QObject> m_drive;
    QPointer<QQuickItem> m_spaceAnchor;
    QPointer<NativeSpaceSurface> m_spaceSurface;
    QPointer<NativeAppsSurface> m_appsSurface;
    bool m_appsActive = false;
    bool m_showingApps = false;
    bool m_spaceActive = false;
    bool m_hosting = false;
    bool m_pendingHosting = false;
    bool m_pendingApps = false;
};

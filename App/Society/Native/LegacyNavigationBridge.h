#pragma once

#include <QObject>
#include <QPointer>

class NativeNavigationSurface;
class NativeSpaceSurface;
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
    /// Create and position the LVUI Space surface in the existing shell.
    bool showSpace();
    void positionSpace();

private slots:
    void synchronize();
    void environmentLoaded();
    void environmentPageChanged();
    void synchronizeSpace();
    /// Leave Space even when a request selects the shell's already-current tab.
    void tabRequested(const QString &page);

private:
    QPointer<QQuickItem> m_view;
    QPointer<QQuickItem> m_viewport;
    QPointer<NativeNavigationSurface> m_surface;
    QPointer<QObject> m_environment;
    QPointer<QObject> m_drive;
    QPointer<QQuickItem> m_spaceAnchor;
    QPointer<NativeSpaceSurface> m_spaceSurface;
    bool m_spaceActive = false;
    bool m_hosting = false;
    bool m_pendingHosting = false;
};

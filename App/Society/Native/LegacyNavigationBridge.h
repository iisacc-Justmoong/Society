#pragma once

#include <QObject>
#include <QPointer>

class NativeNavigationSurface;
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

private slots:
    void synchronize();
    void environmentLoaded();
    void environmentPageChanged();

private:
    QPointer<QQuickItem> m_view;
    QPointer<QQuickItem> m_viewport;
    QPointer<NativeNavigationSurface> m_surface;
    QPointer<QObject> m_environment;
    bool m_hosting = false;
    bool m_pendingHosting = false;
};

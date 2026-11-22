#include "LegacyNavigationBridge.h"
#include "NativeNavigationSurface.h"
#include "Society/Space/NativeSpaceSurface.h"

#include <QCoreApplication>
#include <QQmlProperty>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>
#include <algorithm>

NativeNavigationSurface *LegacyNavigationBridge::install(QQuickWindow *window)
{
    if (!window) return nullptr;
    if (auto *existing = window->findChild<LegacyNavigationBridge *>(QStringLiteral("societyNativeNavigationBridge")))
        return existing->m_surface;
    auto *view = window->findChild<QQuickItem *>(QStringLiteral("societyContent"));
    auto *viewport = window->findChild<QQuickItem *>(QStringLiteral("dashboardTabViewport"));
    auto *legacy = window->findChild<QQuickItem *>(QStringLiteral("dashboardTabs"));
    if (!view || !viewport || !legacy || !legacy->parentItem()) {
        qWarning("Unable to find Society's navigation viewport");
        return nullptr;
    }
    auto *surface = new NativeNavigationSurface(legacy->parentItem());
    if (!surface->ready()) {
        qWarning("Unable to initialize Society LVUI navigation: %s", qPrintable(surface->errorString()));
        delete surface;
        return nullptr;
    }
    new LegacyNavigationBridge(window, view, viewport, legacy, surface);
    return surface;
}

LegacyNavigationBridge::LegacyNavigationBridge(QQuickWindow *window, QQuickItem *view,
    QQuickItem *viewport, QQuickItem *legacy, NativeNavigationSurface *surface)
    : QObject(window), m_view(view), m_viewport(viewport), m_surface(surface)
{
    setObjectName(QStringLiteral("societyNativeNavigationBridge"));
    // The legacy group is retained only as a dormant layout dependency of the
    // old shell. It is neither displayed nor interactive or accessible.
    legacy->setObjectName(QStringLiteral("legacyNavigationSegments"));
    legacy->setVisible(false);
    legacy->setEnabled(false);
    surface->setObjectName(QStringLiteral("dashboardTabs"));
    surface->setSize(QSizeF(surface->implicitWidth(), surface->implicitHeight()));
    QQmlProperty(viewport, QStringLiteral("implicitWidth")).write(surface->implicitWidth());
    QQmlProperty(viewport, QStringLiteral("implicitHeight")).write(surface->implicitHeight());
    QQmlProperty(viewport, QStringLiteral("contentWidth")).write(surface->implicitWidth());
    connect(viewport, &QQuickItem::heightChanged, this, &LegacyNavigationBridge::positionSurface);
    connect(surface, &NativeNavigationSurface::requested, this, &LegacyNavigationBridge::navigate);
    connect(view, SIGNAL(selectedTabChanged()), this, SLOT(synchronize()));
    connect(view, SIGNAL(tabRequested(QString)), this, SLOT(tabRequested(QString)));
    m_drive = view->property("drive").value<QObject *>();
    if (m_drive) {
        connect(m_drive, SIGNAL(locationChanged()), this, SLOT(synchronizeSpace()));
        connect(m_drive, SIGNAL(contentsChanged()), this, SLOT(synchronizeSpace()));
    }
    if (auto *loader = view->findChild<QObject *>(QStringLiteral("environmentLoader")))
        connect(loader, SIGNAL(loaded()), this, SLOT(environmentLoaded()));
    positionSurface();
    synchronize();
    environmentLoaded();
}

void LegacyNavigationBridge::positionSurface()
{
    if (m_surface && m_viewport)
        m_surface->setPosition(QPointF(0, (m_viewport->height() - m_surface->height()) / 2));
}

void LegacyNavigationBridge::synchronize()
{
    if (!m_view || !m_surface) return;
    const auto page = m_view->property("selectedTab").toString();
    if (m_spaceActive) {
        m_spaceActive = false;
        if (m_spaceSurface) m_spaceSurface->setVisible(false);
    }
    if (page != QLatin1String("Environment")) m_hosting = m_pendingHosting = false;
    m_surface->setCurrentPage(m_hosting ? QStringLiteral("Hosting") : page);
}

void LegacyNavigationBridge::environmentLoaded()
{
    if (!m_view) return;
    auto *environment = m_view->findChild<QObject *>(QStringLiteral("environmentView"));
    if (environment && environment != m_environment) {
        if (m_environment) disconnect(m_environment, nullptr, this, nullptr);
        m_environment = environment;
        connect(environment, SIGNAL(pageChanged()), this, SLOT(environmentPageChanged()));
    }
    if (m_pendingHosting && m_environment) showOverview(true);
}

void LegacyNavigationBridge::environmentPageChanged()
{
    if (m_hosting && m_environment && m_environment->property("page").toString() != QLatin1String("Overview")) {
        m_hosting = m_pendingHosting = false;
        synchronize();
    }
}

void LegacyNavigationBridge::showOverview(bool scrollToHosting)
{
    if (!m_environment) return;
    m_environment->setProperty("page", QStringLiteral("Overview"));
    const QPointer<LegacyNavigationBridge> guard(this);
    QTimer::singleShot(0, this, [guard, scrollToHosting] {
        if (!guard || !guard->m_environment || !guard->m_view ||
            guard->m_view->property("selectedTab").toString() != QLatin1String("Environment") ||
            (scrollToHosting && !guard->m_pendingHosting)) return;
        auto *scroll = guard->m_environment->findChild<QQuickItem *>(QStringLiteral("environmentScroll"));
        auto *flick = scroll ? qvariant_cast<QQuickItem *>(scroll->property("contentItem")) : nullptr;
        if (!flick) return;
        qreal position = 0;
        if (scrollToHosting) {
            const auto heading = QCoreApplication::translate("EnvironmentView", "Hosting");
            for (auto *item : guard->m_environment->findChildren<QQuickItem *>()) {
                if (item->isVisible() && item->property("text").toString() == heading) {
                    position = item->mapToItem(flick, QPointF()).y() + flick->property("contentY").toReal();
                    break;
                }
            }
        }
        const auto maximum = std::max(0.0, flick->property("contentHeight").toReal() - flick->height());
        flick->setProperty("contentY", std::clamp(position, 0.0, maximum));
        guard->m_pendingHosting = false;
    });
}

void LegacyNavigationBridge::tabRequested(const QString &page)
{
    if (!m_spaceActive) return;
    m_spaceActive = false;
    if (m_spaceSurface) m_spaceSurface->setVisible(false);
    if (m_surface) m_surface->setCurrentPage(page);
}

bool LegacyNavigationBridge::showSpace()
{
    if (!m_view) return false;
    if (!m_spaceSurface) {
        m_spaceAnchor = m_view->findChild<QQuickItem *>(QStringLiteral("dashboardView"));
        if (!m_spaceAnchor || !m_spaceAnchor->parentItem()) return false;
        auto *surface = new NativeSpaceSurface(m_spaceAnchor->parentItem());
        if (!surface->ready()) {
            qWarning("Unable to initialize Society LVUI Space: %s", qPrintable(surface->errorString()));
            delete surface; return false;
        }
        m_spaceSurface = surface;
        surface->setObjectName(QStringLiteral("nativeSpaceSurface"));
        surface->setZ(200);
        connect(m_spaceAnchor, &QQuickItem::xChanged, this, &LegacyNavigationBridge::positionSpace);
        connect(m_spaceAnchor, &QQuickItem::yChanged, this, &LegacyNavigationBridge::positionSpace);
        connect(m_spaceAnchor, &QQuickItem::widthChanged, this, &LegacyNavigationBridge::positionSpace);
        connect(m_spaceAnchor, &QQuickItem::heightChanged, this, &LegacyNavigationBridge::positionSpace);
    }
    m_spaceActive = true;
    m_hosting = m_pendingHosting = false;
    positionSpace(); synchronizeSpace();
    m_spaceSurface->setVisible(true);
    m_surface->setCurrentPage(QStringLiteral("Space"));
    return true;
}

void LegacyNavigationBridge::positionSpace()
{
    if (!m_spaceSurface || !m_spaceAnchor) return;
    m_spaceSurface->setPosition(m_spaceAnchor->position());
    m_spaceSurface->setSize(m_spaceAnchor->size());
}

void LegacyNavigationBridge::synchronizeSpace()
{
    if (!m_spaceSurface) return;
    const auto root = m_drive ? m_drive->property("rootPath").toString() : QString();
    const bool available = m_drive && m_drive->property("contentsAvailable").toBool();
    if (available && !root.isEmpty()) (void)m_spaceSurface->openContainer(root);
    else m_spaceSurface->clearContainer();
}

void LegacyNavigationBridge::navigate(const QString &destination)
{
    if (!m_view) return;
    if (destination == QLatin1String("Space")) {
        if (!showSpace()) QMetaObject::invokeMethod(m_view, "featureRequested", Q_ARG(QString, destination));
        return;
    }
    if (destination == QLatin1String("People")) {
        QMetaObject::invokeMethod(m_view, "featureRequested", Q_ARG(QString, destination));
        return;
    }
    const bool leavingSpace = m_spaceActive;
    m_spaceActive = false;
    if (m_spaceSurface) m_spaceSurface->setVisible(false);
    const bool hosting = destination == QLatin1String("Hosting");
    m_hosting = m_pendingHosting = false;
    // The first segment is the Workspace entry point; the separate Hosting
    // action still opens the environment overview and its hosting section.
    const auto page = hosting ? QStringLiteral("Environment")
        : destination == QLatin1String("Environment") ? QStringLiteral("Dashboard") : destination;
    QMetaObject::invokeMethod(m_view, "tabRequested", Q_ARG(QString, page));
    if (page == QLatin1String("Environment")) {
        if (m_environment) m_environment->setProperty("page", QStringLiteral("Overview"));
        m_hosting = m_pendingHosting = hosting;
        synchronize();
        if (m_environment) showOverview(hosting);
    } else if (leavingSpace) {
        // Selecting the page beneath Space may not change selectedTab, so no
        // selectedTabChanged signal is guaranteed to update the segment.
        synchronize();
    }
}

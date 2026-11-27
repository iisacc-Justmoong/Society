#include "LegacyNavigationBridge.h"
#include "NativeNavigationSurface.h"
#include "NativeNoticeSurface.h"
#include "NativeErrorNotificationSurface.h"
#include "NativeAppsSurface.h"
#include "Society/Space/NativeSpaceSurface.h"

#include <LVUI/Popover.h>
#include <QAbstractItemModel>
#include <QCoreApplication>
#include <QQmlProperty>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScopedValueRollback>
#include <QTimer>
#include <algorithm>
#include <limits>

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
    : QObject(window), m_view(view), m_viewport(viewport), m_surface(surface), m_window(window)
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
    m_legacyNotice=window->findChild<QObject *>(QStringLiteral("dashboardNotice"));
    if(m_legacyNotice)connect(m_legacyNotice,SIGNAL(openChanged()),this,SLOT(presentNotice()));
    connect(window->contentItem(),&QQuickItem::widthChanged,this,&LegacyNavigationBridge::positionNotice);
    connect(window->contentItem(),&QQuickItem::heightChanged,this,&LegacyNavigationBridge::positionNotice);
    // The dashboard fills the shared page viewport even when another page is
    // selected, so its parent is the stable content area below the toolbar.
    m_errorAnchor=view->findChild<QQuickItem *>(QStringLiteral("dashboardView"));
    if (!m_errorAnchor) m_errorAnchor=view;
    m_onboardingAnchor=window->findChild<QQuickItem *>(QStringLiteral("containerOnboarding"));
    const auto watchAnchor=[this](QQuickItem *anchor) {
        if (!anchor) return;
        connect(anchor,&QQuickItem::xChanged,this,&LegacyNavigationBridge::positionErrorNotification);
        connect(anchor,&QQuickItem::yChanged,this,&LegacyNavigationBridge::positionErrorNotification);
        connect(anchor,&QQuickItem::widthChanged,this,&LegacyNavigationBridge::positionErrorNotification);
        connect(anchor,&QQuickItem::heightChanged,this,&LegacyNavigationBridge::positionErrorNotification);
        connect(anchor,&QQuickItem::visibleChanged,this,&LegacyNavigationBridge::positionErrorNotification);
    };
    watchAnchor(m_errorAnchor);
    watchAnchor(m_onboardingAnchor);
    if (m_errorAnchor!=view)
        connect(view,&QQuickItem::visibleChanged,this,&LegacyNavigationBridge::positionErrorNotification);
    if (auto *notifications=window->findChild<QAbstractItemModel *>(QStringLiteral("notificationCenter"))) {
        connect(notifications,&QAbstractItemModel::rowsInserted,this,
                [this,notifications](const QModelIndex &parent,int first,int last) {
            if (!parent.isValid()) appendErrors(notifications,first,last);
        });
        appendErrors(notifications,0,notifications->rowCount()-1);
    }
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

void LegacyNavigationBridge::positionErrorNotification()
{
    if (!m_errorNotification) return;
    // Onboarding is a sibling of SocietyView: keeping the surface under a
    // hidden dashboard would suppress errors during first-run setup.
    auto *anchor=m_onboardingAnchor && m_onboardingAnchor->isVisible() ? m_onboardingAnchor.data() : m_errorAnchor.data();
    if (!anchor || !anchor->parentItem()) return;
    if (m_errorNotification->parentItem()!=anchor->parentItem())
        m_errorNotification->setParentItem(anchor->parentItem());
    const auto size=anchor->size();
    const auto margin=lvui::PopoverConfiguration{}.margins;
    const auto width=std::min(std::max(0.0,size.width()-2*margin),360.0+2*margin);
    const auto height=std::max(0.0,size.height()-2*margin);
    m_errorNotification->setPosition(anchor->position()+QPointF(std::max(0.0,size.width()-margin-width),margin));
    m_errorNotification->setSize({std::max(0.0,width),std::max(0.0,height)});
}

void LegacyNavigationBridge::appendErrors(QAbstractItemModel *model,int first,int last)
{
    if (!m_window || !model || first>last) return;
    int sourceRole=-1,messageRole=-1;
    const auto roles=model->roleNames();
    for (auto it=roles.cbegin();it!=roles.cend();++it) {
        if (it.value()=="source") sourceRole=it.key();
        if (it.value()=="message") messageRole=it.key();
    }
    if (messageRole<0) return;
    for (int row=first;row<=last;++row) {
        const auto index=model->index(row,0);
        const auto message=model->data(index,messageRole).toString();
        if (message.trimmed().isEmpty()) continue;
        if (!m_errorNotification && m_errorAnchor && m_errorAnchor->parentItem()) {
            auto *surface=new NativeErrorNotificationSurface(m_errorAnchor->parentItem());
            if (!surface->ready()) {
                qWarning("Unable to initialize Society LVUI error notification: %s",qPrintable(surface->errorString()));
                delete surface;return;
            }
            surface->setObjectName(QStringLiteral("societyErrorNotification"));
            surface->setZ((std::numeric_limits<qreal>::max)());
            m_errorNotification=surface;
            positionErrorNotification();
        }
        if (m_errorNotification)
            m_errorNotification->enqueue(sourceRole<0 ? QString() : model->data(index,sourceRole).toString(),message);
    }
}

void LegacyNavigationBridge::positionNotice()
{
    if(m_window&&m_noticeSurface)m_noticeSurface->setSize(m_window->contentItem()->size());
}

void LegacyNavigationBridge::presentNotice()
{
    if(!m_window||!m_legacyNotice||!m_legacyNotice->property("open").toBool())return;
    if(!m_noticeSurface){
        auto *surface=new NativeNoticeSurface(m_window->contentItem());
        if(!surface->ready()){
            qWarning("Unable to initialize Society LVUI notice: %s",qPrintable(surface->errorString()));
            delete surface;return;
        }
        surface->setObjectName(QStringLiteral("societyNativeNotice"));
        surface->setZ((std::numeric_limits<qreal>::max)());
        m_noticeSurface=surface;
    }
    positionNotice();
    if(m_noticeSurface->present(m_legacyNotice->property("title").toString(),
                                m_legacyNotice->property("message").toString(),
                                m_legacyNotice->property("primaryText").toString()))
        m_legacyNotice->setProperty("open",false);
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
    if (m_appsActive) {
        m_appsActive = false;
        if (m_appsSurface) m_appsSurface->setVisible(false);
    }
    if (page != QLatin1String("Environment")) {
        m_hosting = m_pendingHosting = false;
        m_pendingApps = false;
    }
    const bool apps = page == QLatin1String("Environment") && m_environment
        && m_environment->property("page").toString() == QLatin1String("Apps");
    m_surface->setCurrentPage(m_hosting ? QStringLiteral("Hosting") : apps ? QStringLiteral("Apps") : page);
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
    if (m_pendingApps && m_environment) showApps();
    else if (m_pendingHosting && m_environment) showOverview(true);
    else environmentPageChanged();
}

void LegacyNavigationBridge::environmentPageChanged()
{
    if (!m_view || !m_surface || !m_environment || m_spaceActive ||
        m_view->property("selectedTab").toString() != QLatin1String("Environment")) return;
    if (m_hosting && m_environment && m_environment->property("page").toString() != QLatin1String("Overview")) {
        m_hosting = m_pendingHosting = false;
    }
    const bool apps = m_environment->property("page").toString() == QLatin1String("Apps");
    if (!apps && m_appsActive) {
        m_appsActive = false;
        if (m_appsSurface) m_appsSurface->setVisible(false);
    }
    if (apps && !m_appsActive && !m_showingApps) {
        m_pendingApps = true;
        showApps();
        return;
    }
    m_surface->setCurrentPage(m_hosting ? QStringLiteral("Hosting") : apps ? QStringLiteral("Apps") : QStringLiteral("Environment"));
}

void LegacyNavigationBridge::showApps()
{
    if (!m_pendingApps || !m_environment || !m_view ||
        m_view->property("selectedTab").toString() != QLatin1String("Environment")) return;
    m_pendingApps = false;
    QScopedValueRollback<bool> showing(m_showingApps, true);
    // The legacy environment owns the catalogue; the visible view is LVUI.
    if (!QMetaObject::invokeMethod(m_environment, "showApps", Q_ARG(QVariant, QVariant(1)))) {
        QMetaObject::invokeMethod(m_view, "featureRequested", Q_ARG(QString, QStringLiteral("Apps")));
        return;
    }
    QObject *catalogue = nullptr;
    for (auto *child : m_environment->findChildren<QObject *>()) {
        if (child->inherits("EnvironmentAppsModel")) { catalogue = child; break; }
    }
    auto *anchor = m_view->findChild<QQuickItem *>(QStringLiteral("dashboardView"));
    if (!catalogue || !anchor || !anchor->parentItem()) {
        environmentPageChanged();
        QMetaObject::invokeMethod(m_view, "featureRequested", Q_ARG(QString, QStringLiteral("Apps")));
        return;
    }
    if (!m_appsSurface) {
        auto *surface = new NativeAppsSurface(anchor->parentItem());
        if (!surface->ready()) {
            qWarning("Unable to initialize Society LVUI Apps: %s", qPrintable(surface->errorString()));
            delete surface;
            environmentPageChanged();
            QMetaObject::invokeMethod(m_view, "featureRequested", Q_ARG(QString, QStringLiteral("Apps")));
            return;
        }
        m_appsSurface = surface;
        surface->setObjectName(QStringLiteral("nativeAppsSurface"));
        surface->setZ(200);
        m_spaceAnchor = anchor;
        connect(anchor, &QQuickItem::xChanged, this, &LegacyNavigationBridge::positionApps);
        connect(anchor, &QQuickItem::yChanged, this, &LegacyNavigationBridge::positionApps);
        connect(anchor, &QQuickItem::widthChanged, this, &LegacyNavigationBridge::positionApps);
        connect(anchor, &QQuickItem::heightChanged, this, &LegacyNavigationBridge::positionApps);
    }
    m_appsSurface->setEnvironment(m_environment);
    m_appsSurface->setCatalogue(catalogue);
    positionApps();
    m_appsActive = true;
    m_appsSurface->setVisible(true);
    environmentPageChanged();
}

void LegacyNavigationBridge::positionApps()
{
    if (!m_appsSurface || !m_spaceAnchor) return;
    m_appsSurface->setPosition(m_spaceAnchor->position());
    m_appsSurface->setSize(m_spaceAnchor->size());
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
    if (m_appsActive) {
        m_appsActive = false;
        if (m_appsSurface) m_appsSurface->setVisible(false);
    }
    if (!m_spaceActive) return;
    m_spaceActive = false;
    if (m_spaceSurface) m_spaceSurface->setVisible(false);
    if (m_surface) m_surface->setCurrentPage(page);
}

bool LegacyNavigationBridge::showSpace()
{
    if (!m_view) return false;
    m_appsActive = false;
    if (m_appsSurface) m_appsSurface->setVisible(false);
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
    m_pendingApps = false;
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
    if (destination == QLatin1String("Service") || destination == QLatin1String("Community") ||
        destination == QLatin1String("Learning")) {
        QMetaObject::invokeMethod(m_view, "featureRequested", Q_ARG(QString, destination));
        return;
    }
    const bool leavingSpace = m_spaceActive;
    m_spaceActive = false;
    if (m_spaceSurface) m_spaceSurface->setVisible(false);
    const bool hosting = destination == QLatin1String("Hosting");
    m_hosting = m_pendingHosting = false;
    const bool apps = destination == QLatin1String("Apps");
    m_pendingApps = apps;
    // The first segment is the Workspace entry point; the separate Hosting
    // action still opens the environment overview and its hosting section.
    const auto page = (hosting || apps) ? QStringLiteral("Environment")
        : destination == QLatin1String("Environment") ? QStringLiteral("Dashboard") : destination;
    QMetaObject::invokeMethod(m_view, "tabRequested", Q_ARG(QString, page));
    if (apps) {
        environmentLoaded();
    } else if (page == QLatin1String("Environment")) {
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

#include "NativeNavigationSurface.h"
#include "NativeNavigation.generated.h"
#include "QtPaintDevice.h"
#include <LVUI>
#include <iiGUI>
#include <QAccessible>
#include <QAccessibleObject>
#include <QFocusEvent>
#include <QFile>
#include <QHoverEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QQuickWindow>
#include <array>
#include <mutex>

static void initializeNavigationIcons() { Q_INIT_RESOURCE(NavigationIcons); }

namespace {
using View = lvui_generated::NativeNavigation;
using Action = NativeNavigationViewModel::Action;
constexpr std::array<const char *, 7> ids{"environment", "tasks", "library", "space", "browse", "people", "hosting"};

QString destination(Action action) {
    switch (action) {
    case Action::Environment: return QStringLiteral("Environment");
    case Action::Tasks: return QStringLiteral("Tools");
    case Action::Library: return QStringLiteral("Storage");
    case Action::Space: return QStringLiteral("Space");
    case Action::Browse: return QStringLiteral("Browse");
    case Action::People: return QStringLiteral("People");
    case Action::Hosting: return QStringLiteral("Hosting");
    }
    return {};
}

ii::gui::WidgetKey key(int value) {
    switch (value) {
    case Qt::Key_Tab: return ii::gui::WidgetKey::tab;
    case Qt::Key_Return: case Qt::Key_Enter: return ii::gui::WidgetKey::enter;
    case Qt::Key_Space: return ii::gui::WidgetKey::space;
    case Qt::Key_Left: return ii::gui::WidgetKey::left;
    case Qt::Key_Right: return ii::gui::WidgetKey::right;
    case Qt::Key_Home: return ii::gui::WidgetKey::home;
    case Qt::Key_End: return ii::gui::WidgetKey::end;
    default: return ii::gui::WidgetKey::none;
    }
}

class SegmentNode final : public QObject {
public:
    SegmentNode(NativeNavigationSurface *surface, int index) : QObject(surface), surface(surface), index(index) {}
    QPointer<NativeNavigationSurface> surface;
    int index;
};

class SurfaceAccessible final : public QAccessibleObject {
public:
    explicit SurfaceAccessible(NativeNavigationSurface *surface) : QAccessibleObject(surface), m_surface(surface) {}
    QWindow *window() const override { return m_surface ? m_surface->window() : nullptr; }
    QAccessibleInterface *parent() const override {
        return m_surface && m_surface->parentItem() ? QAccessible::queryAccessibleInterface(m_surface->parentItem()) : nullptr;
    }
    QAccessibleInterface *child(int index) const override {
        auto *item = m_surface ? m_surface->accessibleItem(index) : nullptr;
        return item ? QAccessible::queryAccessibleInterface(item) : nullptr;
    }
    int childCount() const override { return m_surface ? int(ids.size()) : 0; }
    int indexOfChild(const QAccessibleInterface *item) const override {
        for (int i = 0; i < childCount(); ++i) if (child(i) == item) return i;
        return -1;
    }
    QAccessibleInterface *childAt(int x, int y) const override {
        for (int i = 0; i < childCount(); ++i) if (m_surface->accessibleItemRect(i).contains(x, y)) return child(i);
        return nullptr;
    }
    QString text(QAccessible::Text type) const override { return type == QAccessible::Name ? QObject::tr("Society navigation") : QString(); }
    QAccessible::Role role() const override { return QAccessible::ToolBar; }
    QAccessible::State state() const override {
        QAccessible::State result;
        result.invisible = !m_surface || !m_surface->isVisible();
        result.disabled = !m_surface || !m_surface->isEnabled();
        return result;
    }
    QRect rect() const override {
        if (!m_surface || !m_surface->window()) return {};
        const auto origin = m_surface->window()->mapToGlobal(m_surface->mapToScene(QPointF()).toPoint());
        return {origin, m_surface->size().toSize()};
    }
private:
    QPointer<NativeNavigationSurface> m_surface;
};

class SegmentAccessible final : public QAccessibleObject, public QAccessibleActionInterface {
public:
    explicit SegmentAccessible(SegmentNode *node) : QAccessibleObject(node), m_node(node) {}
    QWindow *window() const override { return m_node && m_node->surface ? m_node->surface->window() : nullptr; }
    QAccessibleInterface *parent() const override {
        return m_node && m_node->surface ? QAccessible::queryAccessibleInterface(m_node->surface) : nullptr;
    }
    QAccessibleInterface *child(int) const override { return nullptr; }
    int childCount() const override { return 0; }
    int indexOfChild(const QAccessibleInterface *) const override { return -1; }
    QString text(QAccessible::Text type) const override {
        return type == QAccessible::Name && m_node && m_node->surface
            ? m_node->surface->accessibleItemName(m_node->index) : QString();
    }
    QRect rect() const override {
        return m_node && m_node->surface ? m_node->surface->accessibleItemRect(m_node->index) : QRect();
    }
    QAccessible::Role role() const override { return QAccessible::Button; }
    QAccessible::State state() const override {
        QAccessible::State result;
        const auto *surface = m_node ? m_node->surface.data() : nullptr;
        result.invisible = !surface || !surface->isVisible();
        result.disabled = !surface || !surface->isEnabled();
        result.focusable = true;
        result.focused = surface && surface->accessibleItemFocused(m_node->index);
        result.selected = surface && surface->accessibleItemSelected(m_node->index);
        return result;
    }
    void *interface_cast(QAccessible::InterfaceType type) override {
        return type == QAccessible::ActionInterface ? static_cast<QAccessibleActionInterface *>(this) : nullptr;
    }
    QStringList actionNames() const override { return {pressAction(), setFocusAction()}; }
    void doAction(const QString &name) override {
        if (!m_node || !m_node->surface) return;
        if (name == pressAction()) m_node->surface->activateAccessibleItem(m_node->index);
        else if (name == setFocusAction()) m_node->surface->focusAccessibleItem(m_node->index);
    }
    QStringList keyBindingsForAction(const QString &) const override { return {}; }
private:
    QPointer<SegmentNode> m_node;
};

QAccessibleInterface *accessibleFactory(const QString &, QObject *object) {
    if (auto *surface = qobject_cast<NativeNavigationSurface *>(object)) return new SurfaceAccessible(surface);
    if (auto *node = dynamic_cast<SegmentNode *>(object)) return new SegmentAccessible(node);
    return nullptr;
}
}

struct NativeNavigationSurface::Impl final {
    std::shared_ptr<QtPaintDevice> device = std::make_shared<QtPaintDevice>();
    std::shared_ptr<ii::gui::PaintView> view;
    std::shared_ptr<View::Scope> scope;
    std::array<SegmentNode *, ids.size()> accessible{};
    QMetaObject::Connection screenConnection;
    QString page = QStringLiteral("Dashboard"), error;
    std::mutex imageMutex;
    QImage image;
};

NativeNavigationSurface::NativeNavigationSurface(QQuickItem *parent) : QQuickPaintedItem(parent), m(std::make_unique<Impl>()) {
    static std::once_flag accessibilityFactory;
    std::call_once(accessibilityFactory, [] { QAccessible::installFactory(accessibleFactory); });
    for (int i = 0; i < int(ids.size()); ++i) m->accessible[i] = new SegmentNode(this, i);
    setImplicitSize(ids.size() * lvui::icon_button_defaults::extent
                        + (ids.size() - 1) * lvui::segmented_defaults::spacing
                        + 2 * lvui::segmented_defaults::icon_horizontal_padding,
                    lvui::icon_button_defaults::extent + 2 * lvui::segmented_defaults::icon_vertical_padding);
    setAcceptHoverEvents(true);
    setAcceptedMouseButtons(Qt::LeftButton);
    setFlag(ItemIsFocusScope, true);
    setActiveFocusOnTab(true);
    setAntialiasing(true);
    initializeNavigationIcons();
    std::vector<lvui::IconDefinition> icons;
    for (const auto *name : {"toolwindowrun", "asset-library", "toolwindownewUI", "webApp", "cwmUsers", "webServer"}) {
        QFile file(QStringLiteral(":/society/navigation/") + QString::fromLatin1(name) + QStringLiteral(".svg"));
        if (!file.open(QIODevice::ReadOnly)) {
            m->error = tr("Could not load navigation icon: %1").arg(QString::fromLatin1(name));
            return;
        }
        icons.push_back({name, file.readAll().toStdString()});
    }
    auto iconCatalog = lvui::IconCatalog::create(std::move(icons));
    if (!iconCatalog) { m->error = QString::fromStdString(iconCatalog.error().message); return; }
    const QPointer<NativeNavigationSurface> guarded(this);
    auto result = ii::gui::PaintView::create(m->device, [&](ii::gui::WidgetBuilder &builder) {
        return View::build(builder, {}, [&](View::Scope &scope) {
            scope.backend_0.iconSource = *iconCatalog;
            scope.backend_0.onNavigate = [guarded](Action action) {
                if (!guarded) return;
                const auto page = destination(action);
                QMetaObject::invokeMethod(guarded, [guarded, page] { if (guarded) emit guarded->requested(page); }, Qt::QueuedConnection);
            };
            scope.backend_0.setCurrent(m->page.toStdString());
        }, &m->scope);
    }, View::appearance());
    if (result) m->view = *result;
    else m->error = QString::fromStdString(result.error().message);
    const auto bindWindow = [this](QQuickWindow *window) {
        disconnect(m->screenConnection);
        if (window) m->screenConnection = connect(window, &QWindow::screenChanged, this, [this] { render(); });
        render();
    };
    connect(this, &QQuickItem::windowChanged, this, bindWindow);
    bindWindow(window());
}
NativeNavigationSurface::~NativeNavigationSurface() {
    // QQuickItem emits windowChanged while its base destructor detaches it;
    // the derived state must not be called after it has been destroyed.
    disconnect(this, nullptr, this, nullptr);
    if (window()) disconnect(window(), nullptr, this, nullptr);
    if (m->view) (void)m->view->close();
}
QString NativeNavigationSurface::currentPage() const { return m->page; }
bool NativeNavigationSurface::ready() const { return bool(m->view) && m->error.isEmpty(); }
QString NativeNavigationSurface::errorString() const { return m->error; }
void NativeNavigationSurface::setError(const QString &error) {
    if (m->error == error) return;
    const bool oldReady = ready();
    m->error = error;
    emit errorStringChanged();
    if (oldReady != ready()) emit readyChanged();
}
void NativeNavigationSurface::setCurrentPage(const QString &page) {
    if (m->page == page || !m->scope || !m->scope->backend_0.setCurrent(page.toStdString())) return;
    m->page = page;
    emit currentPageChanged();
    render();
    if (QAccessible::isActive()) {
        for (auto *node : m->accessible) {
            QAccessible::State changed; changed.selected = true;
            QAccessibleStateChangeEvent event(node, changed); QAccessible::updateAccessibility(&event);
        }
    }
}
void NativeNavigationSurface::render() {
    if (!m->view || width() <= 0 || height() <= 0) return;
    const double scale = window() ? window()->effectiveDevicePixelRatio() : 1.0;
    auto result = m->view->paint({width(), height()}, scale);
    if (!result) { setError(QString::fromStdString(result.error().message)); return; }
    {
        std::lock_guard lock(m->imageMutex);
        m->image = m->device->image();
    }
    setError({});
    update();
}
void NativeNavigationSurface::paint(QPainter *painter) {
    QImage image;
    { std::lock_guard lock(m->imageMutex); image = m->image; }
    if (!image.isNull()) painter->drawImage(QPointF(), image);
}
void NativeNavigationSurface::geometryChange(const QRectF &next, const QRectF &previous) {
    QQuickPaintedItem::geometryChange(next, previous);
    if (next.size() != previous.size()) render();
}
void NativeNavigationSurface::hoverMoveEvent(QHoverEvent *event) {
    if (m->view) (void)m->view->input({.kind=ii::gui::WidgetInputKind::pointer_move, .x=event->position().x(), .y=event->position().y()});
    render(); event->accept();
}
void NativeNavigationSurface::hoverLeaveEvent(QHoverEvent *event) {
    if (m->view) (void)m->view->input({.kind=ii::gui::WidgetInputKind::pointer_leave});
    render(); event->accept();
}
void NativeNavigationSurface::mousePressEvent(QMouseEvent *event) {
    forceActiveFocus(Qt::MouseFocusReason);
    if (m->view) (void)m->view->input({.kind=ii::gui::WidgetInputKind::pointer_down, .x=event->position().x(), .y=event->position().y(), .buttons=1});
    render(); event->accept();
}
void NativeNavigationSurface::mouseReleaseEvent(QMouseEvent *event) {
    if (m->view) (void)m->view->input({.kind=ii::gui::WidgetInputKind::pointer_up, .x=event->position().x(), .y=event->position().y()});
    render(); event->accept();
}
void NativeNavigationSurface::mouseUngrabEvent() {
    if (m->view) (void)m->view->input({.kind=ii::gui::WidgetInputKind::pointer_cancel});
    render();
}
void NativeNavigationSurface::keyPressEvent(QKeyEvent *event) {
    const auto translated = key(event->key());
    if (!m->view || translated == ii::gui::WidgetKey::none) { event->ignore(); return; }
    if (translated == ii::gui::WidgetKey::tab) {
        const bool reverse = bool(event->modifiers() & Qt::ShiftModifier);
        if ((!reverse && accessibleItemFocused(int(ids.size()) - 1)) || (reverse && accessibleItemFocused(0))) {
            auto *next = nextItemInFocusChain(!reverse);
            if (next && next != this) {
                next->forceActiveFocus(reverse ? Qt::BacktabFocusReason : Qt::TabFocusReason);
                event->accept();
                return;
            }
            event->ignore();
            return;
        }
    }
    (void)m->view->input({.kind=ii::gui::WidgetInputKind::key_down, .key=translated, .shift=bool(event->modifiers() & Qt::ShiftModifier)});
    render(); event->accept();
}
void NativeNavigationSurface::keyReleaseEvent(QKeyEvent *event) {
    const auto translated = key(event->key());
    if (!m->view || translated == ii::gui::WidgetKey::none) { event->ignore(); return; }
    (void)m->view->input({.kind=ii::gui::WidgetInputKind::key_up, .key=translated, .shift=bool(event->modifiers() & Qt::ShiftModifier)});
    render(); event->accept();
}
void NativeNavigationSurface::focusInEvent(QFocusEvent *event) {
    QQuickPaintedItem::focusInEvent(event);
    if (!m->view) return;
    if (event->reason() == Qt::TabFocusReason || event->reason() == Qt::BacktabFocusReason) {
        const auto index = event->reason() == Qt::BacktabFocusReason ? ids.size() - 1 : 0;
        (void)m->view->input({.kind=ii::gui::WidgetInputKind::focus, .target=ids[index]});
        render();
    }
}
void NativeNavigationSurface::focusOutEvent(QFocusEvent *event) {
    if (m->view) (void)m->view->input({.kind=ii::gui::WidgetInputKind::focus_lost});
    render(); QQuickPaintedItem::focusOutEvent(event);
}
QObject *NativeNavigationSurface::accessibleItem(int index) const {
    return index >= 0 && index < int(ids.size()) ? m->accessible[index] : nullptr;
}
QRect NativeNavigationSurface::accessibleItemRect(int index) const {
    if (!window() || !m->view || index < 0 || index >= int(ids.size())) return {};
    const auto item = m->view->find(ids[index]); if (!item) return {};
    const auto snapshot = item->snapshot(); if (!snapshot) return {};
    const auto &bounds = snapshot->bounds;
    const auto origin = window()->mapToGlobal(mapToScene(QPointF(bounds.x, bounds.y)).toPoint());
    return {origin, QSize(qRound(bounds.width), qRound(bounds.height))};
}
QString NativeNavigationSurface::accessibleItemName(int index) const {
    if (!m->view || index < 0 || index >= int(ids.size())) return {};
    const auto item = m->view->find(ids[index]); if (!item) return {};
    const auto snapshot = item->snapshot();
    return snapshot ? QString::fromStdString(snapshot->accessibility_name) : QString();
}
bool NativeNavigationSurface::accessibleItemSelected(int index) const {
    if (!m->view || index < 0 || index >= int(ids.size())) return false;
    const auto item = m->view->find(ids[index]); if (!item) return false;
    const auto snapshot = item->snapshot(); return snapshot && snapshot->checked;
}
bool NativeNavigationSurface::accessibleItemFocused(int index) const {
    if (!hasActiveFocus() || !m->view || index < 0 || index >= int(ids.size())) return false;
    const auto item = m->view->find(ids[index]); if (!item) return false;
    const auto snapshot = item->snapshot(); return snapshot && snapshot->focused;
}
void NativeNavigationSurface::activateAccessibleItem(int index) {
    if (!m->view || index < 0 || index >= int(ids.size())) return;
    (void)m->view->input({.kind=ii::gui::WidgetInputKind::activate, .target=ids[index]});
    render();
}
void NativeNavigationSurface::focusAccessibleItem(int index) {
    if (!m->view || index < 0 || index >= int(ids.size())) return;
    forceActiveFocus(Qt::OtherFocusReason);
    (void)m->view->input({.kind=ii::gui::WidgetInputKind::focus, .target=ids[index]});
    render();
}

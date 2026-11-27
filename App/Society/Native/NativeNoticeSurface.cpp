#include "NativeNoticeSurface.h"
#include "NativeNotice.generated.h"
#include "QtPaintDevice.h"

#include <iiGUI>
#include <QAccessible>
#include <QAccessibleObject>
#include <QFocusEvent>
#include <QHoverEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QQuickWindow>
#include <exception>
#include <mutex>

namespace {
using View = lvui_generated::NativeNotice;

// Translate only keys understood by the retained iiGUI alert.
ii::gui::WidgetKey noticeKey(int key) {
    using Key = ii::gui::WidgetKey;
    switch (key) {
    case Qt::Key_Tab: return Key::tab;
    case Qt::Key_Return: case Qt::Key_Enter: return Key::enter;
    case Qt::Key_Space: return Key::space;
    case Qt::Key_Escape: return Key::escape;
    default: return Key::none;
    }
}

// Expose the modal text and its primary action to the host accessibility API.
class NoticeAccessible final : public QAccessibleObject, public QAccessibleActionInterface {
public:
    explicit NoticeAccessible(NativeNoticeSurface *surface) : QAccessibleObject(surface), m_surface(surface) {}
    QWindow *window() const override { return m_surface ? m_surface->window() : nullptr; }
    QAccessibleInterface *parent() const override {
        return m_surface && m_surface->parentItem() ? QAccessible::queryAccessibleInterface(m_surface->parentItem()) : nullptr;
    }
    QAccessibleInterface *child(int) const override { return nullptr; }
    int childCount() const override { return 0; }
    int indexOfChild(const QAccessibleInterface *) const override { return -1; }
    QString text(QAccessible::Text type) const override {
        if (!m_surface) return {};
        if (type == QAccessible::Name) return m_surface->title();
        if (type == QAccessible::Description) return m_surface->message();
        return {};
    }
    QRect rect() const override {
        if (!m_surface || !m_surface->window()) return {};
        const auto bounds=m_surface->alertBounds();
        const auto origin=m_surface->window()->mapToGlobal(m_surface->mapToScene(bounds.topLeft()).toPoint());
        return {origin,bounds.size().toSize()};
    }
    QAccessible::Role role() const override { return QAccessible::AlertMessage; }
    QAccessible::State state() const override {
        QAccessible::State result;result.invisible=!m_surface||!m_surface->isVisible();
        result.focusable=true;result.focused=m_surface&&m_surface->hasActiveFocus();return result;
    }
    void *interface_cast(QAccessible::InterfaceType type) override {
        return type==QAccessible::ActionInterface ? static_cast<QAccessibleActionInterface *>(this) : nullptr;
    }
    QStringList actionNames() const override { return {pressAction()}; }
    void doAction(const QString &name) override { if (m_surface && name==pressAction()) m_surface->activatePrimary(); }
    QStringList keyBindingsForAction(const QString &) const override { return {}; }
private:
    QPointer<NativeNoticeSurface> m_surface;
};

QAccessibleInterface *noticeAccessibleFactory(const QString &, QObject *object) {
    if (auto *surface=qobject_cast<NativeNoticeSurface *>(object)) return new NoticeAccessible(surface);
    return nullptr;
}
}

struct NativeNoticeSurface::Impl final {
    std::shared_ptr<QtPaintDevice> device=std::make_shared<QtPaintDevice>();
    std::shared_ptr<ii::gui::PaintView> view;
    std::shared_ptr<View::Scope> scope;
    QPointer<QQuickItem> previousFocus;
    QImage image;
    QString error;
    std::mutex mutex;
    quint64 generation=0;
};

NativeNoticeSurface::NativeNoticeSurface(QQuickItem *parent) : QQuickPaintedItem(parent),m(std::make_unique<Impl>()) {
    static std::once_flag accessibleFactory;
    std::call_once(accessibleFactory, [] { QAccessible::installFactory(noticeAccessibleFactory); });
    setVisible(false);setAcceptHoverEvents(true);setAcceptedMouseButtons(Qt::LeftButton);
    setFlag(ItemIsFocusScope,true);setActiveFocusOnTab(true);
    const QPointer<NativeNoticeSurface> guard(this);
    auto result=ii::gui::PaintView::create(m->device,[&](ii::gui::WidgetBuilder &builder) {
        return View::build(builder,{},[&](View::Scope &scope) {
            scope.backend_0.onDismiss=[guard] {
                if(!guard)return;
                const auto generation=guard->m->generation;
                QMetaObject::invokeMethod(guard,[guard,generation] {
                    if(!guard||guard->m->generation!=generation)return;
                    guard->setVisible(false);
                    if(guard->m->previousFocus)guard->m->previousFocus->forceActiveFocus();
                    guard->m->previousFocus.clear();
                },Qt::QueuedConnection);
            };
        },&m->scope);
    },View::appearance());
    if(result)m->view=*result;
    else {
        m->error=QString::fromStdString(result.error().message);
        if(result.error().cause)try { std::rethrow_exception(result.error().cause); }
            catch(const std::exception &error) { m->error+=QStringLiteral(": ")+QString::fromUtf8(error.what()); }
            catch(...) { m->error+=QStringLiteral(": unknown exception"); }
    }
    connect(this,&QQuickItem::windowChanged,this,[this] { if(isVisible())refresh(); });
}

NativeNoticeSurface::~NativeNoticeSurface() {
    disconnect(this,nullptr,this,nullptr);
    if(m->view)(void)m->view->close();
}
bool NativeNoticeSurface::ready() const { return m->view&&m->scope&&m->error.isEmpty(); }
QString NativeNoticeSurface::errorString() const { return m->error; }
QString NativeNoticeSurface::title() const { return m->scope?QString::fromStdString(m->scope->backend_0.title.get()):QString(); }
QString NativeNoticeSurface::message() const { return m->scope?QString::fromStdString(m->scope->backend_0.message.get()):QString(); }

QRectF NativeNoticeSurface::alertBounds() const {
    if(!m->view)return {};
    const auto alert=m->view->find("society-notice");if(!alert)return {};
    const auto snapshot=alert->snapshot();if(!snapshot)return {};
    const auto &b=snapshot->bounds;return {b.x,b.y,b.width,b.height};
}

QRectF NativeNoticeSurface::primaryActionBounds() const {
    if(!m->view)return {};
    const auto alert=m->view->find("society-notice");if(!alert)return {};
    const auto action=alert->popup_content("society-notice.action-0");if(!action)return {};
    const auto snapshot=action->snapshot();if(!snapshot)return {};
    const auto &b=snapshot->bounds;return {b.x,b.y,b.width,b.height};
}

bool NativeNoticeSurface::present(const QString &titleText,const QString &messageText,const QString &actionText) {
    if(!ready()||width()<=0||height()<=0)return false;
    ++m->generation;
    if(!isVisible()&&window())m->previousFocus=window()->activeFocusItem();
    auto &vm=m->scope->backend_0;
    vm.title.set(titleText.toStdString());vm.message.set(messageText.toStdString());
    vm.primaryText.set(actionText.toStdString());vm.open.set(true);
    setVisible(true);forceActiveFocus();
    if(!refresh()){vm.open.set(false);setVisible(false);return false;}
    if(QAccessible::isActive()){QAccessibleEvent event(this,QAccessible::Alert);QAccessible::updateAccessibility(&event);}
    return true;
}

bool NativeNoticeSurface::activatePrimary() {
    if(!m->view||!isVisible())return false;
    const auto alert=m->view->find("society-notice");if(!alert)return false;
    const auto action=alert->popup_content("society-notice.action-0");if(!action)return false;
    if(!action->activate())return false;
    return refresh();
}

bool NativeNoticeSurface::refresh() {
    if(!m->view||width()<=0||height()<=0)return false;
    const double scale=window()?window()->effectiveDevicePixelRatio():1.;
    auto result=m->view->paint({width(),height()},scale);
    if(!result){m->error=QString::fromStdString(result.error().message);return false;}
    {std::lock_guard lock(m->mutex);m->image=m->device->image();}
    m->error.clear();update();return true;
}
void NativeNoticeSurface::paint(QPainter *painter) {
    std::lock_guard lock(m->mutex);if(!m->image.isNull())painter->drawImage(QPointF(),m->image);
}
void NativeNoticeSurface::geometryChange(const QRectF &next,const QRectF &previous) {
    QQuickPaintedItem::geometryChange(next,previous);if(isVisible()&&next.size()!=previous.size())refresh();
}
void NativeNoticeSurface::hoverMoveEvent(QHoverEvent *event) {
    if(m->view)(void)m->view->input({.kind=ii::gui::WidgetInputKind::pointer_move,.x=event->position().x(),.y=event->position().y()});
    refresh();event->accept();
}
void NativeNoticeSurface::hoverLeaveEvent(QHoverEvent *event) {
    if(m->view)(void)m->view->input({.kind=ii::gui::WidgetInputKind::pointer_leave});refresh();event->accept();
}
void NativeNoticeSurface::mousePressEvent(QMouseEvent *event) {
    forceActiveFocus(Qt::MouseFocusReason);
    if(m->view)(void)m->view->input({.kind=ii::gui::WidgetInputKind::pointer_down,.x=event->position().x(),.y=event->position().y(),.buttons=1});
    refresh();event->accept();
}
void NativeNoticeSurface::mouseReleaseEvent(QMouseEvent *event) {
    if(m->view)(void)m->view->input({.kind=ii::gui::WidgetInputKind::pointer_up,.x=event->position().x(),.y=event->position().y()});
    refresh();event->accept();
}
void NativeNoticeSurface::mouseUngrabEvent() {
    if(m->view)(void)m->view->input({.kind=ii::gui::WidgetInputKind::pointer_cancel});refresh();
}
void NativeNoticeSurface::keyPressEvent(QKeyEvent *event) {
    const auto key=noticeKey(event->key());if(!m->view||key==ii::gui::WidgetKey::none){event->ignore();return;}
    (void)m->view->input({.kind=ii::gui::WidgetInputKind::key_down,.key=key,.shift=bool(event->modifiers()&Qt::ShiftModifier)});
    refresh();event->accept();
}
void NativeNoticeSurface::keyReleaseEvent(QKeyEvent *event) {
    const auto key=noticeKey(event->key());if(!m->view||key==ii::gui::WidgetKey::none){event->ignore();return;}
    (void)m->view->input({.kind=ii::gui::WidgetInputKind::key_up,.key=key,.shift=bool(event->modifiers()&Qt::ShiftModifier)});
    refresh();event->accept();
}
void NativeNoticeSurface::focusOutEvent(QFocusEvent *event) {
    if(m->view)(void)m->view->input({.kind=ii::gui::WidgetInputKind::focus_lost});
    refresh();QQuickPaintedItem::focusOutEvent(event);
}

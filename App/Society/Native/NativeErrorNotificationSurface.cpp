#include "NativeErrorNotificationSurface.h"
#include "ErrorNotificationQueue.h"
#include "NativeErrorNotification.generated.h"
#include "QtPaintDevice.h"
#include <LVUI/Popover.h>
#include <iiGUI>
#include <QAccessible>
#include <QAccessibleObject>
#include <QFile>
#include <QHoverEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QQuickWindow>
#include <QWheelEvent>
#include <algorithm>
#include <mutex>

using NotificationView = lvui_generated::NativeErrorNotification;

static void initializeErrorNotificationIcons() { Q_INIT_RESOURCE(WorkspaceIcons); }

namespace {
// Expose the transient message and its close action through the Qt host.
class ErrorNotificationAccessible final : public QAccessibleObject, public QAccessibleActionInterface {
public:
    explicit ErrorNotificationAccessible(NativeErrorNotificationSurface *surface)
        : QAccessibleObject(surface),surface_(surface) {}
    QWindow *window() const override { return surface_ ? surface_->window() : nullptr; }
    QAccessibleInterface *parent() const override {
        return surface_ && surface_->parentItem() ? QAccessible::queryAccessibleInterface(surface_->parentItem()) : nullptr;
    }
    QAccessibleInterface *child(int) const override { return nullptr; }
    int childCount() const override { return 0; }
    int indexOfChild(const QAccessibleInterface *) const override { return -1; }
    QString text(QAccessible::Text type) const override {
        if (type==QAccessible::Name) return QStringLiteral("Error notification");
        return type==QAccessible::Description && surface_ ? surface_->message() : QString();
    }
    QRect rect() const override {
        if (!surface_ || !surface_->window()) return {};
        const auto bounds=surface_->controlBounds(QStringLiteral("society-error-notification"));
        const auto origin=surface_->window()->mapToGlobal(surface_->mapToScene(bounds.topLeft()).toPoint());
        return {origin,bounds.size().toSize()};
    }
    QAccessible::Role role() const override { return QAccessible::AlertMessage; }
    QAccessible::State state() const override {
        QAccessible::State result;result.invisible=!surface_ || !surface_->isVisible();return result;
    }
    void *interface_cast(QAccessible::InterfaceType type) override {
        return type==QAccessible::ActionInterface ? static_cast<QAccessibleActionInterface *>(this) : nullptr;
    }
    QStringList actionNames() const override { return {pressAction()}; }
    void doAction(const QString &name) override { if (surface_ && name==pressAction()) (void)surface_->dismiss(); }
    QStringList keyBindingsForAction(const QString &) const override { return {}; }
private:
    QPointer<NativeErrorNotificationSurface> surface_;
};
QAccessibleInterface *errorNotificationFactory(const QString &,QObject *object) {
    if (auto *surface=qobject_cast<NativeErrorNotificationSurface *>(object)) return new ErrorNotificationAccessible(surface);
    return nullptr;
}
}

struct NativeErrorNotificationSurface::Impl final {
    std::shared_ptr<QtPaintDevice> device=std::make_shared<QtPaintDevice>();
    std::shared_ptr<ii::gui::PaintView> view;
    std::shared_ptr<NotificationView::Scope> scope;
    society::native::ErrorNotificationQueue queue;
    QImage image;
    QString error;
    std::mutex mutex;
    quint64 generation=0;
    bool layoutDirty=true;
};

NativeErrorNotificationSurface::NativeErrorNotificationSurface(QQuickItem *parent)
    : QQuickPaintedItem(parent),m(std::make_unique<Impl>()) {
    static std::once_flag accessibleFactory;
    std::call_once(accessibleFactory,[] { QAccessible::installFactory(errorNotificationFactory); });
    setVisible(false);setClip(true);setAcceptHoverEvents(true);setAcceptedMouseButtons(Qt::LeftButton);
    initializeErrorNotificationIcons();
    QFile icon(QStringLiteral(":/society/workspace/generalclose.svg"));
    if (!icon.open(QIODevice::ReadOnly)) {m->error=tr("Could not load the notification close icon.");return;}
    auto icons=lvui::IconCatalog::create({{"generalclose",icon.readAll().toStdString()}});
    if (!icons) {m->error=QString::fromStdString(icons.error().message);return;}
    const QPointer<NativeErrorNotificationSurface> guard(this);
    auto result=ii::gui::PaintView::create(m->device,[&](ii::gui::WidgetBuilder &builder) {
        return NotificationView::build(builder,{},[&](NotificationView::Scope &scope) {
            scope.backend_0.iconSource=*icons;
            scope.backend_0.onDismiss=[guard] {
                if (!guard) return;
                const auto generation=guard->m->generation;
                QMetaObject::invokeMethod(guard,[guard,generation] {
                    if (!guard || guard->m->generation!=generation) return;
                    guard->m->queue.dismiss();
                    guard->present();
                },Qt::QueuedConnection);
            };
        },&m->scope);
    },NotificationView::appearance());
    if (result) m->view=*result;
    else m->error=QString::fromStdString(result.error().message);
}
NativeErrorNotificationSurface::~NativeErrorNotificationSurface() { if (m->view) (void)m->view->close(); }
bool NativeErrorNotificationSurface::ready() const { return m->view && m->scope && m->error.isEmpty(); }
QString NativeErrorNotificationSurface::errorString() const { return m->error; }
QString NativeErrorNotificationSurface::message() const {
    return m->scope ? QString::fromStdString(m->scope->backend_0.message.get()) : QString();
}
void NativeErrorNotificationSurface::enqueue(const QString &source,const QString &value) {
    if (value.trimmed().isEmpty()) return;
    const auto text=source.isEmpty() ? value : source+QStringLiteral(": ")+value;
    const bool show=m->queue.empty();
    m->queue.push(text.toStdString());
    if (show) present();
}
void NativeErrorNotificationSurface::present() {
    if (!ready()) return;
    ++m->generation;
    const auto popup=m->view->find("society-error-notification");
    if (!popup) return;
    if (m->queue.empty()) {
        (void)popup->close_popup();setVisible(false);return;
    }
    m->scope->backend_0.message.set(std::string(m->queue.current()));
    m->layoutDirty=true;
    setVisible(true);
    const auto margin=lvui::PopoverConfiguration{}.margins;
    (void)popup->open_at(margin,margin);
    (void)refresh();
    if (QAccessible::isActive()) { QAccessibleEvent event(this,QAccessible::Alert);QAccessible::updateAccessibility(&event); }
}
bool NativeErrorNotificationSurface::dismiss() {
    if (!m->view || !isVisible()) return false;
    const auto popup=m->view->find("society-error-notification");
    if (!popup) return false;
    const auto button=popup->popup_content("dismiss-error-notification");
    return button && bool(button->activate());
}
QRectF NativeErrorNotificationSurface::controlBounds(const QString &id) const {
    if (!m->view) return {};
    const auto popup=m->view->find("society-error-notification");
    if (!popup) return {};
    const auto control=id==QLatin1String("society-error-notification") ? popup : popup->popup_content(id.toStdString());
    if (!control) return {};
    const auto snapshot=control->snapshot();
    if (!snapshot) return {};
    const auto &bounds=snapshot->bounds;
    return {bounds.x,bounds.y,bounds.width,bounds.height};
}
QPointF NativeErrorNotificationSurface::controlScrollPosition(const QString &id) const {
    if (!m->view) return {};
    const auto popup=m->view->find("society-error-notification");
    if (!popup) return {};
    const auto control=popup->popup_content(id.toStdString());
    if (!control) return {};
    const auto snapshot=control->snapshot();
    if (!snapshot) return {};
    return {snapshot->scroll_offset,std::max(0.0,snapshot->content_height-snapshot->bounds.height)};
}
bool NativeErrorNotificationSurface::contains(const QPointF &point) const {
    return isVisible() && controlBounds(QStringLiteral("society-error-notification")).contains(point);
}
bool NativeErrorNotificationSurface::refresh() {
    if (!m->view || width()<=0 || height()<=0) return false;
    const auto scale=window()?window()->effectiveDevicePixelRatio():1.;
    if (m->layoutDirty && m->scope) {
        const auto options=lvui::PopoverConfiguration{};
        const auto popupWidth=std::max(1.0,std::min(360.0,width()-2*options.margins));
        const auto maximumHeight=std::max(1.0,std::min(8192.0,height()-2*options.margins));
        auto &model=m->scope->backend_0;
        model.popupWidth.set(popupWidth);
        model.popupHeight.set(0);
        model.messageHeight.set(0);
        // First settle the retained tree at the real available width. iiGUI
        // then measures its label with the same wrapping used for painting.
        auto initial=m->view->paint({width(),height()},scale);
        if (!initial) {m->error=QString::fromStdString(initial.error().message);return false;}
        const auto popup=m->view->find("society-error-notification");
        if (!popup) return false;
        const auto content=popup->popup_content("error-notification-content");
        const auto scroll=popup->popup_content("error-message-scroll");
        const auto label=popup->popup_content("error-message");
        if (!content || !scroll || !label) return false;
        const auto scrollSnapshot=scroll->snapshot();
        if (!scrollSnapshot) return false;
        const auto textWidth=std::max(1.0,std::min(8192.0,double(scrollSnapshot->bounds.width)));
        const auto innerWidth=std::max(1.0,std::min(8192.0,popupWidth-2*options.padding));
        const auto textSize=label->measure(textWidth);
        const auto contentSize=content->measure(innerWidth);
        if (!textSize || !contentSize) return false;
        const auto chrome=std::max(0.0,contentSize->height-textSize->height);
        const auto available=std::max(1.0,maximumHeight-2*options.padding-chrome);
        const auto messageHeight=std::max(1.0,std::min(textSize->height,available));
        model.messageHeight.set(messageHeight);
        model.popupHeight.set(std::min(maximumHeight,2*options.padding+chrome+messageHeight));
        m->layoutDirty=false;
    }
    auto result=m->view->paint({width(),height()},scale);
    if (!result) { m->error=QString::fromStdString(result.error().message);return false; }
    {std::lock_guard lock(m->mutex);m->image=m->device->image();}
    m->error.clear();update();return true;
}
void NativeErrorNotificationSurface::paint(QPainter *painter) {
    std::lock_guard lock(m->mutex);if (!m->image.isNull()) painter->drawImage(QPointF(),m->image);
}
void NativeErrorNotificationSurface::geometryChange(const QRectF &next,const QRectF &previous) {
    QQuickPaintedItem::geometryChange(next,previous);
    if (next.size()!=previous.size()) {m->layoutDirty=true;if (isVisible()) (void)refresh();}
}
void NativeErrorNotificationSurface::hoverMoveEvent(QHoverEvent *event) {
    if (m->view) (void)m->view->input({.kind=ii::gui::WidgetInputKind::pointer_move,.x=event->position().x(),.y=event->position().y()});
    (void)refresh();event->accept();
}
void NativeErrorNotificationSurface::hoverLeaveEvent(QHoverEvent *event) {
    if (m->view) (void)m->view->input({.kind=ii::gui::WidgetInputKind::pointer_leave});
    (void)refresh();event->accept();
}
void NativeErrorNotificationSurface::mousePressEvent(QMouseEvent *event) {
    if (m->view) (void)m->view->input({.kind=ii::gui::WidgetInputKind::pointer_down,.x=event->position().x(),.y=event->position().y(),.buttons=1});
    (void)refresh();event->accept();
}
void NativeErrorNotificationSurface::mouseReleaseEvent(QMouseEvent *event) {
    if (m->view) (void)m->view->input({.kind=ii::gui::WidgetInputKind::pointer_up,.x=event->position().x(),.y=event->position().y()});
    (void)refresh();event->accept();
}
void NativeErrorNotificationSurface::mouseUngrabEvent() {
    if (m->view) (void)m->view->input({.kind=ii::gui::WidgetInputKind::pointer_cancel});
    (void)refresh();
}
void NativeErrorNotificationSurface::wheelEvent(QWheelEvent *event) {
    if (!m->view) {event->ignore();return;}
    const auto scrollId=QStringLiteral("error-message-scroll");
    if (!controlBounds(scrollId).contains(event->position()) || controlScrollPosition(scrollId).y()<=0) {
        event->ignore();return;
    }
    const bool precise=!event->pixelDelta().isNull();
    const auto delta=precise?event->pixelDelta():event->angleDelta();
    const auto scale=precise?1.0:1.0/8.0;
    const auto handled=m->view->input({.kind=ii::gui::WidgetInputKind::scroll,
        .x=event->position().x(),.y=event->position().y(),.shift=bool(event->modifiers() & Qt::ShiftModifier),
        .scroll_y=-double(delta.y())*scale,.scroll_x=-double(delta.x())*scale,.precise_scroll=precise});
    (void)refresh();
    if (handled) event->accept();else event->ignore();
}

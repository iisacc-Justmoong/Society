#include "PhotoAreaTracker.h"
#include <QMetaObject>
#include <QEvent>
#include <QQmlListProperty>
#include <QQuickItem>
#include <QQuickTransform>
#include <QQuickWindow>
#include <QThread>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace society::native {
PhotoAreaTracker::PhotoAreaTracker(QQuickItem* item,QQuickWindow* window,Changed changed,QQuickItem* overlay,QObject* parent)
    :QObject(parent),item_(item),window_(window),overlay_(overlay),changed_(std::move(changed)),expectsOverlay_(overlay!=nullptr) {
    if (!item || !window || !changed_ || item->thread()!=QThread::currentThread() ||
        window->thread()!=QThread::currentThread() || (overlay && overlay->thread()!=QThread::currentThread()) ||
        thread()!=QThread::currentThread())
        throw std::invalid_argument("Photo area tracking requires an owner-thread item, window and observer");
    window->installEventFilter(this);bind();schedule();
}
bool PhotoAreaTracker::eventFilter(QObject* watched,QEvent* event) {
    if(watched==window_ && event->type()==QEvent::DevicePixelRatioChange)schedule();
    return QObject::eventFilter(watched,event);
}
void PhotoAreaTracker::refresh() {
    if (thread()!=QThread::currentThread()) throw std::logic_error("Photo area tracking requires its owner thread");
    schedule();
}
void PhotoAreaTracker::bind() {
    for (const auto& connection:connections_) QObject::disconnect(connection);
    connections_.clear();
    const auto watch=[this](QQuickItem* item) {
        const auto update=[this]{schedule();};
        connections_.push_back(connect(item,&QQuickItem::xChanged,this,update));
        connections_.push_back(connect(item,&QQuickItem::yChanged,this,update));
        connections_.push_back(connect(item,&QQuickItem::widthChanged,this,update));
        connections_.push_back(connect(item,&QQuickItem::heightChanged,this,update));
        connections_.push_back(connect(item,&QQuickItem::visibleChanged,this,update));
        connections_.push_back(connect(item,&QQuickItem::enabledChanged,this,update));
        connections_.push_back(connect(item,&QQuickItem::opacityChanged,this,update));
        connections_.push_back(connect(item,&QQuickItem::clipChanged,this,update));
        connections_.push_back(connect(item,&QQuickItem::rotationChanged,this,update));
        connections_.push_back(connect(item,&QQuickItem::scaleChanged,this,update));
        connections_.push_back(connect(item,&QQuickItem::parentChanged,this,[this]{bind();schedule();}));
        connections_.push_back(connect(item,&QObject::destroyed,this,update));
    };
    for (auto* item=item_.data();item;item=item->parentItem()) watch(item);
    if (overlay_) {
        const auto update=[this]{schedule();};
        // An overlay can contain permanently visible grouping items (for
        // example AccountViews) which draw nothing. Observe their descendants
        // as well, so a later popup still suspends the native surface.
        QList<QQuickItem*> pending{overlay_};
        while (!pending.isEmpty()) {
            auto* item=pending.takeLast();
            connections_.push_back(connect(item,&QQuickItem::visibleChanged,this,update));
            connections_.push_back(connect(item,&QQuickItem::childrenChanged,this,[this]{bind();schedule();}));
            connections_.push_back(connect(item,&QObject::destroyed,this,update));
            pending.append(item->childItems());
        }
    }
    if (window_) {
        const auto update=[this]{schedule();};
        connections_.push_back(connect(window_,&QWindow::widthChanged,this,update));
        connections_.push_back(connect(window_,&QWindow::heightChanged,this,update));
        connections_.push_back(connect(window_,&QWindow::visibleChanged,this,update));
        connections_.push_back(connect(window_,&QObject::destroyed,this,update));
    }
}
void PhotoAreaTracker::schedule() {
    if (scheduled_) return;
    scheduled_=true;
    if (!QMetaObject::invokeMethod(this,[this]{scheduled_=false;publish();},Qt::QueuedConnection)) {
        scheduled_=false;publish();
    }
}
void PhotoAreaTracker::publish() {
    const auto value=area();
    if (hasLast_ && value==last_) return;
    last_=value;hasLast_=true;
    auto changed=changed_;
    if (changed) changed(value);
}
PhotoArea PhotoAreaTracker::area() const {
    if (!item_ || !window_ || !window_->isVisible() || !item_->isVisible()) return {};
    if (expectsOverlay_ && !overlay_) return {.mode=PhotoArea::Mode::unsupported};
    if (overlay_ && overlay_->isVisible()) {
        QList<QQuickItem*> pending=overlay_->childItems();
        while (!pending.isEmpty()) {
            const auto* item=pending.takeLast();
            if (!item->isVisible()) continue;
            if (item->flags().testFlag(QQuickItem::ItemHasContents) || item->inherits("QQuickPopupItem") ||
                item->acceptedMouseButtons()!=Qt::NoButton || item->acceptTouchEvents()) return {};
            pending.append(item->childItems());
        }
    }
    if (item_->window()!=window_) return {.mode=PhotoArea::Mode::unsupported};
    for (auto* node=item_.data();node;node=node->parentItem()) {
        if (node!=item_ && (!node->isEnabled() || node->opacity()<=0)) return {};
        auto transforms=node->transform();
        if (!std::isfinite(node->scale()) || !std::isfinite(node->rotation()) || node->scale()!=1 || node->rotation()!=0 ||
            (transforms.count && transforms.count(&transforms)>0)) return {.mode=PhotoArea::Mode::unsupported};
    }
    const auto finite=[](const QRectF& value){return std::isfinite(value.x()) && std::isfinite(value.y()) &&
        std::isfinite(value.width()) && std::isfinite(value.height());};
    const auto bounds=item_->mapRectToScene(QRectF(0,0,item_->width(),item_->height()));
    if (!finite(bounds)) return {.mode=PhotoArea::Mode::unsupported};
    if (bounds.isEmpty()) return {};
    auto clipped=bounds.intersected(QRectF(0,0,window_->width(),window_->height()));
    for (auto* node=item_->parentItem();node && !clipped.isEmpty();node=node->parentItem()) {
        if (!node->clip()) continue;
        const auto rect=node->mapRectToScene(QRectF(0,0,node->width(),node->height()));
        if (!finite(rect)) return {.mode=PhotoArea::Mode::unsupported};
        clipped=clipped.intersected(rect);
    }
    if (clipped.isEmpty()) return {};
    const auto scale=window_->devicePixelRatio();
    if(!std::isfinite(scale) || scale<=0)return {.mode=PhotoArea::Mode::unsupported};
    return {.mode=PhotoArea::Mode::visible,.bounds=bounds,.clip=clipped,.deviceScale=scale};
}
} // namespace society::native

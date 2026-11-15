#pragma once

#include <QObject>
#include <QPointer>
#include <QRectF>
#include <functional>
#include <vector>

class QQuickItem;
class QQuickWindow;
namespace society::native {
struct PhotoArea final {
    enum class Mode { hidden, visible, unsupported };
    Mode mode=Mode::hidden;
    QRectF bounds,clip;
    double deviceScale=1;
    bool operator==(const PhotoArea&) const=default;
};

// Tracks only geometry and visibility events. It neither paints nor creates a
// native surface; the owner decides when to attach or fall back.
class PhotoAreaTracker final : public QObject {
public:
    using Changed=std::function<void(const PhotoArea&)>;
    PhotoAreaTracker(QQuickItem*,QQuickWindow*,Changed,QQuickItem* overlay=nullptr,QObject* parent=nullptr);
    // Call when the host changes an item's transform list, which has no Qt notify signal.
    void refresh();
protected:
    bool eventFilter(QObject*,QEvent*) override;
private:
    void bind();
    void schedule();
    void publish();
    [[nodiscard]] PhotoArea area() const;
    QPointer<QQuickItem> item_;
    QPointer<QQuickWindow> window_;
    QPointer<QQuickItem> overlay_;
    Changed changed_;
    std::vector<QMetaObject::Connection> connections_;
    PhotoArea last_;
    bool scheduled_=false,hasLast_=false,expectsOverlay_=false;
};
} // namespace society::native

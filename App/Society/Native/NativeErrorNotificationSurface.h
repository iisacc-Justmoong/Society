#pragma once
#include <QQuickPaintedItem>
#include <memory>

// Hosts a nonmodal LVUI notification within the existing Qt window.
class NativeErrorNotificationSurface final : public QQuickPaintedItem {
    Q_OBJECT
public:
    // Construct the initially hidden retained view.
    explicit NativeErrorNotificationSurface(QQuickItem *parent = nullptr);
    // Retire the retained widget tree before its Qt host disappears.
    ~NativeErrorNotificationSurface() override;
    // Report initialization and rendering readiness to the shell.
    [[nodiscard]] bool ready() const;
    // Explain a failed initialization or paint.
    [[nodiscard]] QString errorString() const;
    // Return the text currently presented to the user.
    [[nodiscard]] QString message() const;
    // Add an error occurrence without modifying the source record.
    void enqueue(const QString &source, const QString &message);
    // Activate the LVUI close control, preserving any remaining occurrences.
    bool dismiss();
    // Report the actual retained control bounds in surface coordinates.
    [[nodiscard]] QRectF controlBounds(const QString &id) const;
    // Return the current and maximum vertical scroll offsets of a control.
    [[nodiscard]] QPointF controlScrollPosition(const QString &id) const;
    // Only the visible panel receives input; transparent host space passes it on.
    [[nodiscard]] bool contains(const QPointF &point) const override;
    // Copy the most recent iiGUI frame into the Qt surface.
    void paint(QPainter *painter) override;
protected:
    // Repaint after the shell changes the surface dimensions.
    void geometryChange(const QRectF &next, const QRectF &previous) override;
    // Forward pointer input to the LVUI popup.
    void hoverMoveEvent(QHoverEvent *event) override;
    void hoverLeaveEvent(QHoverEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseUngrabEvent() override;
    // Forward wheel and trackpad movement to a scrollable error message.
    void wheelEvent(QWheelEvent *event) override;
private:
    struct Impl;
    std::unique_ptr<Impl> m;
    // Show the oldest queued error, or hide an empty presentation.
    void present();
    // Render a frame at the containing window's effective scale.
    bool refresh();
};

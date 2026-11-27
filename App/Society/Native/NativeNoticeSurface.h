#pragma once

#include <QQuickPaintedItem>
#include <memory>

// Adapts an LVUI Alert to the existing Qt shell while iiGUI owns its widgets.
class NativeNoticeSurface final : public QQuickPaintedItem {
    Q_OBJECT
public:
    // Construct the LVUI view in an initially hidden shell-owned surface.
    explicit NativeNoticeSurface(QQuickItem *parent = nullptr);
    // Retire the iiGUI view before the Qt host is destroyed.
    ~NativeNoticeSurface() override;

    // Report whether the generated view and drawing device are ready.
    [[nodiscard]] bool ready() const;
    // Describe a construction or drawing failure to the shell bridge.
    [[nodiscard]] QString errorString() const;
    // Return the currently published notice title for accessibility.
    [[nodiscard]] QString title() const;
    // Return the currently published explanatory text for accessibility.
    [[nodiscard]] QString message() const;
    // Return the alert card's logical bounds relative to this surface.
    [[nodiscard]] QRectF alertBounds() const;
    // Return the primary action's logical bounds relative to this surface.
    [[nodiscard]] QRectF primaryActionBounds() const;
    // Publish a complete notice and return false if it could not be displayed.
    bool present(const QString &title, const QString &message, const QString &primaryText);
    // Activate the actual iiGUI action, including its normal callback.
    bool activatePrimary();
    // Present the last completed iiGUI frame through the existing Qt host.
    void paint(QPainter *painter) override;

protected:
    // Recompute the card and drawing when the host viewport changes.
    void geometryChange(const QRectF &next, const QRectF &previous) override;
    // Forward pointer movement to the retained iiGUI widget tree.
    void hoverMoveEvent(QHoverEvent *event) override;
    // Clear the retained hover state when the pointer leaves the surface.
    void hoverLeaveEvent(QHoverEvent *event) override;
    // Forward a primary pointer press in logical surface coordinates.
    void mousePressEvent(QMouseEvent *event) override;
    // Forward a primary pointer release to the same retained widget tree.
    void mouseReleaseEvent(QMouseEvent *event) override;
    // Cancel an interrupted press rather than delivering a stale click.
    void mouseUngrabEvent() override;
    // Translate supported keyboard presses to iiGUI key events.
    void keyPressEvent(QKeyEvent *event) override;
    // Translate supported keyboard releases to iiGUI key events.
    void keyReleaseEvent(QKeyEvent *event) override;
    // Retire focus state when the containing shell transfers focus elsewhere.
    void focusOutEvent(QFocusEvent *event) override;

private:
    // Hide rendering, model and generation state behind the host interface.
    struct Impl;
    std::unique_ptr<Impl> m;
    // Render at the host window's effective scale without changing logical size.
    bool refresh();
};

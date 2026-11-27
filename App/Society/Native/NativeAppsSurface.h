#pragma once

#include <QQuickPaintedItem>
#include <memory>

class NativeAppsViewModel;

/// Hosts the generated LVUI Apps view in the existing Qt shell. The catalogue
/// remains owned by its environment; this adapter holds only a guarded pointer.
/// All calls, input and catalogue signals must be delivered on the UI thread.
class NativeAppsSurface final : public QQuickPaintedItem {
    Q_OBJECT
public:
    explicit NativeAppsSurface(QQuickItem *parent = nullptr);
    ~NativeAppsSurface() override;
    /// Report whether the LVUI view and its bundled assets initialized.
    [[nodiscard]] bool ready() const;
    /// Return the last initialization or paint failure without modifying it.
    [[nodiscard]] QString errorString() const;
    /// Borrow the view-owned model; the pointer expires with this surface.
    [[nodiscard]] NativeAppsViewModel *model() const;
    /// Replace the borrowed catalogue and subscribe to its value snapshots.
    void setCatalogue(QObject *catalogue);
    /// Borrow the existing environment's browser shortcut provider.
    void setEnvironment(QObject *environment);
    /// Repaint the retained view at the current logical size and device scale.
    void refresh();
    /// Present the last completed frame; the painter remains owned by Qt.
    void paint(QPainter *) override;
public slots:
    /// Re-read only catalogue values. No app is opened by synchronization.
    void synchronizeCatalogue();
protected:
    // These framework callbacks forward the corresponding input to iiGUI on
    // the UI thread. They borrow Qt events only during the call and repaint
    // after state changes; they do not own or launch external resources.
    void geometryChange(const QRectF &, const QRectF &) override;
    void hoverMoveEvent(QHoverEvent *) override;
    void hoverLeaveEvent(QHoverEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseUngrabEvent() override;
    void wheelEvent(QWheelEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void keyReleaseEvent(QKeyEvent *) override;
    void inputMethodEvent(QInputMethodEvent *) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery) const override;
    void focusOutEvent(QFocusEvent *) override;
private:
    /// Dispatch an explicit card action after the current LVUI input event ends.
    void requestAction(const QString &id, const QString &displayedAction);
    struct Impl;
    std::unique_ptr<Impl> m;
};

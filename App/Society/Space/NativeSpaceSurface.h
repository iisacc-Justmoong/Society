#pragma once

#include <QQuickPaintedItem>
#include <functional>
#include <memory>
#include <optional>

class NativeSpaceViewModel;
class SpaceDictation;

/// Qt shell adapter for an LVUI/iiGUI Space view and its container store.
/// The retained native view owns all controls; this item only forwards host input.
class NativeSpaceSurface final : public QQuickPaintedItem {
    Q_OBJECT
public:
    /// Supplies a global pointer position, or no value when the host has none.
    using GlobalPointerProvider = std::function<std::optional<QPoint>()>;
    explicit NativeSpaceSurface(QQuickItem *parent = nullptr);
    ~NativeSpaceSurface() override;
    /// Whether the LVUI surface and the requested vector image initialized.
    [[nodiscard]] bool ready() const;
    [[nodiscard]] QString errorString() const;
    [[nodiscard]] NativeSpaceViewModel *model() const;
    /// Replace the host pointer source; an empty provider restores the system cursor.
    void setGlobalPointerProvider(GlobalPointerProvider provider);
    /// Inject a native speech boundary; chiefly useful for deterministic host tests.
    void setDictationProvider(std::unique_ptr<SpaceDictation> provider);
    /// Open a validated container, dropping any previous container's snapshot first.
    bool openContainer(const QString &root);
    /// Clear the previous container when the drive is no longer available.
    void clearContainer();
    /// Re-read persisted data, retaining the selected session if it still exists.
    bool reload();
    /// Find a native control's logical bounds for host integration and tests.
    [[nodiscard]] QRectF controlBounds(const QString &id) const;
    /// Find a repeated row's control without confusing IDs in different rows.
    [[nodiscard]] QRectF keyedControlBounds(const QString &host, const QString &key, const QString &id) const;
    /// Report the enabled state exposed by a native control.
    [[nodiscard]] bool controlEnabled(const QString &id) const;
    /// Activate a native control by ID, following its normal action contract.
    bool activateControl(const QString &id);
    /// Lay out and repaint the retained iiGUI view into the shell's image.
    void refresh();
    void paint(QPainter *painter) override;
protected:
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
    /// Start sampling only while the Space item and its host window are visible.
    void updateGazeTracking();
    /// Translate the current system cursor into this item's coordinates and repaint on change.
    void sampleGazePointer();
    /// Apply a host-local pointer position to both eyes without moving the SVG face.
    bool updateGaze(const QPointF &pointer);
    /// Load one session only after the store confirms that it exists.
    bool selectSession(const std::string &id);
    /// Open the native iiGUI file chooser for the current session only.
    void chooseAttachments();
    /// Start, finish or discard dictation without losing the editable prompt.
    void toggleDictation();
    void cancelDictation();
    /// Rebuild only actual visible relationships after layout or scrolling changes.
    bool updateConnections(double scale);
    struct Impl;
    std::unique_ptr<Impl> m;
};

#pragma once

#include "NativeWorkspaceViewModel.h"
#include <QQuickPaintedItem>
#include <memory>

// The shell presents and forwards input to the LVUI/iiGUI-owned view.
class NativeWorkspaceSurface final : public QQuickPaintedItem {
    Q_OBJECT
public:
    explicit NativeWorkspaceSurface(QQuickItem *parent = nullptr);
    ~NativeWorkspaceSurface() override;
    [[nodiscard]] bool ready() const;
    [[nodiscard]] QString errorString() const;
    [[nodiscard]] NativeWorkspaceViewModel *model() const;
    bool activateControl(const QString &id);
    bool focusControl(const QString &id);
    [[nodiscard]] QRectF controlBounds(const QString &id) const;
    [[nodiscard]] bool controlOpened(const QString &id) const;
    void refresh();
    void paint(QPainter *painter) override;
signals:
    void pageRequested(int page);
    void groupRequested(const QString &kind, const QString &id);
    void openRequested(const QString &path);
    void upRequested();
protected:
    void geometryChange(const QRectF &, const QRectF &) override;
    void hoverMoveEvent(QHoverEvent *) override;
    void hoverLeaveEvent(QHoverEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void mouseUngrabEvent() override;
    void wheelEvent(QWheelEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void keyReleaseEvent(QKeyEvent *) override;
    void inputMethodEvent(QInputMethodEvent *) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery) const override;
    void focusOutEvent(QFocusEvent *) override;
private:
    struct Impl;
    std::unique_ptr<Impl> m;
};

#pragma once

#include <iiGUI/Painter.h>
#include <QImage>
#include <memory>

class QPainter;

// Presents iiGUI's retained drawing commands into a Qt-owned image. The view,
// layout, input and component styles remain owned by LVUI and iiGUI.
class QtPaintDevice final : public ii::gui::PaintDevice {
public:
    QtPaintDevice();
    ~QtPaintDevice() override;
    ii::gui::PaintCapabilities capabilities() const noexcept override;
    ii::core::Result<void> begin_frame(const ii::gui::PaintFrameInfo &) override;
    ii::core::Result<void> begin_scene(const ii::gui::PaintSceneInfo &) override;
    ii::core::Result<void> end_scene() override;
    ii::core::Result<void> begin_layer(const ii::gui::PaintLayerInfo &) override;
    ii::core::Result<void> draw_panels(std::span<const ii::gui::Panel>) override;
    ii::core::Result<void> draw_text(std::span<const ii::gui::TextRun>) override;
    ii::core::Result<void> draw_images(std::span<const ii::gui::ImageRun>) override;
    ii::core::Result<void> end_layer() override;
    ii::core::Result<void> present() override;
    void abort_frame() noexcept override;
    [[nodiscard]] QImage image() const { return m_presented; }
private:
    QImage m_pending, m_presented;
    std::unique_ptr<QPainter> m_painter;
};

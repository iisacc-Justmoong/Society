#include "QtPaintDevice.h"
#include <iiGUI/Image.h>
#include <QFont>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

namespace {
QRectF rect(const ii::gui::Rect &value) { return {value.x, value.y, value.width, value.height}; }
qreal encode(float value) {
    const auto linear = std::clamp(static_cast<double>(value), 0.0, 1.0);
    return linear <= 0.0031308 ? 12.92 * linear : 1.055 * std::pow(linear, 1.0 / 2.4) - 0.055;
}
QColor color(const ii::gui::Color &value) {
    return QColor::fromRgbF(encode(value.red), encode(value.green), encode(value.blue),
                            std::clamp(static_cast<double>(value.alpha), 0.0, 1.0));
}
ii::core::Result<void> unsupported(const char *message) {
    return ii::core::fail(ii::core::ErrorCode::unsupported, message);
}
}

QtPaintDevice::QtPaintDevice() = default;
QtPaintDevice::~QtPaintDevice() = default;
ii::gui::PaintCapabilities QtPaintDevice::capabilities() const noexcept { return {.content_transform = true}; }

ii::core::Result<void> QtPaintDevice::begin_frame(const ii::gui::PaintFrameInfo &info) {
    if (m_painter || !info.pixel_width || !info.pixel_height || info.pixel_width > 8192 || info.pixel_height > 8192)
        return ii::core::fail(ii::core::ErrorCode::invalid_argument, "Invalid iiGUI frame");
    m_pending = QImage(static_cast<int>(info.pixel_width), static_cast<int>(info.pixel_height), QImage::Format_ARGB32_Premultiplied);
    if (m_pending.isNull()) return ii::core::fail(ii::core::ErrorCode::failed, "Cannot allocate iiGUI frame");
    m_pending.setDevicePixelRatio(info.scale);
    m_pending.fill(Qt::transparent);
    m_painter = std::make_unique<QPainter>(&m_pending);
    m_painter->setRenderHint(QPainter::Antialiasing);
    m_painter->setRenderHint(QPainter::SmoothPixmapTransform);
    return {};
}
ii::core::Result<void> QtPaintDevice::begin_scene(const ii::gui::PaintSceneInfo &info) {
    if (info.bounds) return unsupported("Nested scene sources are not supported by this adapter");
    return {};
}
ii::core::Result<void> QtPaintDevice::end_scene() { return {}; }
ii::core::Result<void> QtPaintDevice::begin_layer(const ii::gui::PaintLayerInfo &info) {
    if (info.backdrop || info.shadow) return unsupported("Material and shadow layers require a native renderer");
    m_painter->save();
    if (info.clip) m_painter->setClipRect(rect(*info.clip), Qt::IntersectClip);
    const auto &t = info.transform;
    m_painter->setOpacity(m_painter->opacity() * t.opacity);
    m_painter->translate(t.origin_x + t.offset_x, t.origin_y + t.offset_y);
    m_painter->scale(t.scale, t.scale);
    m_painter->translate(-t.origin_x, -t.origin_y);
    return {};
}
ii::core::Result<void> QtPaintDevice::draw_panels(std::span<const ii::gui::Panel> panels) {
    for (const auto &panel : panels) {
        auto bounds = rect(panel.bounds);
        QLinearGradient gradient(bounds.topLeft(), bounds.bottomLeft());
        gradient.setColorAt(0, color(panel.top));
        gradient.setColorAt(1, color(panel.bottom));
        if (panel.stroke_width > 0) {
            const qreal half = panel.stroke_width / 2;
            bounds.adjust(half, half, -half, -half);
            m_painter->setBrush(Qt::NoBrush);
            m_painter->setPen(QPen(QBrush(gradient), panel.stroke_width));
            m_painter->drawRoundedRect(bounds, std::max(0.0, double(panel.radius) - half), std::max(0.0, double(panel.radius) - half));
        } else {
            m_painter->setPen(Qt::NoPen);
            m_painter->setBrush(QBrush(gradient));
            m_painter->drawRoundedRect(bounds, panel.radius, panel.radius);
        }
    }
    return {};
}
ii::core::Result<void> QtPaintDevice::draw_text(std::span<const ii::gui::TextRun> runs) {
    for (const auto &run : runs) {
        m_painter->save();
        m_painter->setClipRect(run.clip ? rect(*run.clip) : rect(run.bounds), Qt::IntersectClip);
        QFont font(QString::fromStdString(run.font_family));
        font.setPixelSize(std::max(1, qRound(run.font_size)));
        font.setWeight(static_cast<QFont::Weight>(run.font_weight));
        font.setUnderline(run.underline);
        m_painter->setFont(font);
        m_painter->setPen(color(run.color));
        const auto alignment = run.alignment == ii::gui::TextAlignment::right ? Qt::AlignRight
            : run.alignment == ii::gui::TextAlignment::center ? Qt::AlignHCenter : Qt::AlignLeft;
        auto text = QString::fromStdString(run.text);
        if (run.elide_right) text = m_painter->fontMetrics().elidedText(text, Qt::ElideRight, qRound(run.bounds.width));
        m_painter->drawText(rect(run.bounds), alignment | Qt::AlignVCenter, text);
        m_painter->restore();
    }
    return {};
}
ii::core::Result<void> QtPaintDevice::draw_images(std::span<const ii::gui::ImageRun> runs) {
    for (const auto &run : runs) {
        if (!run.image || run.gradient || run.border_width > 0) return unsupported("Image effect is not supported by this adapter");
        const auto bytes = run.image->rgba();
        QImage source(bytes.data(), static_cast<int>(run.image->width()), static_cast<int>(run.image->height()),
                      static_cast<qsizetype>(run.image->width()) * 4, QImage::Format_RGBA8888);
        if (run.tint) {
            source = source.copy();
            QPainter tint(&source);
            tint.setCompositionMode(QPainter::CompositionMode_SourceIn);
            tint.fillRect(source.rect(), color(*run.tint));
        }
        m_painter->save();
        m_painter->setOpacity(m_painter->opacity() * run.opacity);
        if (run.radius > 0) {
            QPainterPath clip;
            clip.addRoundedRect(rect(run.bounds), run.radius, run.radius);
            m_painter->setClipPath(clip, Qt::IntersectClip);
        }
        const QRectF sourceRect(run.source.x * source.width(), run.source.y * source.height(),
                                run.source.width * source.width(), run.source.height * source.height());
        m_painter->drawImage(rect(run.bounds), source, sourceRect);
        m_painter->restore();
    }
    return {};
}
ii::core::Result<void> QtPaintDevice::end_layer() { m_painter->restore(); return {}; }
ii::core::Result<void> QtPaintDevice::present() { m_painter.reset(); m_presented = std::move(m_pending); return {}; }
void QtPaintDevice::abort_frame() noexcept { m_painter.reset(); m_pending = {}; }

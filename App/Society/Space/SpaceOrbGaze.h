#pragma once

#include <iiGUI/Image.h>
#include <QByteArray>
#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <expected>
#include <memory>

/// Produces the supplied SVG orb with its eye paths translated over a fixed face.
/// The original SVG remains the sole source for all geometry and colors.
class SpaceOrbGaze final {
public:
    /// Decode the original artwork and its face and eye layers at the requested resolution.
    /// Reject an invalid asset rather than silently drawing a different character.
    [[nodiscard]] static std::expected<SpaceOrbGaze, QString> create(const QByteArray &svg, std::uint32_t size);

    /// Resolve a pointer in the host's coordinate space to a bounded logical displacement.
    /// The two eyes share a displacement, with a maximum radius of 8% of the orb diameter.
    [[nodiscard]] static QPointF offsetForPointer(const QPointF &pointer, const QRectF &orbBounds) noexcept;

    /// Convert a logical displacement to a pixel while retaining the circular movement limit.
    [[nodiscard]] static QPoint rasterOffset(const QPointF &offset, const QRectF &orbBounds,
                                             const QSize &rasterSize) noexcept;

    /// Compose a new immutable iiGUI image; zero displacement returns the original raster.
    [[nodiscard]] std::expected<std::shared_ptr<const ii::gui::RasterImage>, QString>
    imageAt(const QPoint &displacement) const;

private:
    /// Straight-alpha face raster containing the unchanged circle and outline.
    QImage m_face;
    /// Transparent raster containing only the two original SVG eye paths.
    QImage m_eyes;
    /// Exact neutral decoding of the complete original artwork.
    std::shared_ptr<const ii::gui::RasterImage> m_neutral;
};

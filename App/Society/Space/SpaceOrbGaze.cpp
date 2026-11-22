#include "SpaceOrbGaze.h"

#include <QPainter>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <algorithm>
#include <cmath>
#include <vector>

namespace {
/// The face geometry permits the eyes to move within this fraction of the orb diameter.
constexpr double maximumGazeFraction = 0.08;

/// Retain SVG definitions and exactly one of the original character's face or eye layers.
std::expected<QByteArray, QString> svgLayer(const QByteArray &source, bool face) {
    QXmlStreamReader reader(source);
    QByteArray result;
    QXmlStreamWriter writer(&result);
    int depth = 0, characterDepth = -1, circles = 0, paths = 0;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            ++depth;
            if (reader.name() == u"g" && reader.attributes().value(u"id") == u"Character")
                characterDepth = depth;
            if (characterDepth > 0 && depth == characterDepth + 1) {
                const bool circle = reader.name() == u"circle";
                const bool path = reader.name() == u"path";
                circles += circle;
                paths += path;
                if ((face && path) || (!face && circle)) {
                    reader.skipCurrentElement();
                    --depth;
                    continue;
                }
            }
        }
        writer.writeCurrentToken(reader);
        if (reader.isEndElement()) {
            if (depth == characterDepth) characterDepth = -1;
            --depth;
        }
    }
    if (reader.hasError() || circles != 1 || paths != 2)
        return std::unexpected(QStringLiteral("The Space AI SVG must contain one face and two eye paths."));
    return result;
}

/// Copy an immutable iiGUI straight-alpha raster into an independently owned Qt image.
QImage copyImage(const ii::gui::RasterImage &image) {
    const auto bytes = image.rgba();
    return QImage(bytes.data(), int(image.width()), int(image.height()), int(image.width() * 4),
                  QImage::Format_RGBA8888).copy();
}

/// Decode a memory-only SVG with the same iiGUI codec used by the native view.
std::expected<std::shared_ptr<const ii::gui::RasterImage>, QString>
decode(const QByteArray &svg, std::uint32_t size) {
    const auto image = ii::gui::svg_image(std::string_view(svg.constData(), std::size_t(svg.size())), size, size);
    if (!image) return std::unexpected(QString::fromStdString(image.error().message));
    return *image;
}
}

std::expected<SpaceOrbGaze, QString> SpaceOrbGaze::create(const QByteArray &svg, std::uint32_t size) {
    const auto faceSvg = svgLayer(svg, true);
    if (!faceSvg) return std::unexpected(faceSvg.error());
    const auto eyesSvg = svgLayer(svg, false);
    if (!eyesSvg) return std::unexpected(eyesSvg.error());
    const auto neutral = decode(svg, size);
    if (!neutral) return std::unexpected(neutral.error());
    const auto face = decode(*faceSvg, size);
    if (!face) return std::unexpected(face.error());
    const auto eyes = decode(*eyesSvg, size);
    if (!eyes) return std::unexpected(eyes.error());
    SpaceOrbGaze gaze;
    gaze.m_neutral = *neutral;
    gaze.m_face = copyImage(**face);
    gaze.m_eyes = copyImage(**eyes);
    if (gaze.m_face.isNull() || gaze.m_eyes.isNull())
        return std::unexpected(QStringLiteral("Could not prepare the Space AI image layers."));
    return gaze;
}

QPointF SpaceOrbGaze::offsetForPointer(const QPointF &pointer, const QRectF &orbBounds) noexcept {
    const double diameter = std::min(orbBounds.width(), orbBounds.height());
    const QPointF delta = pointer - orbBounds.center();
    if (!(diameter > 0) || !std::isfinite(diameter) || !std::isfinite(delta.x()) || !std::isfinite(delta.y()))
        return {};
    const double distance = std::hypot(delta.x(), delta.y());
    if (!(distance > 0) || !std::isfinite(distance)) return {};
    // A finite reach avoids a jump at the center and caps movement inside the face.
    constexpr double fullReachInDiameters = 1.5;
    const double radius = diameter * maximumGazeFraction * std::min(1.0, distance / (diameter * fullReachInDiameters));
    return delta * (radius / distance);
}

QPoint SpaceOrbGaze::rasterOffset(const QPointF &offset, const QRectF &orbBounds,
                                 const QSize &rasterSize) noexcept {
    if (orbBounds.width() <= 0 || orbBounds.height() <= 0 || rasterSize.isEmpty() ||
        !std::isfinite(offset.x()) || !std::isfinite(offset.y())) return {};
    double x = offset.x() * rasterSize.width() / orbBounds.width();
    double y = offset.y() * rasterSize.height() / orbBounds.height();
    const double limit = std::min(rasterSize.width(), rasterSize.height()) * maximumGazeFraction;
    const double distance = std::hypot(x, y);
    if (!std::isfinite(distance)) return {};
    if (distance > limit) { x *= limit / distance; y *= limit / distance; }
    QPoint rounded(qRound(x), qRound(y));
    const double roundedDistance = std::hypot(rounded.x(), rounded.y());
    if (roundedDistance > limit) {
        // Rounding both axes outward must not exceed the specified circular bound.
        rounded = {int(std::trunc(rounded.x() * limit / roundedDistance)),
                   int(std::trunc(rounded.y() * limit / roundedDistance))};
    }
    return rounded;
}

std::expected<std::shared_ptr<const ii::gui::RasterImage>, QString>
SpaceOrbGaze::imageAt(const QPoint &displacement) const {
    if (displacement.isNull()) return m_neutral;
    QImage image = m_face.copy();
    if (image.isNull()) return std::unexpected(QStringLiteral("Could not compose the Space AI image."));
    {
        QPainter painter(&image);
        painter.drawImage(displacement, m_eyes);
    }
    const auto stride = std::size_t(image.width()) * 4;
    std::vector<std::uint8_t> pixels(stride * std::size_t(image.height()));
    for (int y = 0; y < image.height(); ++y)
        std::copy_n(image.constScanLine(y), stride, pixels.data() + stride * std::size_t(y));
    auto result = ii::gui::rgba_image(std::uint32_t(image.width()), std::uint32_t(image.height()), std::move(pixels));
    if (!result) return std::unexpected(QString::fromStdString(result.error().message));
    return *result;
}

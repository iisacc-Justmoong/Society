#pragma once
#include <QRectF>
#include <optional>

namespace society::native {
struct PhotoRegionGeometry final { QRectF outer,inner; };
// Reject a borrowed surface if the toolkit and native host disagree on the
// mapping between logical coordinates and display pixels.
[[nodiscard]] bool photoRegionScaleMatches(double toolkitScale,double nativeScale) noexcept;
// Input rectangles use top-left window coordinates; inner coordinates are
// relative to a flipped clipping view, independent of the native parent's axis.
[[nodiscard]] std::optional<PhotoRegionGeometry> photoRegionGeometry(
    const QRectF& bounds,const QRectF& clip,const QRectF& nativeParentBounds,bool parentFlipped);
} // namespace society::native

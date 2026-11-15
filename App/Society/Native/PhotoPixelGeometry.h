#pragma once
#include <optional>

namespace society::native {
struct PhotoLogicalRect final {double x,y,width,height;};
struct PhotoPixelRect final {
    int x,y,width,height;
    bool operator==(const PhotoPixelRect&) const=default;
};
struct PhotoPixelFrames final {PhotoPixelRect outer,inner;};

// Convert shared absolute edges once so rounding cannot shift the clipped
// surface relative to the displayed or hit-tested content.
[[nodiscard]] std::optional<PhotoPixelFrames> photoPixelGeometry(
    PhotoLogicalRect bounds,PhotoLogicalRect clip,int parentWidth,int parentHeight,double scale) noexcept;
[[nodiscard]] bool photoScaleCompatible(double logicalScale,unsigned nativeDpi) noexcept;
} // namespace society::native

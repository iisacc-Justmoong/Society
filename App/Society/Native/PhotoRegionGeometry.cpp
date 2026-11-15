#include "PhotoRegionGeometry.h"
#include <algorithm>
#include <cmath>

namespace society::native {
bool photoRegionScaleMatches(double toolkitScale,double nativeScale) noexcept {
    return std::isfinite(toolkitScale)&&std::isfinite(nativeScale)&&toolkitScale>0&&nativeScale>0&&
        std::abs(toolkitScale-nativeScale)<=1e-6*std::max({1.0,toolkitScale,nativeScale});
}
std::optional<PhotoRegionGeometry> photoRegionGeometry(const QRectF& bounds,const QRectF& clip,
                                                        const QRectF& parent,bool parentFlipped) {
    const auto valid=[](const QRectF& value){return std::isfinite(value.x()) && std::isfinite(value.y()) &&
        std::isfinite(value.width()) && std::isfinite(value.height()) && !value.isEmpty();};
    if (!valid(bounds) || !valid(clip) || !valid(parent) || !bounds.contains(clip) ||
        !QRectF(0,0,parent.width(),parent.height()).contains(clip)) return std::nullopt;
    PhotoRegionGeometry result;
    result.outer=QRectF(parent.x()+clip.x(),parent.y()+(parentFlipped?clip.y():parent.height()-clip.y()-clip.height()),
                        clip.width(),clip.height());
    result.inner=QRectF(bounds.x()-clip.x(),bounds.y()-clip.y(),bounds.width(),bounds.height());
    if (!valid(result.outer) || !valid(result.inner)) return std::nullopt;
    return result;
}
} // namespace society::native

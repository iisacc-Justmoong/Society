#include "PhotoPixelGeometry.h"
#include <cmath>
#include <cstdint>
#include <limits>

namespace society::native {
namespace {
using Integer=std::int64_t;
constexpr auto minimum=static_cast<Integer>(std::numeric_limits<int>::min());
constexpr auto maximum=static_cast<Integer>(std::numeric_limits<int>::max());
bool valid(PhotoLogicalRect r){
    return std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.width) && std::isfinite(r.height) &&
        r.width>0 && r.height>0 && std::isfinite(r.x+r.width) && std::isfinite(r.y+r.height);
}
std::optional<Integer> edge(double value,double scale){
    const double scaled=value*scale;
    if(!std::isfinite(scaled) || scaled<double(minimum)-.5 || scaled>double(maximum)+.5)return {};
    const auto result=static_cast<Integer>(std::round(scaled));
    if(result<minimum || result>maximum)return {};
    return result;
}
bool fits(Integer value){return value>=minimum && value<=maximum;}
}
bool photoScaleCompatible(double logicalScale,unsigned nativeDpi) noexcept {
    return nativeDpi && std::isfinite(logicalScale) && logicalScale>0 &&
        std::abs(logicalScale-double(nativeDpi)/96.0)<1e-4;
}
std::optional<PhotoPixelFrames> photoPixelGeometry(PhotoLogicalRect bounds,PhotoLogicalRect clip,
                                                  int parentWidth,int parentHeight,double scale) noexcept {
    if(!valid(bounds) || !valid(clip) || parentWidth<=0 || parentHeight<=0 || !std::isfinite(scale) || scale<=0 ||
       clip.x<bounds.x || clip.y<bounds.y || clip.x+clip.width>bounds.x+bounds.width ||
       clip.y+clip.height>bounds.y+bounds.height)return {};
    const auto bl=edge(bounds.x,scale),bt=edge(bounds.y,scale),br=edge(bounds.x+bounds.width,scale),bb=edge(bounds.y+bounds.height,scale);
    const auto cl=edge(clip.x,scale),ct=edge(clip.y,scale),cr=edge(clip.x+clip.width,scale),cb=edge(clip.y+clip.height,scale);
    if(!bl||!bt||!br||!bb||!cl||!ct||!cr||!cb || *cl<0 || *ct<0 || *cr>parentWidth || *cb>parentHeight ||
       *cr<=*cl || *cb<=*ct || *br<=*bl || *bb<=*bt)return {};
    const auto innerX=*bl-*cl,innerY=*bt-*ct,innerWidth=*br-*bl,innerHeight=*bb-*bt;
    if(!fits(innerX)||!fits(innerY)||!fits(innerWidth)||!fits(innerHeight))return {};
    return PhotoPixelFrames{{int(*cl),int(*ct),int(*cr-*cl),int(*cb-*ct)},
                            {int(innerX),int(innerY),int(innerWidth),int(innerHeight)}};
}
} // namespace society::native

#include "PhotoNativeRegion.h"
#include "PhotoClippedRegion.h"
#include "PhotoWindowsDriver.h"
#include <utility>

namespace society::native {
namespace {
PhotoLogicalRect logical(const QRectF& rect){return {rect.x(),rect.y(),rect.width(),rect.height()};}
}
struct PhotoNativeRegion::Impl final {std::unique_ptr<PhotoClippedRegion> region;};
PhotoNativeRegion::PhotoNativeRegion(std::unique_ptr<Impl> impl):impl_(std::move(impl)) {}
PhotoNativeRegion::~PhotoNativeRegion()=default;
std::unique_ptr<PhotoNativeRegion> PhotoNativeRegion::create(void* parent,const PhotoArea& area) noexcept {
    if(!parent || area.mode!=PhotoArea::Mode::visible)return {};
    try {
        auto region=PhotoClippedRegion::create(photoWindowsDriver(),PhotoWindow{reinterpret_cast<std::uintptr_t>(parent)},
            logical(area.bounds),logical(area.clip),area.deviceScale);
        if(!region)return {};
        auto impl=std::make_unique<Impl>();impl->region=std::move(region);
        return std::unique_ptr<PhotoNativeRegion>(new PhotoNativeRegion(std::move(impl)));
    } catch(...) {return {};}
}
void* PhotoNativeRegion::nativeParent() const noexcept {
    return impl_ && impl_->region?reinterpret_cast<void*>(impl_->region->nativeParent().native):nullptr;
}
bool PhotoNativeRegion::update(const PhotoArea& area) noexcept {
    return impl_ && impl_->region && area.mode==PhotoArea::Mode::visible &&
        impl_->region->update(logical(area.bounds),logical(area.clip),area.deviceScale);
}
bool PhotoNativeRegion::setVisible(bool visible) noexcept {
    return impl_ && impl_->region && impl_->region->setVisible(visible);
}
} // namespace society::native

#import <UIKit/UIKit.h>
#include "PhotoNativeRegion.h"
#include "PhotoRegionGeometry.h"
#include <exception>
#include <utility>

namespace society::native {
namespace {
CGRect rect(const QRectF& value){return CGRectMake(value.x(),value.y(),value.width(),value.height());}
std::optional<PhotoRegionGeometry> geometry(UIView* root,const PhotoArea& area){
    if(!root||!root.window||!root.window.windowScene||area.mode!=PhotoArea::Mode::visible||
       !photoRegionScaleMatches(area.deviceScale,root.window.screen.scale))return std::nullopt;
    const auto bounds=root.bounds;
    return photoRegionGeometry(area.bounds,area.clip,
        QRectF(bounds.origin.x,bounds.origin.y,bounds.size.width,bounds.size.height),true);
}
}
struct PhotoNativeRegion::Impl final {
    __weak UIView* root=nil;
    __strong UIView* clip=nil;
    __strong UIView* content=nil;
};
PhotoNativeRegion::PhotoNativeRegion(std::unique_ptr<Impl> impl):impl_(std::move(impl)) {}
PhotoNativeRegion::~PhotoNativeRegion(){
    if(![NSThread isMainThread])std::terminate();
    if(impl_&&impl_->clip)[impl_->clip removeFromSuperview];
}
std::unique_ptr<PhotoNativeRegion> PhotoNativeRegion::create(void* nativeParent,const PhotoArea& area) noexcept {
    if(![NSThread isMainThread]||!nativeParent)return {};
    @autoreleasepool {
        @try {
            try {
                UIView* root=(__bridge UIView*)nativeParent;
                if(![root isKindOfClass:UIView.class])return {};
                const auto frames=geometry(root,area);if(!frames)return {};
                auto impl=std::make_unique<Impl>();impl->root=root;
                impl->clip=[[UIView alloc] initWithFrame:rect(frames->outer)];
                impl->content=[[UIView alloc] initWithFrame:rect(frames->inner)];
                if(!impl->clip||!impl->content)return {};
                impl->clip.clipsToBounds=YES;impl->clip.hidden=YES;
                impl->clip.opaque=NO;impl->clip.backgroundColor=UIColor.clearColor;
                impl->content.opaque=NO;impl->content.backgroundColor=UIColor.clearColor;
                [impl->clip addSubview:impl->content];
                auto region=std::unique_ptr<PhotoNativeRegion>(new PhotoNativeRegion(std::move(impl)));
                [root addSubview:region->impl_->clip];
                return region;
            }catch(...){return {};}
        }@catch(NSException*){return {};}
    }
}
void* PhotoNativeRegion::nativeParent() const noexcept {
    return impl_?(__bridge void*)impl_->content:nullptr;
}
bool PhotoNativeRegion::update(const PhotoArea& area) noexcept {
    if(![NSThread isMainThread]||!impl_)return false;
    @try {
        UIView* root=impl_->root;
        if(!root||impl_->clip.superview!=root)return false;
        const auto frames=geometry(root,area);if(!frames)return false;
        const auto outer=rect(frames->outer),inner=rect(frames->inner);
        if(!CGRectEqualToRect(impl_->clip.frame,outer))impl_->clip.frame=outer;
        if(!CGRectEqualToRect(impl_->content.frame,inner))impl_->content.frame=inner;
        return true;
    }@catch(NSException*){return false;}
}
bool PhotoNativeRegion::setVisible(bool visible) noexcept {
    if(![NSThread isMainThread]||!impl_)return false;
    @try {
        UIView* root=impl_->root;
        if(!root||!root.window||!root.window.windowScene||impl_->clip.superview!=root)return false;
        impl_->clip.hidden=!visible;return true;
    }@catch(NSException*){return false;}
}
} // namespace society::native

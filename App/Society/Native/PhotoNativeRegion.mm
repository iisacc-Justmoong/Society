#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>
#include "PhotoNativeRegion.h"
#include "PhotoRegionGeometry.h"
#include <exception>
#include <utility>

@interface SocietyPhotoRegionView : NSView
@end
@implementation SocietyPhotoRegionView
- (BOOL)isFlipped { return YES; }
@end

namespace society::native {
namespace {
NSRect rect(const QRectF& value){return NSMakeRect(value.x(),value.y(),value.width(),value.height());}
std::optional<PhotoRegionGeometry> geometry(NSView* root,const PhotoArea& area){
    if (!root || !root.window || area.mode!=PhotoArea::Mode::visible) return std::nullopt;
    const auto bounds=root.bounds;
    return photoRegionGeometry(area.bounds,area.clip,QRectF(bounds.origin.x,bounds.origin.y,bounds.size.width,bounds.size.height),root.isFlipped);
}
}
struct PhotoNativeRegion::Impl final {
    __weak NSView* root=nil;
    __strong SocietyPhotoRegionView* clip=nil;
    __strong SocietyPhotoRegionView* content=nil;
};
PhotoNativeRegion::PhotoNativeRegion(std::unique_ptr<Impl> impl):impl_(std::move(impl)) {}
PhotoNativeRegion::~PhotoNativeRegion(){
    if (![NSThread isMainThread]) std::terminate();
    if (impl_ && impl_->clip) [impl_->clip removeFromSuperview];
}
std::unique_ptr<PhotoNativeRegion> PhotoNativeRegion::create(void* nativeParent,const PhotoArea& area) noexcept {
    if (![NSThread isMainThread] || !nativeParent) return {};
    @autoreleasepool {
        @try {
            try {
                NSView* root=(__bridge NSView*)nativeParent;
                if (![root isKindOfClass:NSView.class]) return {};
                const auto frames=geometry(root,area);if (!frames) return {};
                auto impl=std::make_unique<Impl>();impl->root=root;
                impl->clip=[[SocietyPhotoRegionView alloc] initWithFrame:rect(frames->outer)];
                impl->content=[[SocietyPhotoRegionView alloc] initWithFrame:rect(frames->inner)];
                if (!impl->clip || !impl->content) return {};
                impl->clip.wantsLayer=YES;impl->clip.layer.masksToBounds=YES;impl->clip.hidden=YES;
                [impl->clip addSubview:impl->content];
                auto region=std::unique_ptr<PhotoNativeRegion>(new PhotoNativeRegion(std::move(impl)));
                [root addSubview:region->impl_->clip positioned:NSWindowAbove relativeTo:nil];
                return region;
            } catch (...) { return {}; }
        } @catch (NSException*) { return {}; }
    }
}
void* PhotoNativeRegion::nativeParent() const noexcept {
    return impl_?(__bridge void*)impl_->content:nullptr;
}
bool PhotoNativeRegion::update(const PhotoArea& area) noexcept {
    if (![NSThread isMainThread] || !impl_) return false;
    @try {
        NSView* root=impl_->root;
        if (!root || impl_->clip.superview!=root) return false;
        const auto frames=geometry(root,area);if (!frames) return false;
        const auto outer=rect(frames->outer),inner=rect(frames->inner);
        if (!NSEqualRects(impl_->clip.frame,outer)) impl_->clip.frame=outer;
        if (!NSEqualRects(impl_->content.frame,inner)) impl_->content.frame=inner;
        return true;
    } @catch (NSException*) { return false; }
}
bool PhotoNativeRegion::setVisible(bool visible) noexcept {
    if (![NSThread isMainThread] || !impl_ || !impl_->root || !impl_->root.window) return false;
    @try { impl_->clip.hidden=!visible;return true; } @catch (NSException*) { return false; }
}
} // namespace society::native

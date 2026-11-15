#include "PhotoClippedRegion.h"
#include <exception>

namespace society::native {
std::unique_ptr<PhotoClippedRegion> PhotoClippedRegion::create(PhotoWindowDriver& driver,PhotoWindow root,
    PhotoLogicalRect bounds,PhotoLogicalRect clip,double scale) noexcept {
    try {
        PhotoWindowMetrics metrics{};
        if(!root || !driver.metrics(root,metrics) || !photoScaleCompatible(scale,metrics.dpi))return {};
        const auto frames=photoPixelGeometry(bounds,clip,metrics.width,metrics.height,scale);
        if(!frames)return {};
        auto region=std::unique_ptr<PhotoClippedRegion>(new PhotoClippedRegion(driver,root,*frames,scale));
        region->outer_=driver.createChild(root,frames->outer,true,false);
        if(!region->outer_)return {};
        region->content_=driver.createChild(region->outer_,frames->inner,false,true);
        if(!region->content_)return {};
        if(!driver.parentIs(region->outer_,root) || !driver.parentIs(region->content_,region->outer_))return {};
        return region;
    } catch(...) {return {};}
}
PhotoClippedRegion::~PhotoClippedRegion(){
    if(owner_!=std::this_thread::get_id())std::terminate();
    if(content_ && driver_.alive(content_))driver_.destroy(content_);
    if(outer_ && driver_.alive(outer_))driver_.destroy(outer_);
}
bool PhotoClippedRegion::update(PhotoLogicalRect bounds,PhotoLogicalRect clip,double scale) noexcept {
    if(owner_!=std::this_thread::get_id() || !outer_ || !content_ ||
       !driver_.alive(outer_) || !driver_.alive(content_) || !driver_.parentIs(outer_,root_) ||
       !driver_.parentIs(content_,outer_))return false;
    PhotoWindowMetrics metrics{};
    if(!driver_.metrics(root_,metrics) || !photoScaleCompatible(scale,metrics.dpi))return false;
    const auto next=photoPixelGeometry(bounds,clip,metrics.width,metrics.height,scale);
    if(!next)return false;
    if(next->outer!=frames_.outer){
        if(!driver_.move(outer_,next->outer))return false;
        frames_.outer=next->outer;
    }
    if(next->inner!=frames_.inner){
        if(!driver_.move(content_,next->inner))return false;
        frames_.inner=next->inner;
    }
    scale_=scale;
    return true;
}
bool PhotoClippedRegion::setVisible(bool visible) noexcept {
    if(owner_!=std::this_thread::get_id())return false;
    const bool outerAlive=outer_ && driver_.alive(outer_);
    bool contentHidden=true;
    if(visible){
        PhotoWindowMetrics metrics{};
        if(!outerAlive || !content_ || !driver_.alive(content_) || !driver_.alive(root_) ||
           !driver_.parentIs(outer_,root_) || !driver_.parentIs(content_,outer_) ||
           !driver_.metrics(root_,metrics) || !photoScaleCompatible(scale_,metrics.dpi) ||
           std::int64_t(frames_.outer.x)+frames_.outer.width>metrics.width ||
           std::int64_t(frames_.outer.y)+frames_.outer.height>metrics.height)return false;
        if(contentHidden_){if(!driver_.show(content_,true))return false;contentHidden_=false;}
    } else if(content_ && driver_.alive(content_) && (!outerAlive || !driver_.parentIs(content_,outer_))) {
        // A content child reparented by the owner is no longer hidden with the
        // clip. Hide it independently before the deferred host close.
        contentHidden=driver_.show(content_,false);
        if(contentHidden)contentHidden_=true;
    }
    if(!outerAlive)return !visible && contentHidden;
    if(visible_==visible)return contentHidden;
    if(!driver_.show(outer_,visible))return false;
    visible_=visible;return contentHidden;
}
} // namespace society::native

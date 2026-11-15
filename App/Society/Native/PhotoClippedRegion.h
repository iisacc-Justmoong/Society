#pragma once
#include "PhotoPixelGeometry.h"
#include <compare>
#include <cstdint>
#include <memory>
#include <thread>

namespace society::native {
struct PhotoWindow final {
    std::uintptr_t native=0,identity=0;
    constexpr PhotoWindow() noexcept=default;
    constexpr PhotoWindow(std::uintptr_t nativeValue,std::uintptr_t identityValue=0) noexcept
        :native(nativeValue),identity(identityValue){}
    constexpr explicit operator bool() const noexcept{return native!=0;}
    auto operator<=>(const PhotoWindow&) const=default;
};
struct PhotoWindowMetrics final {int width,height;unsigned dpi;};
// The app host supplies a window system. All methods are called on the parent
// window's owner thread; destroying a clipping parent destroys its children.
class PhotoWindowDriver {
public:
    virtual ~PhotoWindowDriver()=default;
    virtual bool metrics(PhotoWindow,PhotoWindowMetrics&) noexcept=0;
    virtual bool alive(PhotoWindow) noexcept=0;
    virtual bool parentIs(PhotoWindow child,PhotoWindow parent) noexcept=0;
    virtual PhotoWindow createChild(PhotoWindow,PhotoPixelRect,bool clipsChildren,bool visible) noexcept=0;
    virtual bool move(PhotoWindow,PhotoPixelRect) noexcept=0;
    virtual bool show(PhotoWindow,bool visible) noexcept=0;
    virtual void destroy(PhotoWindow) noexcept=0;
};
class PhotoClippedRegion final {
public:
    static std::unique_ptr<PhotoClippedRegion> create(PhotoWindowDriver&,PhotoWindow,
        PhotoLogicalRect bounds,PhotoLogicalRect clip,double scale) noexcept;
    ~PhotoClippedRegion();
    PhotoClippedRegion(const PhotoClippedRegion&)=delete;
    PhotoClippedRegion& operator=(const PhotoClippedRegion&)=delete;
    [[nodiscard]] PhotoWindow nativeParent() const noexcept{return content_;}
    [[nodiscard]] bool update(PhotoLogicalRect bounds,PhotoLogicalRect clip,double scale) noexcept;
    [[nodiscard]] bool setVisible(bool visible) noexcept;
private:
    PhotoClippedRegion(PhotoWindowDriver& driver,PhotoWindow root,PhotoPixelFrames frames,double scale)
        :driver_(driver),root_(root),frames_(frames),owner_(std::this_thread::get_id()),scale_(scale){}
    PhotoWindowDriver& driver_;
    PhotoWindow root_=0,outer_=0,content_=0;
    PhotoPixelFrames frames_;
    std::thread::id owner_;
    double scale_=1;
    bool visible_=false,contentHidden_=false;
};
} // namespace society::native

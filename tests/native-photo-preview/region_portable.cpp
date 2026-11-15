#include "Society/Native/PhotoNativeRegion.h"
#include "Society/Native/PhotoWindowsDriver.h"
#include <map>
#include <stdexcept>

using namespace society::native;
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
namespace {
struct Driver final : PhotoWindowDriver {
    std::uintptr_t next=2;
    std::map<PhotoWindow,PhotoPixelRect> frames;
    bool metrics(PhotoWindow parent,PhotoWindowMetrics& result)noexcept override {
        if(parent!=1)return false;result={750,500,120};return true;
    }
    bool alive(PhotoWindow parent)noexcept override{return parent==1 || frames.contains(parent);}
    std::map<PhotoWindow,PhotoWindow> parents;
    bool parentIs(PhotoWindow child,PhotoWindow parent)noexcept override{
        return parents.contains(child) && parents[child]==parent && alive(parent);
    }
    PhotoWindow createChild(PhotoWindow parent,PhotoPixelRect rect,bool,bool)noexcept override {
        PhotoWindow value{next++};frames.emplace(value,rect);parents.emplace(value,parent);return value;
    }
    bool move(PhotoWindow child,PhotoPixelRect rect)noexcept override {
        if(!frames.contains(child))return false;frames[child]=rect;return true;
    }
    bool show(PhotoWindow child,bool)noexcept override{return frames.contains(child);}
    void destroy(PhotoWindow)noexcept override{frames.clear();parents.clear();}
} driver;
}
namespace society::native {PhotoWindowDriver& photoWindowsDriver()noexcept{return driver;}}
int main(){
    PhotoArea area{.mode=PhotoArea::Mode::visible,.bounds=QRectF(80,60,220,80),
        .clip=QRectF(100,60,150,80),.deviceScale=1.25};
    check(!PhotoNativeRegion::create(nullptr,area),"A null native parent was accepted");
    auto region=PhotoNativeRegion::create(reinterpret_cast<void*>(1),area);
    check(region && region->nativeParent()==reinterpret_cast<void*>(3),"Adapter did not expose the content child");
    check(driver.frames[2]==PhotoPixelRect{125,75,188,100} && driver.frames[3]==PhotoPixelRect{-25,0,275,100},
          "Qt logical geometry did not reach the native region");
    check(region->setVisible(true),"Adapter did not show the region");
    area.deviceScale=1.5;
    check(!region->update(area),"Adapter accepted incompatible device scales");
    region.reset();check(driver.frames.empty(),"Adapter did not retire native children");
}

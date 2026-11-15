#include "Society/Native/PhotoNativeRegion.h"
#include "Society/Native/PhotoAreaTracker.h"
#include <thread>

int main(){
    const society::native::PhotoArea area{.mode=society::native::PhotoArea::Mode::visible,
        .bounds=QRectF(0,0,100,100),.clip=QRectF(0,0,100,100)};
    if(society::native::PhotoNativeRegion::create(nullptr,area))return 1;
    bool rejected=false;
    std::thread other([&]{rejected=!society::native::PhotoNativeRegion::create(nullptr,area);});
    other.join();return rejected?0:2;
}

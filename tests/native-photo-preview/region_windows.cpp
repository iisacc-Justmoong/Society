#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Society/Native/PhotoClippedRegion.h"
#include "Society/Native/PhotoWindowsDriver.h"
#include <windows.h>
#include <atomic>
#include <stdexcept>
#include <thread>

using namespace society::native;
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM w,LPARAM l){return DefWindowProcW(window,message,w,l);}
static PhotoPixelRect frame(HWND child,HWND parent){
    RECT rect{};check(GetWindowRect(child,&rect),"Cannot get child bounds");
    POINT point{rect.left,rect.top};check(ScreenToClient(parent,&point),"Cannot map child bounds");
    return {int(point.x),int(point.y),int(rect.right-rect.left),int(rect.bottom-rect.top)};
}
int main(){
    const auto instance=GetModuleHandleW(nullptr);
    WNDCLASSW type{};type.hInstance=instance;type.lpfnWndProc=procedure;type.lpszClassName=L"iisacc.PhotoRegion.HiddenTest";
    check(RegisterClassW(&type)!=0,"Cannot register test parent class");
    HWND parent=CreateWindowExW(0,type.lpszClassName,L"",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,
                                0,0,800,600,nullptr,nullptr,instance,nullptr);
    check(parent!=nullptr,"Cannot create hidden parent");
    auto& driver=photoWindowsDriver();PhotoWindowMetrics metrics{};
    const PhotoWindow root{reinterpret_cast<std::uintptr_t>(parent)};
    check(driver.metrics(root,metrics),"Cannot obtain parent metrics");
    const auto scale=double(metrics.dpi)/96.0;
    const PhotoLogicalRect bounds{20,20,160,100},clip{30,20,120,100};
    const auto expected=photoPixelGeometry(bounds,clip,metrics.width,metrics.height,scale);
    auto region=PhotoClippedRegion::create(driver,root,bounds,clip,scale);
    check(expected && region,"Cannot create a native clipped region");
    const auto childToken=region->nativeParent();
    check(driver.alive(childToken) && !driver.alive({childToken.native,childToken.identity+1}),
          "Native child identity did not protect a reused handle");
    const auto content=reinterpret_cast<HWND>(childToken.native);const auto outer=GetParent(content);
    check(GetParent(outer)==parent && frame(outer,parent)==expected->outer && frame(content,outer)==expected->inner,
          "Native child bounds or hierarchy do not match the pixel geometry");
    check(region->setVisible(true) && (GetWindowLongPtrW(outer,GWL_STYLE)&WS_VISIBLE),"Cannot show the clipped region");
    check(region->setVisible(false) && !(GetWindowLongPtrW(outer,GWL_STYLE)&WS_VISIBLE),"Cannot hide the clipped region");
    std::atomic_bool rejected=false;std::thread foreign([&]{rejected=!region->update(bounds,clip,scale);});foreign.join();
    check(rejected,"A foreign thread changed the native clipped region");
    check(DestroyWindow(parent)!=FALSE,"Cannot destroy parent");
    check(!driver.alive(childToken),"A destroyed child retained its identity");
    check(!region->setVisible(true),"A region accepted a destroyed parent");region.reset();
    check(UnregisterClassW(type.lpszClassName,instance)!=FALSE,"Cannot unregister test parent class");
}

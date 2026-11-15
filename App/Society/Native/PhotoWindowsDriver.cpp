#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
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
#include "PhotoWindowsDriver.h"
#include <windows.h>
#include <atomic>
#include <limits>

namespace society::native {
namespace {
constexpr wchar_t regionClass[]=L"iisacc.Society.PhotoRegion.1";
HWND hwnd(PhotoWindow value){return reinterpret_cast<HWND>(value.native);}
LRESULT CALLBACK regionProcedure(HWND,UINT,WPARAM,LPARAM);
bool owned(PhotoWindow value){
    const auto window=hwnd(value);
    DWORD process=0;
    if(!window || !IsWindow(window) || GetWindowThreadProcessId(window,&process)!=GetCurrentThreadId() ||
       process!=GetCurrentProcessId())return false;
    return !value.identity ||
        (static_cast<std::uintptr_t>(GetWindowLongPtrW(window,GWLP_USERDATA))==value.identity &&
         reinterpret_cast<WNDPROC>(GetClassLongPtrW(window,GCLP_WNDPROC))==regionProcedure);
}
LRESULT CALLBACK regionProcedure(HWND window,UINT message,WPARAM wparam,LPARAM lparam){
    if(message==WM_NCCREATE){
        const auto* creation=reinterpret_cast<const CREATESTRUCTW*>(lparam);
        if(!creation || !creation->lpCreateParams)return FALSE;
        SetLastError(ERROR_SUCCESS);
        if(!SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(creation->lpCreateParams)) &&
           GetLastError()!=ERROR_SUCCESS)return FALSE;
    }
    if(message==WM_NCDESTROY)SetWindowLongPtrW(window,GWLP_USERDATA,0);
    if(message==WM_ERASEBKGND)return 1;
    if(message==WM_PAINT){PAINTSTRUCT paint{};if(BeginPaint(window,&paint))EndPaint(window,&paint);return 0;}
    return DefWindowProcW(window,message,wparam,lparam);
}
bool ensureClass(){
    const auto instance=GetModuleHandleW(nullptr);if(!instance)return false;
    WNDCLASSEXW existing{};existing.cbSize=sizeof(existing);
    if(GetClassInfoExW(instance,regionClass,&existing))return existing.lpfnWndProc==regionProcedure;
    WNDCLASSEXW value{};value.cbSize=sizeof(value);value.style=CS_DBLCLKS;
    value.lpfnWndProc=regionProcedure;value.hInstance=instance;value.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    value.lpszClassName=regionClass;
    if(RegisterClassExW(&value))return true;
    if(GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return false;
    existing={};existing.cbSize=sizeof(existing);
    return GetClassInfoExW(instance,regionClass,&existing) && existing.lpfnWndProc==regionProcedure;
}
std::uintptr_t nextIdentity() noexcept {
    static std::atomic<std::uintptr_t> sequence{1};
    auto value=sequence.load(std::memory_order_relaxed);
    while(value!=std::numeric_limits<std::uintptr_t>::max() &&
          !sequence.compare_exchange_weak(value,value+1,std::memory_order_relaxed)){}
    return value==std::numeric_limits<std::uintptr_t>::max()?0:value;
}
class WindowsDriver final : public PhotoWindowDriver {
public:
    bool metrics(PhotoWindow value,PhotoWindowMetrics& result) noexcept override {
        const auto window=hwnd(value);if(!owned(value))return false;
        // Without this style, parent painting is allowed to overwrite a child.
        if(!(GetWindowLongPtrW(window,GWL_STYLE)&WS_CLIPCHILDREN))return false;
        RECT rect{};const auto dpi=GetDpiForWindow(window);
        if(!dpi || !GetClientRect(window,&rect) || rect.right<=rect.left || rect.bottom<=rect.top)return false;
        const auto width=std::int64_t(rect.right)-rect.left,height=std::int64_t(rect.bottom)-rect.top;
        if(width>std::numeric_limits<int>::max() || height>std::numeric_limits<int>::max())return false;
        result={int(width),int(height),dpi};return true;
    }
    bool alive(PhotoWindow value) noexcept override{return owned(value);}
    bool parentIs(PhotoWindow child,PhotoWindow parent) noexcept override {
        return owned(child) && owned(parent) && GetParent(hwnd(child))==hwnd(parent);
    }
    PhotoWindow createChild(PhotoWindow parent,PhotoPixelRect rect,bool clips,bool visible) noexcept override {
        if(!owned(parent) || !ensureClass())return 0;
        const auto identity=nextIdentity();if(!identity)return 0;
        DWORD style=WS_CHILD|WS_CLIPSIBLINGS;
        if(clips)style|=WS_CLIPCHILDREN;
        if(visible)style|=WS_VISIBLE;
        const auto window=CreateWindowExW(0,regionClass,L"",style,rect.x,rect.y,rect.width,rect.height,
            hwnd(parent),nullptr,GetModuleHandleW(nullptr),reinterpret_cast<void*>(identity));
        const PhotoWindow value{reinterpret_cast<std::uintptr_t>(window),identity};
        return owned(value)?value:PhotoWindow{};
    }
    bool move(PhotoWindow value,PhotoPixelRect rect) noexcept override {
        return owned(value) && SetWindowPos(hwnd(value),nullptr,rect.x,rect.y,rect.width,rect.height,
            SWP_NOZORDER|SWP_NOACTIVATE)!=FALSE;
    }
    bool show(PhotoWindow value,bool visible) noexcept override {
        return owned(value) && SetWindowPos(hwnd(value),nullptr,0,0,0,0,
            SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|(visible?SWP_SHOWWINDOW:SWP_HIDEWINDOW))!=FALSE;
    }
    void destroy(PhotoWindow value) noexcept override {if(owned(value))DestroyWindow(hwnd(value));}
};
}
PhotoWindowDriver& photoWindowsDriver() noexcept {static WindowsDriver driver;return driver;}
} // namespace society::native

# Photo overlays require the external NSView, UIView or desktop HWND host. Other
# platforms retain their isolated legacy fallback until a host is available.
function(society_native_photo_enabled result)
    if((WIN32 AND NOT WINDOWS_STORE) OR APPLE)
        set(${result} TRUE PARENT_SCOPE)
    else()
        set(${result} FALSE PARENT_SCOPE)
    endif()
endfunction()

function(society_native_photo_platform_components sources libraries)
    if(APPLE AND IOS)
        set(_sources App/Society/Native/PhotoNativeRegionIOS.mm
            App/Society/Native/PhotoRegionGeometry.h App/Society/Native/PhotoRegionGeometry.cpp)
        set(_libraries "-framework UIKit")
    elseif(APPLE)
        set(_sources App/Society/Native/PhotoNativeRegion.mm
            App/Society/Native/PhotoRegionGeometry.h App/Society/Native/PhotoRegionGeometry.cpp)
        set(_libraries "-framework AppKit" "-framework QuartzCore")
    elseif(WIN32 AND NOT WINDOWS_STORE)
        set(_sources App/Society/Native/PhotoNativeRegionWindows.cpp
            App/Society/Native/PhotoWindowsDriver.h App/Society/Native/PhotoWindowsDriver.cpp
            App/Society/Native/PhotoClippedRegion.h App/Society/Native/PhotoClippedRegion.cpp
            App/Society/Native/PhotoPixelGeometry.h App/Society/Native/PhotoPixelGeometry.cpp)
        set(_libraries user32)
    else()
        set(_sources)
        set(_libraries)
    endif()
    set(${sources} "${_sources}" PARENT_SCOPE)
    set(${libraries} "${_libraries}" PARENT_SCOPE)
endfunction()

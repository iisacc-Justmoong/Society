find_package(iiPhotoLibraryPresentation 0.1 CONFIG REQUIRED)
find_package(iiPhotoLibraryQtPresentationBridge 0.1 CONFIG REQUIRED)
if(NOT EXISTS "${iiPhotoLibraryPresentation_VIEWS_DIR}/PhotosView.lvui" OR
   NOT EXISTS "${iiPhotoLibraryPresentation_STRINGS_DIR}/en.json" OR
   NOT EXISTS "${iiPhotoLibraryPresentation_STRINGS_DIR}/ko.json")
    message(FATAL_ERROR "The selected photo presentation package lacks the LVUI view or strings")
endif()
include(cmake/NativeOverlay.cmake)
lvui_generate(NAME PhotosView SOURCE "${iiPhotoLibraryPresentation_VIEWS_DIR}/PhotosView.lvui"
    FRAMEWORK iiGUI INCLUDES iiPhotoLibrary/PhotoViewModel.h OUTPUT_VARIABLE _society_native_photo_view)
foreach(locale IN ITEMS en ko)
    set_source_files_properties("${iiPhotoLibraryPresentation_STRINGS_DIR}/${locale}.json"
        PROPERTIES QT_RESOURCE_ALIAS "${locale}.json")
endforeach()
qt_add_resources(Society nativePhotoStrings PREFIX "/iiPhotoLibrary/native-strings"
    FILES "${iiPhotoLibraryPresentation_STRINGS_DIR}/en.json" "${iiPhotoLibraryPresentation_STRINGS_DIR}/ko.json")
target_sources(Society PRIVATE
    App/Society/Native/NativePhotoPresenter.h App/Society/Native/NativePhotoPresenter.cpp
    App/Society/Native/ExternalPhotoSurface.h App/Society/Native/ExternalPhotoSurface.cpp
    App/Society/Native/PhotoNativeRegion.h
    App/Society/Native/PhotoAreaTracker.h App/Society/Native/PhotoAreaTracker.cpp
    App/Society/Native/PhotoPreviewLoader.h App/Society/Native/PhotoPreviewLoader.cpp
    App/Society/Native/PhotoPreviewResolver.h App/Society/Native/PhotoPreviewResolver.cpp
    App/Society/Native/PhotoPicker.h App/Society/Native/PhotoPicker.cpp
    "${_society_native_photo_view}")
society_native_photo_platform_components(_society_photo_platform_sources _society_photo_platform_libraries)
if(NOT _society_photo_platform_sources)
    message(FATAL_ERROR "No native photo region host is available for this platform")
endif()
target_sources(Society PRIVATE ${_society_photo_platform_sources})
target_link_libraries(Society PRIVATE ${_society_photo_platform_libraries})
if(APPLE)
    set_target_properties(Society PROPERTIES OBJCXX_STANDARD 23 OBJCXX_STANDARD_REQUIRED YES)
    if(IOS)
        set_source_files_properties(App/Society/Native/PhotoNativeRegionIOS.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    else()
        set_source_files_properties(App/Society/Native/PhotoNativeRegion.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    endif()
endif()
target_include_directories(Society PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/App" "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(Society PRIVATE iiPhotoLibrary::QtPresentationBridge iiPhotoLibrary::Presentation
    LVUI::Native iiGUI::iiGUI Qt6::Gui Qt6::Quick Qt6::Concurrent)
target_compile_definitions(Society PRIVATE SOCIETY_NATIVE_PHOTO_PRESENTER=1)

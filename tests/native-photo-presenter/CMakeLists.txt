cmake_minimum_required(VERSION 3.31)
project(SocietyNativePhotoPresenterTests LANGUAGES CXX)
include(CTest)
include(../../cmake/NativePhotoPlatform.cmake)
society_native_photo_platform_components(photo_host_sources photo_host_libraries)
if(NOT photo_host_sources)
    message(FATAL_ERROR "No native photo presenter host is available")
endif()
if(APPLE)
    enable_language(OBJCXX)
endif()
list(TRANSFORM photo_host_sources PREPEND "${CMAKE_CURRENT_LIST_DIR}/../../")
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Core Gui Qml Quick Concurrent)
include(../../cmake/QtApple.cmake)
find_package(iiPhotoLibrary 0.1 CONFIG REQUIRED COMPONENTS Library)
find_package(iiPhotoLibraryPresentation 0.1 CONFIG REQUIRED)
find_package(iiPhotoLibraryQtPresentationBridge 0.1 CONFIG REQUIRED)
find_package(LVUI 0.1 CONFIG REQUIRED COMPONENTS Native)
find_package(iiGUI 0.5 CONFIG REQUIRED)
lvui_generate(NAME PhotosView SOURCE "${iiPhotoLibraryPresentation_VIEWS_DIR}/PhotosView.lvui"
    FRAMEWORK iiGUI INCLUDES iiPhotoLibrary/PhotoViewModel.h OUTPUT_VARIABLE generated)
foreach(locale IN ITEMS en ko)
    set_source_files_properties("${iiPhotoLibraryPresentation_STRINGS_DIR}/${locale}.json" PROPERTIES QT_RESOURCE_ALIAS "${locale}.json")
endforeach()
add_executable(SocietyNativePhotoPresenterTests presenter.cpp
    ../../App/Society/Native/NativePhotoPresenter.cpp ../../App/Society/Native/NativePhotoPresenter.h
    ../../App/Society/Native/NativeOverlayCoordinator.cpp ../../App/Society/Native/NativeOverlayCoordinator.h
    ../../App/Society/Native/NativeModalPresence.cpp ../../App/Society/Native/NativeModalPresence.h
    ../../App/Society/Native/ExternalPhotoSurface.cpp ../../App/Society/Native/ExternalPhotoSurface.h
    ../../App/Society/Native/PhotoNativeRegion.h ${photo_host_sources}
    ../../App/Society/Native/PhotoAreaTracker.cpp ../../App/Society/Native/PhotoAreaTracker.h
    ../../App/Society/Native/PhotoPreviewLoader.cpp ../../App/Society/Native/PhotoPreviewLoader.h
    ../../App/Society/Native/PhotoPicker.cpp ../../App/Society/Native/PhotoPicker.h
    ../../App/Society/Native/PhotoPreviewResolver.cpp ../../App/Society/Native/PhotoPreviewResolver.h "${generated}")
if(APPLE)
    set_target_properties(SocietyNativePhotoPresenterTests PROPERTIES OBJCXX_STANDARD 23 OBJCXX_STANDARD_REQUIRED YES)
    target_compile_options(SocietyNativePhotoPresenterTests PRIVATE "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
endif()
target_compile_features(SocietyNativePhotoPresenterTests PRIVATE cxx_std_23)
target_include_directories(SocietyNativePhotoPresenterTests PRIVATE ../../App "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(SocietyNativePhotoPresenterTests PRIVATE iiPhotoLibrary::QtPresentationBridge iiPhotoLibrary::Presentation
    LVUI::Native iiGUI::iiGUI Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Concurrent ${photo_host_libraries})
qt_add_resources(SocietyNativePhotoPresenterTests photoNativeStrings PREFIX "/iiPhotoLibrary/native-strings"
    FILES "${iiPhotoLibraryPresentation_STRINGS_DIR}/en.json" "${iiPhotoLibraryPresentation_STRINGS_DIR}/ko.json")
add_test(NAME Society.NativePhotoPresenter COMMAND SocietyNativePhotoPresenterTests)
set_tests_properties(Society.NativePhotoPresenter PROPERTIES TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
    ENVIRONMENT_MODIFICATION "DYLD_LIBRARY_PATH=unset:;DYLD_FALLBACK_LIBRARY_PATH=unset:;DYLD_FRAMEWORK_PATH=unset:;DYLD_FALLBACK_FRAMEWORK_PATH=unset:")

add_executable(SocietyNativePhotoBridgeViewTests bridge_view.cpp "${generated}")
target_compile_features(SocietyNativePhotoBridgeViewTests PRIVATE cxx_std_23)
target_include_directories(SocietyNativePhotoBridgeViewTests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(SocietyNativePhotoBridgeViewTests PRIVATE iiPhotoLibrary::QtPresentationBridge
    iiPhotoLibrary::Presentation LVUI::Native iiGUI::iiGUI Qt6::Core Qt6::Gui Qt6::Quick)
add_test(NAME Society.NativePhotoBridgeView COMMAND SocietyNativePhotoBridgeViewTests)
set_tests_properties(Society.NativePhotoBridgeView PROPERTIES TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
    ENVIRONMENT_MODIFICATION "DYLD_LIBRARY_PATH=unset:;DYLD_FALLBACK_LIBRARY_PATH=unset:;DYLD_FRAMEWORK_PATH=unset:;DYLD_FALLBACK_FRAMEWORK_PATH=unset:")

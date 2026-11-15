cmake_minimum_required(VERSION 3.31)
project(SocietyNativePhotoPreviewTests LANGUAGES CXX)
include(CTest)
add_test(NAME Society.NativePhotoPlatform COMMAND "${CMAKE_COMMAND}"
    "-DSOCIETY_PHOTO_PLATFORM_FILE=${CMAKE_CURRENT_LIST_DIR}/../../cmake/NativePhotoPlatform.cmake"
    -P "${CMAKE_CURRENT_LIST_DIR}/../native_photo_platform.cmake")
find_package(LVUI 0.1 CONFIG REQUIRED COMPONENTS Native)
find_package(iiGUI 0.5 CONFIG REQUIRED)
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Concurrent Qml Quick)
include(../../cmake/QtApple.cmake)

add_executable(SocietyNativePhotoPickerTests picker.cpp
    ../../App/Society/Native/PhotoPicker.cpp ../../App/Society/Native/PhotoPicker.h)
target_compile_features(SocietyNativePhotoPickerTests PRIVATE cxx_std_23)
target_include_directories(SocietyNativePhotoPickerTests PRIVATE ../../App)
target_link_libraries(SocietyNativePhotoPickerTests PRIVATE iiGUI::iiGUI)
add_test(NAME Society.NativePhotoPicker COMMAND SocietyNativePhotoPickerTests)
set_tests_properties(Society.NativePhotoPicker PROPERTIES TIMEOUT 30)

add_executable(SocietyNativePhotoPreviewTests resolver.cpp
    ../../App/Society/Native/PhotoPreviewResolver.cpp ../../App/Society/Native/PhotoPreviewResolver.h)
target_compile_features(SocietyNativePhotoPreviewTests PRIVATE cxx_std_23)
target_include_directories(SocietyNativePhotoPreviewTests PRIVATE ../../App)
target_link_libraries(SocietyNativePhotoPreviewTests PRIVATE LVUI::Native iiGUI::iiGUI)
add_test(NAME Society.NativePhotoPreview COMMAND SocietyNativePhotoPreviewTests)
set_tests_properties(Society.NativePhotoPreview PROPERTIES TIMEOUT 30)
add_executable(SocietyNativePhotoLoaderTests loader.cpp
    ../../App/Society/Native/PhotoPreviewLoader.cpp ../../App/Society/Native/PhotoPreviewLoader.h)
target_compile_features(SocietyNativePhotoLoaderTests PRIVATE cxx_std_23)
target_include_directories(SocietyNativePhotoLoaderTests PRIVATE ../../App)
target_compile_definitions(SocietyNativePhotoLoaderTests PRIVATE SOCIETY_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_link_libraries(SocietyNativePhotoLoaderTests PRIVATE LVUI::Native iiGUI::iiGUI Qt6::Core Qt6::Gui Qt6::Concurrent)
add_test(NAME Society.NativePhotoLoader COMMAND SocietyNativePhotoLoaderTests)
set_tests_properties(Society.NativePhotoLoader PROPERTIES TIMEOUT 30)
add_executable(SocietyNativePhotoAreaTests area.cpp
    ../../App/Society/Native/PhotoAreaTracker.cpp ../../App/Society/Native/PhotoAreaTracker.h)
target_compile_features(SocietyNativePhotoAreaTests PRIVATE cxx_std_23)
target_include_directories(SocietyNativePhotoAreaTests PRIVATE ../../App)
target_link_libraries(SocietyNativePhotoAreaTests PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
add_test(NAME Society.NativePhotoArea COMMAND SocietyNativePhotoAreaTests)
set_tests_properties(Society.NativePhotoArea PROPERTIES TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
add_test(NAME Society.NativePhotoAreaHiDpi COMMAND SocietyNativePhotoAreaTests)
set_tests_properties(Society.NativePhotoAreaHiDpi PROPERTIES TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_SCALE_FACTOR=1.25")
add_executable(SocietyNativePhotoRegionGeometryTests geometry.cpp
    ../../App/Society/Native/PhotoRegionGeometry.cpp ../../App/Society/Native/PhotoRegionGeometry.h)
target_compile_features(SocietyNativePhotoRegionGeometryTests PRIVATE cxx_std_23)
target_include_directories(SocietyNativePhotoRegionGeometryTests PRIVATE ../../App)
target_link_libraries(SocietyNativePhotoRegionGeometryTests PRIVATE Qt6::Core)
add_test(NAME Society.NativePhotoRegionGeometry COMMAND SocietyNativePhotoRegionGeometryTests)
set_tests_properties(Society.NativePhotoRegionGeometry PROPERTIES TIMEOUT 30)
add_executable(SocietyNativePhotoPixelGeometryTests pixels.cpp
    ../../App/Society/Native/PhotoPixelGeometry.cpp ../../App/Society/Native/PhotoPixelGeometry.h)
target_compile_features(SocietyNativePhotoPixelGeometryTests PRIVATE cxx_std_23)
target_include_directories(SocietyNativePhotoPixelGeometryTests PRIVATE ../../App)
add_test(NAME Society.NativePhotoPixelGeometry COMMAND SocietyNativePhotoPixelGeometryTests)
set_tests_properties(Society.NativePhotoPixelGeometry PROPERTIES TIMEOUT 30)
add_executable(SocietyNativePhotoClippedRegionTests clipped_region.cpp
    ../../App/Society/Native/PhotoClippedRegion.cpp ../../App/Society/Native/PhotoClippedRegion.h
    ../../App/Society/Native/PhotoPixelGeometry.cpp ../../App/Society/Native/PhotoPixelGeometry.h)
target_compile_features(SocietyNativePhotoClippedRegionTests PRIVATE cxx_std_23)
target_include_directories(SocietyNativePhotoClippedRegionTests PRIVATE ../../App)
add_test(NAME Society.NativePhotoClippedRegion COMMAND SocietyNativePhotoClippedRegionTests)
set_tests_properties(Society.NativePhotoClippedRegion PROPERTIES TIMEOUT 30)
add_executable(SocietyNativePhotoWindowsAdapterTests region_portable.cpp
    ../../App/Society/Native/PhotoNativeRegionWindows.cpp ../../App/Society/Native/PhotoNativeRegion.h
    ../../App/Society/Native/PhotoClippedRegion.cpp ../../App/Society/Native/PhotoClippedRegion.h
    ../../App/Society/Native/PhotoPixelGeometry.cpp ../../App/Society/Native/PhotoPixelGeometry.h)
target_compile_features(SocietyNativePhotoWindowsAdapterTests PRIVATE cxx_std_23)
target_include_directories(SocietyNativePhotoWindowsAdapterTests PRIVATE ../../App)
target_link_libraries(SocietyNativePhotoWindowsAdapterTests PRIVATE Qt6::Core)
add_test(NAME Society.NativePhotoWindowsAdapter COMMAND SocietyNativePhotoWindowsAdapterTests)
set_tests_properties(Society.NativePhotoWindowsAdapter PROPERTIES TIMEOUT 30)
if(WIN32)
    add_executable(SocietyNativePhotoWindowsRegionTests region_windows.cpp
        ../../App/Society/Native/PhotoWindowsDriver.cpp ../../App/Society/Native/PhotoWindowsDriver.h
        ../../App/Society/Native/PhotoClippedRegion.cpp ../../App/Society/Native/PhotoClippedRegion.h
        ../../App/Society/Native/PhotoPixelGeometry.cpp ../../App/Society/Native/PhotoPixelGeometry.h)
    target_compile_features(SocietyNativePhotoWindowsRegionTests PRIVATE cxx_std_23)
    target_include_directories(SocietyNativePhotoWindowsRegionTests PRIVATE ../../App)
    target_link_libraries(SocietyNativePhotoWindowsRegionTests PRIVATE user32)
    add_test(NAME Society.NativePhotoWindowsRegion COMMAND SocietyNativePhotoWindowsRegionTests)
    set_tests_properties(Society.NativePhotoWindowsRegion PROPERTIES TIMEOUT 30)
endif()
add_executable(SocietyNativeModalPresenceTests modal.cpp
    ../../App/Society/Native/NativeModalPresence.cpp ../../App/Society/Native/NativeModalPresence.h)
target_compile_features(SocietyNativeModalPresenceTests PRIVATE cxx_std_23)
target_include_directories(SocietyNativeModalPresenceTests PRIVATE ../../App)
target_link_libraries(SocietyNativeModalPresenceTests PRIVATE LVUI::Native Qt6::Core Qt6::Gui)
add_test(NAME Society.NativeModalPresence COMMAND SocietyNativeModalPresenceTests)
set_tests_properties(Society.NativeModalPresence PROPERTIES TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
if(APPLE)
    set_tests_properties(Society.NativePhotoPreview Society.NativePhotoLoader Society.NativePhotoArea Society.NativePhotoAreaHiDpi Society.NativePhotoRegionGeometry Society.NativeModalPresence PROPERTIES ENVIRONMENT_MODIFICATION
        "DYLD_LIBRARY_PATH=unset:;DYLD_FALLBACK_LIBRARY_PATH=unset:;DYLD_FRAMEWORK_PATH=unset:;DYLD_FALLBACK_FRAMEWORK_PATH=unset:")
    enable_language(OBJCXX)
    add_executable(SocietyNativePhotoRegionTests region_mac.mm
        ../../App/Society/Native/PhotoNativeRegion.mm ../../App/Society/Native/PhotoNativeRegion.h
        ../../App/Society/Native/PhotoRegionGeometry.cpp ../../App/Society/Native/PhotoRegionGeometry.h)
    set_target_properties(SocietyNativePhotoRegionTests PROPERTIES OBJCXX_STANDARD 23 OBJCXX_STANDARD_REQUIRED YES)
    target_compile_options(SocietyNativePhotoRegionTests PRIVATE -fobjc-arc)
    target_include_directories(SocietyNativePhotoRegionTests PRIVATE ../../App)
    target_link_libraries(SocietyNativePhotoRegionTests PRIVATE Qt6::Core "-framework AppKit" "-framework QuartzCore")
    add_test(NAME Society.NativePhotoRegion COMMAND SocietyNativePhotoRegionTests)
    set_tests_properties(Society.NativePhotoRegion PROPERTIES TIMEOUT 30 ENVIRONMENT_MODIFICATION
        "DYLD_LIBRARY_PATH=unset:;DYLD_FALLBACK_LIBRARY_PATH=unset:;DYLD_FRAMEWORK_PATH=unset:;DYLD_FALLBACK_FRAMEWORK_PATH=unset:")
    add_executable(SocietyNativePhotoSurfaceTests surface.cpp
        ../../App/Society/Native/ExternalPhotoSurface.cpp ../../App/Society/Native/ExternalPhotoSurface.h
        ../../App/Society/Native/PhotoNativeRegion.mm ../../App/Society/Native/PhotoNativeRegion.h
        ../../App/Society/Native/PhotoRegionGeometry.cpp ../../App/Society/Native/PhotoRegionGeometry.h)
    set_target_properties(SocietyNativePhotoSurfaceTests PROPERTIES OBJCXX_STANDARD 23 OBJCXX_STANDARD_REQUIRED YES)
    target_compile_features(SocietyNativePhotoSurfaceTests PRIVATE cxx_std_23)
    target_compile_options(SocietyNativePhotoSurfaceTests PRIVATE "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
    target_include_directories(SocietyNativePhotoSurfaceTests PRIVATE ../../App)
    target_link_libraries(SocietyNativePhotoSurfaceTests PRIVATE iiGUI::iiGUI Qt6::Core Qt6::Gui "-framework AppKit" "-framework QuartzCore")
    add_test(NAME Society.NativePhotoSurface COMMAND SocietyNativePhotoSurfaceTests)
    set_tests_properties(Society.NativePhotoSurface PROPERTIES TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        ENVIRONMENT_MODIFICATION "DYLD_LIBRARY_PATH=unset:;DYLD_FALLBACK_LIBRARY_PATH=unset:;DYLD_FRAMEWORK_PATH=unset:;DYLD_FALLBACK_FRAMEWORK_PATH=unset:")
endif()

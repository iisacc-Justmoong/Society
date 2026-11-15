cmake_minimum_required(VERSION 3.31)
project(SocietyNativePhotoIOSCompile LANGUAGES CXX OBJCXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    message(FATAL_ERROR "This compile fixture requires an iOS toolchain")
endif()
set(SOCIETY_PHOTO_QT_INCLUDE "" CACHE PATH "Target photo-service build include directory")
set(SOCIETY_PHOTO_BRIDGE_HEADER "" CACHE FILEPATH "Photo service bridge header")
set(SOCIETY_CONTAINER_INCLUDE "" CACHE PATH "Directory containing the SocietyDrive declaration")
set(SOCIETY_FILE_PROVIDER_HEADER "" CACHE FILEPATH "Target file-presentation header")
if(NOT EXISTS "${SOCIETY_PHOTO_QT_INCLUDE}/iiPhotoLibrary/PhotoController.h" OR
   NOT EXISTS "${SOCIETY_PHOTO_BRIDGE_HEADER}" OR
   NOT EXISTS "${SOCIETY_CONTAINER_INCLUDE}/SocietyDrive.h" OR
   NOT EXISTS "${SOCIETY_FILE_PROVIDER_HEADER}")
    message(FATAL_ERROR "Supply target photo-service, bridge, container and file-provider declarations")
endif()

find_package(Qt6 6.8.3 EXACT CONFIG REQUIRED COMPONENTS Core Gui Qml Quick Concurrent)
find_package(iiPhotoLibraryPresentation 0.1 CONFIG REQUIRED)
find_package(LVUI 0.1 CONFIG REQUIRED COMPONENTS Native)
find_package(iiGUI 0.5 CONFIG REQUIRED)

file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/compat/iiPhotoLibrary/PhotoPresentationBridge.h"
    CONTENT "#pragma once\n#include \"${SOCIETY_PHOTO_BRIDGE_HEADER}\"\n")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/compat/iiFileProviderPresentation.h"
    CONTENT "#pragma once\n#include \"${SOCIETY_FILE_PROVIDER_HEADER}\"\n")
lvui_generate(NAME PhotosView SOURCE "${iiPhotoLibraryPresentation_VIEWS_DIR}/PhotosView.lvui"
    FRAMEWORK iiGUI INCLUDES iiPhotoLibrary/PhotoViewModel.h OUTPUT_VARIABLE generated)

set(app "${CMAKE_CURRENT_LIST_DIR}/../../App")
add_library(SocietyNativePhotoIOSObjects OBJECT
    "${app}/Society/Native/NativePhotoPresenter.cpp"
    "${app}/Society/Native/PhotoPreviewLoader.cpp"
    "${app}/Society/Native/PhotoPicker.cpp"
    "${app}/Society/Native/PhotoPreviewResolver.cpp"
    "${app}/Society/Native/PhotoAreaTracker.cpp"
    "${app}/Society/Native/PhotoRegionGeometry.cpp"
    "${app}/Society/Native/PhotoNativeRegionIOS.mm"
    "${app}/Society/Native/ExternalPhotoSurface.cpp"
    "${app}/Society/Native/NativeOverlayCoordinator.cpp"
    "${app}/Society/Native/NativeModalPresence.cpp"
    "${generated}")
set_target_properties(SocietyNativePhotoIOSObjects PROPERTIES AUTOMOC ON CXX_EXTENSIONS OFF
    OBJCXX_STANDARD 23 OBJCXX_STANDARD_REQUIRED ON OBJCXX_EXTENSIONS OFF)
target_compile_features(SocietyNativePhotoIOSObjects PRIVATE cxx_std_23)
target_compile_options(SocietyNativePhotoIOSObjects PRIVATE "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
target_include_directories(SocietyNativePhotoIOSObjects PRIVATE
    "${app}" "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/compat"
    "${SOCIETY_PHOTO_QT_INCLUDE}" "${SOCIETY_CONTAINER_INCLUDE}")
target_link_libraries(SocietyNativePhotoIOSObjects PRIVATE
    iiPhotoLibrary::Presentation LVUI::Native iiGUI::iiGUI
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Concurrent "-framework UIKit")

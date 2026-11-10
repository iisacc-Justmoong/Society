cmake_minimum_required(VERSION 3.31)
project(SocietyNativeModelCalibrator LANGUAGES CXX)

find_package(iiSDKCore 0.20 CONFIG REQUIRED COMPONENTS Runtime)
find_package(iiGUI 0.5 CONFIG REQUIRED)
find_package(LVUI 0.1 CONFIG REQUIRED COMPONENTS Native)
find_package(iiFileProviderNative 0.8.2 CONFIG REQUIRED
    HINTS "$ENV{IIFILEPROVIDER_PREFIX}" "$ENV{HOME}/.local/SDK/iiFileProvider")
find_package(OpenSSL REQUIRED COMPONENTS Crypto)
find_package(json-c CONFIG REQUIRED)

# This is the ModelPackaging component installed from the workspace SDK.  Its
# static archive is independent of the optional generation runtime.
set(SOCIETY_CALIBRATOR_IILD_PREFIX "" CACHE PATH "iiLocalDiffusion ModelPackaging component")
if(NOT EXISTS "${SOCIETY_CALIBRATOR_IILD_PREFIX}/lib/libiiModelPackaging.a")
    message(FATAL_ERROR "Install the iiLocalDiffusion ModelPackaging component for the calibrator")
endif()
add_library(SocietyCalibrationSdk STATIC IMPORTED)
set_target_properties(SocietyCalibrationSdk PROPERTIES
    IMPORTED_LOCATION "${SOCIETY_CALIBRATOR_IILD_PREFIX}/lib/libiiModelPackaging.a"
    INTERFACE_LINK_LIBRARIES "iiFileProvider::Native;OpenSSL::Crypto;json-c::json-c-static")

lvui_generate(NAME NativeModelCalibrator SOURCE ../NativeModelCalibrator.lvui
    INCLUDES NativeModelCalibratorViewModel.h OUTPUT_VARIABLE calibrator_view)
iisdkcore_add_application(SocietyModelCalibrator
    SOURCES ../NativeModelCalibratorApplication.cpp ../NativeModelCalibratorViewModel.cpp "${calibrator_view}"
    LIBRARIES iiGUI::iiGUI LVUI::LVUI LVUI::Native SocietyCalibrationSdk)
target_include_directories(SocietyModelCalibrator BEFORE PRIVATE "${SOCIETY_CALIBRATOR_IILD_PREFIX}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/.." "${CMAKE_CURRENT_BINARY_DIR}")
# CPATH may contain an older installed iiLocalDiffusion; prefer this exact SDK
# component's public headers to keep its API and archive on one generation.
if(APPLE)
    set_target_properties(SocietyModelCalibrator PROPERTIES BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "@loader_path/../Frameworks")
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(SocietyNativeCalibratorViewModelTests ../../../../tests/native_model_calibrator_viewmodel.cpp
        ../NativeModelCalibratorViewModel.cpp)
    target_compile_features(SocietyNativeCalibratorViewModelTests PRIVATE cxx_std_23)
    target_include_directories(SocietyNativeCalibratorViewModelTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
    target_link_libraries(SocietyNativeCalibratorViewModelTests PRIVATE iiSDKCore::Runtime)
    add_test(NAME Society.NativeCalibratorViewModel COMMAND SocietyNativeCalibratorViewModelTests)
endif()

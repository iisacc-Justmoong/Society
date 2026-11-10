cmake_minimum_required(VERSION 3.31)
project(SocietyNativeModelChooser LANGUAGES CXX)

find_package(iiSDKCore 0.20 CONFIG REQUIRED COMPONENTS Runtime)
find_package(iiGUI 0.5 CONFIG REQUIRED)
find_package(LVUI 0.1 CONFIG REQUIRED COMPONENTS Native)
find_package(json-c CONFIG REQUIRED)

lvui_generate(NAME NativeModelChooser SOURCE ../NativeModelChooser.lvui
    INCLUDES NativeModelChooserViewModel.h OUTPUT_VARIABLE chooser_view)
iisdkcore_add_application(SocietyModelChooser
    SOURCES ../NativeModelChooserApplication.cpp ../NativeModelChooserViewModel.cpp
        ../NativeModelChooserState.cpp ../NativeModelChooserProtocol.cpp "${chooser_view}"
    LIBRARIES iiGUI::iiGUI LVUI::LVUI LVUI::Native json-c::json-c-static)
target_compile_features(SocietyModelChooser PRIVATE cxx_std_23)
target_include_directories(SocietyModelChooser PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/.." "${CMAKE_CURRENT_BINARY_DIR}")

include(CTest)
if(BUILD_TESTING)
    add_executable(SocietyNativeChooserViewModelTests ../../../../tests/native_model_chooser_viewmodel.cpp
        ../NativeModelChooserViewModel.cpp ../NativeModelChooserState.cpp)
    target_compile_features(SocietyNativeChooserViewModelTests PRIVATE cxx_std_23)
    target_include_directories(SocietyNativeChooserViewModelTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
    target_link_libraries(SocietyNativeChooserViewModelTests PRIVATE LVUI::LVUI LVUI::Native iiGUI::iiGUI)
    add_test(NAME Society.NativeChooserViewModel COMMAND SocietyNativeChooserViewModelTests)
endif()

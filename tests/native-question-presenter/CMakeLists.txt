cmake_minimum_required(VERSION 3.31)
project(SocietyNativeQuestionPresenterTests LANGUAGES CXX)
include(CTest)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Core Gui)
include(../../cmake/QtApple.cmake)
find_package(iiMultimodalAgent 0.1 CONFIG REQUIRED)
find_package(LVUI 0.1 CONFIG REQUIRED COMPONENTS Native)
find_package(iiGUI 0.5 CONFIG REQUIRED)
lvui_generate(NAME UserQuestionsSheet SOURCE "${IIMULTIMODALAGENT_VIEWS_DIR}/UserQuestionsSheet.lvui"
    FRAMEWORK iiGUI INCLUDES iiMultimodalAgent/agent/NativeQuestionViewModel.h OUTPUT_VARIABLE generated)
add_executable(SocietyNativeQuestionPresenterTests presenter.cpp
    ../../App/Society/Mcp/NativeQuestionPresenter.cpp ../../App/Society/Mcp/NativeQuestionPresenter.h
    ../../App/Society/Native/NativeOverlayCoordinator.cpp ../../App/Society/Native/NativeOverlayCoordinator.h
    ../../App/Society/Native/NativeModalPresence.cpp ../../App/Society/Native/NativeModalPresence.h
    ../../App/Society/Native/ExternalOverlaySurface.cpp ../../App/Society/Native/ExternalOverlaySurface.h "${generated}")
target_compile_features(SocietyNativeQuestionPresenterTests PRIVATE cxx_std_23)
target_include_directories(SocietyNativeQuestionPresenterTests PRIVATE ../../App "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(SocietyNativeQuestionPresenterTests PRIVATE Qt6::Core Qt6::Gui
    iiMultimodalAgent::iiMultimodalAgent LVUI::Native iiGUI::iiGUI)
add_test(NAME Society.NativeQuestionPresenterAdmission COMMAND SocietyNativeQuestionPresenterTests)
set_tests_properties(Society.NativeQuestionPresenterAdmission PROPERTIES TIMEOUT 30)
if(APPLE)
    set_tests_properties(Society.NativeQuestionPresenterAdmission PROPERTIES ENVIRONMENT_MODIFICATION
        "DYLD_LIBRARY_PATH=unset:;DYLD_FALLBACK_LIBRARY_PATH=unset:;DYLD_FRAMEWORK_PATH=unset:;DYLD_FALLBACK_FRAMEWORK_PATH=unset:;QT_QPA_PLATFORM=set:offscreen")
endif()

cmake_minimum_required(VERSION 3.31)
project(SocietyNativeAccountIOSCompile LANGUAGES CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    message(FATAL_ERROR "This compile fixture requires an iOS toolchain")
endif()
set(SOCIETY_ACCOUNT_QT_INCLUDE "" CACHE PATH "Target account-service build include directory")
set(SOCIETY_ACCOUNT_LEGACY_HEADER "" CACHE FILEPATH "AccountViews header used by the legacy handoff")
if(NOT EXISTS "${SOCIETY_ACCOUNT_QT_INCLUDE}/iiAccountManager/AccountManager.h" OR
   NOT EXISTS "${SOCIETY_ACCOUNT_LEGACY_HEADER}")
    message(FATAL_ERROR "Supply target account-service includes and the AccountViews header")
endif()

find_package(Qt6 6.8.3 EXACT CONFIG REQUIRED COMPONENTS Core Gui Network Qml Quick)
find_package(iiAccountManagerPresentation 0.2 CONFIG REQUIRED)
find_package(LVUI 0.1 CONFIG REQUIRED COMPONENTS Native)
find_package(iiGUI 0.5 CONFIG REQUIRED)

# The legacy type header participates in handoff; its QML library is not
# linked by this object-only cross-compilation fixture.
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/compat/iiAccountManager/Quick/AccountViews.h"
    CONTENT "#pragma once\n#include \"${SOCIETY_ACCOUNT_LEGACY_HEADER}\"\n")
lvui_generate(NAME AccountDialog SOURCE "${iiAccountManagerPresentation_VIEWS_DIR}/AccountDialog.lvui"
    FRAMEWORK iiGUI INCLUDES iiAccountManager/AccountViewModel.h OUTPUT_VARIABLE generated)

set(app "${CMAKE_CURRENT_LIST_DIR}/../../App")
add_library(SocietyNativeAccountIOSObjects OBJECT
    "${app}/Society/Native/NativeAccountPresenter.cpp"
    "${app}/Society/Native/CoreDeadlineScheduler.cpp"
    "${app}/Society/Native/NativeOverlayCoordinator.cpp"
    "${app}/Society/Native/NativeModalPresence.cpp"
    "${app}/Society/Native/ExternalOverlaySurface.cpp"
    "${generated}")
set_target_properties(SocietyNativeAccountIOSObjects PROPERTIES AUTOMOC ON CXX_EXTENSIONS OFF)
target_compile_features(SocietyNativeAccountIOSObjects PRIVATE cxx_std_23)
target_include_directories(SocietyNativeAccountIOSObjects PRIVATE
    "${app}" "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/compat"
    "${SOCIETY_ACCOUNT_QT_INCLUDE}")
target_link_libraries(SocietyNativeAccountIOSObjects PRIVATE
    iiAccountManager::Presentation LVUI::Native iiGUI::iiGUI
    Qt6::Core Qt6::Gui Qt6::Network Qt6::Qml Qt6::Quick)

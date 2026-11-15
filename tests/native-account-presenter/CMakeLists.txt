cmake_minimum_required(VERSION 3.31)
project(SocietyNativeAccountPresenterTests LANGUAGES CXX)
include(CTest)
add_test(NAME Society.NativeAccountPlatform COMMAND "${CMAKE_COMMAND}"
    "-DSOCIETY_ACCOUNT_PLATFORM_FILE=${CMAKE_CURRENT_LIST_DIR}/../../cmake/NativeAccountPlatform.cmake"
    -P "${CMAKE_CURRENT_LIST_DIR}/../native_account_platform.cmake")
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Core Gui Qml Quick Network)
include(../../cmake/QtApple.cmake)
find_package(iiAccountManager 0.2.10 CONFIG REQUIRED COMPONENTS Quick)
find_package(iiAccountManagerPresentation 0.2.10 CONFIG REQUIRED)
find_package(iiAccountManagerQtPresentationBridge 0.2.10 CONFIG REQUIRED)
find_package(LVUI 0.1 CONFIG REQUIRED COMPONENTS Native)
find_package(iiGUI 0.5 CONFIG REQUIRED)
lvui_generate(NAME AccountDialog SOURCE "${iiAccountManagerPresentation_VIEWS_DIR}/AccountDialog.lvui"
    FRAMEWORK iiGUI INCLUDES iiAccountManager/AccountViewModel.h OUTPUT_VARIABLE generated)
foreach(locale IN ITEMS en ko)
    set_source_files_properties("${iiAccountManagerPresentation_STRINGS_DIR}/${locale}.json"
        PROPERTIES QT_RESOURCE_ALIAS "${locale}.json")
endforeach()
add_executable(SocietyNativeAccountPresenterTests presenter.cpp
    ../../App/Society/Native/NativeAccountPresenter.cpp ../../App/Society/Native/NativeAccountPresenter.h
    ../../App/Society/Native/CoreDeadlineScheduler.cpp ../../App/Society/Native/CoreDeadlineScheduler.h
    ../../App/Society/Native/NativeOverlayCoordinator.cpp ../../App/Society/Native/NativeOverlayCoordinator.h
    ../../App/Society/Native/NativeModalPresence.cpp ../../App/Society/Native/NativeModalPresence.h
    ../../App/Society/Native/ExternalOverlaySurface.cpp ../../App/Society/Native/ExternalOverlaySurface.h "${generated}")
qt_add_resources(SocietyNativeAccountPresenterTests accountNativeStrings PREFIX "/iiAccountManager/native-strings"
    FILES "${iiAccountManagerPresentation_STRINGS_DIR}/en.json" "${iiAccountManagerPresentation_STRINGS_DIR}/ko.json")
target_compile_features(SocietyNativeAccountPresenterTests PRIVATE cxx_std_23)
target_include_directories(SocietyNativeAccountPresenterTests PRIVATE ../../App "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(SocietyNativeAccountPresenterTests PRIVATE iiAccountManager::Quick
    iiAccountManager::iiAccountManager iiAccountManager::Presentation iiAccountManager::QtPresentationBridge LVUI::Native iiGUI::iiGUI
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Network)
add_test(NAME Society.NativeAccountPresenter COMMAND SocietyNativeAccountPresenterTests)
set_tests_properties(Society.NativeAccountPresenter PROPERTIES TIMEOUT 30)
add_executable(SocietyNativeAccountViewTests view.cpp ../../App/Society/Native/CoreDeadlineScheduler.cpp
    ../../App/Society/Native/CoreDeadlineScheduler.h "${generated}")
target_compile_features(SocietyNativeAccountViewTests PRIVATE cxx_std_23)
target_include_directories(SocietyNativeAccountViewTests PRIVATE ../../App "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(SocietyNativeAccountViewTests PRIVATE iiAccountManager::iiAccountManager
    iiAccountManager::Presentation iiAccountManager::QtPresentationBridge LVUI::Native iiGUI::iiGUI Qt6::Core Qt6::Gui Qt6::Network)
add_test(NAME Society.NativeAccountView COMMAND SocietyNativeAccountViewTests)
set_tests_properties(Society.NativeAccountView PROPERTIES TIMEOUT 30)
if(APPLE)
    set_tests_properties(Society.NativeAccountPresenter PROPERTIES ENVIRONMENT_MODIFICATION
        "DYLD_LIBRARY_PATH=unset:;DYLD_FALLBACK_LIBRARY_PATH=unset:;DYLD_FRAMEWORK_PATH=unset:;DYLD_FALLBACK_FRAMEWORK_PATH=unset:;QT_QPA_PLATFORM=set:offscreen")
    set_tests_properties(Society.NativeAccountView PROPERTIES ENVIRONMENT_MODIFICATION
        "DYLD_LIBRARY_PATH=unset:;DYLD_FALLBACK_LIBRARY_PATH=unset:;DYLD_FRAMEWORK_PATH=unset:;DYLD_FALLBACK_FRAMEWORK_PATH=unset:;QT_QPA_PLATFORM=set:offscreen")
endif()

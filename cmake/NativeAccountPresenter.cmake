find_package(iiAccountManagerPresentation 0.2.10 CONFIG REQUIRED)
find_package(iiAccountManagerQtPresentationBridge 0.2.10 CONFIG REQUIRED)
if(NOT EXISTS "${iiAccountManagerPresentation_VIEWS_DIR}/AccountDialog.lvui" OR
   NOT EXISTS "${iiAccountManagerPresentation_STRINGS_DIR}/en.json" OR
   NOT EXISTS "${iiAccountManagerPresentation_STRINGS_DIR}/ko.json")
    message(FATAL_ERROR "The selected account presentation package lacks the LVUI view or strings")
endif()
include(cmake/NativeOverlay.cmake)
lvui_generate(NAME AccountDialog SOURCE "${iiAccountManagerPresentation_VIEWS_DIR}/AccountDialog.lvui"
    FRAMEWORK iiGUI INCLUDES iiAccountManager/AccountViewModel.h OUTPUT_VARIABLE _society_native_account_view)
foreach(locale IN ITEMS en ko)
    set_source_files_properties("${iiAccountManagerPresentation_STRINGS_DIR}/${locale}.json"
        PROPERTIES QT_RESOURCE_ALIAS "${locale}.json")
endforeach()
qt_add_resources(Society nativeAccountStrings PREFIX "/iiAccountManager/native-strings"
    FILES "${iiAccountManagerPresentation_STRINGS_DIR}/en.json" "${iiAccountManagerPresentation_STRINGS_DIR}/ko.json")
target_sources(Society PRIVATE
    App/Society/Native/NativeAccountPresenter.h App/Society/Native/NativeAccountPresenter.cpp
    App/Society/Native/CoreDeadlineScheduler.h App/Society/Native/CoreDeadlineScheduler.cpp
    App/Society/Native/OverlayShutdown.h "${_society_native_account_view}")
target_include_directories(Society PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/App" "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(Society PRIVATE iiAccountManager::QtPresentationBridge iiAccountManager::Presentation
    LVUI::Native iiGUI::iiGUI Qt6::Gui)
target_compile_definitions(Society PRIVATE SOCIETY_NATIVE_ACCOUNT_PRESENTER=1)

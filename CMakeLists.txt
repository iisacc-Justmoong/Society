cmake_minimum_required(VERSION 3.31)
project(Society VERSION 0.1.0 LANGUAGES CXX)

if(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 20)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    enable_language(Swift)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_CROSSCOMPILING)
    if(NOT DEFINED LVRS_DIR OR NOT EXISTS "${LVRS_DIR}/LVRSConfig.cmake")
        message(FATAL_ERROR "Set LVRS_DIR to the installed LVRS package for the target platform")
    endif()
endif()
find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Quick QuickControls2 Concurrent)
find_package(LVRS CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/LVRS")
qt_standard_project_setup(REQUIRES 6.8)

set(society_sdk_packages
    iiAcountManager
    iiCalendar
    iiFileProvider
    iiGeneralDocument
    iiLicenseManager
    iiLocalDiffusion
    iiLocalLLM
    iiServerHost
    iiSharedCanvas
    iiSocietyContainer
    iiSocietyHelper
    iiSocietySync
    iiUpdateManager
    iiVoiceOver
    iiWhatsNew
)
if(NOT APPLE OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
    # These are the SDKs called by the current app. The remaining desktop
    # dependencies are compatibility checks; they do not implement UI features.
    set(society_sdk_packages iiAcountManager iiCalendar iiServerHost iiSocietyContainer iiSocietyHelper iiSocietySync)
endif()
set(society_sdk_targets)
foreach(society_sdk IN LISTS society_sdk_packages)
    if(CMAKE_CROSSCOMPILING)
        if(NOT DEFINED ${society_sdk}_DIR OR NOT EXISTS "${${society_sdk}_DIR}/${society_sdk}Config.cmake")
            message(FATAL_ERROR "Install ${society_sdk} for the target SDK and set ${society_sdk}_DIR; host packages cannot substitute it")
        endif()
    endif()
    if(society_sdk STREQUAL "iiAcountManager")
        find_package(iiAcountManager 0.2.6 CONFIG REQUIRED COMPONENTS Quick HINTS "$ENV{HOME}/.local/SDK/iiAcountManager")
    elseif(society_sdk STREQUAL "iiCalendar")
        find_package(iiCalendar 0.1 CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/iiCalendar")
    elseif(society_sdk STREQUAL "iiSocietyContainer")
        find_package(iiSocietyContainer 0.14.0 CONFIG REQUIRED COMPONENTS Gui HINTS "$ENV{HOME}/.local/SDK/iiSocietyContainer")
    elseif(society_sdk STREQUAL "iiServerHost")
        find_package(iiServerHost 0.6.0 CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/iiServerHost")
    elseif(society_sdk STREQUAL "iiSocietyHelper")
        find_package(iiSocietyHelper 0.7.1 CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/iiSocietyHelper")
    elseif(society_sdk STREQUAL "iiSocietySync")
        find_package(iiSocietySync 0.8.0 CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/iiSocietySync")
    else()
        find_package(${society_sdk} CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/${society_sdk}")
    endif()
    list(APPEND society_sdk_targets "${society_sdk}::${society_sdk}")
endforeach()

add_library(SocietyServices STATIC src/Daemon/DaemonService.cpp src/Daemon/DaemonService.h
    src/App/Services/SyncOwnership.cpp src/App/Services/SyncOwnership.h
    src/App/Services/SocietyInbox.cpp src/App/Services/SocietyInbox.h
    src/App/Services/SocietyRuntime.cpp src/App/Services/SocietyRuntime.h)
target_link_libraries(SocietyServices PUBLIC iiSocietyHelper::iiSocietyHelper)
target_include_directories(SocietyServices PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(SocietyServices PUBLIC SOCIETY_APP_VERSION="${PROJECT_VERSION}")
add_library(SocietyPhotos STATIC src/App/Photos/PhotoStore.cpp src/App/Photos/PhotoStore.h src/App/Photos/PhotoLibrary.h
    src/App/Photos/PhotoController.cpp src/App/Photos/PhotoController.h)
target_include_directories(SocietyPhotos PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(SocietyPhotos PUBLIC Qt6::Gui Qt6::Concurrent iiSocietyContainer::Gui)
set_target_properties(SocietyPhotos PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(APPLE)
    target_sources(SocietyPhotos PRIVATE src/App/Photos/PhotoLibraryApple.mm)
    set_source_files_properties(src/App/Photos/PhotoLibraryApple.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(SocietyPhotos PRIVATE "-framework Photos" "-framework Foundation" "-framework UniformTypeIdentifiers")
    if(IOS)
        target_link_libraries(SocietyPhotos PRIVATE "-framework UIKit")
    else()
        target_link_libraries(SocietyPhotos PRIVATE "-framework AppKit")
    endif()
elseif(ANDROID)
    target_sources(SocietyPhotos PRIVATE src/App/Photos/PhotoLibraryAndroid.cpp)
elseif(WIN32)
    target_sources(SocietyPhotos PRIVATE src/App/Photos/PhotoLibraryWindows.cpp)
    target_link_libraries(SocietyPhotos PRIVATE shell32 ole32 gdi32 uuid)
else()
    target_sources(SocietyPhotos PRIVATE src/App/Photos/PhotoLibraryUnsupported.cpp)
endif()
if(NOT IOS AND NOT ANDROID AND NOT EMSCRIPTEN)
    qt_add_executable(SocietyDaemon src/Daemon/main.cpp)
    target_link_libraries(SocietyDaemon PRIVATE SocietyServices)
    set_target_properties(SocietyDaemon PROPERTIES MACOSX_BUNDLE FALSE QT_QML_MODULE_NO_IMPORT_SCAN TRUE)
endif()

find_package(iiSocietyClient 0.1.0 CONFIG REQUIRED)
if(NOT IOS AND NOT ANDROID AND NOT EMSCRIPTEN)
    find_package(iiSocietyGeneration 0.1.0 CONFIG REQUIRED)
endif()
add_library(SocietyGroupState INTERFACE)
target_include_directories(SocietyGroupState INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(SocietyGroupState INTERFACE iiSocietyClient::iiSocietyClient)
set(society_account_sources src/App/Account/AccountController.cpp src/App/Account/AccountController.h)
add_library(SocietyDiscovery STATIC src/App/Network/MobileSyncActivity.cpp src/App/Network/MobileSyncActivity.h)
set_target_properties(SocietyDiscovery PROPERTIES AUTOMOC ON POSITION_INDEPENDENT_CODE ON)
target_compile_features(SocietyDiscovery PUBLIC cxx_std_23)
target_include_directories(SocietyDiscovery PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(SocietyDiscovery PUBLIC iiSocietyClient::iiSocietyClient)
if(TARGET iiSocietyGeneration::Host)
    target_link_libraries(SocietyDiscovery PUBLIC iiSocietyGeneration::Host)
endif()
set(society_pairing_sources src/App/Network/DevicePairing.cpp src/App/Network/DevicePairing.h
    src/App/Network/PairingQr.cpp src/App/Network/PairingQr.h src/App/Network/QrScanner.cpp src/App/Network/QrScanner.h
    ThirdParty/qrcodegen/qrcodegen.cpp ThirdParty/qrcodegen/qrcodegen.hpp)
if(TARGET SocietyDaemon)
    target_sources(SocietyDaemon PRIVATE ${society_account_sources}
        src/App/Network/NetworkDriveController.cpp src/App/Network/NetworkDriveController.h)
    target_link_libraries(SocietyDaemon PRIVATE SocietyPhotos SocietyDiscovery SocietyGroupState iiSocietySync::iiSocietySync Qt6::Gui Qt6::Qml)
    target_compile_definitions(SocietyDaemon PRIVATE SOCIETY_HEADLESS SOCIETY_APP_VERSION="${PROJECT_VERSION}")
endif()
set(society_model_merge_sources src/App/Tools/ModelMergeController.cpp src/App/Tools/ModelMergeController.h
    src/App/Tools/MergeModelCatalog.cpp src/App/Tools/MergeModelCatalog.h)
if(NOT CMAKE_CROSSCOMPILING)
    if(iiLocalDiffusion_DIR)
        get_filename_component(society_merge_sdk_prefix "${iiLocalDiffusion_DIR}/../../.." ABSOLUTE)
    endif()
    find_program(SOCIETY_MODEL_MERGE_EXECUTABLE NAMES iild-merge
        HINTS "${society_merge_sdk_prefix}/bin" "$ENV{HOME}/.local/SDK/iiLocalDiffusion/bin")
    if(SOCIETY_MODEL_MERGE_EXECUTABLE)
        set_source_files_properties(src/App/Tools/ModelMergeController.cpp PROPERTIES
            COMPILE_DEFINITIONS "SOCIETY_MODEL_MERGE_EXECUTABLE=\"${SOCIETY_MODEL_MERGE_EXECUTABLE}\"")
    endif()
endif()
set(society_calendar_sources src/App/Dashboard/DashboardCalendar.cpp src/App/Dashboard/DashboardCalendar.h)
qt_add_executable(Society
    ${society_calendar_sources}
    ${society_model_merge_sources}
    src/main.cpp ${society_account_sources} ${society_pairing_sources} src/App/Files/DirectoryLocation.cpp src/App/Files/DirectoryLocation.h
        src/App/Dashboard/DashboardFiles.cpp src/App/Dashboard/DashboardFiles.h
        src/App/Models/StorageModels.cpp src/App/Models/StorageModels.h
        src/App/Files/ModelImporter.cpp src/App/Files/ModelImporter.h
        src/App/Files/ModelImportSource.h
        src/App/Drive/DriveController.cpp src/App/Drive/DriveController.h
        src/App/Drive/StorageNavigation.cpp src/App/Drive/StorageNavigation.h
        src/App/Network/NetworkDriveController.cpp src/App/Network/NetworkDriveController.h
)
# Painted C++ items require the QtQuick type metadata as a QML dependency.
# Source location must not change the QML module's existing resource URLs.
file(GLOB_RECURSE _app_source_resources CONFIGURE_DEPENDS
    RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/src/App/*.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/App/*.svg")
foreach(_app_resource IN LISTS _app_source_resources)
    string(REGEX REPLACE "^src/" "" _app_resource_alias "${_app_resource}")
    set_source_files_properties("${_app_resource}" PROPERTIES QT_RESOURCE_ALIAS "${_app_resource_alias}")
endforeach()

qt_add_qml_module(Society
    URI Society VERSION 1.0 RESOURCE_PREFIX "/qt/qml"
    IMPORT_PATH "${iiAcountManager_QML_IMPORT_PATH}"
    DEPENDENCIES QtQuick iiAccountManager
    QML_FILES src/App/Gallery/GalleryTile.qml src/App/Gallery/GalleryZoom.qml src/App/Gallery/GalleryInfo.qml src/App/Main.qml src/App/MobileGestures.qml src/App/FileGridView.qml src/App/Network/NetworkDevices.qml src/App/Network/PairingPanel.qml
        src/App/SocietyView.qml src/App/OnboardingView.qml src/App/StorageView.qml src/App/Dashboard/Dashboard.qml src/App/Dashboard/DashboardSidebar.qml src/App/Dashboard/QuickGenerate.qml src/App/Dashboard/DashboardCalendarView.qml
        src/App/Dashboard/CalendarEntry.qml src/App/Dashboard/CalendarDetailSection.qml
        src/App/Models/ModelsView.qml src/App/Drive/StorageSidebar.qml src/App/Photos/PhotosView.qml src/App/Files/FileActionMenu.qml src/App/FilesBrowser.qml
        src/App/Preferences/PreferencesWindow.qml src/App/Preferences/PreferencesContent.qml
        src/App/Tools/ModelMergeTool.qml src/App/Tools/MergePathField.qml src/App/Tools/MergeModelPicker.qml
        src/App/Tools/ToolsView.qml src/App/Tools/ToolCard.qml
    RESOURCES
        src/App/Dashboard/icons/home.svg src/App/Dashboard/icons/projects.svg
        src/App/Dashboard/icons/calendar.svg src/App/Dashboard/icons/activity.svg
        src/App/Dashboard/icons/people.svg src/App/Dashboard/icons/guild.svg
        src/App/Dashboard/icons/organization.svg
)
target_include_directories(Society PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/App/Models")
# Shared Qt resolves QML plugins at runtime; avoid recursively scanning build/
# (including archived app bundles) on every desktop reconfiguration.
get_target_property(society_qt_core_type Qt6::Core TYPE)
if(society_qt_core_type STREQUAL "SHARED_LIBRARY")
    set_property(TARGET Society PROPERTY QT_QML_MODULE_NO_IMPORT_SCAN TRUE)
endif()
target_link_libraries(Society PRIVATE Qt6::Quick Qt6::QuickControls2 iiAcountManager::Quick)
lvrs_configure_qml_app(Society NO_PLATFORM_RUNTIME_TARGETS)
target_link_libraries(Society PRIVATE ${society_sdk_targets} SocietyPhotos SocietyServices SocietyDiscovery SocietyGroupState Qt6::Concurrent)
qt_add_resources(Society icu_license PREFIX "/licenses/icu"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/icu" FILES ThirdParty/icu/LICENSE.txt)
qt_add_resources(Society qrcodegen_license PREFIX "/licenses/qrcodegen"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/qrcodegen" FILES ThirdParty/qrcodegen/LICENSE)
if(NOT IOS AND NOT ANDROID)
    get_filename_component(society_serverhost_prefix "${iiServerHost_DIR}/../../.." ABSOLUTE)
    qt_add_resources(Society lan_openssl_license PREFIX "/licenses/openssl"
        BASE "${society_serverhost_prefix}/share/iiServerHost/licenses"
        FILES "${society_serverhost_prefix}/share/iiServerHost/licenses/OpenSSL-LICENSE.txt")
endif()
option(SOCIETY_ACCOUNT_RUNTIME_PROBE "Enable credential-free TLS and account panel runtime verification" OFF)
option(SOCIETY_GROUP_STATE_RUNTIME_PROBE "Enable opt-in synthetic encrypted group state device verification" OFF)
option(SOCIETY_DASHBOARD_RUNTIME_PROBE "Enable opt-in read-only dashboard device verification" OFF)
if(SOCIETY_DASHBOARD_RUNTIME_PROBE)
    target_sources(Society PRIVATE tests/DashboardRuntimeProbe.cpp)
    target_compile_definitions(Society PRIVATE SOCIETY_DASHBOARD_RUNTIME_PROBE)
endif()
if(SOCIETY_GROUP_STATE_RUNTIME_PROBE)
    target_sources(Society PRIVATE tests/GroupStateRuntimeProbe.cpp)
    target_compile_definitions(Society PRIVATE SOCIETY_GROUP_STATE_RUNTIME_PROBE)
endif()
if(SOCIETY_ACCOUNT_RUNTIME_PROBE)
    target_sources(Society PRIVATE tests/AccountRuntimeProbe.cpp)
    target_compile_definitions(Society PRIVATE SOCIETY_ACCOUNT_RUNTIME_PROBE)
endif()
target_include_directories(Society PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}/src/App/Account")
if(IOS OR ANDROID)
    target_compile_definitions(Society PRIVATE SOCIETY_CLIENT_ONLY=1)
    if(IOS)
        target_sources(Society PRIVATE src/App/Network/IosSyncActivity.mm)
        set_source_files_properties(src/App/Network/IosSyncActivity.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
        target_link_libraries(Society PRIVATE "-framework UIKit" "-framework BackgroundTasks")
    else()
        target_sources(Society PRIVATE src/App/Network/AndroidSyncActivity.cpp)
    endif()
endif()
if(APPLE AND NOT IOS)
    target_sources(Society PRIVATE src/Daemon/platform/macos/ServiceRegistration.mm
        src/Daemon/platform/macos/ServiceRegistration.h
        src/Daemon/platform/macos/com.iisacc.society.daemon.plist)
    set_source_files_properties(src/Daemon/platform/macos/ServiceRegistration.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    set_source_files_properties(src/Daemon/platform/macos/com.iisacc.society.daemon.plist PROPERTIES
        MACOSX_PACKAGE_LOCATION "Library/LaunchAgents")
    target_link_libraries(Society PRIVATE "-framework ServiceManagement" "-framework Foundation")
    add_dependencies(Society SocietyDaemon)
    set_property(TARGET Society APPEND PROPERTY LINK_DEPENDS "$<TARGET_FILE:SocietyDaemon>"
        "${CMAKE_CURRENT_BINARY_DIR}/SocietyDaemon-Info.plist")
    add_custom_command(TARGET Society POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory
            "$<TARGET_BUNDLE_DIR:Society>/Contents/Helpers/SocietyDaemon.app/Contents/MacOS"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:SocietyDaemon>"
            "$<TARGET_BUNDLE_DIR:Society>/Contents/Helpers/SocietyDaemon.app/Contents/MacOS/SocietyDaemon"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_BINARY_DIR}/SocietyDaemon-Info.plist"
            "$<TARGET_BUNDLE_DIR:Society>/Contents/Helpers/SocietyDaemon.app/Contents/Info.plist"
        COMMAND "${CMAKE_COMMAND}" -E rm -f "$<TARGET_BUNDLE_DIR:Society>/Contents/MacOS/SocietyDaemon")
    find_program(society_macdeployqt macdeployqt HINTS "${Qt6_DIR}/../../../bin" REQUIRED)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(SOCIETY_MAC_SIGN_IDENTITY "-" CACHE STRING "Signing identity for the local daemon package; '-' uses ad-hoc signing")
    set(SOCIETY_MAC_APP_GROUP "group.com.iisacc.society" CACHE STRING "macOS App Group; use <TeamID>.com.iisacc.society with a developer signature")
    set(SOCIETY_MAC_RUNTIME_SEARCH_PATHS "" CACHE STRING "Additional native runtime directories for macOS bundle deployment")
    configure_file(src/Daemon/platform/macos/Info.plist.in "${CMAKE_CURRENT_BINARY_DIR}/SocietyDaemon-Info.plist" @ONLY)
    configure_file(src/platform/macos/App.entitlements.in "${CMAKE_CURRENT_BINARY_DIR}/Society-Mac.entitlements" @ONLY)
    set_property(TARGET Society APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/Society-Mac.entitlements")
    add_custom_target(SocietyDaemonPackage
        COMMAND "${CMAKE_COMMAND}"
            "-DSOCIETY_SOURCE_APP=$<TARGET_BUNDLE_DIR:Society>"
            "-DSOCIETY_PACKAGE_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/package"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/PackageMac.cmake"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_daemon_package.py"
            "${CMAKE_CURRENT_BINARY_DIR}/package/Society.app" "$<TARGET_BUNDLE_DIR:Society>"
            "$<TARGET_FILE:iiSocietyContainer::iiSocietyContainer>"
            "$<TARGET_FILE:iiSocietyContainer::Gui>"
        COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_macos_runtime.py"
            "${CMAKE_CURRENT_BINARY_DIR}/package/Society.app" --gui
        DEPENDS Society SocietyDaemon USES_TERMINAL VERBATIM)
endif()
target_include_directories(Society PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/App/Files" "${CMAKE_CURRENT_SOURCE_DIR}/src/App/Drive"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/App/Network" "${CMAKE_CURRENT_SOURCE_DIR}/src/App/Dashboard"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/App/Tools")
target_compile_definitions(Society PRIVATE
    SOCIETY_APP_VERSION="${PROJECT_VERSION}"
    SOCIETY_LVRS_QML_IMPORT_PATH="${LVRS_QML_IMPORT_PATH}"
)
if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    target_compile_definitions(Society PRIVATE SOCIETY_NATIVE_DRIVE_APP="${iiSocietyContainer_NATIVE_APP}")
endif()
if(iiSocietyContainer_MOUNT_EXECUTABLE)
    target_compile_definitions(Society PRIVATE SOCIETY_MOUNT_EXECUTABLE="${iiSocietyContainer_MOUNT_EXECUTABLE}")
    add_custom_command(TARGET Society POST_BUILD COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${iiSocietyContainer_MOUNT_EXECUTABLE}" "$<TARGET_FILE_DIR:Society>" VERBATIM)
endif()
if(ANDROID)
    iiSocietyContainer_add_android_file_provider(Society)
    include(cmake/AndroidQr.cmake)
    society_add_android_qr(Society)
    set(SOCIETY_ANDROID_OPENSSL_DIR "" CACHE PATH "OpenSSL 3 Android runtime built by tools/build_android_openssl.py")
    foreach(society_ssl_library libcrypto_3.so libssl_3.so)
        if(NOT EXISTS "${SOCIETY_ANDROID_OPENSSL_DIR}/${society_ssl_library}")
            message(FATAL_ERROR "Android HTTPS needs ${society_ssl_library}; use tools/build_android.py or set SOCIETY_ANDROID_OPENSSL_DIR")
        endif()
        set_property(TARGET Society APPEND PROPERTY QT_ANDROID_EXTRA_LIBS
            "${SOCIETY_ANDROID_OPENSSL_DIR}/${society_ssl_library}")
    endforeach()
    if(NOT EXISTS "${SOCIETY_ANDROID_OPENSSL_DIR}/LICENSE.txt")
        message(FATAL_ERROR "The OpenSSL runtime must include LICENSE.txt")
    endif()
    qt_add_resources(Society android_openssl_license PREFIX "/licenses/openssl"
        BASE "${SOCIETY_ANDROID_OPENSSL_DIR}" FILES "${SOCIETY_ANDROID_OPENSSL_DIR}/LICENSE.txt")
endif()
set_target_properties(Society PROPERTIES
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.iisacc.society"
    MACOSX_BUNDLE_BUNDLE_NAME "Society"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    target_sources(Society PRIVATE src/App/Files/AppleModelSource.mm src/App/Files/AppleModelSource.h
        src/App/Network/IosQrScanner.mm src/App/Network/AppleQrDecoder.mm src/App/Network/AppleQrDecoder.h
        src/App/Files/IosModelDrop.mm src/App/Files/IosModelDrop.h
        src/App/Files/IosFileAccess.mm src/App/Files/IosFileAccess.h)
    set_source_files_properties(src/App/Network/IosQrScanner.mm src/App/Network/AppleQrDecoder.mm src/App/Files/AppleModelSource.mm src/App/Files/IosModelDrop.mm src/App/Files/IosFileAccess.mm
        PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(Society PRIVATE "-framework UIKit" "-framework Foundation" "-framework AVFoundation"
        "-framework QuickLook" "-framework UniformTypeIdentifiers" "-framework Vision" "-framework CoreVideo" "-framework CoreMedia")
    # The installed iOS Qt package does not ship FFmpeg's native dependencies.
    # Use its AVFoundation backend for the app's native media integration.
    qt_import_plugins(Society INCLUDE Qt6::QSQLiteDriverPlugin Qt6::QDarwinMediaPlugin
        EXCLUDE Qt6::QFFmpegMediaPlugin)
    # The installed static LVRS package has no separate plugin archive. Keep
    # its QML registration, resources and bundled fonts in the device binary.
    get_target_property(society_lvrs_type LVRS::LVRS TYPE)
    if(society_lvrs_type STREQUAL "STATIC_LIBRARY")
        set_property(TARGET Society PROPERTY LINK_LIBRARY_OVERRIDE "WHOLE_ARCHIVE,LVRS::LVRS")
    endif()
    set(SOCIETY_IOS_APP_GROUP "group.com.iisacc.society" CACHE STRING "Shared Society App Group")
    set(SOCIETY_IOS_TEAM "" CACHE STRING "Apple development team for the app and extension")
    iiSocietyContainer_add_ios_file_provider(Society APP_GROUP "${SOCIETY_IOS_APP_GROUP}" TEAM "${SOCIETY_IOS_TEAM}")
    iiSocietyContainer_add_ios_live_activity(Society TEAM "${SOCIETY_IOS_TEAM}")
    option(SOCIETY_IOS_FILES_INTEGRATION_TEST "Run the real iOS File Provider integration probe on launch" OFF)
    if(SOCIETY_IOS_FILES_INTEGRATION_TEST)
        target_sources(SocietyIosDriveBridge PRIVATE tests/IosFilesIntegration.swift)
        target_compile_definitions(Society PRIVATE SOCIETY_IOS_FILES_INTEGRATION_TEST)
    endif()
endif()

if(APPLE)
    include(cmake/NetworkPrivacy.cmake)
    society_add_network_privacy(Society)
endif()
include(cmake/AppIcons.cmake)
society_add_app_icons(Society)
include(cmake/ApplicationLinks.cmake)
society_add_application_links(Society)
if(APPLE AND NOT IOS)
    target_link_options(Society PRIVATE "LINKER:-dead_strip_dylibs")
    set(society_runtime_arguments)
    foreach(runtime_target IN ITEMS LVRS::LVRS iiSocietyContainer::Gui ${society_sdk_targets})
        get_target_property(runtime_type ${runtime_target} TYPE)
        if(runtime_type STREQUAL "SHARED_LIBRARY")
            list(APPEND society_runtime_arguments --library "$<TARGET_FILE:${runtime_target}>")
        endif()
    endforeach()
    foreach(runtime_path IN LISTS SOCIETY_MAC_RUNTIME_SEARCH_PATHS)
        list(APPEND society_runtime_arguments --search-path "${runtime_path}")
    endforeach()
    # Finder and launchd do not inherit development DYLD/QML search paths.
    # Stage both processes before signing, and reject an incomplete bundle.
    set_property(TARGET Society APPEND PROPERTY LINK_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/deploy_macos_runtime.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_macos_runtime.py")
    add_custom_command(TARGET Society POST_BUILD
        COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tools/deploy_macos_runtime.py"
            --app "$<TARGET_BUNDLE_DIR:Society>" --macdeployqt "${society_macdeployqt}"
            --qml-dir "${CMAKE_CURRENT_SOURCE_DIR}/src/App"
            --qml-import "${LVRS_QML_IMPORT_PATH}" --qml-import "${iiAcountManager_QML_IMPORT_PATH}"
            --qml-import "${CMAKE_CURRENT_BINARY_DIR}"
            --identity "${SOCIETY_MAC_SIGN_IDENTITY}"
            --entitlements "${CMAKE_CURRENT_BINARY_DIR}/Society-Mac.entitlements"
            --log "${CMAKE_CURRENT_BINARY_DIR}/macos-runtime-deployment.log"
            ${society_runtime_arguments}
        COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_macos_runtime.py"
            "$<TARGET_BUNDLE_DIR:Society>"
        VERBATIM)
endif()

include(CTest)
if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Test)
    qt_add_executable(SocietyPhotoTests tests/tst_photos.cpp)
    target_link_libraries(SocietyPhotoTests PRIVATE SocietyPhotos iiSocietySync::iiSocietySync Qt6::Test Qt6::Quick Qt6::QuickControls2)
    lvrs_configure_qml_app(SocietyPhotoTests NO_PLATFORM_RUNTIME_TARGETS)
    target_compile_definitions(SocietyPhotoTests PRIVATE SOCIETY_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}"
        SOCIETY_PHOTOS_QML_FILE="${CMAKE_CURRENT_SOURCE_DIR}/tests/photos/PhotosHarness.qml"
        SOCIETY_LVRS_QML_IMPORT_PATH="${LVRS_QML_IMPORT_PATH}")
    add_test(NAME Society.Photos COMMAND SocietyPhotoTests)
    set_tests_properties(Society.Photos PROPERTIES TIMEOUT 60
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1")
    qt_add_executable(SocietyGroupStateTests tests/tst_groupstate.cpp)
    target_link_libraries(SocietyGroupStateTests PRIVATE SocietyGroupState Qt6::Test)
    target_compile_definitions(SocietyGroupStateTests PRIVATE SOCIETY_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME Society.GroupState COMMAND SocietyGroupStateTests)
    set_tests_properties(Society.GroupState PROPERTIES TIMEOUT 30)
    qt_add_executable(SocietyDiscoveryTests tests/tst_discovery.cpp)
    target_link_libraries(SocietyDiscoveryTests PRIVATE SocietyDiscovery Qt6::Test)
    add_test(NAME Society.Discovery COMMAND SocietyDiscoveryTests)
    set_tests_properties(Society.Discovery PROPERTIES TIMEOUT 45)
    find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Test)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    if(APPLE AND NOT IOS)
        add_test(NAME Society.MacRuntimeDeployment COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_macos_runtime.py")
        add_test(NAME Society.MacRuntimeLaunch COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_macos_runtime.py" "$<TARGET_BUNDLE_DIR:Society>" --gui)
        set_tests_properties(Society.MacRuntimeDeployment Society.MacRuntimeLaunch PROPERTIES TIMEOUT 120)
    endif()
    add_test(NAME Society.IosBuildContract COMMAND "${Python3_EXECUTABLE}" -B
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ios_build.py")
    add_test(NAME Society.AppIcons COMMAND "${Python3_EXECUTABLE}" -B
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_app_icons.py")
    add_test(NAME Society.AndroidTlsBuild COMMAND "${Python3_EXECUTABLE}" -B
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_android_openssl.py")
    qt_add_executable(SocietyPairingTests tests/tst_pairing.cpp ${society_pairing_sources} ${society_account_sources}
        src/App/Network/NetworkDriveController.cpp src/App/Network/NetworkDriveController.h)
    target_include_directories(SocietyPairingTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(SocietyPairingTests PRIVATE iiAcountManager::Quick Qt6::Test Qt6::Quick Qt6::QuickControls2
        iiServerHost::iiServerHost iiAcountManager::iiAcountManager iiSocietyContainer::Gui)
    lvrs_configure_qml_app(SocietyPairingTests NO_PLATFORM_RUNTIME_TARGETS)
    target_compile_definitions(SocietyPairingTests PRIVATE SOCIETY_ACCOUNT_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/account.json" SOCIETY_DISABLE_SESSION_RESTORE SOCIETY_APP_VERSION="${PROJECT_VERSION}"
        SOCIETY_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}"
        SOCIETY_PAIRING_QML_FILE="${CMAKE_CURRENT_SOURCE_DIR}/src/App/Network/PairingPanel.qml"
        SOCIETY_LVRS_QML_IMPORT_PATH="${LVRS_QML_IMPORT_PATH}")
    if(APPLE)
        target_sources(SocietyPairingTests PRIVATE tests/DecodeQr.mm src/App/Network/AppleQrDecoder.mm src/App/Network/AppleQrDecoder.h)
        set_source_files_properties(tests/DecodeQr.mm src/App/Network/AppleQrDecoder.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
        target_link_libraries(SocietyPairingTests PRIVATE "-framework CoreImage" "-framework Foundation" "-framework Vision" "-framework CoreVideo")
    endif()
    add_test(NAME Society.Pairing COMMAND SocietyPairingTests)
    set_tests_properties(Society.Pairing PROPERTIES TIMEOUT 60
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1")
    qt_add_executable(SocietyAccountTests tests/tst_account.cpp
        src/App/Network/NetworkDriveController.cpp src/App/Network/NetworkDriveController.h)
    target_include_directories(SocietyAccountTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(SocietyAccountTests PRIVATE Qt6::Test Qt6::Quick Qt6::QuickControls2
        iiAcountManager::iiAcountManager iiSocietyHelper::iiSocietyHelper
        iiServerHost::iiServerHost iiSocietyContainer::iiSocietyContainer)
    lvrs_configure_qml_app(SocietyAccountTests NO_PLATFORM_RUNTIME_TARGETS)
    target_compile_definitions(SocietyAccountTests PRIVATE
        SOCIETY_APP_VERSION="${PROJECT_VERSION}"
        SOCIETY_ACCOUNT_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/account.json"
        SOCIETY_ACCOUNT_QML_FILE="qrc:/qt/qml/iiAccountManager/AccountDialog.qml"
        SOCIETY_LVRS_QML_IMPORT_PATH="${LVRS_QML_IMPORT_PATH}")
    add_test(NAME Society.Account COMMAND SocietyAccountTests)
    target_compile_definitions(SocietyAccountTests PRIVATE SOCIETY_DAEMON_EXECUTABLE="$<TARGET_FILE:SocietyDaemon>")
    add_dependencies(SocietyAccountTests SocietyDaemon)
    set_tests_properties(Society.Account PROPERTIES TIMEOUT 120
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1")
    qt_add_executable(SocietyRuntimeTests tests/tst_runtime.cpp)
    target_link_libraries(SocietyRuntimeTests PRIVATE Qt6::Test SocietyServices)
    target_compile_definitions(SocietyRuntimeTests PRIVATE SOCIETY_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME Society.MobileLifecycle COMMAND SocietyRuntimeTests)
    qt_add_executable(SocietySyncOwnershipTests tests/tst_syncownership.cpp)
    target_link_libraries(SocietySyncOwnershipTests PRIVATE Qt6::Test SocietyServices)
    target_compile_definitions(SocietySyncOwnershipTests PRIVATE SOCIETY_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME Society.SyncOwnership COMMAND SocietySyncOwnershipTests)
    set_tests_properties(Society.SyncOwnership PROPERTIES TIMEOUT 15)
    qt_add_executable(SocietyNetworkTests tests/tst_networkdrive.cpp
        src/App/Network/NetworkDriveController.cpp src/App/Network/NetworkDriveController.h)
    target_include_directories(SocietyNetworkTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(SocietyNetworkTests PRIVATE Qt6::Test Qt6::Qml Qt6::Gui
        iiAcountManager::iiAcountManager iiServerHost::iiServerHost iiSocietyContainer::iiSocietyContainer)
    target_compile_definitions(SocietyNetworkTests PRIVATE SOCIETY_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME Society.NetworkDrive COMMAND SocietyNetworkTests)
    set_tests_properties(Society.NetworkDrive PROPERTIES TIMEOUT 60)
    qt_add_executable(SocietyClientOnlyNetworkTests tests/tst_networkdrive_clientonly.cpp
        ${society_pairing_sources}
        src/App/Network/NetworkDriveController.cpp src/App/Network/NetworkDriveController.h)
    target_include_directories(SocietyClientOnlyNetworkTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(SocietyClientOnlyNetworkTests PRIVATE Qt6::Test Qt6::Quick Qt6::QuickControls2
        iiAcountManager::iiAcountManager iiServerHost::iiServerHost iiSocietyContainer::iiSocietyContainer)
    lvrs_configure_qml_app(SocietyClientOnlyNetworkTests NO_PLATFORM_RUNTIME_TARGETS)
    target_compile_definitions(SocietyClientOnlyNetworkTests PRIVATE SOCIETY_CLIENT_ONLY=1
        SOCIETY_ACCOUNT_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/account.json"
        SOCIETY_NETWORK_QML_FILE="${CMAKE_CURRENT_SOURCE_DIR}/src/App/Network/NetworkDevices.qml"
        SOCIETY_PAIRING_QML_FILE="${CMAKE_CURRENT_SOURCE_DIR}/src/App/Network/PairingPanel.qml"
        SOCIETY_PREFERENCES_QML_FILE="${CMAKE_CURRENT_SOURCE_DIR}/src/App/Preferences/PreferencesWindow.qml"
        SOCIETY_LVRS_QML_IMPORT_PATH="${LVRS_QML_IMPORT_PATH}"
        SOCIETY_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME Society.ClientOnlyNetwork COMMAND SocietyClientOnlyNetworkTests)
    set_tests_properties(Society.ClientOnlyNetwork PROPERTIES TIMEOUT 60
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1")
    set_tests_properties(Society.MobileLifecycle PROPERTIES TIMEOUT 30)
    qt_add_executable(SocietyDaemonTests tests/tst_daemon.cpp)
    target_include_directories(SocietyDaemonTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(SocietyDaemonTests PRIVATE Qt6::Test SocietyServices)
    target_compile_definitions(SocietyDaemonTests PRIVATE SOCIETY_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}"
        SOCIETY_DAEMON_EXECUTABLE="$<TARGET_FILE:SocietyDaemon>"
        SOCIETY_EXECUTABLE_PATH="$<TARGET_FILE:Society>")
    add_dependencies(SocietyDaemonTests SocietyDaemon Society)
    add_test(NAME Society.Daemon COMMAND SocietyDaemonTests)
    set_tests_properties(Society.Daemon PROPERTIES TIMEOUT 45)
    qt_add_executable(SocietyGuiTests tests/tst_filegrid.cpp
        src/App/Files/DirectoryLocation.cpp src/App/Files/DirectoryLocation.h)
    target_include_directories(SocietyGuiTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(SocietyGuiTests PRIVATE Qt6::Test Qt6::Quick Qt6::QuickControls2 iiSocietyContainer::Gui)
    lvrs_configure_qml_app(SocietyGuiTests NO_PLATFORM_RUNTIME_TARGETS)
    target_compile_definitions(SocietyGuiTests PRIVATE
        SOCIETY_QML_FILE="${CMAKE_CURRENT_SOURCE_DIR}/tests/FileGridHarness.qml"
        SOCIETY_LVRS_QML_IMPORT_PATH="${LVRS_QML_IMPORT_PATH}"
        SOCIETY_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}"
    )
    add_test(NAME Society.Gui COMMAND SocietyGuiTests)
    set_tests_properties(Society.Gui PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1"
        TIMEOUT 60
    )

    find_program(SOCIETY_MODEL_MERGE_TEST_PYTHON NAMES python python3
        HINTS "${society_merge_sdk_prefix}/share/iiLocalDiffusion/reference/diffusers/.venv/bin"
        "$ENV{HOME}/.local/SDK/iiLocalDiffusion/share/iiLocalDiffusion/reference/diffusers/.venv/bin"
        NO_DEFAULT_PATH)
    if(NOT SOCIETY_MODEL_MERGE_TEST_PYTHON)
        set(SOCIETY_MODEL_MERGE_TEST_PYTHON "${Python3_EXECUTABLE}")
    endif()
    qt_add_executable(SocietyModelMergeTests tests/tst_modelmerge.cpp ${society_model_merge_sources})
    target_include_directories(SocietyModelMergeTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(SocietyModelMergeTests PRIVATE Qt6::Test Qt6::Quick Qt6::QuickControls2 Qt6::Concurrent iiSocietyContainer::iiSocietyContainer)
    lvrs_configure_qml_app(SocietyModelMergeTests NO_PLATFORM_RUNTIME_TARGETS)
    target_compile_definitions(SocietyModelMergeTests PRIVATE
        SOCIETY_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}"
        SOCIETY_MERGE_TEST_PYTHON="${SOCIETY_MODEL_MERGE_TEST_PYTHON}"
        SOCIETY_MERGE_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/model_merge_fixture.py"
        SOCIETY_MERGE_QML_FILE="${CMAKE_CURRENT_SOURCE_DIR}/tests/ModelMergeHarness.qml"
        SOCIETY_LVRS_QML_IMPORT_PATH="${LVRS_QML_IMPORT_PATH}")
    add_test(NAME Society.ModelMerge COMMAND SocietyModelMergeTests)
    set_tests_properties(Society.ModelMerge PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1"
        TIMEOUT 120)

    qt_add_executable(SocietyCalendarTests tests/tst_calendar.cpp ${society_calendar_sources})
    target_include_directories(SocietyCalendarTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(SocietyCalendarTests PRIVATE Qt6::Test Qt6::Qml Qt6::Concurrent iiCalendar::iiCalendar iiSocietyContainer::iiSocietyContainer)
    target_compile_definitions(SocietyCalendarTests PRIVATE SOCIETY_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME Society.Calendar COMMAND SocietyCalendarTests)
    set_tests_properties(Society.Calendar PROPERTIES TIMEOUT 60)

    qt_add_executable(SocietyDriveTests tests/tst_drive.cpp ${society_calendar_sources} ${society_model_merge_sources}
        ${society_pairing_sources}
        src/App/Models/StorageModels.cpp src/App/Models/StorageModels.h
        src/App/Dashboard/DashboardFiles.cpp src/App/Dashboard/DashboardFiles.h
        src/App/Network/NetworkDriveController.cpp src/App/Network/NetworkDriveController.h
        src/App/Files/ModelImporter.cpp src/App/Files/ModelImporter.h
        src/App/Drive/DriveController.cpp src/App/Drive/DriveController.h
        src/App/Drive/StorageNavigation.cpp src/App/Drive/StorageNavigation.h
        src/App/Files/DirectoryLocation.cpp src/App/Files/DirectoryLocation.h)
    target_include_directories(SocietyDriveTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(SocietyDriveTests PRIVATE iiCalendar::iiCalendar Qt6::Test Qt6::Quick Qt6::QuickControls2
        Qt6::Concurrent
        iiServerHost::iiServerHost iiAcountManager::iiAcountManager iiSocietyContainer::iiSocietyContainer)
    lvrs_configure_qml_app(SocietyDriveTests NO_PLATFORM_RUNTIME_TARGETS)
    target_compile_definitions(SocietyDriveTests PRIVATE
        SOCIETY_ACCOUNT_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/account.json"
        SOCIETY_QML_FILE="${CMAKE_CURRENT_SOURCE_DIR}/src/App/Main.qml"
        SOCIETY_LVRS_QML_IMPORT_PATH="${LVRS_QML_IMPORT_PATH}"
        SOCIETY_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME Society.Drive COMMAND SocietyDriveTests)
    set_tests_properties(Society.Drive PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1"
        TIMEOUT 300)

    qt_add_executable(SocietyModelImportTests tests/tst_modelimporter.cpp
        src/App/Files/ModelImporter.cpp src/App/Files/ModelImporter.h)
    target_include_directories(SocietyModelImportTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(SocietyModelImportTests PRIVATE Qt6::Test Qt6::Qml Qt6::Gui iiSocietyContainer::iiSocietyContainer)
    target_compile_definitions(SocietyModelImportTests PRIVATE SOCIETY_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME Society.ModelImport COMMAND SocietyModelImportTests)
    set_tests_properties(Society.ModelImport PROPERTIES TIMEOUT 60)

    if(APPLE)
        qt_add_executable(SocietyAppleModelSourceTests tests/tst_applemodelsource.mm
            src/App/Files/AppleModelSource.mm src/App/Files/AppleModelSource.h
            src/App/Files/ModelImporter.cpp src/App/Files/ModelImporter.h)
        target_include_directories(SocietyAppleModelSourceTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_link_libraries(SocietyAppleModelSourceTests PRIVATE Qt6::Test Qt6::Qml Qt6::Gui
            iiSocietyContainer::iiSocietyContainer "-framework Foundation")
        target_compile_options(SocietyAppleModelSourceTests PRIVATE "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
        target_compile_definitions(SocietyAppleModelSourceTests PRIVATE SOCIETY_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
        add_test(NAME Society.AppleModelSource COMMAND SocietyAppleModelSourceTests)
        set_tests_properties(Society.AppleModelSource PROPERTIES TIMEOUT 60)

        # Command Line Tools can type-check UIKit via Mac Catalyst without an iOS SDK.
        # This is a compile check of the drop delegate, not an iOS link or device test.
        execute_process(COMMAND xcrun --sdk macosx --show-sdk-path
            OUTPUT_VARIABLE society_macos_sdk OUTPUT_STRIP_TRAILING_WHITESPACE)
        set(society_uikit_frameworks "${society_macos_sdk}/System/iOSSupport/System/Library/Frameworks")
        if(EXISTS "${society_uikit_frameworks}/UIKit.framework/Headers")
            set(society_drop_include_args)
            foreach(society_qt_module Core Gui Qml QmlIntegration Network)
                get_target_property(society_qt_includes Qt6::${society_qt_module} INTERFACE_INCLUDE_DIRECTORIES)
                foreach(society_qt_include IN LISTS society_qt_includes)
                    list(APPEND society_drop_include_args "-I${society_qt_include}")
                endforeach()
            endforeach()
            get_target_property(society_container_includes iiSocietyContainer::iiSocietyContainer INTERFACE_INCLUDE_DIRECTORIES)
            foreach(society_container_include IN LISTS society_container_includes)
                list(APPEND society_drop_include_args "-I${society_container_include}")
            endforeach()
            get_filename_component(society_native_package_root "${iiSocietyContainer_NATIVE_APP}" DIRECTORY)
            list(APPEND society_drop_include_args "-I${society_native_package_root}/platform/ios")
            get_filename_component(society_qt_frameworks "${Qt6_DIR}/../.." ABSOLUTE)
            add_test(NAME Society.IosDropSyntax COMMAND "${CMAKE_OBJCXX_COMPILER}"
                -fsyntax-only -x objective-c++ -std=c++20 -fobjc-arc
                -target "${CMAKE_SYSTEM_PROCESSOR}-apple-ios16.0-macabi"
                -isysroot "${society_macos_sdk}"
                -iframework "${society_uikit_frameworks}"
                -isystem "${society_macos_sdk}/System/iOSSupport/usr/include"
                -iframework "${society_qt_frameworks}" ${society_drop_include_args}
                "${CMAKE_CURRENT_SOURCE_DIR}/src/App/Files/IosModelDrop.mm"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/App/Files/IosFileAccess.mm"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/App/Files/ModelImporter.cpp"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/App/Drive/DriveController.cpp")
            set_tests_properties(Society.IosDropSyntax PROPERTIES TIMEOUT 60)
        endif()
    endif()

    foreach(account_consumer SocietyAccountTests SocietyNetworkTests SocietyClientOnlyNetworkTests SocietyDriveTests)
        target_sources(${account_consumer} PRIVATE ${society_account_sources})
        target_link_libraries(${account_consumer} PRIVATE iiAcountManager::Quick)
        target_compile_definitions(${account_consumer} PRIVATE SOCIETY_DISABLE_SESSION_RESTORE SOCIETY_APP_VERSION="${PROJECT_VERSION}")
    endforeach()
    foreach(discovery_consumer SocietyPairingTests SocietyAccountTests SocietyNetworkTests SocietyClientOnlyNetworkTests SocietyDriveTests)
        target_link_libraries(${discovery_consumer} PRIVATE SocietyPhotos SocietyDiscovery SocietyGroupState iiSocietySync::iiSocietySync)
    endforeach()

    if("iiFileProvider" IN_LIST society_sdk_packages)
    qt_add_executable(SocietyDependencyTests tests/tst_dependencies.cpp
            src/App/AI/HuggingFaceIntergration.cpp
            src/App/AI/HuggingFaceIntergration.h
            src/App/AI/CivitaiIntergration.cpp
            src/App/AI/CivitaiIntergration.h
            src/App/AI/OllamaIntergration.cpp
            src/App/AI/OllamaIntergration.h
            src/App/Environment/User/AccountInfo.cpp
            src/App/Environment/User/AccountInfo.h
            src/App/Environment/Device/CurrentDevice.cpp
            src/App/Environment/Device/CurrentDevice.h
            src/App/Environment/Device/DevicesList.cpp
            src/App/Environment/Device/DevicesList.h)
    # Compile the consumer against the app's actual direct link dependencies.
    get_target_property(society_app_link_libraries Society LINK_LIBRARIES)
    target_link_libraries(SocietyDependencyTests PRIVATE Qt6::Test ${society_app_link_libraries})
    target_compile_definitions(SocietyDependencyTests PRIVATE
        SOCIETY_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}"
        SOCIETY_EXECUTABLE_PATH="$<TARGET_FILE:Society>")
    add_dependencies(SocietyDependencyTests Society)
    set_target_properties(SocietyDependencyTests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin"
        QT_QML_MODULE_NO_IMPORT_SCAN TRUE
    )
    add_test(NAME Society.Dependencies COMMAND SocietyDependencyTests)
    set_tests_properties(Society.Dependencies PROPERTIES
        ENVIRONMENT_MODIFICATION "DYLD_LIBRARY_PATH=unset:;DYLD_FALLBACK_LIBRARY_PATH=unset:"
        TIMEOUT 30
    )
    endif()
    # Native tests load QML dynamically; scanning the entire source/build tree
    # once per test executable adds minutes without providing static plugins.
    get_target_property(society_qt_core_type Qt6::Core TYPE)
    if(society_qt_core_type STREQUAL "SHARED_LIBRARY")
        get_property(society_directory_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
        foreach(society_test_target IN LISTS society_directory_targets)
            if(society_test_target MATCHES "^Society.*Tests$")
                set_property(TARGET ${society_test_target} PROPERTY QT_QML_MODULE_NO_IMPORT_SCAN TRUE)
            endif()
        endforeach()
    endif()
endif()

# Running desktop apps publish authenticated same-user MCP endpoints.
if(UNIX AND NOT IOS AND NOT ANDROID AND NOT EMSCRIPTEN)
    find_package(iiLocalLLM 0.36.0 CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/iiLocalLLM")
    target_sources(Society PRIVATE src/App/Mcp/SocietyMcp.cpp src/App/Mcp/SocietyMcp.h)
    target_link_libraries(Society PRIVATE iiLocalLLM::iiLocalLLM)
    set_source_files_properties("${IILOCALLLM_QML_DIR}/UserQuestionsSheet.qml" PROPERTIES QT_RESOURCE_ALIAS "UserQuestionsSheet.qml")
    qt_add_resources(Society AgentQuestionUi PREFIX "/iiLocalLLM"
        FILES "${IILOCALLLM_QML_DIR}/UserQuestionsSheet.qml")
    target_compile_definitions(Society PRIVATE SOCIETY_WITH_LOCAL_MCP=1)
    if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
        # Existing UI tests must not advertise fixtures to the user's live agent.
        get_property(_existing_app_tests DIRECTORY PROPERTY TESTS)
        set_property(TEST ${_existing_app_tests} APPEND PROPERTY ENVIRONMENT "IILOCALLLM_DISABLE_APP_MCP=1")
        qt_add_executable(SocietyMcpTests tests/tst_mcp.cpp)
        target_link_libraries(SocietyMcpTests PRIVATE Qt6::Test iiLocalLLM::iiLocalLLM iiSocietyContainer::iiSocietyContainer)
        target_compile_definitions(SocietyMcpTests PRIVATE
            MCP_APP_EXECUTABLE="$<TARGET_FILE:Society>"
            MCP_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
        add_dependencies(SocietyMcpTests Society)
        set(SOCIETY_MCP_TEST_GGUF "" CACHE FILEPATH "Optional pinned Qwen2.5 0.5B Q4_K_M for real app-control inference")
        if(SOCIETY_MCP_TEST_GGUF)
            target_compile_definitions(SocietyMcpTests PRIVATE SOCIETY_MCP_TEST_GGUF="${SOCIETY_MCP_TEST_GGUF}")
        endif()
        add_test(NAME Society.Mcp COMMAND SocietyMcpTests)
        set_tests_properties(Society.Mcp PROPERTIES TIMEOUT 90)
    endif()
endif()

# All general tests may construct a NetworkDriveController or launch Society.
# Only the dedicated photo fixture is allowed to activate its injected library.
if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    get_property(society_registered_tests DIRECTORY PROPERTY TESTS)
    # Test the linked SDK versions, independent of an interactive shell's
    # installed-library overrides (which can load an older LVRS QML module).
    set_property(TEST ${society_registered_tests} APPEND PROPERTY ENVIRONMENT_MODIFICATION
        "DYLD_LIBRARY_PATH=unset:" "DYLD_FRAMEWORK_PATH=unset:" "DYLD_FALLBACK_LIBRARY_PATH=unset:")
    set_property(TEST ${society_registered_tests} APPEND PROPERTY ENVIRONMENT "SOCIETY_DISABLE_SESSION_RESTORE=1")
    set_property(TEST ${society_registered_tests} APPEND PROPERTY ENVIRONMENT "IISOCIETYSYNC_DISABLE_BLE=1")
    list(REMOVE_ITEM society_registered_tests Society.Photos)
    set_property(TEST ${society_registered_tests} APPEND PROPERTY ENVIRONMENT "SOCIETY_DISABLE_PHOTOS=1")
endif()

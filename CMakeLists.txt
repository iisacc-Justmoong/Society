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

set(CMAKE_CXX_STANDARD 20)
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
    set(society_sdk_packages iiAcountManager iiServerHost iiSocietyContainer iiSocietyHelper)
endif()
set(society_sdk_targets)
foreach(society_sdk IN LISTS society_sdk_packages)
    if(CMAKE_CROSSCOMPILING)
        if(NOT DEFINED ${society_sdk}_DIR OR NOT EXISTS "${${society_sdk}_DIR}/${society_sdk}Config.cmake")
            message(FATAL_ERROR "Install ${society_sdk} for the target SDK and set ${society_sdk}_DIR; host packages cannot substitute it")
        endif()
    endif()
    if(society_sdk STREQUAL "iiAcountManager")
        find_package(iiAcountManager 0.2.4 CONFIG REQUIRED COMPONENTS Quick HINTS "$ENV{HOME}/.local/SDK/iiAcountManager")
    elseif(society_sdk STREQUAL "iiSocietyContainer")
        find_package(iiSocietyContainer 0.9.1 CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/iiSocietyContainer")
    elseif(society_sdk STREQUAL "iiServerHost")
        find_package(iiServerHost 0.4.1 CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/iiServerHost")
    elseif(society_sdk STREQUAL "iiSocietyHelper")
        find_package(iiSocietyHelper 0.7.0 CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/iiSocietyHelper")
    else()
        find_package(${society_sdk} CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/${society_sdk}")
    endif()
    list(APPEND society_sdk_targets "${society_sdk}::${society_sdk}")
endforeach()

add_library(SocietyServices STATIC Daemon/DaemonService.cpp Daemon/DaemonService.h
    App/Services/SocietyInbox.cpp App/Services/SocietyInbox.h
    App/Services/SocietyRuntime.cpp App/Services/SocietyRuntime.h)
target_link_libraries(SocietyServices PUBLIC iiSocietyHelper::iiSocietyHelper)
target_include_directories(SocietyServices PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(SocietyServices PUBLIC SOCIETY_APP_VERSION="${PROJECT_VERSION}")
if(NOT IOS AND NOT ANDROID AND NOT EMSCRIPTEN)
    qt_add_executable(SocietyDaemon Daemon/main.cpp)
    target_link_libraries(SocietyDaemon PRIVATE SocietyServices)
    set_target_properties(SocietyDaemon PROPERTIES MACOSX_BUNDLE FALSE QT_QML_MODULE_NO_IMPORT_SCAN TRUE)
endif()

set(society_account_sources App/Account/AccountController.cpp App/Account/AccountController.h
    App/Account/DeviceInfo.cpp App/Account/DeviceInfo.h)
add_library(SocietyDiscovery STATIC App/Network/DiscoveryService.h App/Network/NearbyDevices.h App/Network/NearbyDevices.cpp)
set_target_properties(SocietyDiscovery PROPERTIES AUTOMOC ON POSITION_INDEPENDENT_CODE ON)
target_compile_features(SocietyDiscovery PUBLIC cxx_std_20)
target_include_directories(SocietyDiscovery PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(SocietyDiscovery PUBLIC Qt6::Network iiServerHost::iiServerHost)
if(ANDROID)
    target_sources(SocietyDiscovery PRIVATE App/Network/AndroidDiscoveryService.cpp)
else()
    find_path(SOCIETY_DNSSD_INCLUDE dns_sd.h PATH_SUFFIXES avahi-compat-libdns_sd REQUIRED)
    target_sources(SocietyDiscovery PRIVATE App/Network/DnsSdDiscoveryService.cpp)
    target_include_directories(SocietyDiscovery PRIVATE "${SOCIETY_DNSSD_INCLUDE}")
    if(NOT APPLE)
        find_library(SOCIETY_DNSSD_LIBRARY NAMES dns_sd dnssd REQUIRED)
        target_link_libraries(SocietyDiscovery PRIVATE "${SOCIETY_DNSSD_LIBRARY}")
    endif()
endif()
set(society_pairing_sources App/Network/DevicePairing.cpp App/Network/DevicePairing.h
    App/Network/PairingQr.cpp App/Network/PairingQr.h App/Network/QrScanner.cpp App/Network/QrScanner.h
    ThirdParty/qrcodegen/qrcodegen.cpp ThirdParty/qrcodegen/qrcodegen.hpp)
qt_add_executable(Society
    main.cpp ${society_account_sources} ${society_pairing_sources} App/Files/DirectoryLocation.cpp App/Files/DirectoryLocation.h
        App/Dashboard/DashboardFiles.cpp App/Dashboard/DashboardFiles.h
        App/Files/ModelImporter.cpp App/Files/ModelImporter.h
        App/Files/ModelImportSource.h
        App/Drive/DriveController.cpp App/Drive/DriveController.h
        App/Network/NetworkDriveController.cpp App/Network/NetworkDriveController.h
)
# Painted C++ items require the QtQuick type metadata as a QML dependency.
qt_add_qml_module(Society
    URI Society VERSION 1.0 RESOURCE_PREFIX "/qt/qml"
    IMPORT_PATH "${iiAcountManager_QML_IMPORT_PATH}"
    DEPENDENCIES QtQuick iiAccountManager
    QML_FILES App/Main.qml App/FileGridView.qml App/Network/NetworkDevices.qml App/Network/PairingPanel.qml
        App/SocietyView.qml App/StorageView.qml App/Dashboard/Dashboard.qml App/Dashboard/QuickGenerate.qml
        App/Preferences/PreferencesWindow.qml
)
target_link_libraries(Society PRIVATE Qt6::Quick Qt6::QuickControls2 iiAcountManager::Quick)
lvrs_configure_qml_app(Society NO_PLATFORM_RUNTIME_TARGETS)
target_link_libraries(Society PRIVATE ${society_sdk_targets} SocietyServices SocietyDiscovery Qt6::Concurrent)
qt_add_resources(Society qrcodegen_license PREFIX "/licenses/qrcodegen"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/qrcodegen" FILES ThirdParty/qrcodegen/LICENSE)
if(NOT IOS AND NOT ANDROID)
    get_filename_component(society_serverhost_prefix "${iiServerHost_DIR}/../../.." ABSOLUTE)
    qt_add_resources(Society lan_openssl_license PREFIX "/licenses/openssl"
        BASE "${society_serverhost_prefix}/share/iiServerHost/licenses"
        FILES "${society_serverhost_prefix}/share/iiServerHost/licenses/OpenSSL-LICENSE.txt")
endif()
option(SOCIETY_ACCOUNT_RUNTIME_PROBE "Enable credential-free TLS and account panel runtime verification" OFF)
if(SOCIETY_ACCOUNT_RUNTIME_PROBE)
    target_sources(Society PRIVATE tests/AccountRuntimeProbe.cpp)
    target_compile_definitions(Society PRIVATE SOCIETY_ACCOUNT_RUNTIME_PROBE)
endif()
target_include_directories(Society PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/App/Account")
if(IOS OR ANDROID)
    target_compile_definitions(Society PRIVATE SOCIETY_CLIENT_ONLY=1)
endif()
if(APPLE AND NOT IOS)
    target_sources(Society PRIVATE Daemon/platform/macos/ServiceRegistration.mm
        Daemon/platform/macos/ServiceRegistration.h
        Daemon/platform/macos/com.iisacc.society.daemon.plist)
    set_source_files_properties(Daemon/platform/macos/ServiceRegistration.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    set_source_files_properties(Daemon/platform/macos/com.iisacc.society.daemon.plist PROPERTIES
        MACOSX_PACKAGE_LOCATION "Library/LaunchAgents")
    target_link_libraries(Society PRIVATE "-framework ServiceManagement" "-framework Foundation")
    add_dependencies(Society SocietyDaemon)
    configure_file(Daemon/platform/macos/Info.plist.in "${CMAKE_CURRENT_BINARY_DIR}/SocietyDaemon-Info.plist" @ONLY)
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
    add_custom_target(SocietyDaemonPackage
        COMMAND "${CMAKE_COMMAND}"
            "-DSOCIETY_SOURCE_APP=$<TARGET_BUNDLE_DIR:Society>"
            "-DSOCIETY_PACKAGE_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/package"
            "-DSOCIETY_MACDEPLOYQT=${society_macdeployqt}"
            "-DSOCIETY_SIGN_IDENTITY=${SOCIETY_MAC_SIGN_IDENTITY}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/PackageMac.cmake"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_daemon_package.py"
            "${CMAKE_CURRENT_BINARY_DIR}/package/Society.app" "$<TARGET_BUNDLE_DIR:Society>"
        DEPENDS Society SocietyDaemon USES_TERMINAL VERBATIM)
endif()
target_include_directories(Society PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/App/Files" "${CMAKE_CURRENT_SOURCE_DIR}/App/Drive"
    "${CMAKE_CURRENT_SOURCE_DIR}/App/Network" "${CMAKE_CURRENT_SOURCE_DIR}/App/Dashboard")
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
    target_sources(Society PRIVATE App/Files/AppleModelSource.mm App/Files/AppleModelSource.h
        App/Account/IosDeviceInfo.mm App/Network/IosQrScanner.mm
        App/Files/IosModelDrop.mm App/Files/IosModelDrop.h
        App/Files/IosFileAccess.mm App/Files/IosFileAccess.h)
    set_source_files_properties(App/Account/IosDeviceInfo.mm App/Network/IosQrScanner.mm App/Files/AppleModelSource.mm App/Files/IosModelDrop.mm App/Files/IosFileAccess.mm
        PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(Society PRIVATE "-framework UIKit" "-framework Foundation" "-framework AVFoundation"
        "-framework QuickLook" "-framework UniformTypeIdentifiers")
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

include(CTest)
if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    qt_add_executable(SocietyDiscoveryTests tests/tst_discovery.cpp)
    target_link_libraries(SocietyDiscoveryTests PRIVATE SocietyDiscovery Qt6::Test)
    add_test(NAME Society.Discovery COMMAND SocietyDiscoveryTests)
    set_tests_properties(Society.Discovery PROPERTIES TIMEOUT 45)
    find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Test)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME Society.IosBuildContract COMMAND "${Python3_EXECUTABLE}" -B
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ios_build.py")
    add_test(NAME Society.AndroidTlsBuild COMMAND "${Python3_EXECUTABLE}" -B
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_android_openssl.py")
    qt_add_executable(SocietyPairingTests tests/tst_pairing.cpp ${society_pairing_sources} ${society_account_sources}
        App/Network/NetworkDriveController.cpp App/Network/NetworkDriveController.h)
    target_include_directories(SocietyPairingTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(SocietyPairingTests PRIVATE iiAcountManager::Quick Qt6::Test Qt6::Quick Qt6::QuickControls2
        iiServerHost::iiServerHost iiAcountManager::iiAcountManager iiSocietyContainer::iiSocietyContainer)
    lvrs_configure_qml_app(SocietyPairingTests NO_PLATFORM_RUNTIME_TARGETS)
    target_compile_definitions(SocietyPairingTests PRIVATE SOCIETY_DISABLE_SESSION_RESTORE SOCIETY_APP_VERSION="${PROJECT_VERSION}"
        SOCIETY_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}"
        SOCIETY_PAIRING_QML_FILE="${CMAKE_CURRENT_SOURCE_DIR}/App/Network/PairingPanel.qml"
        SOCIETY_LVRS_QML_IMPORT_PATH="${LVRS_QML_IMPORT_PATH}")
    if(APPLE)
        target_sources(SocietyPairingTests PRIVATE tests/DecodeQr.mm)
        set_source_files_properties(tests/DecodeQr.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
        target_link_libraries(SocietyPairingTests PRIVATE "-framework CoreImage" "-framework Foundation")
    endif()
    add_test(NAME Society.Pairing COMMAND SocietyPairingTests)
    set_tests_properties(Society.Pairing PROPERTIES TIMEOUT 60
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1")
    qt_add_executable(SocietyAccountTests tests/tst_account.cpp
        App/Network/NetworkDriveController.cpp App/Network/NetworkDriveController.h)
    target_include_directories(SocietyAccountTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
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
    set_tests_properties(Society.Account PROPERTIES TIMEOUT 60
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1")
    qt_add_executable(SocietyRuntimeTests tests/tst_runtime.cpp)
    target_link_libraries(SocietyRuntimeTests PRIVATE Qt6::Test SocietyServices)
    target_compile_definitions(SocietyRuntimeTests PRIVATE SOCIETY_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME Society.MobileLifecycle COMMAND SocietyRuntimeTests)
    qt_add_executable(SocietyNetworkTests tests/tst_networkdrive.cpp
        App/Network/NetworkDriveController.cpp App/Network/NetworkDriveController.h)
    target_include_directories(SocietyNetworkTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(SocietyNetworkTests PRIVATE Qt6::Test Qt6::Qml Qt6::Gui
        iiAcountManager::iiAcountManager iiServerHost::iiServerHost iiSocietyContainer::iiSocietyContainer)
    target_compile_definitions(SocietyNetworkTests PRIVATE SOCIETY_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME Society.NetworkDrive COMMAND SocietyNetworkTests)
    set_tests_properties(Society.NetworkDrive PROPERTIES TIMEOUT 60)
    qt_add_executable(SocietyClientOnlyNetworkTests tests/tst_networkdrive_clientonly.cpp
        ${society_pairing_sources}
        App/Network/NetworkDriveController.cpp App/Network/NetworkDriveController.h)
    target_include_directories(SocietyClientOnlyNetworkTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(SocietyClientOnlyNetworkTests PRIVATE Qt6::Test Qt6::Quick Qt6::QuickControls2
        iiAcountManager::iiAcountManager iiServerHost::iiServerHost iiSocietyContainer::iiSocietyContainer)
    lvrs_configure_qml_app(SocietyClientOnlyNetworkTests NO_PLATFORM_RUNTIME_TARGETS)
    target_compile_definitions(SocietyClientOnlyNetworkTests PRIVATE SOCIETY_CLIENT_ONLY=1
        SOCIETY_NETWORK_QML_FILE="${CMAKE_CURRENT_SOURCE_DIR}/App/Network/NetworkDevices.qml"
        SOCIETY_PAIRING_QML_FILE="${CMAKE_CURRENT_SOURCE_DIR}/App/Network/PairingPanel.qml"
        SOCIETY_PREFERENCES_QML_FILE="${CMAKE_CURRENT_SOURCE_DIR}/App/Preferences/PreferencesWindow.qml"
        SOCIETY_LVRS_QML_IMPORT_PATH="${LVRS_QML_IMPORT_PATH}"
        SOCIETY_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME Society.ClientOnlyNetwork COMMAND SocietyClientOnlyNetworkTests)
    set_tests_properties(Society.ClientOnlyNetwork PROPERTIES TIMEOUT 60
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1")
    set_tests_properties(Society.MobileLifecycle PROPERTIES TIMEOUT 30)
    qt_add_executable(SocietyDaemonTests tests/tst_daemon.cpp)
    target_include_directories(SocietyDaemonTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(SocietyDaemonTests PRIVATE Qt6::Test SocietyServices)
    target_compile_definitions(SocietyDaemonTests PRIVATE SOCIETY_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}"
        SOCIETY_DAEMON_EXECUTABLE="$<TARGET_FILE:SocietyDaemon>"
        SOCIETY_EXECUTABLE_PATH="$<TARGET_FILE:Society>")
    add_dependencies(SocietyDaemonTests SocietyDaemon Society)
    add_test(NAME Society.Daemon COMMAND SocietyDaemonTests)
    set_tests_properties(Society.Daemon PROPERTIES TIMEOUT 45)
    qt_add_executable(SocietyGuiTests tests/tst_filegrid.cpp
        App/Files/DirectoryLocation.cpp App/Files/DirectoryLocation.h)
    target_include_directories(SocietyGuiTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(SocietyGuiTests PRIVATE Qt6::Test Qt6::Quick Qt6::QuickControls2)
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

    qt_add_executable(SocietyDriveTests tests/tst_drive.cpp
        ${society_pairing_sources}
        App/Dashboard/DashboardFiles.cpp App/Dashboard/DashboardFiles.h
        App/Network/NetworkDriveController.cpp App/Network/NetworkDriveController.h
        App/Files/ModelImporter.cpp App/Files/ModelImporter.h
        App/Drive/DriveController.cpp App/Drive/DriveController.h
        App/Files/DirectoryLocation.cpp App/Files/DirectoryLocation.h)
    target_include_directories(SocietyDriveTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(SocietyDriveTests PRIVATE Qt6::Test Qt6::Quick Qt6::QuickControls2
        Qt6::Concurrent
        iiServerHost::iiServerHost iiAcountManager::iiAcountManager iiSocietyContainer::iiSocietyContainer)
    lvrs_configure_qml_app(SocietyDriveTests NO_PLATFORM_RUNTIME_TARGETS)
    target_compile_definitions(SocietyDriveTests PRIVATE
        SOCIETY_QML_FILE="${CMAKE_CURRENT_SOURCE_DIR}/App/Main.qml"
        SOCIETY_LVRS_QML_IMPORT_PATH="${LVRS_QML_IMPORT_PATH}"
        SOCIETY_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME Society.Drive COMMAND SocietyDriveTests)
    set_tests_properties(Society.Drive PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1"
        TIMEOUT 60)

    qt_add_executable(SocietyModelImportTests tests/tst_modelimporter.cpp
        App/Files/ModelImporter.cpp App/Files/ModelImporter.h)
    target_include_directories(SocietyModelImportTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(SocietyModelImportTests PRIVATE Qt6::Test Qt6::Qml Qt6::Gui iiSocietyContainer::iiSocietyContainer)
    target_compile_definitions(SocietyModelImportTests PRIVATE SOCIETY_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME Society.ModelImport COMMAND SocietyModelImportTests)
    set_tests_properties(Society.ModelImport PROPERTIES TIMEOUT 60)

    if(APPLE)
        qt_add_executable(SocietyAppleModelSourceTests tests/tst_applemodelsource.mm
            App/Files/AppleModelSource.mm App/Files/AppleModelSource.h
            App/Files/ModelImporter.cpp App/Files/ModelImporter.h)
        target_include_directories(SocietyAppleModelSourceTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
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
                "${CMAKE_CURRENT_SOURCE_DIR}/App/Files/IosModelDrop.mm"
                "${CMAKE_CURRENT_SOURCE_DIR}/App/Files/IosFileAccess.mm"
                "${CMAKE_CURRENT_SOURCE_DIR}/App/Files/ModelImporter.cpp"
                "${CMAKE_CURRENT_SOURCE_DIR}/App/Drive/DriveController.cpp")
            set_tests_properties(Society.IosDropSyntax PROPERTIES TIMEOUT 60)
        endif()
    endif()

    foreach(account_consumer SocietyAccountTests SocietyNetworkTests SocietyClientOnlyNetworkTests SocietyDriveTests)
        target_sources(${account_consumer} PRIVATE ${society_account_sources})
        target_link_libraries(${account_consumer} PRIVATE iiAcountManager::Quick)
        target_compile_definitions(${account_consumer} PRIVATE SOCIETY_DISABLE_SESSION_RESTORE SOCIETY_APP_VERSION="${PROJECT_VERSION}")
    endforeach()
    foreach(discovery_consumer SocietyPairingTests SocietyAccountTests SocietyNetworkTests SocietyClientOnlyNetworkTests SocietyDriveTests)
        target_link_libraries(${discovery_consumer} PRIVATE SocietyDiscovery)
    endforeach()

    if("iiFileProvider" IN_LIST society_sdk_packages)
    qt_add_executable(SocietyDependencyTests tests/tst_dependencies.cpp
            App/AI/HuggingFaceIntergration.cpp
            App/AI/HuggingFaceIntergration.h
            App/AI/CivitaiIntergration.cpp
            App/AI/CivitaiIntergration.h
            App/AI/OllamaIntergration.cpp
            App/AI/OllamaIntergration.h
            App/Environment/User/AccountInfo.cpp
            App/Environment/User/AccountInfo.h
            App/Environment/Device/CurrentDevice.cpp
            App/Environment/Device/CurrentDevice.h
            App/Environment/Device/DevicesList.cpp
            App/Environment/Device/DevicesList.h)
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
endif()

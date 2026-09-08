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
find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Quick QuickControls2)
find_package(LVRS CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/LVRS")
qt_standard_project_setup(REQUIRES 6.8)

set(society_sdk_packages
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
    set(society_sdk_packages iiSocietyContainer iiSocietyHelper)
endif()
set(society_sdk_targets)
foreach(society_sdk IN LISTS society_sdk_packages)
    if(CMAKE_CROSSCOMPILING)
        if(NOT DEFINED ${society_sdk}_DIR OR NOT EXISTS "${${society_sdk}_DIR}/${society_sdk}Config.cmake")
            message(FATAL_ERROR "Install ${society_sdk} for the target SDK and set ${society_sdk}_DIR; host packages cannot substitute it")
        endif()
    endif()
    if(society_sdk STREQUAL "iiSocietyContainer")
        find_package(iiSocietyContainer 0.9.0 CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/iiSocietyContainer")
    elseif(society_sdk STREQUAL "iiSocietyHelper")
        find_package(iiSocietyHelper 0.5.0 CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/iiSocietyHelper")
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

lvrs_add_qml_app(
    TARGET Society
    URI Society
    SOURCES main.cpp App/Files/DirectoryLocation.cpp App/Files/DirectoryLocation.h
        App/Files/ModelImporter.cpp App/Files/ModelImporter.h
        App/Files/ModelImportSource.h
        App/Drive/DriveController.cpp App/Drive/DriveController.h
    QML_FILES App/Main.qml App/FileGridView.qml
    NO_PLATFORM_RUNTIME_TARGETS
)
target_link_libraries(Society PRIVATE ${society_sdk_targets} SocietyServices)
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
target_include_directories(Society PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/App/Files" "${CMAKE_CURRENT_SOURCE_DIR}/App/Drive")
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
        App/Files/IosModelDrop.mm App/Files/IosModelDrop.h
        App/Files/IosFileAccess.mm App/Files/IosFileAccess.h)
    set_source_files_properties(App/Files/AppleModelSource.mm App/Files/IosModelDrop.mm App/Files/IosFileAccess.mm
        PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(Society PRIVATE "-framework UIKit" "-framework Foundation"
        "-framework QuickLook" "-framework UniformTypeIdentifiers")
    qt_import_plugins(Society INCLUDE Qt6::QSQLiteDriverPlugin)
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

include(CTest)
if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Test)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME Society.IosBuildContract COMMAND "${Python3_EXECUTABLE}" -B
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ios_build.py")
    qt_add_executable(SocietyRuntimeTests tests/tst_runtime.cpp)
    target_link_libraries(SocietyRuntimeTests PRIVATE Qt6::Test SocietyServices)
    target_compile_definitions(SocietyRuntimeTests PRIVATE SOCIETY_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME Society.MobileLifecycle COMMAND SocietyRuntimeTests)
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
        App/Files/ModelImporter.cpp App/Files/ModelImporter.h
        App/Drive/DriveController.cpp App/Drive/DriveController.h
        App/Files/DirectoryLocation.cpp App/Files/DirectoryLocation.h)
    target_include_directories(SocietyDriveTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(SocietyDriveTests PRIVATE Qt6::Test Qt6::Quick Qt6::QuickControls2 iiSocietyContainer::iiSocietyContainer)
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

cmake_minimum_required(VERSION 3.31)
project(Society VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Quick QuickControls2)
find_package(LVRS CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/LVRS")
qt_standard_project_setup(REQUIRES 6.8)

set(society_sdk_packages
    iiFilePreview
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
set(society_sdk_targets)
foreach(society_sdk IN LISTS society_sdk_packages)
    find_package(${society_sdk} CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/${society_sdk}")
    list(APPEND society_sdk_targets "${society_sdk}::${society_sdk}")
endforeach()

lvrs_add_qml_app(
    TARGET Society
    URI Society
    SOURCES main.cpp
    QML_FILES App/Main.qml
    NO_PLATFORM_RUNTIME_TARGETS
)
target_link_libraries(Society PRIVATE ${society_sdk_targets})
target_compile_definitions(Society PRIVATE
    SOCIETY_LVRS_QML_IMPORT_PATH="${LVRS_QML_IMPORT_PATH}"
)
set_target_properties(Society PROPERTIES
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.iisacc.society"
    MACOSX_BUNDLE_BUNDLE_NAME "Society"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)

include(CTest)
if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Test)
    qt_add_executable(SocietyGuiTests tests/tst_hello.cpp)
    target_link_libraries(SocietyGuiTests PRIVATE Qt6::Test Qt6::Quick Qt6::QuickControls2)
    lvrs_configure_qml_app(SocietyGuiTests NO_PLATFORM_RUNTIME_TARGETS)
    target_compile_definitions(SocietyGuiTests PRIVATE
        SOCIETY_QML_FILE="${CMAKE_CURRENT_SOURCE_DIR}/App/Main.qml"
        SOCIETY_LVRS_QML_IMPORT_PATH="${LVRS_QML_IMPORT_PATH}"
    )
    add_test(NAME Society.Gui COMMAND SocietyGuiTests)
    set_tests_properties(Society.Gui PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1"
        TIMEOUT 30
    )

    qt_add_executable(SocietyDependencyTests tests/tst_dependencies.cpp
            App/AI/HuggingFaceIntergration.cpp
            App/AI/HuggingFaceIntergration.h
            App/AI/CivitaiIntergration.cpp
            App/AI/CivitaiIntergration.h
            App/AI/OllamaIntergration.cpp
            App/AI/OllamaIntergration.h)
    # Compile the consumer against the app's actual direct link dependencies.
    get_target_property(society_app_link_libraries Society LINK_LIBRARIES)
    target_link_libraries(SocietyDependencyTests PRIVATE Qt6::Test ${society_app_link_libraries})
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

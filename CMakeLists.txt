cmake_minimum_required(VERSION 3.31)
include("${CMAKE_CURRENT_LIST_DIR}/cmake/single-app/Defaults.cmake")
project(Society VERSION 0.1.0 LANGUAGES CXX)
include(cmake/MacRuntimePackages.cmake)
society_use_local_macos_runtime_packages("${CMAKE_CURRENT_SOURCE_DIR}/resources/dependencies/macos-runtime")
option(SOCIETY_VERIFY_RUNTIME_ON_BUILD "Launch Society to verify the macOS bundle after building" OFF)
set(SOCIETY_MLX_RUNTIME_DIR "" CACHE PATH "Optional matching MLX library and Metal resource directory")
# Projects use the package-aware Catalog and the independent project SDK.
if(CMAKE_CROSSCOMPILING)
    foreach(project_sdk IN ITEMS iiFileProviderCatalog iiProjectManager iiProjectManagerFileProvider)
        if(NOT DEFINED ${project_sdk}_DIR OR NOT EXISTS "${${project_sdk}_DIR}/${project_sdk}Config.cmake")
            message(FATAL_ERROR "Install ${project_sdk} for the target SDK and set ${project_sdk}_DIR; host packages cannot substitute it")
        endif()
    endforeach()
endif()
find_package(iiFileProviderCatalog 0.8 CONFIG REQUIRED HINTS
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/dependencies/catalog-compat"
    "$ENV{HOME}/.local/SDK/iiFileProvider")
get_target_property(society_catalog_include iiFileProvider::Catalog INTERFACE_INCLUDE_DIRECTORIES)
# The legacy Qt facade includes its sibling FileCatalog.h with quotes. Exposing
# the newer Catalog include directory transitively would mix two definitions in
# the same translation unit. Project targets explicitly opt into the new path.
set_property(TARGET iiFileProvider::Catalog PROPERTY INTERFACE_INCLUDE_DIRECTORIES "")
find_package(iiProjectManager 0.1 CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/iiProjectManager"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../SDK/iiProjectManager/resources/dependencies/society-compat/stage")
find_package(iiProjectManagerFileProvider 0.1 CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/iiProjectManagerFileProvider"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../SDK/iiProjectManager/resources/dependencies/society-compat/stage-bridge")
# File creation is owned by iiFileProvider.
find_package(iiFileProvider 0.7.1 CONFIG REQUIRED HINTS "$ENV{IIFILEPROVIDER_PREFIX}" "$ENV{HOME}/.local/SDK/iiFileProvider")
find_package(iiResource 0.1 CONFIG QUIET HINTS "$ENV{HOME}/.local/SDK/iiResource")
if(NOT TARGET iiResource::iiResource)
    set(_society_resource_source "${CMAKE_CURRENT_SOURCE_DIR}/../../SDK/iiResource")
    if(NOT EXISTS "${_society_resource_source}/CMakeLists.txt")
        message(FATAL_ERROR "Install iiResource or provide its source in the Workspace SDK directory")
    endif()
    add_subdirectory("${_society_resource_source}" "${CMAKE_CURRENT_BINARY_DIR}/iiResource" EXCLUDE_FROM_ALL)
endif()
find_package(iiJob 0.1 CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/iiJob")
get_target_property(society_provider_include iiFileProvider::iiFileProvider INTERFACE_INCLUDE_DIRECTORIES)





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
include(cmake/SdkHeaders.cmake)

if(CMAKE_CROSSCOMPILING)
    if(NOT DEFINED LVRS_DIR OR NOT EXISTS "${LVRS_DIR}/LVRSConfig.cmake")
        message(FATAL_ERROR "Set LVRS_DIR to the installed LVRS package for the target platform")
    endif()
endif()
find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Quick QuickControls2 Concurrent)
include(cmake/QtApple.cmake)
find_package(LVRS CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/LVRS")
set(SOCIETY_NAV_LVUI_DIR "$ENV{HOME}/.local/SDK/LVUI/lib/cmake/LVUI"
    CACHE PATH "LVUI package used by Society navigation")
set(SOCIETY_NAV_IIGUI_PREFIX "$ENV{HOME}/.local/SDK/iiGUI"
    CACHE PATH "iiGUI package used by Society navigation")
set(SOCIETY_NAV_VULKAN_PREFIX "$ENV{HOME}/.local/SDK/Vulkan"
    CACHE PATH "Vulkan SDK used by Society navigation")
set(_society_prefix_before_native "${CMAKE_PREFIX_PATH}")
list(PREPEND CMAKE_PREFIX_PATH "${SOCIETY_NAV_IIGUI_PREFIX}" "${SOCIETY_NAV_VULKAN_PREFIX}")
find_package(iiGUI 0.5 CONFIG REQUIRED HINTS "${SOCIETY_NAV_IIGUI_PREFIX}/lib/cmake/iiGUI")
get_target_property(_society_iigui_includes iiGUI::iiGUI INTERFACE_INCLUDE_DIRECTORIES)
set(_society_has_paint_view FALSE)
foreach(_society_iigui_include IN LISTS _society_iigui_includes)
    if(EXISTS "${_society_iigui_include}/iiGUI/PaintView.h")
        set(_society_has_paint_view TRUE)
    endif()
endforeach()
if(NOT _society_has_paint_view)
    message(FATAL_ERROR "Society navigation needs an iiGUI package with PaintView. Update the installed SDK or set SOCIETY_NAV_IIGUI_PREFIX and iiGUI_DIR to a compatible package.")
endif()
unset(_society_has_paint_view)
unset(_society_iigui_includes)
unset(_society_iigui_include)
find_package(LVUI 0.1 CONFIG REQUIRED COMPONENTS Native HINTS "${SOCIETY_NAV_LVUI_DIR}")
set(CMAKE_PREFIX_PATH "${_society_prefix_before_native}")
unset(_society_prefix_before_native)
find_package(iiSharedCanvas 0.35.39 CONFIG REQUIRED COMPONENTS Core HINTS
    "$ENV{HOME}/.local/SDK/iiSharedCanvas")
# Older Helper packages may resolve Canvas again after its Core target is imported.
set(society_canvas_config "${iiSharedCanvas_CONFIG}")
if(APPLE AND NOT IOS)
    # An SDK directory in LIBRARY_PATH is an implicit linker directory, so
    # CMake can omit its automatic dyld rpath. Core consumers must not need
    # the compatibility facade to inherit this runtime lookup boundary.
    target_link_options(iiSharedCanvas::Core INTERFACE
        "LINKER:-rpath,$<TARGET_FILE_DIR:iiSharedCanvas::Core>")
endif()
qt_standard_project_setup(REQUIRES 6.8)

set(society_sdk_packages
    iiAccountManager
    iiCalendarIntegration
    iiFileProvider
    iiPhotoLibrary
    iiLocalDiffusion
    iiWebServer
    iiSocietyContainer
    iiSocietyHelper
)
if(NOT APPLE OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(society_sdk_packages iiAccountManager iiCalendarIntegration iiFileProvider iiWebServer iiSocietyContainer iiSocietyHelper iiPhotoLibrary)
endif()
set(society_sdk_targets)
foreach(society_sdk IN LISTS society_sdk_packages)
    if(CMAKE_CROSSCOMPILING)
        if(NOT DEFINED ${society_sdk}_DIR OR NOT EXISTS "${${society_sdk}_DIR}/${society_sdk}Config.cmake")
            message(FATAL_ERROR "Install ${society_sdk} for the target SDK and set ${society_sdk}_DIR; host packages cannot substitute it")
        endif()
    endif()
    if(society_sdk STREQUAL "iiAccountManager")
        find_package(iiAccountManager 0.2.6 CONFIG REQUIRED COMPONENTS Quick HINTS "$ENV{HOME}/.local/SDK/iiAccountManager")
    elseif(society_sdk STREQUAL "iiCalendarIntegration")
        find_package(iiCalendarIntegration 0.2 CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/iiCalendarIntegration")
    elseif(society_sdk STREQUAL "iiPhotoLibrary")
        find_package(iiPhotoLibrary 0.1 CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/iiPhotoLibrary")
        if(NOT iiPhotoLibrary_PROVIDER_DEVICE_SHA256)
            message(FATAL_ERROR "The selected iiPhotoLibrary does not record its iiFileProvider device ABI. Rebuild iiPhotoLibrary with the selected iiFileProvider package before building Society.")
        endif()
    elseif(society_sdk STREQUAL "iiSocietyContainer")
        find_package(iiSocietyContainer 0.16.1 CONFIG REQUIRED COMPONENTS Gui Maintenance HINTS "$ENV{HOME}/.local/SDK/iiSocietyContainer")
    elseif(society_sdk STREQUAL "iiWebServer")
        find_package(iiWebServer 0.6.0 CONFIG REQUIRED HINTS
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/dependencies/iiWebServer"
            "$ENV{HOME}/.local/SDK/iiWebServer")
    elseif(society_sdk STREQUAL "iiSocietyHelper")
        find_package(iiSocietyHelper 0.11.0 CONFIG REQUIRED HINTS
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/dependencies/iiSocietyHelper"
            "$ENV{HOME}/.local/SDK/iiSocietyHelper")
        if(NOT iiSharedCanvas_CONFIG STREQUAL society_canvas_config)
            message(FATAL_ERROR "Society and iiSocietyHelper selected different iiSharedCanvas packages. Set iiSharedCanvas_DIR to a package compatible with both.")
        endif()
    else()
        find_package(${society_sdk} CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/${society_sdk}")
    endif()
    # The host owns inference; the app calls only the model-packaging component.
    if(NOT society_sdk STREQUAL "iiLocalDiffusion")
        list(APPEND society_sdk_targets "${society_sdk}::${society_sdk}")
    endif()
endforeach()
list(APPEND society_sdk_targets iiFileProvider::Temporary)
set(society_agent_runtime_targets)
if((UNIX OR WIN32) AND NOT IOS AND NOT ANDROID AND NOT EMSCRIPTEN)
    if(NOT TARGET iiLocalDiffusion::ModelPackaging)
        find_package(iiLocalDiffusion CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/iiLocalDiffusion")
    endif()
    find_package(iiMultimodalAgent 0.1 CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/iiMultimodalAgent")
    list(APPEND society_agent_runtime_targets iiMultimodalAgent::iiMultimodalAgent
        iiMultimodalAgent::NativeImages iiLocalLLM::iiLocalLLM iiFileProvider::Native)
    if(TARGET iiLocalDiffusion::iiLocalDiffusion)
        list(APPEND society_agent_runtime_targets iiLocalDiffusion::iiLocalDiffusion)
    endif()
endif()
if(APPLE AND NOT IOS AND SOCIETY_MLX_RUNTIME_DIR)
    foreach(mlx_file IN ITEMS libmlx.dylib mlx.metallib)
        if(NOT EXISTS "${SOCIETY_MLX_RUNTIME_DIR}/${mlx_file}")
            message(FATAL_ERROR "SOCIETY_MLX_RUNTIME_DIR must contain a matching libmlx.dylib and mlx.metallib")
        endif()
    endforeach()
    if(TARGET mlx)
        get_target_property(society_mlx_imported mlx IMPORTED)
        if(NOT society_mlx_imported)
            message(FATAL_ERROR "SOCIETY_MLX_RUNTIME_DIR cannot replace a locally built mlx target")
        endif()
    else()
        add_library(mlx SHARED IMPORTED GLOBAL)
    endif()
    set_target_properties(mlx PROPERTIES
        IMPORTED_LOCATION "${SOCIETY_MLX_RUNTIME_DIR}/libmlx.dylib"
        IMPORTED_LOCATION_RELEASE "${SOCIETY_MLX_RUNTIME_DIR}/libmlx.dylib"
        IMPORTED_LOCATION_DEBUG "${SOCIETY_MLX_RUNTIME_DIR}/libmlx.dylib"
        IMPORTED_LOCATION_RELWITHDEBINFO "${SOCIETY_MLX_RUNTIME_DIR}/libmlx.dylib"
        IMPORTED_LOCATION_MINSIZEREL "${SOCIETY_MLX_RUNTIME_DIR}/libmlx.dylib")
    list(APPEND society_agent_runtime_targets mlx)
endif()
set(society_lazy_runtime_arguments)
if(NOT iiSocietyContainer_HAS_MANAGED_FILES OR NOT iiSocietyHelper_HAS_MANAGED_FILES)
    message(FATAL_ERROR "Society requires the managed iiFileProvider storage build of iiSocietyContainer and iiSocietyHelper")
endif()
if(NOT iiSocietyContainer_HAS_RELOCATION_PARTICIPANT)
    message(FATAL_ERROR "Society requires an iiSocietyContainer build with the relocation participant API")
endif()
if(NOT iiSocietyContainer_HAS_WAITING_FILE_JOBS)
    message(FATAL_ERROR "Society requires an iiSocietyContainer build whose file jobs wait for the commit lock. Rebuild the SDK.")
endif()
if(NOT iiSocietyContainer_HAS_APPLICATION_STORAGE OR NOT iiSocietyHelper_HAS_APPLICATION_STORAGE)
    message(FATAL_ERROR "Society requires shared logical storage and safe first-host adoption in both storage SDKs")
endif()

if(APPLE AND NOT IOS)
    # Society uses native/Metal image generation, not the optional LibTorch
    # linear-compute backend. Reject the broad developer SDK at configure time.
    get_target_property(society_diffusion_library iiLocalDiffusion::iiLocalDiffusion IMPORTED_LOCATION_RELEASE)
    if(NOT society_diffusion_library)
        get_target_property(society_diffusion_library iiLocalDiffusion::iiLocalDiffusion IMPORTED_LOCATION)
    endif()
    execute_process(COMMAND /usr/bin/otool -L "${society_diffusion_library}"
        OUTPUT_VARIABLE society_diffusion_dependencies RESULT_VARIABLE society_diffusion_inspection)
    if(NOT society_diffusion_inspection EQUAL 0 OR society_diffusion_dependencies MATCHES "libtorch|libc10")
        message(FATAL_ERROR "Build Society's macOS runtime with tools/build_macos_inference_sdk.py and use the package under resources/dependencies/macos-runtime/iiLocalDiffusion-apple. LibTorch is not required by Society's native image generation API.")
    endif()
endif()

find_package(Threads REQUIRED)
add_library(SocietyServices STATIC App/Daemon/DaemonService.cpp App/Daemon/DaemonService.h
    App/Society/Services/SyncOwnership.cpp App/Society/Services/SyncOwnership.h
    App/Society/Services/SocietyInbox.cpp App/Society/Services/SocietyInbox.h
    App/Society/Services/SocietyRuntime.cpp App/Society/Services/SocietyRuntime.h)
target_link_libraries(SocietyServices PUBLIC iiSocietyHelper::iiSocietyHelper iiWebServer::iiWebServer Qt6::Concurrent
    PRIVATE Threads::Threads)
target_include_directories(SocietyServices PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/App")
target_compile_definitions(SocietyServices PUBLIC SOCIETY_APP_VERSION="${PROJECT_VERSION}")
# The legacy App/Photos sources remain as reference; only the SDK is linked.
include(cmake/PhotoLibrary.cmake)
if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(SOCIETY_NATIVE_DRIVE_BUNDLE "${iiSocietyContainer_NATIVE_APP}" CACHE PATH
        "Provisioned macOS Society filesystem host used at runtime")
endif()
if(NOT IOS AND NOT ANDROID AND NOT EMSCRIPTEN)
    qt_add_executable(SocietyDaemon App/Daemon/main.cpp
        App/Daemon/BackendRuntime.cpp App/Daemon/BackendRuntime.h
        App/Daemon/ModelBackendServer.cpp App/Daemon/ModelBackendServer.h
        App/Daemon/NativeDirectorySession.cpp App/Daemon/NativeDirectorySession.h
        App/Daemon/SystemDriveCoordinator.cpp App/Daemon/SystemDriveCoordinator.h)
    # The agent runtime also exports an older FileCatalog ABI. Resolve the
    # container's constructors and destructor from the same current provider.
    target_link_libraries(SocietyDaemon PRIVATE iiFileProvider::Catalog SocietyServices)
    set_target_properties(SocietyDaemon PROPERTIES MACOSX_BUNDLE FALSE QT_QML_MODULE_NO_IMPORT_SCAN TRUE)

    unset(SOCIETY_CONTAINER_DAEMON_EXECUTABLE CACHE)
    find_program(SOCIETY_CONTAINER_DAEMON_EXECUTABLE SocietyContainerDaemon
        PATHS "${iiSocietyHelper_DIR}/../../../bin" NO_DEFAULT_PATH REQUIRED)
endif()

if(NOT IOS AND NOT ANDROID AND NOT EMSCRIPTEN)
    find_package(iiSocietyHelper 0.11.0 CONFIG REQUIRED COMPONENTS GenerationHost)
endif()
add_library(SocietyGroupState INTERFACE)
target_include_directories(SocietyGroupState INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/App")
target_link_libraries(SocietyGroupState INTERFACE iiSocietyHelper::iiSocietyHelper)
if(NOT iiAccountManager_OWNS_LICENSE_MANAGER)
    message(FATAL_ERROR "Society requires iiAccountManager with its owned iiLicenseManager implementation")
endif()
set(society_account_sources App/Society/Account/AccountController.cpp App/Society/Account/AccountController.h App/Society/Account/LicenseBroker.cpp App/Society/Account/LocalLicenseBroker.cpp)
add_library(SocietyDiscovery STATIC App/Society/Network/MobileSyncActivity.cpp App/Society/Network/MobileSyncActivity.h)
set_target_properties(SocietyDiscovery PROPERTIES AUTOMOC ON POSITION_INDEPENDENT_CODE ON)
target_compile_features(SocietyDiscovery PUBLIC cxx_std_23)
target_include_directories(SocietyDiscovery PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/App")
target_link_libraries(SocietyDiscovery PUBLIC iiSocietyHelper::iiSocietyHelper)
set(society_pairing_sources App/Society/Network/DevicePairing.cpp App/Society/Network/DevicePairing.h
    App/Society/Network/PairingQr.cpp App/Society/Network/PairingQr.h App/Society/Network/QrScanner.cpp App/Society/Network/QrScanner.h
    ThirdParty/qrcodegen/qrcodegen.cpp ThirdParty/qrcodegen/qrcodegen.hpp)
if(TARGET SocietyDaemon)
    target_sources(SocietyDaemon PRIVATE ${society_account_sources}
        App/Society/Network/NetworkDriveController.cpp App/Society/Network/NetworkDriveController.h)
    target_link_libraries(SocietyDaemon PRIVATE iiPhotoLibrary::iiPhotoLibrary SocietyDiscovery SocietyGroupState iiSocietyHelper::iiSocietyHelper Qt6::Gui Qt6::QmlIntegration)
    target_compile_definitions(SocietyDaemon PRIVATE SOCIETY_HEADLESS SOCIETY_APP_VERSION="${PROJECT_VERSION}")
    if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
        target_compile_definitions(SocietyDaemon PRIVATE SOCIETY_NATIVE_DRIVE_APP="${SOCIETY_NATIVE_DRIVE_BUNDLE}")
    endif()
    if(iiSocietyContainer_MOUNT_EXECUTABLE)
        target_compile_definitions(SocietyDaemon PRIVATE SOCIETY_MOUNT_EXECUTABLE="${iiSocietyContainer_MOUNT_EXECUTABLE}")
    endif()
endif()
set(society_model_merge_sources App/Society/Tools/ModelMergeController.cpp App/Society/Tools/ModelMergeController.h
    App/Society/Tools/MergeModelCatalog.cpp App/Society/Tools/MergeModelCatalog.h)
set(society_model_packaging_sources App/Society/Tools/ModelPackagingController.cpp App/Society/Tools/ModelPackagingController.h)
set(society_model_calibrator_sources App/Society/Tools/ModelCalibratorLauncher.cpp App/Society/Tools/ModelCalibratorLauncher.h)
add_library(SocietyModelPackaging INTERFACE)
if(TARGET iiLocalDiffusion::ModelPackaging)
    target_link_libraries(SocietyModelPackaging INTERFACE iiLocalDiffusion::ModelPackaging)
    target_compile_definitions(SocietyModelPackaging INTERFACE SOCIETY_HAS_MODEL_PACKAGING)
elseif(APPLE AND NOT IOS)
    message(FATAL_ERROR "Install the iiLocalDiffusion ModelPackaging SDK component before building Society")
endif()
add_library(SocietyResourceAdapter STATIC
    App/Society/Resources/ContainerResourceAdapter.cpp App/Society/Resources/ContainerResourceAdapter.h
    App/Society/Resources/ResourceInspection.cpp App/Society/Resources/ResourceInspection.h)
target_include_directories(SocietyResourceAdapter PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/App")
target_include_directories(SocietyResourceAdapter BEFORE PRIVATE ${society_catalog_include})
target_link_libraries(SocietyResourceAdapter PUBLIC iiResource::iiResource Qt6::Core Qt6::Concurrent
    PRIVATE iiSocietyContainer::iiSocietyContainer iiFileProvider::Native iiFileProvider::Objects)
if(TARGET SocietyDaemon)
    target_link_libraries(SocietyDaemon PRIVATE SocietyResourceAdapter)
endif()
if((UNIX OR WIN32) AND NOT IOS AND NOT ANDROID AND NOT EMSCRIPTEN AND TARGET SocietyDaemon AND TARGET iiLocalDiffusion::ModelPackaging AND TARGET iiMultimodalAgent::iiMultimodalAgent)
    if(NOT iiSocietyContainer_HAS_MODEL_CACHE)
        message(FATAL_ERROR "Society's workspace agent requires an iiSocietyContainer SDK with model cache support. Rebuild or stage the updated SDK.")
    endif()
    add_library(SocietyWorkspaceAgentBackend STATIC App/Daemon/WorkspaceAgentBackend.cpp App/Daemon/WorkspaceAgentBackend.h
        App/Daemon/WorkspaceAgentRuntimePolicy.h
        App/Daemon/ResidentGateway.cpp App/Daemon/ResidentGateway.h
        App/Daemon/WorkspaceAgentRouter.cpp App/Daemon/WorkspaceAgentRouter.h
        App/Daemon/WorkspaceAgentTools.cpp App/Daemon/WorkspaceAgentTools.h
        App/Daemon/UiToolBridge.cpp App/Daemon/UiToolBridge.h
        App/Shared/BackendCommand.cpp App/Shared/BackendCommand.h
        App/Society/Mcp/FileJobTools.cpp App/Society/Mcp/FileJobTools.h
        App/Society/Mcp/ResourceTools.cpp App/Society/Mcp/ResourceTools.h
        App/Daemon/SuggestedPolicy.cpp App/Daemon/SuggestedPolicy.h
        App/Daemon/SuggestedCatalog.cpp App/Daemon/SuggestedCatalog.h
        App/Daemon/SuggestedModel.cpp App/Daemon/SuggestedModel.h
        App/Daemon/SuggestedBackend.cpp App/Daemon/SuggestedBackend.h)
    set_target_properties(SocietyWorkspaceAgentBackend PROPERTIES AUTOMOC ON)
    target_include_directories(SocietyWorkspaceAgentBackend PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/App")
    target_link_libraries(SocietyWorkspaceAgentBackend PUBLIC Qt6::Core Qt6::Gui Qt6::Concurrent Qt6::Network iiFileProvider::Catalog iiMultimodalAgent::iiMultimodalAgent
        iiSocietyContainer::iiSocietyContainer iiSocietyContainer::FileTree iiLocalDiffusion::ModelPackaging iiLocalLLM::iiLocalLLM SocietyResourceAdapter iiFileProvider::Objects)
    target_compile_definitions(SocietyWorkspaceAgentBackend INTERFACE SOCIETY_AGENT_BACKEND=1)
    target_link_libraries(SocietyDaemon PRIVATE SocietyWorkspaceAgentBackend)
endif()
if(NOT CMAKE_CROSSCOMPILING)
    if(iiLocalDiffusion_DIR)
        get_filename_component(society_merge_sdk_prefix "${iiLocalDiffusion_DIR}/../../.." ABSOLUTE)
    endif()
    find_program(SOCIETY_MODEL_MERGE_EXECUTABLE NAMES iild-merge
        HINTS "${society_merge_sdk_prefix}/bin" "$ENV{HOME}/.local/SDK/iiLocalDiffusion/bin")
    if(SOCIETY_MODEL_MERGE_EXECUTABLE)
        set_source_files_properties(App/Society/Tools/ModelMergeController.cpp PROPERTIES
            COMPILE_DEFINITIONS "SOCIETY_MODEL_MERGE_EXECUTABLE=\"${SOCIETY_MODEL_MERGE_EXECUTABLE}\"")
    endif()
endif()
set(society_environment_sources App/Society/Environment/EnvironmentAppsModel.cpp App/Society/Environment/EnvironmentAppsModel.h)
set(society_calendar_sources App/Society/Dashboard/CalendarDatabase.h App/Society/Dashboard/DashboardCalendar.cpp App/Society/Dashboard/DashboardCalendar.h
    App/Society/Dashboard/CalendarWorkspace.cpp App/Society/Dashboard/CalendarWorkspace.h)
add_library(SocietyProjectRepository STATIC App/Society/Dashboard/ProjectRepository.cpp App/Society/Dashboard/ProjectRepository.h
    App/Society/Dashboard/ProjectResourceBinding.cpp App/Society/Dashboard/ProjectResourceBinding.h
    App/Society/Semantics/PhysicalIdentity.cpp App/Society/Semantics/PhysicalIdentity.h
    App/Society/Semantics/RelocationVerification.cpp App/Society/Semantics/RelocationVerification.h
    App/Society/Semantics/SemanticRepository.cpp App/Society/Semantics/SemanticRepository.h)
# The independently installed Qt facade may still carry an older Catalog
# header. Only the repository needs the package-aware API; keep that header in
# its own translation unit instead of mixing both versions across the app.
target_include_directories(SocietyProjectRepository BEFORE PRIVATE ${society_catalog_include})
target_link_libraries(SocietyProjectRepository PUBLIC iiResource::iiResource PRIVATE Qt6::Core iiSocietyContainer::iiSocietyContainer
    iiFileProvider::Catalog iiFileProvider::Objects iiProjectManagerFileProvider::Provider)
if(TARGET SocietyDaemon)
    add_library(SocietyWorkspaceAddBackend STATIC App/Daemon/WorkspaceAddService.cpp App/Daemon/WorkspaceAddService.h)
    target_include_directories(SocietyWorkspaceAddBackend PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/App")
    target_link_libraries(SocietyWorkspaceAddBackend PUBLIC SocietyProjectRepository Qt6::Core
        iiSocietyContainer::iiSocietyContainer iiSharedCanvas::Core)
    target_link_libraries(SocietyDaemon PRIVATE SocietyWorkspaceAddBackend)
    add_library(SocietyRecentBackend STATIC App/Daemon/RecentCatalog.cpp App/Daemon/RecentCatalog.h
        App/Daemon/RecentBackend.cpp App/Daemon/RecentBackend.h)
    set_target_properties(SocietyRecentBackend PROPERTIES AUTOMOC ON)
    target_include_directories(SocietyRecentBackend PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/App")
    target_include_directories(SocietyRecentBackend BEFORE PRIVATE ${society_catalog_include})
    target_link_libraries(SocietyRecentBackend PUBLIC Qt6::Core Qt6::Concurrent iiFileProvider::Catalog
        iiFileProvider::Native iiSocietyContainer::iiSocietyContainer SocietyProjectRepository)
    target_link_libraries(SocietyDaemon PRIVATE SocietyRecentBackend)
endif()
set(society_project_sources App/Society/Dashboard/ProjectWorkspace.cpp App/Society/Dashboard/ProjectWorkspace.h)
set(society_job_sources App/Society/Jobs/JobStorage.h)
set(society_model_backend_client_sources
    App/Society/Files/ModelBackendClient.cpp App/Society/Files/ModelBackendClient.h
    App/Shared/LocalBackendAddress.h App/Shared/LocalBackendPeer.h)
add_library(SocietyModelBackendClient STATIC ${society_model_backend_client_sources})
set_target_properties(SocietyModelBackendClient PROPERTIES AUTOMOC ON)
target_include_directories(SocietyModelBackendClient PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/App")
target_link_libraries(SocietyModelBackendClient PUBLIC Qt6::Core Qt6::Network)
set(society_native_files_launcher_sources App/Society/Native/NativeFilesLauncher.cpp App/Society/Native/NativeFilesLauncher.h)
set(society_notification_sources App/Society/Notifications/NotificationStore.h
    App/Society/Notifications/NotificationCenter.cpp App/Society/Notifications/NotificationCenter.h)
set(society_backend_command_sources App/Shared/BackendCommand.cpp App/Shared/BackendCommand.h)
lvui_generate(NAME NativeNavigation SOURCE App/Society/Native/NativeNavigation.lvui
    INCLUDES NativeNavigationViewModel.h OUTPUT_VARIABLE society_native_navigation_view)
lvui_generate(NAME NativeWorkspace SOURCE App/Society/Native/NativeWorkspace.lvui
    INCLUDES NativeWorkspaceViewModel.h NativeFilesNavigation.h OUTPUT_VARIABLE society_native_workspace_view)
lvui_generate(NAME NativeSpace SOURCE App/Society/Space/NativeSpace.lvui
    INCLUDES NativeSpaceViewModel.h OUTPUT_VARIABLE society_native_space_view)
lvui_generate(NAME NativeGraphNode SOURCE App/Society/Space/NativeGraphNode.lvui
    INCLUDES GraphNodeViewModel.h OUTPUT_VARIABLE society_native_graph_node_view)
add_library(SocietyGraphNode OBJECT
    App/Society/Space/GraphNode.cpp App/Society/Space/GraphNode.h
    App/Society/Space/GraphNodeViewModel.h "${society_native_graph_node_view}")
target_compile_features(SocietyGraphNode PUBLIC cxx_std_23)
target_include_directories(SocietyGraphNode PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/App"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/App/Society/Space" "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(SocietyGraphNode PUBLIC iiGUI::iiGUI PRIVATE LVUI::Native)
add_library(SocietyNativeNavigationSurface STATIC
    App/Society/Native/LegacyNavigationBridge.cpp App/Society/Native/LegacyNavigationBridge.h
    App/Society/Space/NativeSpaceSurface.cpp App/Society/Space/NativeSpaceSurface.h
    App/Society/Space/SpaceOrbGaze.cpp App/Society/Space/SpaceOrbGaze.h
    App/Society/Space/NativeSpaceViewModel.cpp App/Society/Space/NativeSpaceViewModel.h
    App/Society/Space/SpaceDictation.h
    App/Society/Space/SpaceAssets.qrc
    App/Society/Native/NativeNavigationSurface.cpp App/Society/Native/NativeNavigationSurface.h
    App/Society/Native/NativeNavigationViewModel.cpp App/Society/Native/NativeNavigationViewModel.h
    App/Society/Native/QtPaintDevice.cpp App/Society/Native/QtPaintDevice.h
    App/Society/Native/NativeWorkspaceViewModel.cpp App/Society/Native/NativeWorkspaceViewModel.h
    App/Society/Native/NativeWorkspaceSurface.cpp App/Society/Native/NativeWorkspaceSurface.h
    App/Society/Native/PhotoPreviewLoader.cpp App/Society/Native/PhotoPreviewLoader.h
    App/Society/Native/PhotoPreviewResolver.cpp App/Society/Native/PhotoPreviewResolver.h
    App/Society/Native/LegacyWorkspaceBridge.cpp App/Society/Native/LegacyWorkspaceBridge.h
    App/Society/Native/NavigationIcons.qrc App/Society/Native/WorkspaceIcons.qrc
    "${society_native_navigation_view}" "${society_native_workspace_view}" "${society_native_space_view}")
set_target_properties(SocietyNativeNavigationSurface PROPERTIES AUTOMOC ON AUTORCC ON)
target_compile_features(SocietyNativeNavigationSurface PUBLIC cxx_std_23)
target_include_directories(SocietyNativeNavigationSurface PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/App"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/App/Society/Native" "${CMAKE_CURRENT_SOURCE_DIR}/App/Society/Space" "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(SocietyNativeNavigationSurface PUBLIC Qt6::Quick SocietyModelBackendClient PRIVATE LVUI::Native iiGUI::iiGUI iiSocietyContainer::Gui iiSocietyContainer::FileTree Qt6::Concurrent)
if(APPLE AND NOT IOS)
    find_package(iiDeviceMultimedia 0.2 CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/iiDeviceMultimedia")
    target_sources(SocietyNativeNavigationSurface PRIVATE App/Society/Space/SpaceDictationApple.mm)
    set_source_files_properties(App/Society/Space/SpaceDictationApple.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    set_target_properties(SocietyNativeNavigationSurface PROPERTIES OBJCXX_STANDARD 23 OBJCXX_STANDARD_REQUIRED YES)
    target_link_libraries(SocietyNativeNavigationSurface PRIVATE iiDeviceMultimedia::iiDeviceMultimedia "-framework Speech" "-framework AVFoundation")
else()
    target_sources(SocietyNativeNavigationSurface PRIVATE App/Society/Space/SpaceDictationUnsupported.cpp)
endif()
qt_add_executable(Society
    ${society_notification_sources}
    ${society_environment_sources}
    ${society_calendar_sources}
    ${society_project_sources}
    ${society_job_sources}
    ${society_backend_command_sources}
    ${society_model_merge_sources}
    ${society_model_packaging_sources}
    ${society_model_calibrator_sources}
    ${society_native_files_launcher_sources}
    App/main.cpp ${society_account_sources} ${society_pairing_sources} App/Society/Files/DirectoryLocation.cpp App/Society/Files/DirectoryLocation.h
        App/Society/Dashboard/DashboardFiles.cpp App/Society/Dashboard/DashboardFiles.h
        App/Society/Models/StorageModels.cpp App/Society/Models/StorageModels.h
        App/Society/Files/ModelImporter.cpp App/Society/Files/ModelImporter.h
        App/Society/Files/ModelImportSource.h
        App/Society/Drive/DriveController.cpp App/Society/Drive/DriveController.h
        App/Society/Drive/CloudFilesController.cpp App/Society/Drive/CloudFilesController.h
        App/Society/Drive/StorageNavigation.cpp App/Society/Drive/StorageNavigation.h
        App/Society/Network/NetworkDriveController.cpp App/Society/Network/NetworkDriveController.h
)
include(cmake/NativeOverlay.cmake)
target_link_libraries(SocietyNativeNavigationSurface PRIVATE SocietyNativeOverlay)
if(NOT IOS AND NOT ANDROID AND NOT EMSCRIPTEN)
    qt_add_executable(SocietyCli App/Society/Commands/main.cpp ${society_backend_command_sources})
    target_include_directories(SocietyCli PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/App")
    target_link_libraries(SocietyCli PRIVATE Qt6::Core Qt6::Network)
    set_target_properties(SocietyCli PROPERTIES OUTPUT_NAME society-cli
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin")
    add_dependencies(Society SocietyCli)
    set_property(TARGET Society APPEND PROPERTY LINK_DEPENDS "$<TARGET_FILE:SocietyCli>"
        "${CMAKE_CURRENT_SOURCE_DIR}/App/Society/Bindings/society.py")
endif()
# Painted C++ items require the QtQuick type metadata as a QML dependency.
# Source location must not change the QML module's existing resource URLs.
file(GLOB_RECURSE _app_source_resources CONFIGURE_DEPENDS
    RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/App/Society/*.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/App/Society/*.svg")
foreach(_app_resource IN LISTS _app_source_resources)
    string(REGEX REPLACE "^App/Society/" "App/" _app_resource_alias "${_app_resource}")
    set_source_files_properties("${_app_resource}" PROPERTIES QT_RESOURCE_ALIAS "${_app_resource_alias}")
endforeach()

# Qt 6.8's generated registrar includes QML_ELEMENT headers by basename.
target_include_directories(Society PRIVATE
    App/Society/Account App/Society/Dashboard App/Society/Drive App/Society/Network
    App/Society/Environment App/Society/Files App/Society/Models App/Society/Notifications App/Society/Tools)
target_include_directories(Society BEFORE PRIVATE ${society_provider_include})
if(TARGET iiLocalDiffusion::ModelPackaging)
    # Prefer the header paired with the linked packaging library over an older
    # SDK exposed through an ambient CPATH on developer machines.
    get_target_property(society_packaging_includes iiLocalDiffusion::ModelPackaging INTERFACE_INCLUDE_DIRECTORIES)
    target_include_directories(Society BEFORE PRIVATE ${society_packaging_includes})
    foreach(society_packaging_include IN LISTS society_packaging_includes)
        if(EXISTS "${society_packaging_include}/ModelPackaging/ModelPackaging.hpp")
            set_property(SOURCE App/Society/Tools/ModelPackagingController.cpp APPEND PROPERTY COMPILE_OPTIONS
                "$<$<COMPILE_LANG_AND_ID:CXX,AppleClang,Clang,GNU>:-iquote${society_packaging_include}>")
        endif()
    endforeach()
endif()

qt_add_qml_module(Society
    URI Society VERSION 1.0 RESOURCE_PREFIX "/qt/qml"
    IMPORT_PATH "${iiAccountManager_QML_IMPORT_PATH}" "${iiPhotoLibrary_QML_IMPORT_PATH}"
    DEPENDENCIES QtQuick iiAccountManager iiPhotoLibrary
    QML_FILES App/Society/PanelOutline.qml App/Society/NotificationPopover.qml App/Society/Gallery/GalleryTile.qml App/Society/Gallery/GalleryZoom.qml App/Society/Gallery/GalleryInfo.qml App/Society/Main.qml App/Society/MobileGestures.qml App/Society/FileGridView.qml App/Society/Network/NetworkDevices.qml App/Society/Network/PairingPanel.qml
        App/Society/SocietyView.qml App/Society/OnboardingView.qml App/Society/StorageView.qml App/Society/Dashboard/Dashboard.qml App/Society/Dashboard/DashboardSidebar.qml App/Society/Dashboard/DashboardFileSection.qml App/Society/Dashboard/QuickGenerate.qml App/Society/Dashboard/DashboardCalendarView.qml
        App/Society/Dashboard/CalendarEntry.qml App/Society/Dashboard/CalendarDetailSection.qml
        App/Society/Dashboard/CalendarWorkspaceView.qml App/Society/Dashboard/CalendarPeriodView.qml
        App/Society/Dashboard/CalendarMiniMonth.qml App/Society/Dashboard/CalendarContext.qml
        App/Society/Dashboard/CalendarInspector.qml App/Society/Dashboard/CalendarEventEditor.qml
        App/Society/Dashboard/CalendarChoice.qml App/Society/Dashboard/CalendarEventCard.qml
        App/Society/Dashboard/ProjectsView.qml App/Society/Dashboard/ProjectsCollection.qml App/Society/Dashboard/ProjectStudio.qml App/Society/Dashboard/ProjectInspector.qml
        App/Society/Models/ModelsView.qml App/Society/Drive/StorageSidebar.qml App/Society/Files/FileActionMenu.qml App/Society/FilesBrowser.qml
        App/Society/Preferences/PreferencesWindow.qml App/Society/Preferences/PreferencesContent.qml
        App/Society/Tools/ModelMergeTool.qml App/Society/Tools/MergePathField.qml App/Society/Tools/MergeModelPicker.qml
        App/Society/Tools/ToolsView.qml App/Society/Tools/ToolCard.qml
        App/Society/Tools/ModelPackagingTool.qml
        App/Society/Environment/EnvironmentView.qml App/Society/Environment/EnvironmentApps.qml
        App/Society/Environment/EnvironmentCard.qml App/Society/Environment/EnvironmentBadge.qml App/Society/Environment/EnvironmentButton.qml
        App/Society/Environment/EnvironmentRow.qml App/Society/Environment/EnvironmentSection.qml
        App/Society/Network/BrowseView.qml
    RESOURCES
        App/Society/Drive/icons/files.svg App/Society/Drive/icons/generation-history.svg App/Society/Drive/icons/published.svg
        App/Society/Dashboard/Assets/media-chevron.svg
        App/Society/Environment/icons/home.svg App/Society/Environment/icons/device.svg
        App/Society/Environment/icons/application.svg App/Society/Environment/icons/folder.svg
        App/Society/Dashboard/icons/home.svg App/Society/Dashboard/icons/projects.svg
        App/Society/Dashboard/icons/calendar.svg App/Society/Dashboard/icons/activity.svg
        App/Society/Dashboard/icons/people.svg App/Society/Dashboard/icons/guild.svg
        App/Society/Dashboard/icons/organization.svg
        App/Society/Toolbar/icons/search.svg App/Society/Toolbar/icons/notifications.svg App/Society/Toolbar/icons/account.svg App/Society/Toolbar/icons/avatar-placeholder.svg
)
society_add_photo_qml(Society)
# GNU's PE big-object LTO plugin cannot read the generated registrar reliably.
if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/society_qmltyperegistrations.cpp"
        PROPERTIES COMPILE_OPTIONS "-fno-lto")
endif()

target_include_directories(Society PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/App/Society/Models" "${CMAKE_CURRENT_SOURCE_DIR}/App/Society/Environment")
# Shared Qt resolves QML plugins at runtime; avoid recursively scanning build/
# (including archived app bundles) on every desktop reconfiguration.
get_target_property(society_qt_core_type Qt6::Core TYPE)
if(society_qt_core_type STREQUAL "SHARED_LIBRARY")
    set_property(TARGET Society PROPERTY QT_QML_MODULE_NO_IMPORT_SCAN TRUE)
endif()
target_link_libraries(Society PRIVATE Qt6::Quick Qt6::QuickControls2 iiAccountManager::Quick)
target_link_libraries(Society PRIVATE SocietyNativeNavigationSurface)
target_link_libraries(Society PRIVATE SocietyGraphNode)
if(APPLE AND NOT IOS)
    target_link_options(Society BEFORE PRIVATE "LINKER:-rpath,$<TARGET_FILE_DIR:iiAccountManager::iiAccountManager>")
endif()
lvrs_configure_qml_app(Society NO_PLATFORM_RUNTIME_TARGETS)
target_link_libraries(Society PRIVATE ${society_sdk_targets}
    iiSocietyContainer::Gui SocietyServices SocietyDiscovery SocietyGroupState Qt6::Concurrent)
target_link_libraries(Society PRIVATE SocietyModelPackaging)
target_link_libraries(Society PRIVATE iiFileProvider::Native)
target_link_libraries(Society PRIVATE iiJob::iiJob)
target_link_libraries(Society PRIVATE SocietyProjectRepository)
if(TARGET mlx AND SOCIETY_MLX_RUNTIME_DIR)
    target_link_libraries(Society PRIVATE mlx)
endif()
add_library(SocietyFilePreviews STATIC App/Society/Files/CanvasFilePreview.cpp App/Society/Files/CanvasFilePreview.h)
target_include_directories(SocietyFilePreviews PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/App")
target_link_libraries(SocietyFilePreviews PRIVATE Qt6::Gui iiFileProvider::Native iiSharedCanvas::Core)
target_link_libraries(Society PRIVATE SocietyFilePreviews)
qt_add_resources(Society icu_license PREFIX "/licenses/icu"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/icu" FILES ThirdParty/icu/LICENSE.txt)
qt_add_resources(Society qrcodegen_license PREFIX "/licenses/qrcodegen"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/qrcodegen" FILES ThirdParty/qrcodegen/LICENSE)
if(NOT IOS AND NOT ANDROID)
    get_filename_component(society_serverhost_prefix "${iiWebServer_DIR}/../../.." ABSOLUTE)
    qt_add_resources(Society lan_openssl_license PREFIX "/licenses/openssl"
        BASE "${society_serverhost_prefix}/share/iiWebServer/licenses"
        FILES "${society_serverhost_prefix}/share/iiWebServer/licenses/OpenSSL-LICENSE.txt")
endif()
option(SOCIETY_ACCOUNT_RUNTIME_PROBE "Enable credential-free TLS and account panel runtime verification" OFF)
option(SOCIETY_GROUP_STATE_RUNTIME_PROBE "Enable opt-in synthetic encrypted group state device verification" OFF)
option(SOCIETY_DASHBOARD_RUNTIME_PROBE "Enable opt-in read-only dashboard device verification" OFF)
option(SOCIETY_FILES_RUNTIME_PROBE "Enable isolated file-list frame measurements" OFF)
if(SOCIETY_FILES_RUNTIME_PROBE)
    target_sources(Society PRIVATE tests/FilesRuntimeProbe.cpp)
    target_compile_definitions(Society PRIVATE SOCIETY_FILES_RUNTIME_PROBE)
endif()
option(SOCIETY_LIFECYCLE_RUNTIME_PROBE "Enable isolated native-window lifecycle measurements" OFF)
if(SOCIETY_LIFECYCLE_RUNTIME_PROBE)
    target_sources(Society PRIVATE tests/LifecycleRuntimeProbe.cpp)
    target_compile_definitions(Society PRIVATE SOCIETY_LIFECYCLE_RUNTIME_PROBE)
endif()
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
target_include_directories(Society PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/App" "${CMAKE_CURRENT_SOURCE_DIR}/App/Society/Account")
if(IOS OR ANDROID)
    target_compile_definitions(Society PRIVATE SOCIETY_CLIENT_ONLY=1)
    if(IOS)
        target_sources(Society PRIVATE App/Society/Network/IosSyncActivity.mm)
        set_source_files_properties(App/Society/Network/IosSyncActivity.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
        target_link_libraries(Society PRIVATE "-framework UIKit" "-framework BackgroundTasks")
    else()
        target_sources(Society PRIVATE App/Society/Network/AndroidSyncActivity.cpp)
    endif()
endif()
if(APPLE AND NOT IOS)
    # iiGUI owns its application event loop, so the LVUI picker is an
    # independent helper and exchanges only a catalogue snapshot with Society.
    include(ExternalProject)
    set(SOCIETY_NATIVE_LVUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resources/dependencies/lvui-native-model-chooser/lib/cmake/LVUI"
        CACHE PATH "LVUI package used to build the native model chooser")
    set(SOCIETY_NATIVE_IIGUI_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/resources/dependencies/iigui-native-model-chooser"
        CACHE PATH "iiGUI 0.5 package used to build the native model chooser")
    set(SOCIETY_NATIVE_VULKAN_PREFIX "$ENV{HOME}/.local/SDK/Vulkan"
        CACHE PATH "Vulkan SDK used by the native model chooser")
    set(society_native_chooser_binary "${CMAKE_CURRENT_BINARY_DIR}/native-model-chooser/SocietyModelChooser")
    set(SOCIETY_CALIBRATOR_IILD_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/resources/dependencies/model-calibrator-iild"
        CACHE PATH "iiLocalDiffusion ModelPackaging component for the native calibrator")
    set(society_native_calibrator_binary "${CMAKE_CURRENT_BINARY_DIR}/native-model-calibrator/SocietyModelCalibrator")
    set(society_native_files_binary "${CMAKE_CURRENT_BINARY_DIR}/native-files-app/SocietyNativeFilesMigration")
    ExternalProject_Add(SocietyNativeFilesBuild
        SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/App/Society/Native"
        BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/native-files-app"
        CMAKE_ARGS "-DCMAKE_BUILD_TYPE=Release" "-DBUILD_TESTING=OFF"
            "-DLVUI_DIR=${SOCIETY_NATIVE_LVUI_DIR}"
            "-DiiGUI_DIR=${SOCIETY_NATIVE_IIGUI_PREFIX}/lib/cmake/iiGUI"
            "-Dlunasvg_DIR=${SOCIETY_NATIVE_IIGUI_PREFIX}/lib/cmake/lunasvg"
            "-Dplutovg_DIR=${SOCIETY_NATIVE_IIGUI_PREFIX}/lib/cmake/plutovg"
            "-DiiSDKCore_DIR=${iiSDKCore_DIR}"
            "-DiiNetwork_DIR=${iiNetwork_DIR}"
            "-DiiJson_DIR=${iiJson_DIR}"
            "-DiiColorManager_DIR=$ENV{HOME}/.local/SDK/iiColorManager/lib/cmake/iiColorManager"
            "-DVulkan_INCLUDE_DIR=${SOCIETY_NATIVE_VULKAN_PREFIX}/include"
            "-DVulkan_LIBRARY=${SOCIETY_NATIVE_VULKAN_PREFIX}/lib/libvulkan.dylib"
        BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --target SocietyNativeFilesMigration --config Release
        BUILD_ALWAYS TRUE INSTALL_COMMAND "" BUILD_BYPRODUCTS "${society_native_files_binary}")
    add_dependencies(Society SocietyNativeFilesBuild)
    set_property(TARGET Society APPEND PROPERTY LINK_DEPENDS "${society_native_files_binary}")
    ExternalProject_Add(SocietyNativeModelChooserBuild
        SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/App/Society/Tools/NativeModelChooser"
        BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/native-model-chooser"
        CMAKE_ARGS "-DCMAKE_BUILD_TYPE=Release"
            "-DLVUI_DIR=${SOCIETY_NATIVE_LVUI_DIR}"
            "-DiiGUI_DIR=${SOCIETY_NATIVE_IIGUI_PREFIX}/lib/cmake/iiGUI"
            "-Dlunasvg_DIR=${SOCIETY_NATIVE_IIGUI_PREFIX}/lib/cmake/lunasvg"
            "-Dplutovg_DIR=${SOCIETY_NATIVE_IIGUI_PREFIX}/lib/cmake/plutovg"
            "-DiiSDKCore_DIR=${iiSDKCore_DIR}"
            "-DiiColorManager_DIR=$ENV{HOME}/.local/SDK/iiColorManager/lib/cmake/iiColorManager"
            "-DVulkan_INCLUDE_DIR=${SOCIETY_NATIVE_VULKAN_PREFIX}/include"
            "-DVulkan_LIBRARY=${SOCIETY_NATIVE_VULKAN_PREFIX}/lib/libvulkan.dylib"
        BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --target SocietyModelChooser --config Release
        BUILD_ALWAYS TRUE INSTALL_COMMAND "" BUILD_BYPRODUCTS "${society_native_chooser_binary}")
    add_dependencies(Society SocietyNativeModelChooserBuild)
    set_property(TARGET Society APPEND PROPERTY LINK_DEPENDS "${society_native_chooser_binary}")
    ExternalProject_Add(SocietyNativeModelCalibratorBuild
        SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/App/Society/Tools/NativeModelCalibrator"
        BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/native-model-calibrator"
        CMAKE_ARGS "-DCMAKE_BUILD_TYPE=Release"
            "-DLVUI_DIR=${SOCIETY_NATIVE_LVUI_DIR}"
            "-DiiGUI_DIR=${SOCIETY_NATIVE_IIGUI_PREFIX}/lib/cmake/iiGUI"
            "-Dlunasvg_DIR=${SOCIETY_NATIVE_IIGUI_PREFIX}/lib/cmake/lunasvg"
            "-Dplutovg_DIR=${SOCIETY_NATIVE_IIGUI_PREFIX}/lib/cmake/plutovg"
            "-DiiSDKCore_DIR=${iiSDKCore_DIR}"
            "-DiiColorManager_DIR=$ENV{HOME}/.local/SDK/iiColorManager/lib/cmake/iiColorManager"
            "-DVulkan_INCLUDE_DIR=${SOCIETY_NATIVE_VULKAN_PREFIX}/include"
            "-DVulkan_LIBRARY=${SOCIETY_NATIVE_VULKAN_PREFIX}/lib/libvulkan.dylib"
            "-DSOCIETY_CALIBRATOR_IILD_PREFIX=${SOCIETY_CALIBRATOR_IILD_PREFIX}"
        BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --target SocietyModelCalibrator --config Release
        BUILD_ALWAYS TRUE INSTALL_COMMAND "" BUILD_BYPRODUCTS "${society_native_calibrator_binary}")
    add_dependencies(Society SocietyNativeModelCalibratorBuild)
    set_property(TARGET Society APPEND PROPERTY LINK_DEPENDS "${society_native_calibrator_binary}")
    target_sources(Society PRIVATE App/Daemon/platform/macos/ServiceRegistration.mm
        App/Daemon/platform/macos/ServiceRegistration.h
        App/Daemon/platform/macos/com.iisacc.society.daemon.plist
        App/Daemon/platform/macos/com.iisacc.society.container-monitor.plist)
    set_source_files_properties(App/Daemon/platform/macos/ServiceRegistration.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    set_source_files_properties(App/Daemon/platform/macos/com.iisacc.society.daemon.plist
        App/Daemon/platform/macos/com.iisacc.society.container-monitor.plist PROPERTIES
        MACOSX_PACKAGE_LOCATION "Library/LaunchAgents")
    target_link_libraries(Society PRIVATE "-framework ServiceManagement" "-framework Foundation")
    add_dependencies(Society SocietyDaemon)
    set_property(TARGET Society APPEND PROPERTY LINK_DEPENDS "$<TARGET_FILE:SocietyDaemon>"
        "${SOCIETY_CONTAINER_DAEMON_EXECUTABLE}"
        "${CMAKE_CURRENT_BINARY_DIR}/SocietyDaemon-Info.plist")
    add_custom_command(TARGET Society POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_BUNDLE_DIR:Society>/Contents/MacOS"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:SocietyDaemon>"
            "$<TARGET_BUNDLE_DIR:Society>/Contents/MacOS/SocietyDaemon"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${SOCIETY_CONTAINER_DAEMON_EXECUTABLE}"
            "$<TARGET_BUNDLE_DIR:Society>/Contents/MacOS/SocietyContainerDaemon"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${society_native_chooser_binary}"
            "$<TARGET_BUNDLE_DIR:Society>/Contents/MacOS/SocietyModelChooser"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${society_native_calibrator_binary}"
            "$<TARGET_BUNDLE_DIR:Society>/Contents/MacOS/SocietyModelCalibrator"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${society_native_files_binary}"
            "$<TARGET_BUNDLE_DIR:Society>/Contents/MacOS/SocietyNativeFiles"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:SocietyCli>"
            "$<TARGET_BUNDLE_DIR:Society>/Contents/MacOS/society-cli"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_BUNDLE_DIR:Society>/Contents/Resources/python"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/App/Society/Bindings/society.py"
            "$<TARGET_BUNDLE_DIR:Society>/Contents/Resources/python/society.py"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${SOCIETY_NATIVE_IIGUI_PREFIX}/share/iiGUI/resources/native"
            "$<TARGET_BUNDLE_DIR:Society>/Contents/Resources"
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "$<TARGET_BUNDLE_DIR:Society>/Contents/Helpers/SocietyDaemon.app")
    find_program(society_macdeployqt macdeployqt HINTS "${Qt6_DIR}/../../../bin" REQUIRED)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(SOCIETY_MAC_SIGN_IDENTITY "-" CACHE STRING "Signing identity for the local daemon package; '-' uses ad-hoc signing")
    set(SOCIETY_MAC_APP_GROUP "group.com.iisacc.society" CACHE STRING "macOS App Group; use <TeamID>.com.iisacc.society with a developer signature")
    set(SOCIETY_MAC_RUNTIME_SEARCH_PATHS "" CACHE STRING "Additional native runtime directories for macOS bundle deployment")
    configure_file(App/Daemon/platform/macos/Info.plist.in "${CMAKE_CURRENT_BINARY_DIR}/SocietyDaemon-Info.plist" @ONLY)
    configure_file(App/platform/macos/App.entitlements.in "${CMAKE_CURRENT_BINARY_DIR}/Society-Mac.entitlements" @ONLY)
    set_property(TARGET Society APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/Society-Mac.entitlements")
    add_custom_target(SocietyDaemonPackage
        COMMAND "${CMAKE_COMMAND}"
            "-DSOCIETY_SOURCE_APP=$<TARGET_BUNDLE_DIR:Society>"
            "-DSOCIETY_PACKAGE_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/package"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/PackageMac.cmake"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_daemon_package.py"
            "$<TARGET_BUNDLE_DIR:Society>" "$<TARGET_BUNDLE_DIR:Society>"
            "$<TARGET_FILE:iiSocietyContainer::iiSocietyContainer>"
            "$<TARGET_FILE:iiSocietyContainer::Gui>"
        COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_macos_runtime.py"
            "$<TARGET_BUNDLE_DIR:Society>" --gui ${society_lazy_runtime_arguments}
        DEPENDS Society SocietyDaemon USES_TERMINAL VERBATIM)
endif()
target_include_directories(Society PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/App/Society/Files" "${CMAKE_CURRENT_SOURCE_DIR}/App/Society/Drive"
    "${CMAKE_CURRENT_SOURCE_DIR}/App/Society/Network" "${CMAKE_CURRENT_SOURCE_DIR}/App/Society/Dashboard"
    "${CMAKE_CURRENT_SOURCE_DIR}/App/Society/Tools")
target_compile_definitions(Society PRIVATE
    SOCIETY_APP_VERSION="${PROJECT_VERSION}"
    SOCIETY_LVRS_QML_IMPORT_PATH="${LVRS_QML_IMPORT_PATH}"
)
if(iiSocietyContainer_MOUNT_EXECUTABLE)
    add_custom_command(TARGET Society POST_BUILD COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${iiSocietyContainer_MOUNT_EXECUTABLE}" "$<TARGET_FILE_DIR:Society>" VERBATIM)
    if(WIN32 AND iiSocietyContainer_MOUNT_RUNTIME)
        add_custom_command(TARGET Society POST_BUILD COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${iiSocietyContainer_MOUNT_RUNTIME}" "$<TARGET_FILE_DIR:Society>" VERBATIM)
        if(iiSocietyContainer_MOUNT_LICENSE)
            add_custom_command(TARGET Society POST_BUILD COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${iiSocietyContainer_MOUNT_LICENSE}" "$<TARGET_FILE_DIR:Society>" VERBATIM)
        endif()
    endif()
endif()
if(ANDROID)
    iiSocietyContainer_add_android_file_provider(Society)
    include(cmake/AndroidQr.cmake)
    society_add_android_qr(Society)
    iiFileProvider_add_android_storage(Society)
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
    target_sources(Society PRIVATE App/Society/Files/AppleModelSource.mm App/Society/Files/AppleModelSource.h
        App/Society/Network/IosQrScanner.mm App/Society/Network/AppleQrDecoder.mm App/Society/Network/AppleQrDecoder.h
        App/Society/Files/IosModelDrop.mm App/Society/Files/IosModelDrop.h
        App/Society/Files/IosFileAccess.mm App/Society/Files/IosFileAccess.h)
    set_source_files_properties(App/Society/Network/IosQrScanner.mm App/Society/Network/AppleQrDecoder.mm App/Society/Files/AppleModelSource.mm App/Society/Files/IosModelDrop.mm App/Society/Files/IosFileAccess.mm
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
    # A previously deployed FileTree can otherwise win transitive resolution
    # over the currently selected SDK and silently preserve old container behavior.
    set(society_container_runtime_targets iiSocietyContainer::iiSocietyContainer
        iiSocietyContainer::Gui iiSocietyContainer::Sharing iiSocietyContainer::FileTree
        iiSocietyContainer::Maintenance)
    set(society_container_verify_arguments)
    foreach(runtime_target IN LISTS society_container_runtime_targets)
        list(APPEND society_container_verify_arguments --library "$<TARGET_FILE:${runtime_target}>")
    endforeach()
    foreach(runtime_target IN ITEMS LVRS::LVRS ${society_container_runtime_targets} iiSharedCanvas::Core iiDeviceMultimedia::iiDeviceMultimedia ${society_sdk_targets} ${society_agent_runtime_targets})
        get_target_property(runtime_type ${runtime_target} TYPE)
        if(runtime_type STREQUAL "SHARED_LIBRARY")
            list(APPEND society_runtime_arguments --library "$<TARGET_FILE:${runtime_target}>")
        endif()
    endforeach()
    foreach(runtime_path IN LISTS SOCIETY_MAC_RUNTIME_SEARCH_PATHS)
        list(APPEND society_runtime_arguments --search-path "${runtime_path}")
    endforeach()
    if(TARGET iiMultimodalAgent::NativeImages)
        list(APPEND society_runtime_arguments --runtime-plugin "$<TARGET_FILE:iiMultimodalAgent::NativeImages>")
    endif()
        if(TARGET OpenSSL::Crypto)
        list(APPEND society_runtime_arguments --library "$<TARGET_FILE:OpenSSL::Crypto>")
    endif()
    # Finder and launchd do not inherit development DYLD/QML search paths.
    # Stage both processes before signing, and reject an incomplete bundle.
    set_property(TARGET Society APPEND PROPERTY LINK_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/deploy_macos_runtime.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/single-app/deploy_macos.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_macos_runtime.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_macos_sdk_identity.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_macos_footprint.py")
    set(society_post_build_runtime_check)
    if(SOCIETY_VERIFY_RUNTIME_ON_BUILD)
        list(APPEND society_post_build_runtime_check
            COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_macos_runtime.py"
                "$<TARGET_BUNDLE_DIR:Society>" ${society_lazy_runtime_arguments})
    endif()
    set(society_deployment_log "${CMAKE_CURRENT_SOURCE_DIR}/resources/.state/macos-runtime-deployment-$<CONFIG>.json")
    add_custom_command(TARGET Society POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_SOURCE_DIR}/resources/.state"
        COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tools/deploy_macos_runtime.py"
            --app "$<TARGET_BUNDLE_DIR:Society>" --macdeployqt "${society_macdeployqt}"
            --qml-dir "${CMAKE_CURRENT_SOURCE_DIR}/App/Society"
            --qml-import "${LVRS_QML_IMPORT_PATH}" --qml-import "${iiAccountManager_QML_IMPORT_PATH}"
            --qml-import "${iiPhotoLibrary_QML_IMPORT_PATH}"
            --qml-import "${CMAKE_CURRENT_BINARY_DIR}"
            --identity "${SOCIETY_MAC_SIGN_IDENTITY}"
            --entitlements "${CMAKE_CURRENT_BINARY_DIR}/Society-Mac.entitlements"
            --log "${society_deployment_log}"
            --compact --quick-style Basic --configuration "$<CONFIG>" --embedded-resources-module LVRS
            ${society_runtime_arguments}
        COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_macos_sdk_identity.py"
            --app "$<TARGET_BUNDLE_DIR:Society>" ${society_container_verify_arguments}
        COMMAND "${CMAKE_COMMAND}" -E rm -f "${society_deployment_log}"
        ${society_post_build_runtime_check}
        COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_macos_footprint.py"
            "$<TARGET_BUNDLE_DIR:Society>"
        VERBATIM)
endif()

include(CTest)
if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    add_executable(SocietyGraphNodeTests tests/graph_node.cpp)
    target_link_libraries(SocietyGraphNodeTests PRIVATE SocietyGraphNode LVUI::Native iiGUI::iiGUI)
    add_test(NAME Society.GraphNode COMMAND SocietyGraphNodeTests)
    add_executable(SocietyGraphNodeValueTests tests/graph_node_values.cpp)
    target_link_libraries(SocietyGraphNodeValueTests PRIVATE SocietyGraphNode LVUI::Native iiGUI::iiGUI)
    add_test(NAME Society.GraphNodeValues COMMAND SocietyGraphNodeValueTests)
    add_executable(SocietyNotificationStoreTests tests/notification_store.cpp)
    target_include_directories(SocietyNotificationStoreTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/App")
    target_compile_features(SocietyNotificationStoreTests PRIVATE cxx_std_23)
    add_test(NAME Society.NotificationStore COMMAND SocietyNotificationStoreTests)
    qt_add_executable(SocietyNotificationCenterTests tests/tst_notification_center.cpp ${society_notification_sources})
    target_include_directories(SocietyNotificationCenterTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/App")
    target_link_libraries(SocietyNotificationCenterTests PRIVATE Qt6::Qml)
    add_test(NAME Society.NotificationCenter COMMAND SocietyNotificationCenterTests)
    add_test(NAME Society.NativeAccountPlatform
        COMMAND "${CMAKE_COMMAND}"
            "-DSOCIETY_ACCOUNT_PLATFORM_FILE=${CMAKE_CURRENT_SOURCE_DIR}/cmake/NativeAccountPlatform.cmake"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/native_account_platform.cmake")
    add_test(NAME Society.NativePhotoPlatform
        COMMAND "${CMAKE_COMMAND}"
            "-DSOCIETY_PHOTO_PLATFORM_FILE=${CMAKE_CURRENT_SOURCE_DIR}/cmake/NativePhotoPlatform.cmake"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/native_photo_platform.cmake")
    add_executable(SocietyNativeNavigationFocusTests tests/native_navigation_focus.cpp
        App/Society/Native/NativeNavigationViewModel.cpp "${society_native_navigation_view}")
    set_target_properties(SocietyNativeNavigationFocusTests PROPERTIES AUTOMOC OFF AUTOUIC OFF)
    target_include_directories(SocietyNativeNavigationFocusTests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/App/Society/Native" "${CMAKE_CURRENT_BINARY_DIR}")
    target_link_libraries(SocietyNativeNavigationFocusTests PRIVATE LVUI::Native iiGUI::iiGUI)
    add_test(NAME Society.NativeNavigationFocus COMMAND SocietyNativeNavigationFocusTests)
    add_executable(SocietyNativeWorkspaceViewModelTests tests/native_workspace_viewmodel.cpp)
    target_link_libraries(SocietyNativeWorkspaceViewModelTests PRIVATE SocietyNativeNavigationSurface LVUI::Native iiGUI::iiGUI)
    target_compile_features(SocietyNativeWorkspaceViewModelTests PRIVATE cxx_std_23)
    add_test(NAME Society.NativeWorkspaceViewModel COMMAND SocietyNativeWorkspaceViewModelTests)
    string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef _society_test_nonce)
    set(_society_state_root "${CMAKE_CURRENT_SOURCE_DIR}/resources/.state")
    set(_society_test_directory "${_society_state_root}/tests-${_society_test_nonce}")
    foreach(_action IN ITEMS validate setup cleanup)
        add_test(NAME Society.TestOutput_${_action} COMMAND "${CMAKE_COMMAND}"
            "-DRESOURCE_ROOT=${_society_state_root}" "-DOUTPUT_DIR=${_society_test_directory}"
            "-DACTION=${_action}" -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_output_fixture.cmake")
    endforeach()
    set_tests_properties(Society.TestOutput_setup PROPERTIES FIXTURES_SETUP societyTestOutput)
    set_tests_properties(Society.TestOutput_cleanup PROPERTIES FIXTURES_CLEANUP societyTestOutput)
    find_package(json-c CONFIG REQUIRED)
    add_executable(SocietyNativeModelChooserStateTests tests/native_model_chooser.cpp
        App/Society/Tools/NativeModelChooserState.cpp App/Society/Tools/NativeModelChooserState.h
        App/Society/Tools/NativeModelChooserProtocol.cpp App/Society/Tools/NativeModelChooserProtocol.h)
    target_include_directories(SocietyNativeModelChooserStateTests PRIVATE App)
    target_compile_features(SocietyNativeModelChooserStateTests PRIVATE cxx_std_23)
    target_link_libraries(SocietyNativeModelChooserStateTests PRIVATE json-c::json-c)
    add_test(NAME Society.NativeModelChooserState COMMAND SocietyNativeModelChooserStateTests)
    add_executable(SocietyServiceRegistrationPolicyTests tests/registration_policy.cpp)
    target_include_directories(SocietyServiceRegistrationPolicyTests PRIVATE App)
    target_compile_features(SocietyServiceRegistrationPolicyTests PRIVATE cxx_std_23)
    add_test(NAME Society.ServiceRegistrationPolicy COMMAND SocietyServiceRegistrationPolicyTests)
    add_executable(SocietyApplicationLifetimeTests tests/tst_applicationlifetime.cpp)
    set_target_properties(SocietyApplicationLifetimeTests PROPERTIES MACOSX_BUNDLE FALSE)
    target_link_libraries(SocietyApplicationLifetimeTests PRIVATE Qt6::Gui Qt6::Qml Qt6::Quick)
    add_test(NAME Society.ApplicationLifetime COMMAND SocietyApplicationLifetimeTests)
    set_tests_properties(Society.ApplicationLifetime PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 10)
    find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Test)
    if(TARGET SocietyWorkspaceAgentBackend)
        add_executable(SocietyResidentGatewayTests tests/tst_resident_gateway.cpp)
        set_target_properties(SocietyResidentGatewayTests PROPERTIES AUTOMOC ON)
        target_link_libraries(SocietyResidentGatewayTests PRIVATE SocietyWorkspaceAgentBackend)
        add_test(NAME Society.ResidentGateway COMMAND SocietyResidentGatewayTests)
        add_executable(SocietyWorkspaceAgentTests tests/tst_workspace_agent.cpp)
        set_target_properties(SocietyWorkspaceAgentTests PROPERTIES AUTOMOC ON)
        target_link_libraries(SocietyWorkspaceAgentTests PRIVATE iiFileProvider::Catalog SocietyWorkspaceAgentBackend Qt6::Test)
        target_compile_definitions(SocietyWorkspaceAgentTests PRIVATE SOCIETY_TEST_DIRECTORY="${_society_test_directory}")
        add_test(NAME Society.WorkspaceAgent COMMAND SocietyWorkspaceAgentTests)
        set_tests_properties(Society.WorkspaceAgent PROPERTIES FIXTURES_REQUIRED societyTestOutput TIMEOUT 120)
        add_test(NAME Society.SpaceAgent COMMAND SocietyWorkspaceAgentTests spaceSessionsAreIsolatedAndUseReasoningBudgets)
        set_tests_properties(Society.SpaceAgent PROPERTIES FIXTURES_REQUIRED societyTestOutput TIMEOUT 120)
        add_test(NAME Society.SpaceAgentAttachments COMMAND SocietyWorkspaceAgentTests spaceAcceptsSelectedExternalAttachments)
        set_tests_properties(Society.SpaceAgentAttachments PROPERTIES FIXTURES_REQUIRED societyTestOutput TIMEOUT 120)
        add_test(NAME Society.SpaceUnsupportedMedia COMMAND SocietyWorkspaceAgentTests spaceReportsUnsupportedImageModels)
        set_tests_properties(Society.SpaceUnsupportedMedia PROPERTIES FIXTURES_REQUIRED societyTestOutput TIMEOUT 120)
        add_test(NAME Society.ResidentModelRefresh COMMAND SocietyWorkspaceAgentTests relevantModelImportRefreshesUnavailableResident)
        set_tests_properties(Society.ResidentModelRefresh PROPERTIES FIXTURES_REQUIRED societyTestOutput TIMEOUT 120)
        add_test(NAME Society.WorkspaceAgentAttachment COMMAND SocietyWorkspaceAgentTests attachmentsReachTheModelAndStayInsideTheSelectedDrive)
        set_tests_properties(Society.WorkspaceAgentAttachment PROPERTIES FIXTURES_REQUIRED societyTestOutput TIMEOUT 120)
        add_executable(SocietySuggestedPolicyTests tests/tst_suggested_policy.cpp)
        target_include_directories(SocietySuggestedPolicyTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/App")
        target_link_libraries(SocietySuggestedPolicyTests PRIVATE iiFileProvider::Catalog SocietyWorkspaceAgentBackend)
        target_compile_features(SocietySuggestedPolicyTests PRIVATE cxx_std_23)
        add_test(NAME Society.SuggestedPolicy COMMAND SocietySuggestedPolicyTests)
        add_executable(SocietySuggestedCatalogTests tests/tst_suggested_catalog.cpp)
        set_target_properties(SocietySuggestedCatalogTests PROPERTIES AUTOMOC ON)
        target_link_libraries(SocietySuggestedCatalogTests PRIVATE iiFileProvider::Catalog SocietyWorkspaceAgentBackend Qt6::Test)
        target_compile_definitions(SocietySuggestedCatalogTests PRIVATE SOCIETY_TEST_DIRECTORY="${_society_test_directory}")
        add_test(NAME Society.SuggestedCatalog COMMAND SocietySuggestedCatalogTests)
        set_tests_properties(Society.SuggestedCatalog PROPERTIES FIXTURES_REQUIRED societyTestOutput TIMEOUT 300)
        add_executable(SocietySuggestedModelTests tests/tst_suggested_model.cpp)
        set_target_properties(SocietySuggestedModelTests PROPERTIES AUTOMOC ON)
        target_link_libraries(SocietySuggestedModelTests PRIVATE iiFileProvider::Catalog SocietyWorkspaceAgentBackend)
        add_test(NAME Society.SuggestedModel COMMAND SocietySuggestedModelTests)
        add_executable(SocietySuggestedBackendTests tests/tst_suggested_backend.cpp)
        set_target_properties(SocietySuggestedBackendTests PROPERTIES AUTOMOC ON)
        target_link_libraries(SocietySuggestedBackendTests PRIVATE iiFileProvider::Catalog SocietyWorkspaceAgentBackend Qt6::Test)
        target_compile_definitions(SocietySuggestedBackendTests PRIVATE SOCIETY_TEST_DIRECTORY="${_society_test_directory}")
        add_test(NAME Society.SuggestedBackend COMMAND SocietySuggestedBackendTests)
        set_tests_properties(Society.SuggestedBackend PROPERTIES FIXTURES_REQUIRED societyTestOutput TIMEOUT 120)
    endif()
    if(TARGET SocietyRecentBackend)
        add_executable(SocietyRecentCatalogTests tests/tst_recent_catalog.cpp)
        set_target_properties(SocietyRecentCatalogTests PROPERTIES AUTOMOC ON)
        target_include_directories(SocietyRecentCatalogTests BEFORE PRIVATE ${society_catalog_include})
        target_link_libraries(SocietyRecentCatalogTests PRIVATE SocietyRecentBackend Qt6::Test)
        target_compile_definitions(SocietyRecentCatalogTests PRIVATE SOCIETY_TEST_DIRECTORY="${_society_test_directory}")
        add_test(NAME Society.RecentCatalog COMMAND SocietyRecentCatalogTests)
        set_tests_properties(Society.RecentCatalog PROPERTIES FIXTURES_REQUIRED societyTestOutput TIMEOUT 300)
        add_executable(SocietyRecentBackendTests tests/tst_recent_backend.cpp)
        set_target_properties(SocietyRecentBackendTests PROPERTIES AUTOMOC ON)
        target_link_libraries(SocietyRecentBackendTests PRIVATE SocietyRecentBackend)
        add_test(NAME Society.RecentBackend COMMAND SocietyRecentBackendTests)
        set_tests_properties(Society.RecentBackend PROPERTIES TIMEOUT 120)
    endif()
    add_executable(SocietyResourceAdapterTests tests/tst_resource_adapter.cpp)
    set_target_properties(SocietyResourceAdapterTests PROPERTIES AUTOMOC ON)
    target_include_directories(SocietyResourceAdapterTests BEFORE PRIVATE ${society_catalog_include})
    target_link_libraries(SocietyResourceAdapterTests PRIVATE iiFileProvider::Catalog Qt6::Test
        iiSocietyContainer::iiSocietyContainer SocietyResourceAdapter)
    target_compile_definitions(SocietyResourceAdapterTests PRIVATE SOCIETY_TEST_DIRECTORY="${_society_test_directory}")
    add_test(NAME Society.ResourceAdapter COMMAND SocietyResourceAdapterTests)
    set_tests_properties(Society.ResourceAdapter PROPERTIES FIXTURES_REQUIRED societyTestOutput TIMEOUT 300)
    add_executable(SocietyNativeWorkspaceSurfaceTests tests/tst_native_workspace.cpp)
    set_target_properties(SocietyNativeWorkspaceSurfaceTests PROPERTIES AUTOMOC ON)
    target_link_libraries(SocietyNativeWorkspaceSurfaceTests PRIVATE SocietyNativeNavigationSurface Qt6::Test LVUI::Native iiGUI::iiGUI)
    target_compile_definitions(SocietyNativeWorkspaceSurfaceTests PRIVATE SOCIETY_TEST_DIRECTORY="${_society_test_directory}")
    add_test(NAME Society.NativeWorkspaceSurface COMMAND SocietyNativeWorkspaceSurfaceTests)
    set_tests_properties(Society.NativeWorkspaceSurface PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    add_executable(SocietyNativeSpaceTests tests/tst_native_space.cpp)
    set_target_properties(SocietyNativeSpaceTests PROPERTIES AUTOMOC ON)
    target_link_libraries(SocietyNativeSpaceTests PRIVATE SocietyNativeNavigationSurface Qt6::Test iiGUI::iiGUI iiSocietyContainer::FileTree)
    add_test(NAME Society.NativeSpace COMMAND SocietyNativeSpaceTests)
    set_tests_properties(Society.NativeSpace PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    add_executable(SocietyPaintDevicePixelsTests
        tests/qt_paint_device_pixels.cpp App/Society/Native/QtPaintDevice.cpp)
    target_include_directories(SocietyPaintDevicePixelsTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/App")
    target_link_libraries(SocietyPaintDevicePixelsTests PRIVATE Qt6::Gui iiGUI::iiGUI)
    add_test(NAME Society.PaintDevicePixels COMMAND SocietyPaintDevicePixelsTests)
    set_tests_properties(Society.PaintDevicePixels PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    qt_add_executable(SocietyPhotoTests tests/tst_photos.cpp)
    society_add_photo_qml(SocietyPhotoTests)
    target_link_libraries(SocietyPhotoTests PRIVATE iiPhotoLibrary::iiPhotoLibrary iiSocietyHelper::iiSocietyHelper Qt6::Test Qt6::Quick Qt6::QuickControls2)
    lvrs_configure_qml_app(SocietyPhotoTests NO_PLATFORM_RUNTIME_TARGETS)
    target_compile_definitions(SocietyPhotoTests PRIVATE SOCIETY_TEST_DIRECTORY="${_society_test_directory}"
        SOCIETY_PHOTOS_QML_FILE="${CMAKE_CURRENT_SOURCE_DIR}/tests/photos/PhotosHarness.qml"
        SOCIETY_LVRS_QML_IMPORT_PATH="${LVRS_QML_IMPORT_PATH}")
    add_test(NAME Society.Photos COMMAND SocietyPhotoTests)
    set_tests_properties(Society.Photos PROPERTIES TIMEOUT 300
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1")
    qt_add_executable(SocietyGroupStateTests tests/tst_groupstate.cpp)
    target_link_libraries(SocietyGroupStateTests PRIVATE SocietyGroupState)
    target_compile_definitions(SocietyGroupStateTests PRIVATE SOCIETY_TEST_DIRECTORY="${_society_test_directory}")
    add_test(NAME Society.GroupState COMMAND SocietyGroupStateTests)
    set_tests_properties(Society.GroupState PROPERTIES TIMEOUT 30)
    add_test(NAME Society.GroupStateOffline COMMAND SocietyGroupStateTests cryptoUsesFreshNoncesAndAuthenticatesContext)
    set_tests_properties(Society.GroupStateOffline PROPERTIES TIMEOUT 30)
    add_test(NAME Society.GroupStateRevocationOffline COMMAND SocietyGroupStateTests logoutInvalidatesPendingWriteFromAnotherStore)
    set_tests_properties(Society.GroupStateRevocationOffline PROPERTIES TIMEOUT 30)
    add_executable(SocietyCTestSupportTests tests/ctest_support.cpp)
    target_compile_features(SocietyCTestSupportTests PRIVATE cxx_std_23)
    add_test(NAME Society.CTestSupport COMMAND SocietyCTestSupportTests)
    add_executable(SocietyCTestEventWaitTests tests/ctest_event_wait.cpp)
    target_link_libraries(SocietyCTestEventWaitTests PRIVATE Qt6::Core)
    target_compile_features(SocietyCTestEventWaitTests PRIVATE cxx_std_23)
    add_test(NAME Society.CTestEventWait COMMAND SocietyCTestEventWaitTests)
    qt_add_executable(SocietyDiscoveryTests tests/tst_discovery.cpp)
    target_link_libraries(SocietyDiscoveryTests PRIVATE SocietyDiscovery Qt6::Test)
    add_test(NAME Society.Discovery COMMAND SocietyDiscoveryTests)
    set_tests_properties(Society.Discovery PROPERTIES TIMEOUT 45)
    find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Test)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME Society.GroupStateCTestContract COMMAND "${Python3_EXECUTABLE}" -B
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ctest_group_state.py")
    add_test(NAME Society.NativeEntryCTestContract COMMAND "${Python3_EXECUTABLE}" -B
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ctest_native_entry.py")
    add_test(NAME Society.DependenciesCTestContract COMMAND "${Python3_EXECUTABLE}" -B
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ctest_dependencies.py")
    add_test(NAME Society.JobAndSuggestionsCTestContract COMMAND "${Python3_EXECUTABLE}" -B
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ctest_job_and_suggestions.py")
    add_test(NAME Society.RecentGatewayNotificationCTestContract COMMAND "${Python3_EXECUTABLE}" -B
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ctest_recent_gateway_notification.py")
    if(APPLE AND TARGET SocietySuggestedCatalogTests AND TARGET SocietyDaemon)
        add_test(NAME Society.SuggestedCatalogAbi COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_suggested_symbol_binding.py"
            "$<TARGET_FILE:SocietyDaemon>" "$<TARGET_FILE:SocietySuggestedCatalogTests>")
    endif()
    if(APPLE AND NOT IOS AND SOCIETY_NATIVE_LVUI_DIR)
        if(LVUI_HOST_COMPILER)
            set(_society_lvui_host_compiler "${LVUI_HOST_COMPILER}")
        else()
            get_filename_component(_society_lvui_prefix "${SOCIETY_NATIVE_LVUI_DIR}/../../.." ABSOLUTE)
            set(_society_lvui_host_compiler "${_society_lvui_prefix}/bin/lvui-compile")
        endif()
        if(EXISTS "${_society_lvui_host_compiler}")
            add_test(NAME Society.NativeLVUISyntax COMMAND "${Python3_EXECUTABLE}" -B
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_lvui_native_syntax.py"
                --compiler "${_society_lvui_host_compiler}")
        endif()
    endif()
    add_test(NAME Society.TempFixturePolicy COMMAND "${Python3_EXECUTABLE}" -B
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_temp_fixture_policy.py")
    add_test(NAME Society.SourceLayout COMMAND "${Python3_EXECUTABLE}" -B
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_source_layout.py")
    set_tests_properties(Society.SourceLayout PROPERTIES TIMEOUT 300)
    add_test(NAME Society.PhotoLibraryPackaging COMMAND "${Python3_EXECUTABLE}" -B
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_photolibrary_integration.py")
    set_tests_properties(Society.PhotoLibraryPackaging PROPERTIES TIMEOUT 30)
    if(APPLE AND NOT IOS)
        add_test(NAME Society.MacDeploymentLayout COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/deploy_layout_test.py")
        set_tests_properties(Society.MacDeploymentLayout PROPERTIES TIMEOUT 60)
        add_test(NAME Society.MacMlxDeployment COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_mlx_deployment.py")
        set_tests_properties(Society.MacMlxDeployment PROPERTIES TIMEOUT 60)
        add_test(NAME Society.ContainerFormat COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_container_format.py")
        add_test(NAME Society.MacRuntimeDeployment COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_macos_runtime.py")
        add_test(NAME Society.BackendDependencies COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_backend_boundary.py")
        add_test(NAME Society.MacRuntimeLaunch COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_macos_runtime.py" "$<TARGET_BUNDLE_DIR:Society>" --gui ${society_lazy_runtime_arguments})
        add_test(NAME Society.MacBundleFootprint COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_macos_footprint.py" "$<TARGET_BUNDLE_DIR:Society>")
        add_executable(SocietyBundledResourcesTests tests/tst_bundled_resources.cpp)
        # The read-only resource probe must resolve Qt from the bundle itself.
        set_property(TARGET SocietyBundledResourcesTests PROPERTY LINK_LIBRARIES "")
        target_link_libraries(SocietyBundledResourcesTests PRIVATE Qt6::Core ${CMAKE_DL_LIBS})
        set_target_properties(SocietyBundledResourcesTests PROPERTIES
            BUILD_WITH_INSTALL_RPATH TRUE INSTALL_RPATH "$<TARGET_BUNDLE_DIR:Society>/Contents/Frameworks")
        add_dependencies(SocietyBundledResourcesTests Society)
        add_test(NAME Society.BundledResources COMMAND SocietyBundledResourcesTests
            "$<TARGET_BUNDLE_DIR:Society>" "${LVRS_QML_IMPORT_PATH}/LVRS")
        set_tests_properties(Society.MacRuntimeDeployment Society.MacRuntimeLaunch PROPERTIES TIMEOUT 120)
    endif()
    add_test(NAME Society.IosBuildContract COMMAND "${Python3_EXECUTABLE}" -B
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ios_build.py")
    add_test(NAME Society.SdkHeaderDependencies COMMAND "${Python3_EXECUTABLE}" -B
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_sdk_header_dependencies.py")
    add_test(NAME Society.AppIcons COMMAND "${Python3_EXECUTABLE}" -B
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_app_icons.py")
    add_test(NAME Society.AndroidTlsBuild COMMAND "${Python3_EXECUTABLE}" -B
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_android_openssl.py")
    qt_add_executable(SocietyCloudFilesTests tests/tst_cloud_files.cpp
        App/Society/Drive/CloudFilesController.cpp App/Society/Drive/CloudFilesController.h)
    target_link_libraries(SocietyCloudFilesTests PRIVATE Qt6::Test Qt6::Qml Qt6::Network Qt6::Concurrent OpenSSL::Crypto
        iiAccountManager::iiAccountManager iiSocietyContainer::iiSocietyContainer)
    target_compile_definitions(SocietyCloudFilesTests PRIVATE
        SOCIETY_TEST_DIRECTORY="${_society_test_directory}"
        SOCIETY_ACCOUNT_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/account.json")
    add_test(NAME Society.CloudFiles COMMAND SocietyCloudFilesTests)
    set_tests_properties(Society.CloudFiles PROPERTIES TIMEOUT 60 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    qt_add_executable(SocietyPairingTests tests/tst_pairing.cpp ${society_pairing_sources} ${society_account_sources}
        App/Society/Network/NetworkDriveController.cpp App/Society/Network/NetworkDriveController.h)
    target_include_directories(SocietyPairingTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/App")
    target_link_libraries(SocietyPairingTests PRIVATE iiAccountManager::Quick Qt6::Test Qt6::Quick Qt6::QuickControls2
        iiWebServer::iiWebServer iiAccountManager::iiAccountManager iiSocietyContainer::Gui)
    lvrs_configure_qml_app(SocietyPairingTests NO_PLATFORM_RUNTIME_TARGETS)
    target_compile_definitions(SocietyPairingTests PRIVATE SOCIETY_ACCOUNT_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/account.json" SOCIETY_DISABLE_SESSION_RESTORE SOCIETY_APP_VERSION="${PROJECT_VERSION}"
        SOCIETY_TEST_DIRECTORY="${_society_test_directory}"
        SOCIETY_PAIRING_QML_FILE="${CMAKE_CURRENT_SOURCE_DIR}/App/Society/Network/PairingPanel.qml"
        SOCIETY_LVRS_QML_IMPORT_PATH="${LVRS_QML_IMPORT_PATH}")
    if(APPLE)
        target_sources(SocietyPairingTests PRIVATE tests/DecodeQr.mm App/Society/Network/AppleQrDecoder.mm App/Society/Network/AppleQrDecoder.h)
        set_source_files_properties(tests/DecodeQr.mm App/Society/Network/AppleQrDecoder.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
        target_link_libraries(SocietyPairingTests PRIVATE "-framework CoreImage" "-framework Foundation" "-framework Vision" "-framework CoreVideo")
    endif()
    add_test(NAME Society.Pairing COMMAND SocietyPairingTests)
    set_tests_properties(Society.Pairing PROPERTIES TIMEOUT 180
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1")
    set(society_test_daemon "$<TARGET_FILE:SocietyDaemon>")
    if(APPLE AND NOT IOS)
        set(society_test_daemon "$<TARGET_BUNDLE_DIR:Society>/Contents/MacOS/SocietyDaemon")
    endif()
    qt_add_executable(SocietyAccountTests tests/tst_account.cpp
        App/Society/Network/NetworkDriveController.cpp App/Society/Network/NetworkDriveController.h)
    target_include_directories(SocietyAccountTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/App")
    target_compile_definitions(SocietyAccountTests PRIVATE SOCIETY_TEST_RESOURCES="${_society_test_directory}" SOCIETY_TEST_DIRECTORY="${_society_test_directory}")
    target_link_libraries(SocietyAccountTests PRIVATE Qt6::Test Qt6::Quick Qt6::QuickControls2
        iiAccountManager::iiAccountManager iiSocietyHelper::iiSocietyHelper
        iiWebServer::iiWebServer iiSocietyContainer::iiSocietyContainer)
    lvrs_configure_qml_app(SocietyAccountTests NO_PLATFORM_RUNTIME_TARGETS)
    target_compile_definitions(SocietyAccountTests PRIVATE
        SOCIETY_APP_VERSION="${PROJECT_VERSION}"
        SOCIETY_ACCOUNT_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/account.json"
        SOCIETY_ACCOUNT_QML_FILE="qrc:/qt/qml/iiAccountManager/AccountDialog.qml"
        SOCIETY_LVRS_QML_IMPORT_PATH="${LVRS_QML_IMPORT_PATH}")
    add_test(NAME Society.Account COMMAND SocietyAccountTests)
    target_compile_definitions(SocietyAccountTests PRIVATE SOCIETY_DAEMON_EXECUTABLE="${society_test_daemon}")
    add_dependencies(SocietyAccountTests SocietyDaemon Society)
    set_tests_properties(Society.Account PROPERTIES TIMEOUT 240
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1")
    qt_add_executable(SocietyContainerFileOpenTests tests/tst_containerfileopen.cpp)
    target_include_directories(SocietyContainerFileOpenTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/App")
    target_link_libraries(SocietyContainerFileOpenTests PRIVATE Qt6::Gui)
    add_test(NAME Society.ContainerFileOpen COMMAND SocietyContainerFileOpenTests)

    qt_add_executable(SocietyRuntimeTests tests/tst_runtime.cpp)
    target_link_libraries(SocietyRuntimeTests PRIVATE Qt6::Test SocietyServices)
    target_compile_definitions(SocietyRuntimeTests PRIVATE SOCIETY_TEST_DIRECTORY="${_society_test_directory}")
    add_test(NAME Society.MobileLifecycle COMMAND SocietyRuntimeTests)
    qt_add_executable(SocietySyncOwnershipTests tests/tst_syncownership.cpp)
    target_link_libraries(SocietySyncOwnershipTests PRIVATE Qt6::Test SocietyServices)
    target_compile_definitions(SocietySyncOwnershipTests PRIVATE SOCIETY_TEST_DIRECTORY="${_society_test_directory}")
    add_test(NAME Society.SyncOwnership COMMAND SocietySyncOwnershipTests)
    set_tests_properties(Society.SyncOwnership PROPERTIES TIMEOUT 15)
    qt_add_executable(SocietyNetworkTests tests/tst_networkdrive.cpp
        App/Society/Network/NetworkDriveController.cpp App/Society/Network/NetworkDriveController.h)
    target_include_directories(SocietyNetworkTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/App")
    target_link_libraries(SocietyNetworkTests PRIVATE Qt6::Test Qt6::Qml Qt6::Gui
        iiAccountManager::iiAccountManager iiWebServer::iiWebServer iiSocietyContainer::iiSocietyContainer)
    target_compile_definitions(SocietyNetworkTests PRIVATE SOCIETY_TEST_DIRECTORY="${_society_test_directory}"
        SOCIETY_ACCOUNT_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/account.json")
    add_test(NAME Society.NetworkDrive COMMAND SocietyNetworkTests)
    add_test(NAME Society.ContainerAccountIsolation COMMAND SocietyNetworkTests
        differentLocalContainerDoesNotJoinAccountSynchronization)
    set_tests_properties(Society.ContainerAccountIsolation PROPERTIES TIMEOUT 60)
    add_test(NAME Society.PhotoLibraryIntegration COMMAND SocietyNetworkTests photosUsesSdkAndKeepsLegacyDisableSwitch)
    set_tests_properties(Society.PhotoLibraryIntegration PROPERTIES TIMEOUT 60)
    set_tests_properties(Society.NetworkDrive PROPERTIES TIMEOUT 180)
    qt_add_executable(SocietyClientOnlyNetworkTests tests/tst_networkdrive_clientonly.cpp
        ${society_pairing_sources}
        App/Society/Network/NetworkDriveController.cpp App/Society/Network/NetworkDriveController.h)
    target_include_directories(SocietyClientOnlyNetworkTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/App")
    target_link_libraries(SocietyClientOnlyNetworkTests PRIVATE Qt6::Test Qt6::Quick Qt6::QuickControls2
        iiAccountManager::iiAccountManager iiWebServer::iiWebServer iiSocietyContainer::iiSocietyContainer)
    lvrs_configure_qml_app(SocietyClientOnlyNetworkTests NO_PLATFORM_RUNTIME_TARGETS)
    target_compile_definitions(SocietyClientOnlyNetworkTests PRIVATE SOCIETY_CLIENT_ONLY=1
        SOCIETY_ACCOUNT_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/account.json"
        SOCIETY_NETWORK_QML_FILE="${CMAKE_CURRENT_SOURCE_DIR}/App/Society/Network/NetworkDevices.qml"
        SOCIETY_PAIRING_QML_FILE="${CMAKE_CURRENT_SOURCE_DIR}/App/Society/Network/PairingPanel.qml"
        SOCIETY_PREFERENCES_QML_FILE="${CMAKE_CURRENT_SOURCE_DIR}/App/Society/Preferences/PreferencesWindow.qml"
        SOCIETY_LVRS_QML_IMPORT_PATH="${LVRS_QML_IMPORT_PATH}"
        SOCIETY_TEST_DIRECTORY="${_society_test_directory}")
    add_test(NAME Society.ClientOnlyNetwork COMMAND SocietyClientOnlyNetworkTests)
    set_tests_properties(Society.ClientOnlyNetwork PROPERTIES TIMEOUT 60
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1")
    set_tests_properties(Society.MobileLifecycle PROPERTIES TIMEOUT 30)
    qt_add_executable(SocietyDaemonTests tests/tst_daemon.cpp)
    target_include_directories(SocietyDaemonTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/App")
    target_link_libraries(SocietyDaemonTests PRIVATE Qt6::Test SocietyServices iiSocietyContainer::iiSocietyContainer)
    target_compile_definitions(SocietyDaemonTests PRIVATE SOCIETY_TEST_DIRECTORY="${_society_test_directory}"
        SOCIETY_DAEMON_EXECUTABLE="${society_test_daemon}"
        SOCIETY_EXECUTABLE_PATH="$<TARGET_FILE:Society>")
    add_dependencies(SocietyDaemonTests SocietyDaemon Society)
    add_test(NAME Society.Daemon COMMAND SocietyDaemonTests)
    set_tests_properties(Society.Daemon PROPERTIES TIMEOUT 45)
    add_test(NAME Society.DaemonHeadless COMMAND SocietyDaemonTests
        explicitPathsOutsideTheSelectedRuntimeAreRejectedBeforeWriting
        daemonMaintainsSelectedFolderWithoutAnAccount
        headlessServerRejectsUnsafeConfigurationBeforeStartingServices
        daemonIncludesUsableTlsAndSqliteBackends
        isolatedSyncServicePublishesStateWithoutAccessingAnAccount
        offlineQueueAndLateSocietyReceiver)
    set_tests_properties(Society.DaemonHeadless PROPERTIES TIMEOUT 45)
    if(UNIX)
        if(TARGET SocietyWorkspaceAgentBackend)
            add_executable(SocietyWorkspaceFileIntegrationTests tests/tst_workspace_file_integration.cpp
                App/Daemon/ModelBackendServer.cpp App/Daemon/ModelBackendServer.h
                App/Daemon/NativeDirectorySession.cpp App/Daemon/NativeDirectorySession.h
                App/Society/Files/ModelImporter.cpp App/Society/Files/ModelImporter.h
                ${society_backend_command_sources})
            set_target_properties(SocietyWorkspaceFileIntegrationTests PROPERTIES AUTOMOC ON)
            target_include_directories(SocietyWorkspaceFileIntegrationTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/App")
            target_link_libraries(SocietyWorkspaceFileIntegrationTests PRIVATE iiFileProvider::Catalog Qt6::Test Qt6::Quick Qt6::Network
                SocietyNativeNavigationSurface SocietyServices SocietyRecentBackend SocietyResourceAdapter
                SocietyWorkspaceAgentBackend iiSocietyContainer::iiSocietyContainer LVUI::Native iiGUI::iiGUI)
            target_compile_definitions(SocietyWorkspaceFileIntegrationTests PRIVATE SOCIETY_TEST_DIRECTORY="${_society_test_directory}")
            add_test(NAME Society.WorkspaceFileIntegration COMMAND SocietyWorkspaceFileIntegrationTests)
            set_tests_properties(Society.WorkspaceFileIntegration PROPERTIES TIMEOUT 600
                ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;SOCIETY_DISABLE_BACKEND_AUTOSTART=1")
        endif()
        qt_add_executable(SocietyBackendDaemonTests tests/tst_backend_daemon.cpp
            App/Daemon/ModelBackendServer.cpp App/Daemon/ModelBackendServer.h
            App/Daemon/NativeDirectorySession.cpp App/Daemon/NativeDirectorySession.h
            App/Society/Files/ModelImporter.cpp App/Society/Files/ModelImporter.h
            ${society_backend_command_sources})
        target_include_directories(SocietyBackendDaemonTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/App")
        target_link_libraries(SocietyBackendDaemonTests PRIVATE iiFileProvider::Catalog Qt6::Test Qt6::Gui Qt6::Qml Qt6::Network SocietyServices iiSocietyContainer::iiSocietyContainer)
        target_compile_definitions(SocietyBackendDaemonTests PRIVATE
            SOCIETY_TEST_DIRECTORY="${_society_test_directory}"
            SOCIETY_DAEMON_EXECUTABLE="$<TARGET_FILE:SocietyDaemon>")
        add_dependencies(SocietyBackendDaemonTests SocietyDaemon)
        if(TARGET SocietyCli AND Python3_EXECUTABLE)
            target_compile_definitions(SocietyBackendDaemonTests PRIVATE
                SOCIETY_CLI_EXECUTABLE="$<TARGET_FILE:SocietyCli>"
                SOCIETY_PYTHON_EXECUTABLE="${Python3_EXECUTABLE}"
                SOCIETY_SOURCE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}")
            add_dependencies(SocietyBackendDaemonTests SocietyCli)
        endif()
        add_test(NAME Society.BackendDaemon COMMAND SocietyBackendDaemonTests)
        if(TARGET SocietyWorkspaceAgentBackend)
            add_test(NAME Society.ResidentProtocol COMMAND SocietyBackendDaemonTests residentRequestsStayOnTheirOwnConnection)
            add_test(NAME Society.SpaceProtocol COMMAND SocietyBackendDaemonTests spaceProtocolChecksSessionAndContainer)
            add_test(NAME Society.SpaceClientIsolation COMMAND SocietyBackendDaemonTests oldSpaceRepliesCannotCrossSessionSelection)
        endif()
        set_tests_properties(Society.BackendDaemon PROPERTIES TIMEOUT 120)
        add_test(NAME Society.NativeDirectoryBackend COMMAND SocietyBackendDaemonTests
            nativeDirectorySessionsExposeBoundedIndependentSnapshots)
        set_tests_properties(Society.NativeDirectoryBackend PROPERTIES TIMEOUT 60)
        add_test(NAME Society.NativeDirectoryReplyCache COMMAND SocietyBackendDaemonTests
            nativeDirectoryReplyCacheIsBounded)
        add_test(NAME Society.LocalBackendPeerIdentity COMMAND SocietyBackendDaemonTests
            localPeerIdentityIsObtainedFromTheSocket)
        add_test(NAME Society.NativeDirectoryQuery COMMAND SocietyBackendDaemonTests
            nativeDirectoryQueriesAreRevisionBound)
        set_tests_properties(Society.NativeDirectoryQuery PROPERTIES TIMEOUT 120)
        add_test(NAME Society.NativeDirectoryActivate COMMAND SocietyBackendDaemonTests
            nativeDirectoryActivatesOnlyCurrentFiles)
        set_tests_properties(Society.NativeDirectoryActivate PROPERTIES TIMEOUT 120)
        add_test(NAME Society.NativeFileJobsBackend COMMAND SocietyBackendDaemonTests
            nativeFileJobsSurviveClientDisconnect)
        set_tests_properties(Society.NativeFileJobsBackend PROPERTIES TIMEOUT 60)
        if(TARGET SocietyCli AND Python3_EXECUTABLE)
            add_test(NAME Society.HeadlessFileCommands COMMAND SocietyBackendDaemonTests
                headlessMutationTimeoutRemainsUncertain headlessImportTimeoutRemainsUncertain headlessFileCommandsUseTheSelectedContainer)
            set_tests_properties(Society.HeadlessFileCommands PROPERTIES TIMEOUT 90)
            add_test(NAME Society.PythonFileClient COMMAND "${Python3_EXECUTABLE}" -B
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_society_client.py")
        endif()
        qt_add_executable(SocietyNativeFilesLauncherTests tests/tst_native_files_launcher.cpp
            App/Society/Native/NativeFilesLauncher.cpp App/Society/Native/NativeFilesLauncher.h)
        target_include_directories(SocietyNativeFilesLauncherTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/App")
        target_link_libraries(SocietyNativeFilesLauncherTests PRIVATE Qt6::Core)
        target_compile_definitions(SocietyNativeFilesLauncherTests PRIVATE SOCIETY_TEST_DIRECTORY="${_society_test_directory}")
        add_test(NAME Society.NativeFilesLauncher COMMAND SocietyNativeFilesLauncherTests)
    endif()
    qt_add_executable(SocietyGuiTests tests/tst_filegrid.cpp
        App/Society/Files/DirectoryLocation.cpp App/Society/Files/DirectoryLocation.h)
    target_include_directories(SocietyGuiTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/App")
    target_link_libraries(SocietyGuiTests PRIVATE Qt6::Test Qt6::Quick Qt6::QuickControls2 Qt6::Concurrent iiFileProvider::Native iiSocietyContainer::Gui SocietyFilePreviews iiSharedCanvas::Core)
    lvrs_configure_qml_app(SocietyGuiTests NO_PLATFORM_RUNTIME_TARGETS)
    target_compile_definitions(SocietyGuiTests PRIVATE
        SOCIETY_QML_FILE="${CMAKE_CURRENT_SOURCE_DIR}/tests/FileGridHarness.qml"
        SOCIETY_LVRS_QML_IMPORT_PATH="${LVRS_QML_IMPORT_PATH}"
        SOCIETY_TEST_DIRECTORY="${_society_test_directory}"
    )
    add_test(NAME Society.Gui COMMAND SocietyGuiTests)
    set_tests_properties(Society.Gui PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1"
        TIMEOUT 120
    )
    add_test(NAME Society.ModelsBrowser COMMAND SocietyGuiTests
        filesBrowserUsesNativeTableAndRealFiles modelsBrowserUsesFolderTreeAndRetainsModelActions)
    set_tests_properties(Society.ModelsBrowser PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1"
        TIMEOUT 120)

    qt_add_executable(SocietySystemDriveTests tests/tst_system_drive.cpp
        App/Daemon/SystemDriveCoordinator.cpp App/Daemon/SystemDriveCoordinator.h)
    target_include_directories(SocietySystemDriveTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/App")
    target_link_libraries(SocietySystemDriveTests PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME Society.SystemDriveCoordinator COMMAND SocietySystemDriveTests)
    qt_add_executable(SocietyDriveBackendTests tests/tst_system_drive_client.cpp
        App/Society/Drive/DriveController.cpp App/Society/Drive/DriveController.h
        ${society_native_files_launcher_sources}
       )
    target_include_directories(SocietyDriveBackendTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/App")
    target_link_libraries(SocietyDriveBackendTests PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Network Qt6::Concurrent
        Qt6::Test iiAccountManager::iiAccountManager iiSocietyContainer::iiSocietyContainer)
    target_compile_definitions(SocietyDriveBackendTests PRIVATE SOCIETY_TEST_DIRECTORY="${_society_test_directory}"
        SOCIETY_ACCOUNT_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/account.json")
    add_test(NAME Society.SystemDriveBackendClient COMMAND SocietyDriveBackendTests)
    add_test(NAME Society.ContainerAccountSelection COMMAND SocietyDriveBackendTests
        selectedContainerOpensIndependentlyOfAccountRegistration)
    set_tests_properties(Society.SystemDriveBackendClient PROPERTIES TIMEOUT 60)
    set_tests_properties(Society.ContainerAccountSelection PROPERTIES TIMEOUT 60)
    if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
        add_test(NAME Society.SystemDriveHostPath COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_system_drive_host.py"
            "$<TARGET_FILE:SocietyDaemon>" "${SOCIETY_NATIVE_DRIVE_BUNDLE}" "$<TARGET_FILE:Society>")
    endif()

    find_program(SOCIETY_MODEL_MERGE_TEST_PYTHON NAMES python python3
        HINTS "${society_merge_sdk_prefix}/share/iiLocalDiffusion/reference/diffusers/.venv/bin"
        "$ENV{HOME}/.local/SDK/iiLocalDiffusion/share/iiLocalDiffusion/reference/diffusers/.venv/bin"
        NO_DEFAULT_PATH)
    if(NOT SOCIETY_MODEL_MERGE_TEST_PYTHON)
        set(SOCIETY_MODEL_MERGE_TEST_PYTHON "${Python3_EXECUTABLE}")
    endif()
    qt_add_executable(SocietyModelCatalogTests tests/tst_modelcatalog.cpp
        App/Society/Tools/MergeModelCatalog.cpp App/Society/Tools/MergeModelCatalog.h)
    target_include_directories(SocietyModelCatalogTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/App")
    target_link_libraries(SocietyModelCatalogTests PRIVATE Qt6::Test Qt6::Concurrent Qt6::QmlIntegration
        iiSocietyContainer::iiSocietyContainer)
    target_compile_definitions(SocietyModelCatalogTests PRIVATE SOCIETY_TEST_DIRECTORY="${_society_test_directory}")
    add_test(NAME Society.ModelCatalog COMMAND SocietyModelCatalogTests)
    set_tests_properties(Society.ModelCatalog PROPERTIES TIMEOUT 60)
    if(APPLE AND NOT IOS)
        add_test(NAME Society.ModelCatalogBoundary COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_modelcatalog_dependencies.py"
            "$<TARGET_FILE:SocietyModelCatalogTests>")
        add_test(NAME Society.ComponentRuntimeBoundary COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_component_runtime.py"
            "$<TARGET_FILE:SocietyDiscoveryTests>" "$<TARGET_FILE:iiSharedCanvas::Core>")
        add_test(NAME Society.ClientRuntimeBoundary COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_component_runtime.py"
            "$<TARGET_FILE:SocietyClientOnlyNetworkTests>" "$<TARGET_FILE:iiSharedCanvas::Core>")
        set_tests_properties(Society.ClientRuntimeBoundary PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    endif()
    qt_add_executable(SocietyModelMergeTests tests/tst_modelmerge.cpp ${society_model_merge_sources})
    target_include_directories(SocietyModelMergeTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/App")
    target_link_libraries(SocietyModelMergeTests PRIVATE Qt6::Test Qt6::Quick Qt6::QuickControls2 Qt6::Concurrent iiSocietyContainer::iiSocietyContainer iiFileProvider::Temporary)
    lvrs_configure_qml_app(SocietyModelMergeTests NO_PLATFORM_RUNTIME_TARGETS)
    target_compile_definitions(SocietyModelMergeTests PRIVATE
        SOCIETY_TEST_DIRECTORY="${_society_test_directory}"
        SOCIETY_MERGE_TEST_PYTHON="${SOCIETY_MODEL_MERGE_TEST_PYTHON}"
        SOCIETY_MERGE_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/model_merge_fixture.py"
        SOCIETY_MERGE_QML_FILE="${CMAKE_CURRENT_SOURCE_DIR}/tests/ModelMergeHarness.qml"
        SOCIETY_LVRS_QML_IMPORT_PATH="${LVRS_QML_IMPORT_PATH}")
    add_test(NAME Society.ModelMerge COMMAND SocietyModelMergeTests)
    set_tests_properties(Society.ModelMerge PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1"
        TIMEOUT 600)
    add_test(NAME Society.ModelMergeCatalogCompatibility COMMAND SocietyModelMergeTests
        catalogListsOnlyContainerModelsAndKeepsPackagesWhole
        wrappedCheckpointsAppearOnceAndResolveForExecution
        qmlModelMenusRefreshScrollAndResetWithContainer)
    set_tests_properties(Society.ModelMergeCatalogCompatibility PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1;SOCIETY_MERGE_CATALOG_ONLY=1"
        TIMEOUT 180)

    if(TARGET iiLocalDiffusion::ModelPackaging)
        qt_add_executable(SocietyModelPackagingTests tests/tst_modelpackaging.cpp ${society_model_packaging_sources}
            App/Society/Tools/MergeModelCatalog.cpp App/Society/Tools/MergeModelCatalog.h)
        target_include_directories(SocietyModelPackagingTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/App")
        target_link_libraries(SocietyModelPackagingTests PRIVATE SocietyModelPackaging Qt6::Test Qt6::Quick Qt6::QuickControls2 Qt6::Concurrent iiSocietyContainer::iiSocietyContainer iiFileProvider::Temporary)
        lvrs_configure_qml_app(SocietyModelPackagingTests NO_PLATFORM_RUNTIME_TARGETS)
        target_compile_definitions(SocietyModelPackagingTests PRIVATE
            SOCIETY_TEST_DIRECTORY="${_society_test_directory}"
            SOCIETY_PACKAGING_QML_FILE="${CMAKE_CURRENT_SOURCE_DIR}/tests/ModelPackagingHarness.qml"
            SOCIETY_LVRS_QML_IMPORT_PATH="${LVRS_QML_IMPORT_PATH}")
        add_test(NAME Society.ModelPackaging COMMAND SocietyModelPackagingTests)
        set_tests_properties(Society.ModelPackaging PROPERTIES TIMEOUT 120
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1")
    endif()

    qt_add_executable(SocietyCalendarTests tests/tst_calendar.cpp ${society_calendar_sources})
    target_include_directories(SocietyCalendarTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/App")
    target_link_libraries(SocietyCalendarTests PRIVATE Qt6::Test Qt6::Qml Qt6::Concurrent iiCalendarIntegration::iiCalendarIntegration iiSocietyContainer::iiSocietyContainer iiFileProvider::Native SQLite3::SQLite3)
    target_compile_definitions(SocietyCalendarTests PRIVATE SOCIETY_TEST_DIRECTORY="${_society_test_directory}")
    add_test(NAME Society.Calendar COMMAND SocietyCalendarTests)
    set_tests_properties(Society.Calendar PROPERTIES TIMEOUT 300)

    qt_add_executable(SocietyProjectTests tests/tst_projects.cpp ${society_project_sources})
    target_include_directories(SocietyProjectTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/App")
    target_include_directories(SocietyProjectTests BEFORE PRIVATE ${society_catalog_include})
    target_link_libraries(SocietyProjectTests PRIVATE Qt6::Test Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Concurrent iiSocietyContainer::iiSocietyContainer
        iiFileProvider::Catalog iiProjectManagerFileProvider::Provider SocietyProjectRepository)
    lvrs_configure_qml_app(SocietyProjectTests NO_PLATFORM_RUNTIME_TARGETS)
    target_compile_definitions(SocietyProjectTests PRIVATE SOCIETY_TEST_DIRECTORY="${_society_test_directory}"
        SOCIETY_PROJECTS_QML_FILE="${CMAKE_CURRENT_SOURCE_DIR}/tests/ProjectsHarness.qml"
        SOCIETY_LVRS_QML_IMPORT_PATH="${LVRS_QML_IMPORT_PATH}")
    add_test(NAME Society.Projects COMMAND SocietyProjectTests)
    set_tests_properties(Society.Projects PROPERTIES TIMEOUT 600
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1")
    if(TARGET SocietyWorkspaceAddBackend)
        qt_add_executable(SocietyWorkspaceAddTests tests/tst_workspace_add.cpp)
        target_link_libraries(SocietyWorkspaceAddTests PRIVATE Qt6::Test SocietyWorkspaceAddBackend)
        target_compile_definitions(SocietyWorkspaceAddTests PRIVATE SOCIETY_TEST_DIRECTORY="${_society_test_directory}")
        add_test(NAME Society.WorkspaceAdd COMMAND SocietyWorkspaceAddTests)
        set_tests_properties(Society.WorkspaceAdd PROPERTIES TIMEOUT 600)
    endif()
    qt_add_executable(SocietySemanticRelationsTests tests/tst_semantic_relations.cpp)
    target_include_directories(SocietySemanticRelationsTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/App")
    target_include_directories(SocietySemanticRelationsTests BEFORE PRIVATE ${society_catalog_include})
    target_link_libraries(SocietySemanticRelationsTests PRIVATE Qt6::Test Qt6::Core iiSocietyContainer::iiSocietyContainer
        iiFileProvider::Catalog iiFileProvider::Objects iiProjectManagerFileProvider::Provider SocietyProjectRepository)
    target_compile_definitions(SocietySemanticRelationsTests PRIVATE SOCIETY_TEST_DIRECTORY="${_society_test_directory}")
    add_test(NAME Society.SemanticRelations COMMAND SocietySemanticRelationsTests)
    set_tests_properties(Society.SemanticRelations PROPERTIES TIMEOUT 1200)
    add_test(NAME Society.ProjectResourceIdentity COMMAND SocietyProjectTests
        managedReferencesSurviveRenameAndDoNotDuplicate repeatedPackageCopyDoesNotDuplicateTheSameInput
        changedDuplicateRequiresEditingExistingResource staleCatalogReferenceDoesNotOpenAnUnregisteredReplacement
        externallyMovedFileResolvesAfterCatalogReconciliation explicitlyEditingAnUnboundReferenceCanBindItToTheCatalog)
    set_tests_properties(Society.ProjectResourceIdentity PROPERTIES TIMEOUT 300
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1")

    qt_add_executable(SocietyDriveTests tests/tst_drive.cpp ${society_notification_sources} ${society_environment_sources} ${society_calendar_sources} ${society_project_sources} ${society_job_sources} ${society_model_merge_sources} ${society_model_packaging_sources} ${society_model_calibrator_sources}
        ${society_native_files_launcher_sources}
        App/Society/Drive/CloudFilesController.cpp App/Society/Drive/CloudFilesController.h
        ${society_pairing_sources}
        App/Society/Models/StorageModels.cpp App/Society/Models/StorageModels.h
        App/Society/Dashboard/DashboardFiles.cpp App/Society/Dashboard/DashboardFiles.h
        App/Society/Network/NetworkDriveController.cpp App/Society/Network/NetworkDriveController.h
        App/Society/Files/ModelImporter.cpp App/Society/Files/ModelImporter.h
        App/Society/Drive/DriveController.cpp App/Society/Drive/DriveController.h
        App/Society/Drive/StorageNavigation.cpp App/Society/Drive/StorageNavigation.h
        App/Society/Files/DirectoryLocation.cpp App/Society/Files/DirectoryLocation.h)
    if(APPLE AND NOT IOS)
        target_sources(SocietyDriveTests PRIVATE tests/NativeTitleBarGeometry.mm)
    endif()
    society_add_photo_qml(SocietyDriveTests)
    target_include_directories(SocietyDriveTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/App")
    target_include_directories(SocietyDriveTests BEFORE PRIVATE ${society_provider_include})
    target_link_libraries(SocietyDriveTests PRIVATE iiFileProvider::Temporary iiFileProvider::Native SQLite3::SQLite3 iiCalendarIntegration::iiCalendarIntegration Qt6::Test Qt6::Quick Qt6::QuickControls2
        Qt6::Concurrent
        iiWebServer::iiWebServer iiAccountManager::iiAccountManager iiSocietyContainer::iiSocietyContainer iiJob::iiJob)
    lvrs_configure_qml_app(SocietyDriveTests NO_PLATFORM_RUNTIME_TARGETS)
    target_link_libraries(SocietyDriveTests PRIVATE iiSocietyContainer::Gui)
    target_link_libraries(SocietyDriveTests PRIVATE SocietyProjectRepository)
    target_link_libraries(SocietyDriveTests PRIVATE SocietyModelPackaging)
    target_link_libraries(SocietyDriveTests PRIVATE SocietyNativeNavigationSurface)
    target_compile_definitions(SocietyDriveTests PRIVATE
        SOCIETY_ACCOUNT_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/account.json"
        SOCIETY_QML_FILE="${CMAKE_CURRENT_SOURCE_DIR}/App/Society/Main.qml"
        SOCIETY_LVRS_QML_IMPORT_PATH="${LVRS_QML_IMPORT_PATH}"
        SOCIETY_TEST_DIRECTORY="${_society_test_directory}")
    add_test(NAME Society.Drive COMMAND SocietyDriveTests)
    add_test(NAME Society.ModelCardObject COMMAND SocietyDriveTests modelCardRenameUpdatesFileCompanionsAndView)
    set_tests_properties(Society.ModelCardObject PROPERTIES TIMEOUT 300
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1")
    add_test(NAME Society.ModelsTree COMMAND SocietyDriveTests modelsBrowsePhysicalFoldersInListAndGrid)
    set_tests_properties(Society.ModelsTree PROPERTIES TIMEOUT 300
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1")
    add_test(NAME Society.StorageModelNavigation COMMAND SocietyDriveTests
        storageSectionsUseFullHeightWithoutCommonFooter driveWindowOpensFilesAndNavigatesSections)
    set_tests_properties(Society.StorageModelNavigation PROPERTIES TIMEOUT 300
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1")
    add_test(NAME Society.DashboardSidebar COMMAND SocietyDriveTests
        dashboardSidebarMatchesFigmaAndRoutesWorkspaceTargets)
    set_tests_properties(Society.DashboardSidebar PROPERTIES TIMEOUT 300
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1")
    add_test(NAME Society.DashboardProjectsNavigation COMMAND SocietyDriveTests
        dashboardSidebarOpensCalendarAndAccountMemberships)
    set_tests_properties(Society.DashboardProjectsNavigation PROPERTIES TIMEOUT 60
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1")
    add_test(NAME Society.ToolbarSegmentAppearance COMMAND SocietyDriveTests
        globalToolbarShowsOnlyTheSelectedSegmentBackground)
    set_tests_properties(Society.ToolbarSegmentAppearance PROPERTIES TIMEOUT 240
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1")
    add_test(NAME Society.NotificationPopoverIntegration COMMAND SocietyDriveTests
        notificationCenterIntegratesWithExistingPopover)
    set_tests_properties(Society.NotificationPopoverIntegration PROPERTIES TIMEOUT 60
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1;SOCIETY_DISABLE_BACKEND_AUTOSTART=1;IILOCALLLM_DISABLE_APP_MCP=1")
    add_test(NAME Society.SpaceNavigation COMMAND SocietyDriveTests spaceSegmentOpensContainerAndRestoresTab)
    set_tests_properties(Society.SpaceNavigation PROPERTIES TIMEOUT 120
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1")
    add_test(NAME Society.TasksNavigation COMMAND SocietyDriveTests
        tasksSegmentOpensExistingTools globalToolbarMatchesFigmaAndRoutesActions)
    set_tests_properties(Society.TasksNavigation PROPERTIES TIMEOUT 300
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1")
    add_test(NAME Society.CalendarWorkspace COMMAND SocietyDriveTests
        calendarWorkspaceRoutesEditsAndRendersFiveDesigns)
    set_tests_properties(Society.CalendarWorkspace PROPERTIES TIMEOUT 300
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1")
    add_test(NAME Society.MobileOfflineShell COMMAND SocietyDriveTests
        mobileShellOpensWithoutAccountContainerOrHost)
    set_tests_properties(Society.MobileOfflineShell PROPERTIES TIMEOUT 90
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1")
    add_test(NAME Society.QuickGenerate COMMAND SocietyDriveTests
        quickGenerateUsesLvrsComposer mobileViewsShareDesktopContentAndKeepState
        dashboardKeepsStorageAndPromptState)
    set_tests_properties(Society.QuickGenerate PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1"
        TIMEOUT 180)

    add_test(NAME Society.PhotosNavigation COMMAND SocietyDriveTests
        filesStartEmptyAndPhotosShowsImagesAndVideos mobileStorageKeepsSectionsActionsAndGalleryReachable)
    set_tests_properties(Society.PhotosNavigation PROPERTIES TIMEOUT 120
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1")
    set_tests_properties(Society.Drive PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1"
        TIMEOUT 300)

    qt_add_executable(SocietyModelImportTests tests/tst_modelimporter.cpp
        App/Society/Files/ModelImporter.cpp App/Society/Files/ModelImporter.h ${society_job_sources})
    target_include_directories(SocietyModelImportTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/App")
    target_link_libraries(SocietyModelImportTests PRIVATE Qt6::Test Qt6::Qml Qt6::Gui iiSocietyContainer::iiSocietyContainer iiFileProvider::Temporary iiFileProvider::Native iiJob::iiJob)
    target_compile_definitions(SocietyModelImportTests PRIVATE SOCIETY_TEST_DIRECTORY="${_society_test_directory}")
    add_test(NAME Society.ModelImport COMMAND SocietyModelImportTests)
    set_tests_properties(Society.ModelImport PROPERTIES TIMEOUT 600)
    add_test(NAME Society.ModelMetadataImport COMMAND SocietyModelImportTests importsLocalModelWithItsProviderMetadata)
    set_tests_properties(Society.ModelMetadataImport PROPERTIES TIMEOUT 180)
    add_test(NAME Society.ModelCategorySynchronization COMMAND SocietyModelImportTests
        recategorizesModelsAndPreservesManagedIdentity categoryMoveWaitsForIndexAndMovesCompanionsTogether
        categoryMoveDoesNotInheritAnEarlierModelsMetadata
        categoryMoveCanBeCancelledWhileWaitingForIndex categoryMoveRetriesAfterLongRepositoryContention
        categoryMoveRecordsChangedContentBeforeMoving reconcilesAnAlreadyMovedModelWithoutLosingIdentity)
    set_tests_properties(Society.ModelCategorySynchronization PROPERTIES TIMEOUT 240)

    qt_add_executable(SocietyJobStorageTests tests/tst_jobstorage.cpp App/Society/Jobs/JobStorage.cpp App/Society/Jobs/JobStorage.h)
    target_include_directories(SocietyJobStorageTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/App")
    target_link_libraries(SocietyJobStorageTests PRIVATE iiSocietyContainer::iiSocietyContainer iiFileProvider::Native iiFileProvider::Temporary iiJob::iiJob)
    target_compile_definitions(SocietyJobStorageTests PRIVATE SOCIETY_TEST_DIRECTORY="${_society_test_directory}")
    add_test(NAME Society.JobStorage COMMAND SocietyJobStorageTests)

    qt_add_executable(SocietyModelCategoryMenuTests tests/tst_model_category_menu.cpp)
    target_link_libraries(SocietyModelCategoryMenuTests PRIVATE iiSocietyContainer::Gui Qt6::Test Qt6::Quick Qt6::QuickControls2)
    lvrs_configure_qml_app(SocietyModelCategoryMenuTests NO_PLATFORM_RUNTIME_TARGETS)
    target_compile_definitions(SocietyModelCategoryMenuTests PRIVATE
        SOCIETY_CATEGORY_MENU_HARNESS="${CMAKE_CURRENT_SOURCE_DIR}/tests/ModelCategoryMenuHarness.qml"
        SOCIETY_LVRS_QML_IMPORT_PATH="${LVRS_QML_IMPORT_PATH}")
    add_test(NAME Society.ModelCategoryMenu COMMAND SocietyModelCategoryMenuTests)
    set_tests_properties(Society.ModelCategoryMenu PROPERTIES TIMEOUT 120
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1")

    if(APPLE)
        qt_add_executable(SocietyAppleModelSourceTests tests/tst_applemodelsource.mm
            App/Society/Files/AppleModelSource.mm App/Society/Files/AppleModelSource.h
            App/Society/Files/ModelImporter.cpp App/Society/Files/ModelImporter.h ${society_job_sources})
        target_include_directories(SocietyAppleModelSourceTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/App")
        target_link_libraries(SocietyAppleModelSourceTests PRIVATE Qt6::Test Qt6::Qml Qt6::Gui
            iiSocietyContainer::iiSocietyContainer iiFileProvider::Temporary iiFileProvider::Native iiJob::iiJob "-framework Foundation")
        target_compile_options(SocietyAppleModelSourceTests PRIVATE "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
        target_compile_definitions(SocietyAppleModelSourceTests PRIVATE SOCIETY_TEST_DIRECTORY="${_society_test_directory}")
        add_test(NAME Society.AppleModelSource COMMAND SocietyAppleModelSourceTests)
        set_tests_properties(Society.AppleModelSource PROPERTIES TIMEOUT 120)

        # Command Line Tools can type-check UIKit via Mac Catalyst without an iOS SDK.
        # This is a compile check of the drop delegate, not an iOS link or device test.
        execute_process(COMMAND xcrun --sdk macosx --show-sdk-path
            OUTPUT_VARIABLE society_macos_sdk OUTPUT_STRIP_TRAILING_WHITESPACE)
        set(society_uikit_frameworks "${society_macos_sdk}/System/iOSSupport/System/Library/Frameworks")
        if(EXISTS "${society_uikit_frameworks}/UIKit.framework/Headers")
            set(society_drop_include_args)
            foreach(society_qt_module Core Gui Qml QmlIntegration Network Concurrent)
                get_target_property(society_qt_includes Qt6::${society_qt_module} INTERFACE_INCLUDE_DIRECTORIES)
                foreach(society_qt_include IN LISTS society_qt_includes)
                    list(APPEND society_drop_include_args "-I${society_qt_include}")
                endforeach()
            endforeach()
            foreach(society_drop_dependency iiSocietyContainer::iiSocietyContainer iiAccountManager::iiAccountManager iiFileProvider::Temporary iiFileProvider::Native iiJob::iiJob)
                get_target_property(society_dependency_includes ${society_drop_dependency} INTERFACE_INCLUDE_DIRECTORIES)
                foreach(society_dependency_include IN LISTS society_dependency_includes)
                    list(APPEND society_drop_include_args "-I${society_dependency_include}")
                endforeach()
            endforeach()
            get_filename_component(society_native_package_root "${iiSocietyContainer_NATIVE_APP}" DIRECTORY)
            list(APPEND society_drop_include_args "-I${society_native_package_root}/platform/ios")
            get_filename_component(society_qt_frameworks "${Qt6_DIR}/../.." ABSOLUTE)
            add_test(NAME Society.IosDropSyntax COMMAND "${CMAKE_OBJCXX_COMPILER}"
                -fsyntax-only -x objective-c++ -std=c++23 -fobjc-arc
                -target "${CMAKE_SYSTEM_PROCESSOR}-apple-ios16.0-macabi"
                -isysroot "${society_macos_sdk}"
                -iframework "${society_uikit_frameworks}"
                -isystem "${society_macos_sdk}/System/iOSSupport/usr/include"
                -iframework "${society_qt_frameworks}" ${society_drop_include_args}
                "${CMAKE_CURRENT_SOURCE_DIR}/App/Society/Files/IosModelDrop.mm"
                "${CMAKE_CURRENT_SOURCE_DIR}/App/Society/Files/IosFileAccess.mm"
                "${CMAKE_CURRENT_SOURCE_DIR}/App/Society/Files/ModelImporter.cpp"
                "${CMAKE_CURRENT_SOURCE_DIR}/App/Society/Drive/DriveController.cpp")
            set_tests_properties(Society.IosDropSyntax PROPERTIES TIMEOUT 60)
        endif()
    endif()

    # App-relative QML imports use the same resource aliases in every GUI fixture.
    foreach(qml_consumer SocietyDriveTests SocietyModelMergeTests SocietyModelPackagingTests SocietyClientOnlyNetworkTests)
        if(TARGET ${qml_consumer})
            qt_add_resources(${qml_consumer} SocietyAppQml PREFIX "/qt/qml/Society"
                FILES ${_app_source_resources})
        endif()
    endforeach()
    find_package(OpenSSL REQUIRED COMPONENTS Crypto)
    if(UNIX AND NOT IOS AND NOT ANDROID)
        qt_add_executable(SocietyLocalLicenseTests tests/tst_local_license.cpp ${society_account_sources})
        target_include_directories(SocietyLocalLicenseTests PRIVATE "${CMAKE_SOURCE_DIR}/App" "${CMAKE_SOURCE_DIR}/tests")
        target_link_libraries(SocietyLocalLicenseTests PRIVATE Qt6::Test Qt6::Network iiAccountManager::iiAccountManager iiSocietyHelper::iiSocietyHelper OpenSSL::Crypto)
        target_compile_definitions(SocietyLocalLicenseTests PRIVATE SOCIETY_HEADLESS SOCIETY_DISABLE_SESSION_RESTORE SOCIETY_APP_VERSION="${PROJECT_VERSION}" SOCIETY_ACCOUNT_FIXTURE="${CMAKE_SOURCE_DIR}/tests/fixtures/account.json" SOCIETY_TEST_RESOURCES="${_society_test_directory}")
        set_target_properties(SocietyLocalLicenseTests PROPERTIES MACOSX_BUNDLE FALSE)
        find_package(iiAccountManagerNative CONFIG QUIET HINTS "${CMAKE_SOURCE_DIR}/../../SDK/iiAccountManager/build/native-society/stage/lib/cmake/iiAccountManagerNative" "$ENV{HOME}/.local/SDK/iiAccountManager")
        if(iiAccountManagerNative_FOUND AND iiAccountManagerNative_HAS_SOCIETY_SESSION)
            add_executable(SocietyLocalClientProbe tests/local_license_client.cpp)
            target_link_libraries(SocietyLocalClientProbe PRIVATE iiAccountManager::Native OpenSSL::Crypto)
            target_compile_features(SocietyLocalClientProbe PRIVATE cxx_std_23)
            target_compile_definitions(SocietyLocalLicenseTests PRIVATE SOCIETY_LOCAL_CLIENT_PROBE="$<TARGET_FILE:SocietyLocalClientProbe>")
            add_dependencies(SocietyLocalLicenseTests SocietyLocalClientProbe)
        endif()
        add_test(NAME Society.LocalLicense COMMAND SocietyLocalLicenseTests)
        set_tests_properties(Society.LocalLicense PROPERTIES TIMEOUT 60)
        add_test(NAME Society.LocalLicenseInMemory COMMAND SocietyLocalLicenseTests
            daemonBrokerUsesStoredSessionWithoutGuiAndSeesLogout
            sameUserReceivesOnlySignedDeviceBoundLicense rejectsSharedDirectory)
        set_tests_properties(Society.LocalLicenseInMemory PROPERTIES TIMEOUT 60)
    endif()
    foreach(license_fixture_consumer SocietyAccountTests SocietyPairingTests SocietyNetworkTests SocietyClientOnlyNetworkTests SocietyDriveTests SocietyDriveBackendTests)
        target_link_libraries(${license_fixture_consumer} PRIVATE OpenSSL::Crypto)
    endforeach()
    foreach(account_consumer SocietyAccountTests SocietyNetworkTests SocietyClientOnlyNetworkTests SocietyDriveTests)
        target_sources(${account_consumer} PRIVATE ${society_account_sources})
        target_link_libraries(${account_consumer} PRIVATE iiAccountManager::Quick)
        if(APPLE AND NOT IOS)
            target_link_options(${account_consumer} BEFORE PRIVATE "LINKER:-rpath,$<TARGET_FILE_DIR:iiAccountManager::iiAccountManager>")
        endif()
        target_compile_definitions(${account_consumer} PRIVATE SOCIETY_DISABLE_SESSION_RESTORE SOCIETY_APP_VERSION="${PROJECT_VERSION}")
    endforeach()
    if(APPLE AND NOT IOS)
        # Network/Drive integration also loads the generation SDK. Its native
        # dependencies need the same explicit search paths as app packaging.
        foreach(photo_integration_test SocietyNetworkTests SocietyClientOnlyNetworkTests SocietyDriveTests)
            set_property(TARGET ${photo_integration_test} APPEND PROPERTY BUILD_RPATH "${SOCIETY_MAC_RUNTIME_SEARCH_PATHS}")
        endforeach()
    endif()
    foreach(discovery_consumer SocietyPairingTests SocietyAccountTests SocietyNetworkTests SocietyClientOnlyNetworkTests SocietyDriveTests)
        target_link_libraries(${discovery_consumer} PRIVATE iiPhotoLibrary::iiPhotoLibrary SocietyDiscovery SocietyGroupState iiSocietyHelper::iiSocietyHelper)
    endforeach()

    qt_add_executable(SocietyDependencyTests tests/tst_dependencies.cpp)
    target_link_libraries(SocietyDependencyTests PRIVATE iiSocietyHelper::iiSocietyHelper iiWebServer::iiWebServer)
    target_compile_definitions(SocietyDependencyTests PRIVATE
        SOCIETY_TEST_DIRECTORY="${_society_test_directory}"
        SOCIETY_DIRECT_LINKS_FILE="${CMAKE_CURRENT_BINARY_DIR}/SocietyDirectLinks-$<CONFIG>.txt"
        SOCIETY_EXECUTABLE_PATH="$<TARGET_FILE:Society>")
    add_dependencies(SocietyDependencyTests Society)
    set_target_properties(SocietyDependencyTests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin"
        QT_QML_MODULE_NO_IMPORT_SCAN TRUE
    )
    add_test(NAME Society.Dependencies COMMAND SocietyDependencyTests)
    add_test(NAME Society.DependenciesOffline COMMAND SocietyDependencyTests fileProjectionHidesProviderControlFiles)
    set_tests_properties(Society.Dependencies Society.DependenciesOffline PROPERTIES
        ENVIRONMENT_MODIFICATION "DYLD_LIBRARY_PATH=unset:;DYLD_FALLBACK_LIBRARY_PATH=unset:"
        TIMEOUT 120
    )
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

# Native inference belongs to the controllers that can actually host jobs.
# Discovery and the client-only build must not acquire the inference runtime.
if(TARGET iiSocietyHelper::GenerationHost)
    foreach(host_consumer Society SocietyDaemon SocietyPairingTests SocietyAccountTests SocietyNetworkTests SocietyDriveTests)
        if(TARGET ${host_consumer})
            target_link_libraries(${host_consumer} PRIVATE iiSocietyHelper::GenerationHost)
        endif()
    endforeach()
endif()

# Running desktop apps publish authenticated same-user MCP endpoints.
if(UNIX AND NOT IOS AND NOT ANDROID AND NOT EMSCRIPTEN)
    if(TARGET SocietyDaemon)
        target_sources(SocietyDaemon PRIVATE App/Daemon/DaemonMcp.cpp App/Daemon/DaemonMcp.h App/Daemon/UiToolBridge.cpp App/Daemon/UiToolBridge.h
            App/Society/Mcp/FileJobTools.cpp App/Society/Mcp/FileJobTools.h
            App/Society/Mcp/ResourceTools.cpp App/Society/Mcp/ResourceTools.h
            App/Society/Mcp/SemanticTools.cpp App/Society/Mcp/SemanticTools.h ${society_backend_command_sources})
        target_link_libraries(SocietyDaemon PRIVATE iiMultimodalAgent::iiMultimodalAgent SocietyProjectRepository)
        target_compile_definitions(SocietyDaemon PRIVATE SOCIETY_DAEMON_MCP=1)
    endif()
    target_sources(Society PRIVATE App/Society/Mcp/SocietyMcp.cpp App/Society/Mcp/SocietyMcp.h
        App/Society/Mcp/UiBridgeRegistration.cpp App/Society/Mcp/UiBridgeRegistration.h)
    target_link_libraries(Society PRIVATE iiMultimodalAgent::iiMultimodalAgent)
    if(APPLE AND NOT IOS)
        include(cmake/NativeQuestionPresenter.cmake)
    endif()
    set_source_files_properties("${IIMULTIMODALAGENT_QML_DIR}/UserQuestionsSheet.qml" PROPERTIES QT_RESOURCE_ALIAS "UserQuestionsSheet.qml")
    qt_add_resources(Society AgentQuestionUi PREFIX "/iiLocalLLM"
        FILES "${IIMULTIMODALAGENT_QML_DIR}/UserQuestionsSheet.qml")
    target_compile_definitions(Society PRIVATE SOCIETY_WITH_LOCAL_MCP=1)
    if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
        qt_add_executable(SocietyFileJobToolTests tests/tst_file_job_tools.cpp
            App/Society/Mcp/FileJobTools.cpp App/Society/Mcp/FileJobTools.h)
        target_include_directories(SocietyFileJobToolTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/App")
        target_link_libraries(SocietyFileJobToolTests PRIVATE Qt6::Test iiMultimodalAgent::iiMultimodalAgent)
        add_test(NAME Society.FileJobTools COMMAND SocietyFileJobToolTests)
        qt_add_executable(SocietyDaemonMcpTests tests/tst_daemon_mcp.cpp
            App/Daemon/DaemonMcp.cpp App/Daemon/DaemonMcp.h App/Daemon/UiToolBridge.cpp App/Daemon/UiToolBridge.h
            App/Daemon/ModelBackendServer.cpp App/Daemon/ModelBackendServer.h
            App/Daemon/NativeDirectorySession.cpp App/Daemon/NativeDirectorySession.h
            App/Society/Mcp/FileJobTools.cpp App/Society/Mcp/FileJobTools.h
            App/Society/Mcp/ResourceTools.cpp App/Society/Mcp/ResourceTools.h
            App/Society/Mcp/SemanticTools.cpp App/Society/Mcp/SemanticTools.h
            ${society_backend_command_sources})
        target_include_directories(SocietyDaemonMcpTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/App")
        target_link_libraries(SocietyDaemonMcpTests PRIVATE iiFileProvider::Catalog Qt6::Test Qt6::Network
            iiMultimodalAgent::iiMultimodalAgent iiSocietyContainer::iiSocietyContainer SocietyProjectRepository)
        target_compile_definitions(SocietyDaemonMcpTests PRIVATE SOCIETY_TEST_DIRECTORY="${_society_test_directory}"
            SOCIETY_APP_VERSION="${PROJECT_VERSION}" SOCIETY_DAEMON_EXECUTABLE="$<TARGET_FILE:SocietyDaemon>")
        add_dependencies(SocietyDaemonMcpTests SocietyDaemon)
        add_test(NAME Society.DaemonMcp COMMAND SocietyDaemonMcpTests)
        set_tests_properties(Society.DaemonMcp PROPERTIES TIMEOUT 180)
        qt_add_executable(SocietyUiBridgeTests tests/tst_ui_bridge.cpp
            App/Daemon/DaemonMcp.cpp App/Daemon/DaemonMcp.h App/Daemon/UiToolBridge.cpp App/Daemon/UiToolBridge.h
            App/Daemon/ModelBackendServer.cpp App/Daemon/ModelBackendServer.h
            App/Daemon/NativeDirectorySession.cpp App/Daemon/NativeDirectorySession.h
            App/Society/Mcp/UiBridgeRegistration.cpp App/Society/Mcp/UiBridgeRegistration.h
            App/Society/Mcp/FileJobTools.cpp App/Society/Mcp/FileJobTools.h
            App/Society/Mcp/ResourceTools.cpp App/Society/Mcp/ResourceTools.h
            App/Society/Mcp/SemanticTools.cpp App/Society/Mcp/SemanticTools.h
            ${society_backend_command_sources})
        target_include_directories(SocietyUiBridgeTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/App")
        target_link_libraries(SocietyUiBridgeTests PRIVATE iiFileProvider::Catalog Qt6::Test Qt6::Network Qt6::Concurrent
            iiMultimodalAgent::iiMultimodalAgent iiSocietyContainer::iiSocietyContainer SocietyProjectRepository)
        target_compile_definitions(SocietyUiBridgeTests PRIVATE SOCIETY_TEST_DIRECTORY="${_society_test_directory}"
            SOCIETY_APP_VERSION="${PROJECT_VERSION}")
        add_test(NAME Society.UiBridge COMMAND SocietyUiBridgeTests)
        set_tests_properties(Society.UiBridge PROPERTIES TIMEOUT 180)
        # Existing UI tests must not advertise fixtures to the user's live agent.
        get_property(_existing_app_tests DIRECTORY PROPERTY TESTS)
        set_property(TEST ${_existing_app_tests} APPEND PROPERTY ENVIRONMENT "IILOCALLLM_DISABLE_APP_MCP=1")
        qt_add_executable(SocietyMcpTests tests/tst_mcp.cpp)
        target_link_libraries(SocietyMcpTests PRIVATE Qt6::Test iiMultimodalAgent::iiMultimodalAgent iiSocietyContainer::iiSocietyContainer)
        if(TARGET mlx AND SOCIETY_MLX_RUNTIME_DIR)
            target_link_libraries(SocietyMcpTests PRIVATE mlx)
        endif()
        target_compile_definitions(SocietyDependencyTests PRIVATE SOCIETY_TEST_WITH_LOCAL_MCP=1)
        target_compile_definitions(SocietyMcpTests PRIVATE
            MCP_APP_EXECUTABLE="$<TARGET_FILE:Society>"
            MCP_TEST_DIRECTORY="${_society_test_directory}")
        add_dependencies(SocietyMcpTests Society)
        set(SOCIETY_MCP_TEST_GGUF "" CACHE FILEPATH "Optional pinned Qwen2.5 0.5B Q4_K_M for real app-control inference")
        if(SOCIETY_MCP_TEST_GGUF)
            target_compile_definitions(SocietyMcpTests PRIVATE SOCIETY_MCP_TEST_GGUF="${SOCIETY_MCP_TEST_GGUF}")
        endif()
        add_test(NAME Society.Mcp COMMAND SocietyMcpTests)
        set_tests_properties(Society.Mcp PROPERTIES TIMEOUT 90)
    endif()
endif()

include(cmake/NativeAccountPlatform.cmake)
society_native_account_enabled(_society_native_account)
if(_society_native_account)
    include(cmake/NativeAccountPresenter.cmake)
endif()
include(cmake/NativePhotoPlatform.cmake)
society_native_photo_enabled(_society_native_photo)
if(_society_native_photo)
    include(cmake/NativePhotoPresenter.cmake)
endif()

# All general tests may construct a NetworkDriveController or launch Society.
if(TARGET SocietyDependencyTests)
    file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/SocietyDirectLinks-$<CONFIG>.txt"
        CONTENT "$<JOIN:$<TARGET_PROPERTY:Society,LINK_LIBRARIES>,\n>\n")
endif()
# Only the dedicated photo fixture is allowed to activate its injected library.
if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    get_property(society_registered_tests DIRECTORY PROPERTY TESTS)
    set(_society_fixture_tests ${society_registered_tests})
    list(REMOVE_ITEM _society_fixture_tests Society.TestOutput_validate Society.TestOutput_setup Society.TestOutput_cleanup)
    set_property(TEST ${_society_fixture_tests} APPEND PROPERTY FIXTURES_REQUIRED societyTestOutput)
    # Test the linked SDK versions, independent of an interactive shell's
    # installed-library overrides (which can load an older LVRS QML module).
    set_property(TEST ${society_registered_tests} APPEND PROPERTY ENVIRONMENT_MODIFICATION
        "DYLD_LIBRARY_PATH=unset:" "DYLD_FRAMEWORK_PATH=unset:" "DYLD_FALLBACK_LIBRARY_PATH=unset:")
    if(APPLE AND EXISTS "${OPENSSL_CRYPTO_LIBRARY}")
        # Standalone tests also load SDK binaries with an absolute Crypto ID.
        # Limit the override to the selected Crypto directory, keeping Qt and
        # SDK resolution tied to the linked packages.
        get_filename_component(_society_test_crypto_dir "${OPENSSL_CRYPTO_LIBRARY}" DIRECTORY)
        set(_society_test_crypto_dirs "${_society_test_crypto_dir}")
        foreach(_society_crypto_path IN LISTS SOCIETY_MAC_RUNTIME_SEARCH_PATHS)
            if(EXISTS "${_society_crypto_path}/libcrypto.3.dylib")
                list(APPEND _society_test_crypto_dirs "${_society_crypto_path}")
            endif()
        endforeach()
        list(REMOVE_DUPLICATES _society_test_crypto_dirs)
        list(JOIN _society_test_crypto_dirs ":" _society_test_crypto_search)
        set_property(TEST ${society_registered_tests} APPEND PROPERTY ENVIRONMENT_MODIFICATION
            "DYLD_LIBRARY_PATH=set:${_society_test_crypto_search}")
    endif()
    set_property(TEST ${society_registered_tests} APPEND PROPERTY ENVIRONMENT "SOCIETY_TEST_OUTPUT_DIR=${_society_test_directory}")
    set_property(TEST ${society_registered_tests} APPEND PROPERTY ENVIRONMENT "SOCIETY_JOB_STATE_ROOT=${_society_test_directory}/jobs")
    set_property(TEST ${society_registered_tests} APPEND PROPERTY ENVIRONMENT "SOCIETY_DISABLE_SESSION_RESTORE=1")
    if(APPLE AND TARGET iiFileProvider::Native)
        # A transitive SDK can otherwise resolve an older compatible install ID
        # before the package selected for this build. Keep calendar verification
        # tied to the same native storage binary as the application.
        set_property(TEST Society.Calendar Society.CalendarWorkspace APPEND PROPERTY ENVIRONMENT_MODIFICATION
            "DYLD_LIBRARY_PATH=path_list_prepend:$<TARGET_FILE_DIR:iiFileProvider::Native>")
    endif()
    set_property(TEST ${society_registered_tests} APPEND PROPERTY ENVIRONMENT "IISOCIETYSYNC_DISABLE_BLE=1")
    set_property(TEST ${society_registered_tests} APPEND PROPERTY ENVIRONMENT "SOCIETY_DISABLE_NATIVE_DRIVE=1")
    list(REMOVE_ITEM society_registered_tests Society.Photos)
    set_property(TEST ${society_registered_tests} APPEND PROPERTY ENVIRONMENT "SOCIETY_DISABLE_PHOTOS=1")
endif()

# Share the asynchronous client implementation across the app and test hosts.
foreach(_society_add_host SocietyWorkspaceFileIntegrationTests SocietyBackendDaemonTests SocietyDaemonMcpTests SocietyUiBridgeTests)
    if(TARGET ${_society_add_host} AND TARGET SocietyWorkspaceAddBackend)
        target_link_libraries(${_society_add_host} PRIVATE SocietyWorkspaceAddBackend)
    endif()
endforeach()
foreach(_society_client_target Society SocietyBackendDaemonTests SocietyDriveBackendTests SocietyDriveTests SocietyModelImportTests SocietyAppleModelSourceTests)
    if(TARGET ${_society_client_target})
        target_link_libraries(${_society_client_target} PRIVATE SocietyModelBackendClient)
    endif()
endforeach()
if(TARGET SocietyRecentBackend)
    foreach(_society_recent_host SocietyBackendDaemonTests SocietyDaemonMcpTests SocietyUiBridgeTests)
        if(TARGET ${_society_recent_host})
            target_link_libraries(${_society_recent_host} PRIVATE SocietyRecentBackend)
        endif()
    endforeach()
endif()
foreach(_society_resource_host SocietyBackendDaemonTests SocietyDaemonMcpTests SocietyUiBridgeTests)
    if(TARGET ${_society_resource_host})
        target_link_libraries(${_society_resource_host} PRIVATE SocietyResourceAdapter)
    endif()
endforeach()
if(TARGET SocietyWorkspaceAgentBackend)
    foreach(_society_backend_target SocietyBackendDaemonTests SocietyDaemonMcpTests SocietyUiBridgeTests)
        if(TARGET ${_society_backend_target})
            target_link_libraries(${_society_backend_target} PRIVATE SocietyWorkspaceAgentBackend)
        endif()
    endforeach()
    if(APPLE)
        get_target_property(_society_catalog_type iiFileProvider::Catalog TYPE)
        if(_society_catalog_type STREQUAL "SHARED_LIBRARY")
            # CMake may reorder transitive libraries after direct dependencies.
            # An early, needed dylib keeps old agent-exported symbols from
            # capturing a newer catalog object's destructor on Mach-O.
            foreach(_society_catalog_host SocietyDaemon SocietyWorkspaceAgentTests
                    SocietySuggestedPolicyTests SocietySuggestedCatalogTests
                    SocietySuggestedModelTests SocietySuggestedBackendTests
                    SocietyRecentCatalogTests SocietyRecentBackendTests
                    SocietyResourceAdapterTests
                    SocietyBackendDaemonTests SocietyDaemonMcpTests SocietyUiBridgeTests SocietyWorkspaceFileIntegrationTests)
                if(TARGET ${_society_catalog_host})
                    target_link_options(${_society_catalog_host} PRIVATE
                        "LINKER:-needed_library,$<TARGET_FILE:iiFileProvider::Catalog>")
                endif()
            endforeach()
        endif()
    endif()
endif()

# The Windows backend uses the peer process token to reject another user's
# local named pipe, in addition to QLocalServer's user-only access control.
if(WIN32)
    foreach(_society_ipc_target Society SocietyDaemon SocietyDriveBackendTests SocietyDriveTests SocietyModelImportTests)
        if(TARGET ${_society_ipc_target})
            target_link_libraries(${_society_ipc_target} PRIVATE advapi32)
        endif()
    endforeach()
endif()

# Apply after Qt/LVRS finalizers, which may enable bundles for test targets.
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/single-app/SingleApp.cmake")
product_plain_executables("${CMAKE_SOURCE_DIR}" Society)
cmake_language(DEFER CALL product_single_app Society "bin")

if(WIN32 AND BUILD_TESTING)
    get_property(_society_tests DIRECTORY PROPERTY TESTS)
    foreach(_society_test IN LISTS _society_tests)
        get_property(_test_environment TEST ${_society_test} PROPERTY ENVIRONMENT)
        list(TRANSFORM _test_environment REPLACE "QT_QPA_PLATFORM=offscreen" "QT_QPA_PLATFORM=windows")
        set_tests_properties(${_society_test} PROPERTIES ENVIRONMENT "${_test_environment}")
    endforeach()
endif()

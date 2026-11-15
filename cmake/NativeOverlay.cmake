if(NOT TARGET SocietyNativeOverlay)
    add_library(SocietyNativeOverlay STATIC
        App/Society/Native/NativeOverlayCoordinator.h App/Society/Native/NativeOverlayCoordinator.cpp
        App/Society/Native/NativeModalPresence.h App/Society/Native/NativeModalPresence.cpp
        App/Society/Native/ExternalOverlaySurface.h App/Society/Native/ExternalOverlaySurface.cpp)
    set_target_properties(SocietyNativeOverlay PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_compile_features(SocietyNativeOverlay PUBLIC cxx_std_23)
    target_include_directories(SocietyNativeOverlay PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/App")
    target_link_libraries(SocietyNativeOverlay PUBLIC iiGUI::iiGUI Qt6::Core Qt6::Gui)
endif()
target_link_libraries(Society PRIVATE SocietyNativeOverlay)

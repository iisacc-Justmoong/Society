# Qt 6.8's imported desktop OpenGL target still lists AGL, which current
# macOS SDKs no longer ship. The retained frameworks provide the APIs used by
# Qt and Society. Do not apply this compatibility fix to iOS or non-Apple.
if(APPLE AND NOT IOS)
    foreach(_society_target WrapOpenGL::WrapOpenGL Qt6::Gui)
        if(TARGET ${_society_target})
            foreach(_society_property INTERFACE_LINK_LIBRARIES INTERFACE_LINK_OPTIONS)
                get_target_property(_society_links ${_society_target} ${_society_property})
                if(_society_links)
                    list(FILTER _society_links EXCLUDE REGEX "(^|[ /])AGL($|[ /.])|AGL\\.framework")
                    set_property(TARGET ${_society_target} PROPERTY ${_society_property} "${_society_links}")
                endif()
            endforeach()
        endif()
    endforeach()
endif()

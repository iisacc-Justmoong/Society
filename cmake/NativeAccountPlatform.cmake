# The account overlay uses iiGUI's external NSView, UIView or HWND surface.
# Other platforms retain the isolated legacy view until they have a host.
function(society_native_account_enabled result)
    if(WIN32 OR APPLE)
        set(${result} TRUE PARENT_SCOPE)
    else()
        set(${result} FALSE PARENT_SCOPE)
    endif()
endfunction()

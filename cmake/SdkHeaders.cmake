# Xcode uses -MMD: imported SDK headers marked -isystem are omitted from
# dependency files. A changed SDK class layout must rebuild every consumer.
# Keep vendor/toolchain headers as system headers; our installed SDKs change
# alongside the product and must participate in normal dependency tracking.
function(society_track_sdk_headers)
    get_property(imported DIRECTORY PROPERTY IMPORTED_TARGETS)
    foreach(dependency IN LISTS imported)
        if(dependency MATCHES "^(ii|LVRS)")
            set_property(TARGET "${dependency}" PROPERTY SYSTEM FALSE)
        endif()
    endforeach()
endfunction()
cmake_language(DEFER CALL society_track_sdk_headers)

# Package the daemon runtime in a development app copy. The GUI retains its SDK links.
if(NOT IS_DIRECTORY "${SOCIETY_SOURCE_APP}" OR
   NOT SOCIETY_PACKAGE_DIRECTORY MATCHES "/build/package$")
    message(FATAL_ERROR "Society packaging requires an app bundle and build/package output directory")
endif()
set(bundle "${SOCIETY_PACKAGE_DIRECTORY}/Society.app")
file(REMOVE_RECURSE "${bundle}")
file(MAKE_DIRECTORY "${SOCIETY_PACKAGE_DIRECTORY}")
file(COPY "${SOCIETY_SOURCE_APP}" DESTINATION "${SOCIETY_PACKAGE_DIRECTORY}")
set(helper "${bundle}/Contents/Helpers/SocietyDaemon.app")
get_filename_component(qt_bin "${SOCIETY_MACDEPLOYQT}" DIRECTORY)
get_filename_component(qt_prefix "${qt_bin}" DIRECTORY)
set(sqlite "${helper}/Contents/PlugIns/sqldrivers/libqsqlite.dylib")
set(tls "${helper}/Contents/PlugIns/tls/libqsecuretransportbackend.dylib")
file(MAKE_DIRECTORY "${helper}/Contents/PlugIns/sqldrivers" "${helper}/Contents/PlugIns/tls")
file(COPY_FILE "${qt_prefix}/plugins/sqldrivers/libqsqlite.dylib" "${sqlite}")
file(COPY_FILE "${qt_prefix}/plugins/tls/libqsecuretransportbackend.dylib" "${tls}")
# Resolve the original graph before relocation. Dependencies such as curl's
# Brotli decoder use their own @rpath, which macdeployqt does not discover alone.
file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES "${SOCIETY_SOURCE_APP}/Contents/Helpers/SocietyDaemon.app/Contents/MacOS/SocietyDaemon"
    RESOLVED_DEPENDENCIES_VAR source_dependencies
    UNRESOLVED_DEPENDENCIES_VAR missing_dependencies
    POST_EXCLUDE_REGEXES "^/System/" "^/usr/lib/")
if(missing_dependencies)
    message(FATAL_ERROR "Cannot resolve the daemon runtime: ${missing_dependencies}")
endif()
set(runtime_search_paths)
foreach(dependency IN LISTS source_dependencies)
    get_filename_component(directory "${dependency}" DIRECTORY)
    list(APPEND runtime_search_paths "-libpath=${directory}")
endforeach()
list(REMOVE_DUPLICATES runtime_search_paths)
# The helper owns its runtime; the outer GUI keeps a single existing Qt runtime.
execute_process(COMMAND "${SOCIETY_MACDEPLOYQT}" "${helper}" "-executable=${sqlite}" "-executable=${tls}"
    ${runtime_search_paths} -no-plugins -always-overwrite -no-strip
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
file(WRITE "${SOCIETY_PACKAGE_DIRECTORY}/macdeployqt.log" "${output}\n${errors}")
if(NOT result EQUAL 0 OR "${output}\n${errors}" MATCHES "ERROR:")
    message(FATAL_ERROR "macdeployqt failed: ${result}; see build/package/macdeployqt.log")
endif()
# macdeployqt can omit native-only @rpath children even with a search path.
# Complete that graph from CMake's resolved source libraries before signing.
foreach(dependency IN LISTS source_dependencies)
    if(dependency MATCHES "\\.dylib$")
        get_filename_component(name "${dependency}" NAME)
        if(NOT EXISTS "${helper}/Contents/Frameworks/${name}")
            file(COPY_FILE "${dependency}" "${helper}/Contents/Frameworks/${name}")
        endif()
    endif()
endforeach()
# A transitive SDK can carry an older iiSocietyContainer in its own rpath.
# Preserve the classifier selected by CMake, even if macdeployqt found that copy first.
if(NOT EXISTS "${SOCIETY_CONTAINER_LIBRARY}")
    message(FATAL_ERROR "The selected iiSocietyContainer runtime is missing")
endif()
get_filename_component(container_name "${SOCIETY_CONTAINER_LIBRARY}" NAME)
set(container_runtime "${helper}/Contents/Frameworks/${container_name}")
file(COPY_FILE "${SOCIETY_CONTAINER_LIBRARY}" "${container_runtime}")
execute_process(COMMAND install_name_tool
    -id "@rpath/${container_name}"
    -change "@rpath/QtCore.framework/Versions/A/QtCore" "@loader_path/../Frameworks/QtCore.framework/Versions/A/QtCore"
    "${container_runtime}" COMMAND_ERROR_IS_FATAL ANY)
# Sign inside out, including the complete nested helper before the outer GUI.
file(GLOB runtime_libraries "${helper}/Contents/Frameworks/*.framework" "${helper}/Contents/Frameworks/*.dylib")
foreach(runtime IN LISTS runtime_libraries)
    if(runtime MATCHES "\\.dylib$")
        execute_process(COMMAND install_name_tool
            -change "@rpath/${container_name}" "@loader_path/${container_name}"
            "${runtime}" COMMAND_ERROR_IS_FATAL ANY)
    endif()
    execute_process(COMMAND codesign --force --sign "${SOCIETY_SIGN_IDENTITY}" --timestamp=none "${runtime}" COMMAND_ERROR_IS_FATAL ANY)
endforeach()
execute_process(COMMAND codesign --force --sign "${SOCIETY_SIGN_IDENTITY}" --timestamp=none "${sqlite}" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND codesign --force --sign "${SOCIETY_SIGN_IDENTITY}" --timestamp=none "${tls}" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND codesign --force --sign "${SOCIETY_SIGN_IDENTITY}" --timestamp=none
    --entitlements "${SOCIETY_ENTITLEMENTS}" "${helper}" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND codesign --force --sign "${SOCIETY_SIGN_IDENTITY}" --timestamp=none
    --entitlements "${SOCIETY_ENTITLEMENTS}" "${bundle}" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND codesign --verify --strict "${bundle}" COMMAND_ERROR_IS_FATAL ANY)

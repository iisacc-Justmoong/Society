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
file(MAKE_DIRECTORY "${helper}/Contents/PlugIns/sqldrivers")
file(COPY_FILE "${qt_prefix}/plugins/sqldrivers/libqsqlite.dylib" "${sqlite}")
# The helper owns its runtime; the outer GUI keeps a single existing Qt runtime.
execute_process(COMMAND "${SOCIETY_MACDEPLOYQT}" "${helper}" "-executable=${sqlite}"
    -no-plugins -always-overwrite -no-strip
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
file(WRITE "${SOCIETY_PACKAGE_DIRECTORY}/macdeployqt.log" "${output}\n${errors}")
if(NOT result EQUAL 0 OR "${output}\n${errors}" MATCHES "ERROR:")
    message(FATAL_ERROR "macdeployqt failed: ${result}; see build/package/macdeployqt.log")
endif()
# Sign inside out, including the complete nested helper before the outer GUI.
file(GLOB runtime_libraries "${helper}/Contents/Frameworks/*.framework" "${helper}/Contents/Frameworks/*.dylib")
foreach(runtime IN LISTS runtime_libraries)
    execute_process(COMMAND codesign --force --sign "${SOCIETY_SIGN_IDENTITY}" --timestamp=none "${runtime}" COMMAND_ERROR_IS_FATAL ANY)
endforeach()
execute_process(COMMAND codesign --force --sign "${SOCIETY_SIGN_IDENTITY}" --timestamp=none "${sqlite}" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND codesign --force --sign "${SOCIETY_SIGN_IDENTITY}" --timestamp=none "${helper}" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND codesign --force --sign "${SOCIETY_SIGN_IDENTITY}" --timestamp=none "${bundle}" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND codesign --verify --strict "${bundle}" COMMAND_ERROR_IS_FATAL ANY)

# Society's post-build step already stages and signs both complete runtimes.
if(NOT IS_DIRECTORY "${SOCIETY_SOURCE_APP}" OR
   NOT SOCIETY_PACKAGE_DIRECTORY MATCHES "/build/package$")
    message(FATAL_ERROR "Society packaging requires an app bundle and build/package output directory")
endif()
set(bundle "${SOCIETY_PACKAGE_DIRECTORY}/Society.app")
file(REMOVE_RECURSE "${bundle}")
file(MAKE_DIRECTORY "${SOCIETY_PACKAGE_DIRECTORY}")
file(COPY "${SOCIETY_SOURCE_APP}" DESTINATION "${SOCIETY_PACKAGE_DIRECTORY}")
execute_process(COMMAND codesign --verify --deep --strict "${bundle}" COMMAND_ERROR_IS_FATAL ANY)

# The canonical build bundle is already deployed and signed. Never copy it.
if(NOT IS_DIRECTORY "${SOCIETY_SOURCE_APP}")
    message(FATAL_ERROR "Society packaging requires the canonical app bundle")
endif()
execute_process(COMMAND codesign --verify --deep --strict "${SOCIETY_SOURCE_APP}"
    COMMAND_ERROR_IS_FATAL ANY)

# Apply the product's distribution contract to iiSDKBuilder's app-tree hook.
# The hook is deferred until after this project is configured.  Preserve its
# existing exclusions because independently configured apps are built separately.
function(society_configure_app_distribution)
    if(NOT IISDKBUILDER_APP_TREE_ENTRYPOINTS_ONLY)
        return()
    endif()

    set(required_apps Society Daemon Uninstaller)
    set(excluded_apps ${IISDKBUILDER_APP_TREE_EXCLUDE})
    foreach(name IN LISTS required_apps)
        set(directory "${CMAKE_SOURCE_DIR}/App/${name}")
        if(NOT IS_DIRECTORY "${directory}" OR
           (NOT EXISTS "${directory}/main.cpp" AND NOT EXISTS "${directory}/CMakeLists.txt"))
            message(FATAL_ERROR "Required Society distribution app is missing: App/${name}")
        endif()
        # Builder excludes standalone projects from the integrated hook because
        # it builds those projects separately; an excluded direct entry vanishes.
        if(name IN_LIST excluded_apps AND NOT EXISTS "${directory}/CMakeLists.txt")
            message(FATAL_ERROR "Required Society distribution app is excluded: App/${name}")
        endif()
    endforeach()

    file(GLOB children CONFIGURE_DEPENDS LIST_DIRECTORIES TRUE "${CMAKE_SOURCE_DIR}/App/*")
    foreach(child IN LISTS children)
        if(NOT IS_DIRECTORY "${child}")
            continue()
        endif()
        get_filename_component(name "${child}" NAME)
        if(name IN_LIST required_apps)
            continue()
        endif()
        # Builder discovers standalone projects before this hook, independently
        # of its exclusion variable. Refuse to produce an incomplete policy.
        if(EXISTS "${child}/CMakeLists.txt")
            message(FATAL_ERROR "Non-distributed standalone app requires an explicit Builder policy: App/${name}")
        endif()
        list(APPEND excluded_apps "${name}")
    endforeach()
    list(REMOVE_DUPLICATES excluded_apps)
    set(IISDKBUILDER_APP_TREE_EXCLUDE "${excluded_apps}" PARENT_SCOPE)
endfunction()

society_configure_app_distribution()

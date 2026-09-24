# Bundle the installed SDK's QML module, including qmldir and private gallery
# components. No runtime access to the developer's SDK prefix is required.
function(society_add_photo_qml target)
    set(photo_qml_files)
    foreach(name qmldir PhotosView.qml GalleryTile.qml GalleryInfo.qml GalleryZoom.qml)
        set(path "${iiPhotoLibrary_QML_IMPORT_PATH}/iiPhotoLibrary/${name}")
        if(NOT EXISTS "${path}")
            message(FATAL_ERROR "The iiPhotoLibrary package is missing ${path}")
        endif()
        set_source_files_properties("${path}" PROPERTIES QT_RESOURCE_ALIAS "iiPhotoLibrary/${name}")
        list(APPEND photo_qml_files "${path}")
    endforeach()
    qt_add_resources(${target} iiPhotoLibraryQml PREFIX "/qt/qml" FILES ${photo_qml_files})
endfunction()

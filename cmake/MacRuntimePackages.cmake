# Prefer a product-local compatible runtime when one has been prepared.
# Explicit caller-selected packages always take precedence.
function(society_use_local_macos_runtime_packages root)
    if(NOT APPLE OR NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        return()
    endif()
    foreach(entry IN ITEMS
            "iiLocalDiffusion|iiLocalDiffusion-apple"
            "iiDecision|iiDecision-onnx-free"
            "iiLocalLLM|iiLocalLLM-onnx-free"
            "iiMultimodalAgent|iiMultimodalAgent-onnx-free"
            "iiVoiceOver|iiVoiceOver-onnx-free"
            "iiSocietyContainer|iiSocietyContainer-model-cache"
            "iiSocietyHelper|iiSocietyHelper"
            "iiWebServer|iiWebServer-core-compatible"
            "iiPhotoLibrary|iiPhotoLibrary-fileprovider"
            "LVRS|LVRS")
        string(REPLACE "|" ";" parts "${entry}")
        list(GET parts 0 package)
        list(GET parts 1 directory)
        set(variable "${package}_DIR")
        set(candidate "${root}/${directory}/lib/cmake/${package}")
        if(EXISTS "${candidate}/${package}Config.cmake" AND
                (NOT DEFINED ${variable} OR "${${variable}}" MATCHES "-NOTFOUND$"))
            set(${variable} "${candidate}" CACHE PATH "Society local ${package} package" FORCE)
        endif()
    endforeach()
    set(mlx_root "${root}/iiLocalDiffusion-apple")
    if(EXISTS "${mlx_root}/share/cmake/MLX/MLXConfig.cmake" AND
            (NOT DEFINED MLX_DIR OR "${MLX_DIR}" MATCHES "-NOTFOUND$"))
        set(MLX_DIR "${mlx_root}/share/cmake/MLX" CACHE PATH "Society local MLX package" FORCE)
    endif()
    if(EXISTS "${mlx_root}/lib/libmlx.dylib" AND
            (NOT DEFINED MLX_LIBRARY OR "${MLX_LIBRARY}" MATCHES "-NOTFOUND$"))
        set(MLX_LIBRARY "${mlx_root}/lib/libmlx.dylib" CACHE FILEPATH "Society local MLX library" FORCE)
    endif()
    get_filename_component(runtime_parent "${root}" DIRECTORY)
    set(local_runtime "${runtime_parent}/macos-mlx-runtime")
    if(NOT DEFINED SOCIETY_MLX_RUNTIME_DIR AND
            EXISTS "${local_runtime}/libmlx.dylib" AND EXISTS "${local_runtime}/mlx.metallib")
        set(SOCIETY_MLX_RUNTIME_DIR "${local_runtime}" CACHE PATH "Society local matching MLX runtime")
    endif()
endfunction()

# Exported from "//modules/skottie:skottie"
if(NOT TARGET unofficial::skia::modules::skottie)
    add_library(unofficial::skia::modules::skottie UNKNOWN IMPORTED)
    if("1")
        include("${CMAKE_CURRENT_LIST_DIR}/unofficial-skia-modules-skottie-targets-debug.cmake" OPTIONAL)
    endif()
    include("${CMAKE_CURRENT_LIST_DIR}/unofficial-skia-modules-skottie-targets-release.cmake")
endif()

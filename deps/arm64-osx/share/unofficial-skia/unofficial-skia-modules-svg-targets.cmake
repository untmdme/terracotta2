# Exported from "//modules/svg:svg"
if(NOT TARGET unofficial::skia::modules::svg)
    add_library(unofficial::skia::modules::svg UNKNOWN IMPORTED)
    if("1")
        include("${CMAKE_CURRENT_LIST_DIR}/unofficial-skia-modules-svg-targets-debug.cmake" OPTIONAL)
    endif()
    include("${CMAKE_CURRENT_LIST_DIR}/unofficial-skia-modules-svg-targets-release.cmake")
endif()

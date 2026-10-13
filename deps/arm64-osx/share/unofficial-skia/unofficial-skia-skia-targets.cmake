# Exported from "//:skia"
if(NOT TARGET unofficial::skia::skia)
    add_library(unofficial::skia::skia UNKNOWN IMPORTED)
    if("1")
        include("${CMAKE_CURRENT_LIST_DIR}/unofficial-skia-skia-targets-debug.cmake" OPTIONAL)
    endif()
    include("${CMAKE_CURRENT_LIST_DIR}/unofficial-skia-skia-targets-release.cmake")
endif()

# Exported from "//modules/bentleyottmann:bentleyottmann"
if(NOT TARGET unofficial::skia::modules::bentleyottmann)
    add_library(unofficial::skia::modules::bentleyottmann UNKNOWN IMPORTED)
    if("1")
        include("${CMAKE_CURRENT_LIST_DIR}/unofficial-skia-modules-bentleyottmann-targets-debug.cmake" OPTIONAL)
    endif()
    include("${CMAKE_CURRENT_LIST_DIR}/unofficial-skia-modules-bentleyottmann-targets-release.cmake")
endif()

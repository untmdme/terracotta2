# Exported from "//modules/skshaper:skshaper"
if(NOT TARGET unofficial::skia::modules::skshaper)
    add_library(unofficial::skia::modules::skshaper UNKNOWN IMPORTED)
    if("1")
        include("${CMAKE_CURRENT_LIST_DIR}/unofficial-skia-modules-skshaper-targets-debug.cmake" OPTIONAL)
    endif()
    include("${CMAKE_CURRENT_LIST_DIR}/unofficial-skia-modules-skshaper-targets-release.cmake")
endif()

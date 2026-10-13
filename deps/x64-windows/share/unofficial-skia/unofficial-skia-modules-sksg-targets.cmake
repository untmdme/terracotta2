# Exported from "//modules/sksg:sksg"
if(NOT TARGET unofficial::skia::modules::sksg)
    add_library(unofficial::skia::modules::sksg UNKNOWN IMPORTED)
    if("1")
        include("${CMAKE_CURRENT_LIST_DIR}/unofficial-skia-modules-sksg-targets-debug.cmake" OPTIONAL)
    endif()
    include("${CMAKE_CURRENT_LIST_DIR}/unofficial-skia-modules-sksg-targets-release.cmake")
endif()

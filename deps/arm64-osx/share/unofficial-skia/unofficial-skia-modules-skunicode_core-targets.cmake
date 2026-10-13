# Exported from "//modules/skunicode:skunicode_core"
if(NOT TARGET unofficial::skia::modules::skunicode_core)
    add_library(unofficial::skia::modules::skunicode_core UNKNOWN IMPORTED)
    if("1")
        include("${CMAKE_CURRENT_LIST_DIR}/unofficial-skia-modules-skunicode_core-targets-debug.cmake" OPTIONAL)
    endif()
    include("${CMAKE_CURRENT_LIST_DIR}/unofficial-skia-modules-skunicode_core-targets-release.cmake")
endif()

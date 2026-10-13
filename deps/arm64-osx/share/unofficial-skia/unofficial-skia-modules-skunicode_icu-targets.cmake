# Exported from "//modules/skunicode:skunicode_icu"
if(NOT TARGET unofficial::skia::modules::skunicode_icu)
    add_library(unofficial::skia::modules::skunicode_icu UNKNOWN IMPORTED)
    if("1")
        include("${CMAKE_CURRENT_LIST_DIR}/unofficial-skia-modules-skunicode_icu-targets-debug.cmake" OPTIONAL)
    endif()
    include("${CMAKE_CURRENT_LIST_DIR}/unofficial-skia-modules-skunicode_icu-targets-release.cmake")
endif()

# Exported from "//modules/jsonreader:jsonreader"
if(NOT TARGET unofficial::skia::modules::jsonreader)
    add_library(unofficial::skia::modules::jsonreader UNKNOWN IMPORTED)
    if("1")
        include("${CMAKE_CURRENT_LIST_DIR}/unofficial-skia-modules-jsonreader-targets-debug.cmake" OPTIONAL)
    endif()
    include("${CMAKE_CURRENT_LIST_DIR}/unofficial-skia-modules-jsonreader-targets-release.cmake")
endif()

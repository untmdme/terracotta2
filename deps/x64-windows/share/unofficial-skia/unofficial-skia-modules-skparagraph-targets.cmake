# Exported from "//modules/skparagraph:skparagraph"
if(NOT TARGET unofficial::skia::modules::skparagraph)
    add_library(unofficial::skia::modules::skparagraph UNKNOWN IMPORTED)
    if("1")
        include("${CMAKE_CURRENT_LIST_DIR}/unofficial-skia-modules-skparagraph-targets-debug.cmake" OPTIONAL)
    endif()
    include("${CMAKE_CURRENT_LIST_DIR}/unofficial-skia-modules-skparagraph-targets-release.cmake")
endif()

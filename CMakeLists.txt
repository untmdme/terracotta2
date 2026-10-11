cmake_minimum_required(VERSION 3.30)

# vcpkg is a git submodule pinned to vcpkg.json's builtin-baseline. Fetch it on first configure
# and bootstrap it, so a plain clone builds with no manual setup.
set(TC2_VCPKG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg")
if(NOT EXISTS "${TC2_VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
    message(STATUS "Fetching vcpkg submodule")
    execute_process(COMMAND git submodule update --init vcpkg
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" COMMAND_ERROR_IS_FATAL ANY)
endif()
if(CMAKE_HOST_WIN32)
    set(TC2_VCPKG_EXE "${TC2_VCPKG_ROOT}/vcpkg.exe")
    set(TC2_VCPKxG_BOOTSTRAP "${TC2_VCPKG_ROOT}/bootstrap-vcpkg.bat")
else()
    set(TC2_VCPKG_EXE "${TC2_VCPKG_ROOT}/vcpkg")
    set(TC2_VCPKG_BOOTSTRAP "${TC2_VCPKG_ROOT}/bootstrap-vcpkg.sh")
endif()
if(NOT EXISTS "${TC2_VCPKG_EXE}")
    message(STATUS "Bootstrapping vcpkg")
    execute_process(COMMAND "${TC2_VCPKG_BOOTSTRAP}" -disableMetrics
        WORKING_DIRECTORY "${TC2_VCPKG_ROOT}" COMMAND_ERROR_IS_FATAL ANY)
endif()
set(CMAKE_TOOLCHAIN_FILE "${TC2_VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE FILEPATH "")

project(Terracotta2Workspace LANGUAGES CXX)
if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 23) # deducing this: chainable widget setters
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(yoga CONFIG REQUIRED)
find_package(unofficial-skia CONFIG REQUIRED)
find_package(Dawn CONFIG REQUIRED)

# All src/**/*.cpp; CONFIGURE_DEPENDS re-globs on build so new files are picked up.
# Platform_win.cpp is #ifdef _WIN32, so it compiles to nothing elsewhere.
file(GLOB_RECURSE TC2_SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_library(Terracotta2 ${TC2_SOURCES})
if(APPLE)
    target_sources(Terracotta2 PRIVATE src/Platform/Platform_mac.mm)
    set_source_files_properties(src/Platform/Platform_mac.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
endif()

target_include_directories(Terracotta2 PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(Terracotta2
    PUBLIC
        yoga::yogacore
        unofficial::skia::skia
    PRIVATE
        unofficial::skia::modules::skparagraph
        dawn::webgpu_dawn
)

if(APPLE)
    target_link_libraries(Terracotta2 PRIVATE
        "-framework Metal"
        "-framework QuartzCore"
        "-framework Cocoa"
    )
elseif(WIN32)
    target_link_libraries(Terracotta2 PRIVATE d3d12.lib dxgi.lib d3dcompiler.lib user32.lib shcore.lib)
endif()

add_subdirectory(app)

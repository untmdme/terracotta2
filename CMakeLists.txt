cmake_minimum_required(VERSION 3.30)

# Prebuilt, static, release-only Yoga/Skia/Dawn live in deps/<platform> (copied from a vcpkg
# install tree, so its CMake config files are reused as-is). Override with -DTC2_DEPS_DIR=...
if(CMAKE_HOST_WIN32)
    if("$ENV{PROCESSOR_ARCHITECTURE}" STREQUAL "ARM64")
        set(TC2_DEFAULT_DEPS arm64-windows)
    else()
        set(TC2_DEFAULT_DEPS x64-windows)
    endif()
else()
    set(TC2_DEFAULT_DEPS arm64-osx)
endif()
set(TC2_DEPS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/${TC2_DEFAULT_DEPS}" CACHE PATH "Prebuilt dependency prefix")
list(PREPEND CMAKE_PREFIX_PATH "${TC2_DEPS_DIR}")

# The debug preset turns this on: RelWithDebInfo built unoptimized with debug info and asserts.
option(TC2_UNOPTIMIZED "Build RelWithDebInfo without optimization" OFF)
if(TC2_UNOPTIMIZED)
    if(CMAKE_HOST_WIN32)
        set(TC2_UNOPT_FLAGS "/Od /Ob0 /Zi")
    else()
        set(TC2_UNOPT_FLAGS "-O0 -g")
    endif()
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${TC2_UNOPT_FLAGS}" CACHE STRING "" FORCE)
    set(CMAKE_OBJCXX_FLAGS_RELWITHDEBINFO "${TC2_UNOPT_FLAGS}" CACHE STRING "" FORCE)
endif()

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
# Windows' Dawn archive is split into webgpu_dawn.lib + webgpu_dawn_<n>.lib to keep each file under
# GitHub's 100 MB limit; link the extra parts with it (MSVC's linker rescans them for cross-references).
file(GLOB TC2_DAWN_PARTS "${TC2_DEPS_DIR}/lib/webgpu_dawn_*.lib")
if(TC2_DAWN_PARTS)
    set_property(TARGET dawn::webgpu_dawn APPEND PROPERTY INTERFACE_LINK_LIBRARIES ${TC2_DAWN_PARTS})
endif()

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
    target_link_libraries(Terracotta2 PRIVATE d3d12.lib dxgi.lib d3dcompiler.lib user32.lib shcore.lib dwmapi.lib)
endif()

option(TC2_BUILD_APPS "Build the example apps" ${PROJECT_IS_TOP_LEVEL})
if(TC2_BUILD_APPS)
    add_subdirectory(app)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
install(TARGETS Terracotta2 EXPORT Terracotta2Targets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT Terracotta2Targets NAMESPACE Terracotta2:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Terracotta2)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/Terracotta2Config.cmake
"include(CMakeFindDependencyMacro)
find_dependency(yoga CONFIG)
find_dependency(unofficial-skia CONFIG)
include(\"\${CMAKE_CURRENT_LIST_DIR}/Terracotta2Targets.cmake\")
")
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/Terracotta2ConfigVersion.cmake
    VERSION 0.1.0 COMPATIBILITY SameMinorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/Terracotta2Config.cmake ${CMAKE_CURRENT_BINARY_DIR}/Terracotta2ConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Terracotta2)

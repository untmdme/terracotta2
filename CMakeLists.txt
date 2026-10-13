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
endif()

project(Terracotta2Workspace LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23) # deducing this: chainable widget setters
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Dawn CONFIG REQUIRED)
# Windows' Dawn archive is split into webgpu_dawn.lib + webgpu_dawn_<n>.lib to keep each file under
# GitHub's 100 MB limit; link the extra parts with it (MSVC's linker rescans them for cross-references).
file(GLOB TC2_DAWN_PARTS "${TC2_DEPS_DIR}/lib/webgpu_dawn_*.lib")
if(TC2_DAWN_PARTS)
    set_property(TARGET dawn::webgpu_dawn APPEND PROPERTY INTERFACE_LINK_LIBRARIES ${TC2_DAWN_PARTS})
endif()

# Terracotta2 itself is prebuilt (static library, headers, CMake config) in prebuilt/<platform>.
# GLOBAL so a parent project that add_subdirectory()s this one sees Terracotta2::Terracotta2 too.
find_package(Terracotta2 CONFIG REQUIRED GLOBAL NO_DEFAULT_PATH
    PATHS "${CMAKE_CURRENT_SOURCE_DIR}/prebuilt/${TC2_DEFAULT_DEPS}")

option(TC2_BUILD_APPS "Build the example apps" ${PROJECT_IS_TOP_LEVEL})
if(TC2_BUILD_APPS)
    add_subdirectory(app)
endif()

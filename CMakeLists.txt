cmake_minimum_required(VERSION 3.30)

# Standalone builds bootstrap the vcpkg submodule; as a subproject or vcpkg port the parent supplies the toolchain.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
# vcpkg is a git submodule pinned to vcpkg.json's builtin-baseline. Fetch it on first configure
# and bootstrap it, so a plain clone builds with no manual setup.
set(TC2_VCPKG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg")
if(NOT EXISTS "${TC2_VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
    message(STATUS "Fetching vcpkg submodule")
    # Also search Git for Windows' install locations, so git need not be on PATH.
    find_program(TC2_GIT git REQUIRED
        HINTS "$ENV{ProgramFiles}/Git/cmd" "$ENV{ProgramW6432}/Git/cmd" "$ENV{LOCALAPPDATA}/Programs/Git/cmd")
    execute_process(COMMAND "${TC2_GIT}" submodule update --init vcpkg
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" COMMAND_ERROR_IS_FATAL ANY)
endif()
if(CMAKE_HOST_WIN32)
    set(TC2_VCPKG_EXE "${TC2_VCPKG_ROOT}/vcpkg.exe")
    set(TC2_VCPKG_BOOTSTRAP "${TC2_VCPKG_ROOT}/bootstrap-vcpkg.bat")
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

# Static, release-only triplets for the host (a DLL Skia drops its SVG module and leaves
# skparagraph symbols unexported); override with -DVCPKG_TARGET_TRIPLET=...
if(CMAKE_HOST_WIN32)
    set(TC2_DEFAULT_TRIPLET x64-windows-static-md-release)
else()
    set(TC2_DEFAULT_TRIPLET arm64-osx-release)
endif()
set(VCPKG_TARGET_TRIPLET "${TC2_DEFAULT_TRIPLET}" CACHE STRING "")
set(VCPKG_HOST_TRIPLET "${TC2_DEFAULT_TRIPLET}" CACHE STRING "")
endif()

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
# A bundle (TC2_BUNDLE_DEPS) ships Yoga, Skia and Dawn in this same prefix.
get_filename_component(_tc2_prefix \"\${CMAKE_CURRENT_LIST_DIR}/../../..\" ABSOLUTE)
list(APPEND CMAKE_PREFIX_PATH \"\${_tc2_prefix}\")
find_dependency(yoga CONFIG)
find_dependency(unofficial-skia CONFIG)
find_dependency(Dawn CONFIG)
include(\"\${CMAKE_CURRENT_LIST_DIR}/Terracotta2Targets.cmake\")
")
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/Terracotta2ConfigVersion.cmake
    VERSION 0.1.0 COMPATIBILITY SameMinorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/Terracotta2Config.cmake ${CMAKE_CURRENT_BINARY_DIR}/Terracotta2ConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Terracotta2)

# Ready-to-ship bundle: `cmake --install` also copies the vcpkg-built Yoga/Skia/Dawn into the prefix,
# so `cpack` yields one self-contained archive that find_package(Terracotta2) can use as-is.
option(TC2_BUNDLE_DEPS "Install the vcpkg dependencies next to Terracotta2 and enable cpack" OFF)
if(TC2_BUNDLE_DEPS)
    install(DIRECTORY "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/" DESTINATION .
        PATTERN "tools" EXCLUDE PATTERN "debug" EXCLUDE)
    set(CPACK_PACKAGE_NAME Terracotta2)
    set(CPACK_PACKAGE_VERSION 0.1.0)
    set(CPACK_PACKAGE_FILE_NAME "Terracotta2-${CPACK_PACKAGE_VERSION}-${VCPKG_TARGET_TRIPLET}")
    set(CPACK_GENERATOR TGZ)
    if(WIN32)
        set(CPACK_GENERATOR ZIP)
    endif()
    include(CPack)
endif()

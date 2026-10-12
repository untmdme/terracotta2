add_executable(Docking docking_example.cpp)
target_link_libraries(Docking PRIVATE Terracotta2)

add_executable(Benchmark benchmark.cpp)
target_link_libraries(Benchmark PRIVATE Terracotta2)

add_executable(Hello_World hello_world.cpp)
target_link_libraries(Hello_World PRIVATE Terracotta2)

add_executable(Widget_Gallery widget_gallery.cpp)
target_link_libraries(Widget_Gallery PRIVATE Terracotta2)

add_executable(Node_Editor node_editor/main.cpp)
target_link_libraries(Node_Editor PRIVATE Terracotta2)

add_executable(Node_Editor_Test node_editor/model_test.cpp)
target_link_libraries(Node_Editor_Test PRIVATE Terracotta2)

set(TC2_APPS Docking Benchmark Hello_World Widget_Gallery Node_Editor)

# Skia only builds its SVG module for static builds (skia_enable_svg = !is_component_build),
# so a DLL triplet such as x64-windows-release has no svg target.
if(TARGET unofficial::skia::modules::svg)
    add_executable(Media_Example media_example.cpp)
    target_link_libraries(Media_Example PRIVATE Terracotta2 unofficial::skia::modules::svg)
    target_compile_definitions(Media_Example PRIVATE TC2_ASSETS="${CMAKE_CURRENT_SOURCE_DIR}/assets")
    list(APPEND TC2_APPS Media_Example)
else()
    message(STATUS "Skipping Media_Example: Skia was built without its SVG module")
endif()

# Dawn's D3D backends load their shader compilers at runtime from the executable's directory:
# DXC (dxcompiler.dll + dxil.dll, from vcpkg's directx-dxc) and FXC (d3dcompiler_47.dll, from the
# Windows SDK redist). Without them RequestDevice fails.
if(WIN32)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|arm64")
        set(TC2_SDK_ARCH arm64)
    else()
        set(TC2_SDK_ARCH x64)
    endif()
    find_file(TC2_D3DCOMPILER_DLL d3dcompiler_47.dll REQUIRED NO_DEFAULT_PATH
        PATHS "$ENV{ProgramFiles\(x86\)}/Windows Kits/10/Redist/D3D/${TC2_SDK_ARCH}")
    set(TC2_VCPKG_BIN "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
    set(TC2_RUNTIME_DLLS "${TC2_VCPKG_BIN}/dxcompiler.dll" "${TC2_VCPKG_BIN}/dxil.dll" "${TC2_D3DCOMPILER_DLL}")
endif()

# App icon from assets/logo.svg: a .app bundle with app.icns on macOS, an icon resource on Windows.
foreach(t IN LISTS TC2_APPS)
    if(APPLE)
        set_target_properties(${t} PROPERTIES MACOSX_BUNDLE ON MACOSX_BUNDLE_ICON_FILE app.icns)
        target_sources(${t} PRIVATE assets/app.icns)
    elseif(WIN32)
        target_sources(${t} PRIVATE app.rc)
        add_custom_command(TARGET ${t} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${TC2_RUNTIME_DLLS} "$<TARGET_FILE_DIR:${t}>")
    endif()
endforeach()
set_source_files_properties(assets/app.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

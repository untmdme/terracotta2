add_executable(Docking docking_example.cpp)
target_link_libraries(Docking PRIVATE Terracotta2)

add_executable(Benchmark benchmark.cpp)
target_link_libraries(Benchmark PRIVATE Terracotta2)

add_executable(Hello_World hello_world.cpp)
target_link_libraries(Hello_World PRIVATE Terracotta2)

add_executable(Widget_Gallery widget_gallery.cpp)
target_link_libraries(Widget_Gallery PRIVATE Terracotta2)

add_executable(Media_Example media_example.cpp)
target_link_libraries(Media_Example PRIVATE Terracotta2 unofficial::skia::modules::svg)
target_compile_definitions(Media_Example PRIVATE TC2_ASSETS="${CMAKE_CURRENT_SOURCE_DIR}/assets")

# App icon from assets/logo.svg: a .app bundle with app.icns on macOS, an icon resource on Windows.
foreach(t Docking Benchmark Hello_World Widget_Gallery Media_Example)
    if(APPLE)
        set_target_properties(${t} PROPERTIES MACOSX_BUNDLE ON MACOSX_BUNDLE_ICON_FILE app.icns)
        target_sources(${t} PRIVATE assets/app.icns)
    elseif(WIN32)
        target_sources(${t} PRIVATE app.rc)
    endif()
endforeach()
set_source_files_properties(assets/app.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "Terracotta2::Terracotta2" for configuration "Release"
set_property(TARGET Terracotta2::Terracotta2 APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(Terracotta2::Terracotta2 PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX;OBJCXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libTerracotta2.a"
  )

list(APPEND _cmake_import_check_targets Terracotta2::Terracotta2 )
list(APPEND _cmake_import_check_files_for_Terracotta2::Terracotta2 "${_IMPORT_PREFIX}/lib/libTerracotta2.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)

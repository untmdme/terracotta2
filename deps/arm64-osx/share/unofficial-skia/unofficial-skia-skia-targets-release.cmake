get_filename_component(VCPKG_IMPORT_PREFIX "${CMAKE_CURRENT_LIST_DIR}/../../" ABSOLUTE)
# Exported from "//:skia"
if("1")
    set_property(TARGET unofficial::skia::skia APPEND PROPERTY INTERFACE_LINK_LIBRARIES "\$<LINK_ONLY:\$<\$<NOT:\$<CONFIG:DEBUG>>:>>")
endif()
if("1")
    set_property(TARGET unofficial::skia::skia APPEND PROPERTY IMPORTED_CONFIGURATIONS "RELEASE")
    set_target_properties(unofficial::skia::skia PROPERTIES IMPORTED_LOCATION_RELEASE "${VCPKG_IMPORT_PREFIX}/lib/libskia.a")
    if("1")
        set_property(TARGET unofficial::skia::skia APPEND PROPERTY INTERFACE_COMPILE_DEFINITIONS "\$<\$<NOT:\$<CONFIG:DEBUG>>:SK_CODEC_DECODES_BMP;SK_CODEC_DECODES_WBMP;SK_ENABLE_PRECOMPILE;SK_ASSUME_GL=1;SK_GANESH;SK_GRAPHITE;SK_DISABLE_TRACING;SK_USE_PERFETTO;SK_ENABLE_API_AVAILABLE;SK_GAMMA_APPLY_TO_A8;SK_ENABLE_AVX512_OPTS;SK_TYPEFACE_FACTORY_CORETEXT;SK_FONTMGR_CORETEXT_AVAILABLE;SK_DAWN;SK_VULKAN;SK_HAS_WUFFS_LIBRARY;SK_CODEC_DECODES_GIF;SK_XML>")
        set_target_properties(unofficial::skia::skia PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${VCPKG_IMPORT_PREFIX}/include/skia"
            IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
        )
        z_vcpkg_skia_get_link_libraries(z_vcpkg_skia_link_libs "RELEASE" "-framework ApplicationServices;-framework OpenGL;-framework AppKit;webgpu_dawn;absl_raw_hash_set;absl_cord;absl_cordz_info;absl_cord_internal;absl_cordz_functions;absl_cordz_handle;absl_crc_cord_state;absl_crc32c;absl_crc_internal;absl_crc_cpu_detect;absl_str_format_internal;absl_hashtablez_sampler;absl_exponential_biased;absl_synchronization;absl_graphcycles_internal;absl_kernel_timeout_internal;absl_stacktrace;absl_borrowed_fixup_buffer;absl_hash;absl_city;absl_symbolize;absl_debugging_internal;absl_demangle_internal;absl_demangle_rust;absl_decode_rust_punycode;absl_utf8_for_code_point;absl_malloc_internal;absl_tracing_internal;absl_time;absl_civil_time;absl_time_zone;absl_strings;absl_strings_internal;absl_int128;absl_base;absl_spinlock_wait;absl_throw_delegate;absl_raw_logging_internal;absl_log_severity;expat;m;dl")
        set_property(TARGET unofficial::skia::skia APPEND PROPERTY INTERFACE_LINK_LIBRARIES "\$<LINK_ONLY:\$<\$<NOT:\$<CONFIG:DEBUG>>:${z_vcpkg_skia_link_libs}>>")
        unset(z_vcpkg_skia_link_libs)
    endif()
endif()

get_filename_component(VCPKG_IMPORT_PREFIX "${CMAKE_CURRENT_LIST_DIR}/../../" ABSOLUTE)
# Exported from "//modules/skunicode:skunicode_icu"
if("1")
    set_property(TARGET unofficial::skia::modules::skunicode_icu APPEND PROPERTY INTERFACE_LINK_LIBRARIES "\$<LINK_ONLY:\$<\$<NOT:\$<CONFIG:DEBUG>>:unofficial::skia::skia;unofficial::skia::modules::skunicode_core>>")
endif()
if("1")
    set_property(TARGET unofficial::skia::modules::skunicode_icu APPEND PROPERTY IMPORTED_CONFIGURATIONS "RELEASE")
    set_target_properties(unofficial::skia::modules::skunicode_icu PROPERTIES IMPORTED_LOCATION_RELEASE "${VCPKG_IMPORT_PREFIX}/lib/skunicode_icu.lib")
    if("1")
        set_property(TARGET unofficial::skia::modules::skunicode_icu APPEND PROPERTY INTERFACE_COMPILE_DEFINITIONS "\$<\$<NOT:\$<CONFIG:DEBUG>>:SK_GAMMA_APPLY_TO_A8;SK_ENABLE_AVX512_OPTS;SK_TYPEFACE_FACTORY_DIRECTWRITE;SK_FONTMGR_DIRECTWRITE_AVAILABLE;SK_FONTMGR_GDI_AVAILABLE;SK_DAWN;SK_VULKAN;SK_SUPPORT_XPS;SK_HAS_WUFFS_LIBRARY;SK_CODEC_DECODES_GIF;SK_XML;SK_CODEC_DECODES_BMP;SK_CODEC_DECODES_WBMP;SK_ENABLE_PRECOMPILE;SK_GANESH;SK_GRAPHITE;SK_DISABLE_TRACING>")
        set_target_properties(unofficial::skia::modules::skunicode_icu PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${VCPKG_IMPORT_PREFIX}/include/skia"
            IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
        )
        z_vcpkg_skia_get_link_libraries(z_vcpkg_skia_link_libs "RELEASE" "${z_vcpkg_skia_root}/lib/webgpu_dawn.lib;onecore.lib;delayimp.lib;dxguid.lib;${z_vcpkg_skia_root}/lib/absl_raw_hash_set.lib;${z_vcpkg_skia_root}/lib/absl_cord.lib;${z_vcpkg_skia_root}/lib/absl_cordz_info.lib;${z_vcpkg_skia_root}/lib/absl_cord_internal.lib;${z_vcpkg_skia_root}/lib/absl_cordz_functions.lib;${z_vcpkg_skia_root}/lib/absl_cordz_handle.lib;${z_vcpkg_skia_root}/lib/absl_crc_cord_state.lib;${z_vcpkg_skia_root}/lib/absl_crc32c.lib;${z_vcpkg_skia_root}/lib/absl_crc_internal.lib;${z_vcpkg_skia_root}/lib/absl_crc_cpu_detect.lib;${z_vcpkg_skia_root}/lib/absl_str_format_internal.lib;${z_vcpkg_skia_root}/lib/absl_hashtablez_sampler.lib;${z_vcpkg_skia_root}/lib/absl_exponential_biased.lib;${z_vcpkg_skia_root}/lib/absl_synchronization.lib;${z_vcpkg_skia_root}/lib/absl_graphcycles_internal.lib;${z_vcpkg_skia_root}/lib/absl_kernel_timeout_internal.lib;${z_vcpkg_skia_root}/lib/absl_stacktrace.lib;${z_vcpkg_skia_root}/lib/absl_borrowed_fixup_buffer.lib;${z_vcpkg_skia_root}/lib/absl_hash.lib;${z_vcpkg_skia_root}/lib/absl_city.lib;${z_vcpkg_skia_root}/lib/absl_symbolize.lib;${z_vcpkg_skia_root}/lib/absl_debugging_internal.lib;${z_vcpkg_skia_root}/lib/absl_demangle_internal.lib;${z_vcpkg_skia_root}/lib/absl_demangle_rust.lib;${z_vcpkg_skia_root}/lib/absl_decode_rust_punycode.lib;${z_vcpkg_skia_root}/lib/absl_utf8_for_code_point.lib;${z_vcpkg_skia_root}/lib/absl_malloc_internal.lib;${z_vcpkg_skia_root}/lib/absl_tracing_internal.lib;${z_vcpkg_skia_root}/lib/absl_time.lib;${z_vcpkg_skia_root}/lib/absl_civil_time.lib;${z_vcpkg_skia_root}/lib/absl_time_zone.lib;${z_vcpkg_skia_root}/lib/absl_strings.lib;${z_vcpkg_skia_root}/lib/absl_strings_internal.lib;${z_vcpkg_skia_root}/lib/absl_int128.lib;${z_vcpkg_skia_root}/lib/absl_base.lib;${z_vcpkg_skia_root}/lib/absl_spinlock_wait.lib;${z_vcpkg_skia_root}/lib/absl_throw_delegate.lib;${z_vcpkg_skia_root}/lib/absl_raw_logging_internal.lib;${z_vcpkg_skia_root}/lib/absl_log_severity.lib;${z_vcpkg_skia_root}/lib/libexpatMD.lib;${z_vcpkg_skia_root}/lib/icuuc.lib;${z_vcpkg_skia_root}/lib/icudt.lib;FontSub.lib;Usp10.lib")
        set_property(TARGET unofficial::skia::modules::skunicode_icu APPEND PROPERTY INTERFACE_LINK_LIBRARIES "\$<LINK_ONLY:\$<\$<NOT:\$<CONFIG:DEBUG>>:${z_vcpkg_skia_link_libs}>>")
        unset(z_vcpkg_skia_link_libs)
    endif()
endif()

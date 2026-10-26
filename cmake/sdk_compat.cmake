# 0.10.0's packaged host source predates OnConfigureLogging. Its default log
# directory must also be writable when the game lives inside a signed app.
set(renut_sdk_app_source "${REXGLUE_SHARE_DIR}/rex_app.cpp")
if(EXISTS "${renut_sdk_app_source}")
    file(READ "${renut_sdk_app_source}" renut_sdk_app)
    if(NOT renut_sdk_app MATCHES "OnConfigureLogging\\(log_config\\)")
        set(renut_old_log_path "log_config.log_dir = (exe_dir / \"logs\").string();")
        string(FIND "${renut_sdk_app}" "${renut_old_log_path}" renut_log_at)
        if(renut_log_at EQUAL -1)
            message(FATAL_ERROR "Unrecognized SDK logging setup; update the compatibility adapter")
        endif()
        string(REPLACE "${renut_old_log_path}"
            "log_config.log_dir = (renut::paths::WritableFolder() / \"logs\").string();"
            renut_sdk_app "${renut_sdk_app}")
        set(renut_sdk_copy "${CMAKE_BINARY_DIR}/sdk-compat/rex_app.cpp")
        file(CONFIGURE OUTPUT "${renut_sdk_copy}"
            CONTENT "#include \"renut_engine/app/app_paths.h\"\n${renut_sdk_app}" @ONLY)
        get_target_property(renut_host_sources renut SOURCES)
        list(REMOVE_ITEM renut_host_sources "${renut_sdk_app_source}")
        set_property(TARGET renut PROPERTY SOURCES "${renut_host_sources}")
        target_sources(renut PRIVATE "${renut_sdk_copy}")
    endif()
endif()

if(APPLE)
    set(renut_sdk_main_source "${REXGLUE_SHARE_DIR}/windowed_app_main_sdl.cpp")
    file(READ "${renut_sdk_main_source}" renut_sdk_main)
    set(renut_main_call "return RunWindowedApp(argc, argv);")
    string(FIND "${renut_sdk_main}" "${renut_main_call}" renut_main_at)
    if(renut_main_at EQUAL -1)
        message(FATAL_ERROR "Unrecognized SDK SDL entry point")
    endif()
    string(REPLACE "${renut_main_call}"
        "renut::ConfigureMacSDL();\n  ${renut_main_call}" renut_sdk_main "${renut_sdk_main}")
    set(renut_sdk_main_copy "${CMAKE_BINARY_DIR}/sdk-compat/windowed_app_main_sdl.cpp")
    file(CONFIGURE OUTPUT "${renut_sdk_main_copy}"
        CONTENT "#include \"renut_engine/app/mac_sdl.h\"\n${renut_sdk_main}" @ONLY)
    get_target_property(renut_main_sources renut SOURCES)
    list(REMOVE_ITEM renut_main_sources "${renut_sdk_main_source}")
    set_property(TARGET renut PROPERTY SOURCES "${renut_main_sources}")
    target_sources(renut PRIVATE "${renut_sdk_main_copy}")
    include(ExternalProject)
    ExternalProject_Add(renut_mac_sdl
        DOWNLOAD_COMMAND "" CONFIGURE_COMMAND ""
        BUILD_COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/build_macos_sdl.py"
            --prefix "${CMAKE_BINARY_DIR}/mac-sdl"
        INSTALL_COMMAND ""
        BUILD_BYPRODUCTS "${CMAKE_BINARY_DIR}/mac-sdl/lib/libSDL3.0.dylib"
        BUILD_ALWAYS TRUE)
    add_dependencies(renut renut_mac_sdl)
endif()

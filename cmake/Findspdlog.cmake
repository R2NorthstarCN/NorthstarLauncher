if(NOT spdlog_FOUND)
    check_init_submodule(${PROJECT_SOURCE_DIR}/thirdparty/spdlog)

    set(SPDLOG_BUILD_SHARED ON)
    set(SPDLOG_WCHAR_SUPPORT ON)
    set(SPDLOG_WCHAR_FILENAMES ON)
    add_subdirectory(${PROJECT_SOURCE_DIR}/thirdparty/spdlog spdlog)
    
    set(spdlog_FOUND 1)
endif()

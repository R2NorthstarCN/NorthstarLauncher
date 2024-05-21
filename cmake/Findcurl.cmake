if(NOT curl_FOUND)
    check_init_submodule(${PROJECT_SOURCE_DIR}/thirdparty/curl)

    set(BUILD_CURL_EXE OFF)
    set(BUILD_SHARED_LIBS OFF)
    set(BUILD_STATIC_LIBS ON)
    set(ENABLE_UNICODE ON)
    set(HTTP_ONLY ON)
    set(BUILD_LIBCURL_DOCS OFF)
    set(BUILD_MISC_DOCS OFF)
    set(ENABLE_CURL_MANUAL OFF)
    set(CURL_DISABLE_INSTALL ON)
    set(CURL_USE_SCHANNEL ON)
    set(USE_ZLIB ON)
    set(USE_LIBIDN2 OFF)
    set(USE_WIN32_IDN ON)

    add_subdirectory(${PROJECT_SOURCE_DIR}/thirdparty/curl curl)
    set(curl_FOUND
        1
        PARENT_SCOPE
        )
endif()

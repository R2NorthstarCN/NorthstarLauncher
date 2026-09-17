if(NOT httplib_FOUND)
    check_init_submodule(${PROJECT_SOURCE_DIR}/primedev/thirdparty/cpp-httplib)

    set(HTTPLIB_REQUIRE_OPENSSL
        OFF
        CACHE BOOL "Requires OpenSSL to be found & linked, or fails build."
        )
    set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE
        OFF
        CACHE BOOL "Uses OpenSSL (if available) to enable HTTPS support."
        )
    set(HTTPLIB_REQUIRE_ZLIB
        OFF
        CACHE BOOL "Requires ZLIB to be found & linked, or fails build."
        )
    set(HTTPLIB_USE_ZLIB_IF_AVAILABLE
        OFF
        CACHE BOOL "Uses ZLIB (if available) to enable Zlib compression support."
        )
    set(HTTPLIB_REQUIRE_BROTLI
        OFF
        CACHE BOOL "Requires Brotli to be found & linked, or fails build."
        )
    set(HTTPLIB_USE_BROTLI_IF_AVAILABLE
        OFF
        CACHE BOOL "Uses Brotli (if available) to enable Brotli decompression support."
        )
    set(HTTPLIB_REQUIRE_ZSTD
        OFF
        CACHE BOOL "Requires ZSTD to be found & linked, or fails build."
        )
    set(HTTPLIB_USE_ZSTD_IF_AVAILABLE
        OFF
        CACHE BOOL "Uses zstd (if available) to enable zstd support."
        )
    set(HTTPLIB_INSTALL
        OFF
        CACHE BOOL "Enables the installation target"
        )

    add_subdirectory(${PROJECT_SOURCE_DIR}/primedev/thirdparty/cpp-httplib cpp-httplib)

    target_link_libraries(httplib INTERFACE ssl crypto)
    target_compile_definitions(httplib INTERFACE CPPHTTPLIB_OPENSSL_SUPPORT)

    set(httplib_FOUND
        1
        PARENT_SCOPE
        )
endif()

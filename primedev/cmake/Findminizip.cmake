if(NOT minizip_FOUND)

    # zlib 1.3.1 had a cmake change that broke stuff, so this branch on our fork reverts that one commit :)
    set(ZLIB_TAG "fix-things")
    set(ZLIB_REPOSITORY "https://github.com/R2Northstar/zlib")

    check_init_submodule(${PROJECT_SOURCE_DIR}/primedev/thirdparty/minizip)

    set(MZ_ZLIB
        ON
        CACHE BOOL "Enable ZLIB compression, needed for DEFLATE"
        )
    set(MZ_BZIP2
        OFF
        CACHE BOOL "Disable BZIP2 compression"
        )
    set(MZ_LZMA
        OFF
        CACHE BOOL "Disable LZMA & XZ compression"
        )
    set(MZ_PKCRYPT
        OFF
        CACHE BOOL "Disable PKWARE traditional encryption"
        )
    set(MZ_WZAES
        OFF
        CACHE BOOL "Disable WinZIP AES encryption"
        )
    set(MZ_ZSTD
        OFF
        CACHE BOOL "Disable ZSTD compression"
        )
    set(MZ_SIGNING
        OFF
        CACHE BOOL "Disable zip signing support"
        )

    add_subdirectory(${PROJECT_SOURCE_DIR}/primedev/thirdparty/minizip minizip)

    # make the zlib instance fetched by minizip usable by first party code
    get_target_property(zlib_SOURCE_DIR zlibstatic SOURCE_DIR)
    get_target_property(zlib_BINARY_DIR zlibstatic BINARY_DIR)
    target_include_directories(zlibstatic INTERFACE "$<BUILD_INTERFACE:${zlib_SOURCE_DIR}>" "$<BUILD_INTERFACE:${zlib_BINARY_DIR}>")

    set(minizip_FOUND
        1
        PARENT_SCOPE
        )
endif()

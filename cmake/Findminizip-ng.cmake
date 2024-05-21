if(NOT minizip-ng_FOUND)

    # zlib 1.3.1 had a cmake change that broke stuff, so this branch on our fork reverts that one commit :)
    set(ZLIB_TAG "develop")
    set(ZLIB_REPOSITORY "https://github.com/madler/zlib")

    check_init_submodule(${PROJECT_SOURCE_DIR}/thirdparty/minizip-ng)

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

    set(MZ_FORCE_FETCH_LIBS ON)

    add_subdirectory(${PROJECT_SOURCE_DIR}/thirdparty/minizip-ng minizip-ng)
    set(minizip-ng_FOUND
        1
        PARENT_SCOPE
        )
endif()

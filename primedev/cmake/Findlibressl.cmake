if(NOT libressl_FOUND)
    include(FetchContent)

    set(LIBRESSL_APPS
        OFF
        CACHE BOOL "Build apps"
        )
    set(LIBRESSL_TESTS
        OFF
        CACHE BOOL "Build tests"
        )
    set(LIBRESSL_SKIP_INSTALL
        ON
        CACHE BOOL "Skip installation"
        )
    set(USE_STATIC_MSVC_RUNTIMES
        ON
        CACHE BOOL "Use /MT instead of /MD in MSVC"
        )
    set(ENABLE_ASM
        OFF
        CACHE BOOL "Enable assembly"
        )

    if(NOT CMAKE_SYSTEM_PROCESSOR)
        set(CMAKE_SYSTEM_PROCESSOR "AMD64")
    endif()

    FetchContent_Declare(
        libressl
        URL https://ftp.openbsd.org/pub/OpenBSD/LibreSSL/libressl-4.3.2.tar.gz
        URL_HASH SHA256=edf01aee24c65d69e6a9efcb9d44bcda682ff9d4f3bbbd95e794e1dfa90847b5
        )
    FetchContent_MakeAvailable(libressl)

    target_compile_definitions(ssl INTERFACE LIBRESSL_DISABLE_OVERRIDE_WINCRYPT_DEFINES_WARNING)

    set(libressl_FOUND
        1
        PARENT_SCOPE
        )
endif()

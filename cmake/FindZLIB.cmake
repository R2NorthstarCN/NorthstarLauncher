if(NOT ZLIB_FOUND)
    check_init_submodule(${PROJECT_SOURCE_DIR}/thirdparty/zlib-ng)

    set(ZLIB_COMPAT ON)
    set(ZLIB_ENABLE_TESTS OFF)
    set(ZLIBNG_ENABLE_TESTS OFF)
    set(WITH_GTEST OFF)
    add_subdirectory(${PROJECT_SOURCE_DIR}/thirdparty/zlib-ng zlib-ng)

    add_library(ZLIB::ZLIB ALIAS zlib)
    export(TARGETS zlib FILE zlibTargets.cmake)
    get_target_property(ZLIB_INCLUDE_DIRS zlib INTERFACE_INCLUDE_DIRECTORIES)
    
    set(ZLIB_LIBRARIES z)

    set(ZLIB_FOUND 1)
endif()

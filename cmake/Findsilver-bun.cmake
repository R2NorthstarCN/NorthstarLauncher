if(NOT silver-bun_FOUND)
    check_init_submodule(${PROJECT_SOURCE_DIR}/thirdparty/silver-bun)

    add_library(silver-bun INTERFACE IMPORTED)
    target_include_directories(silver-bun INTERFACE ${PROJECT_SOURCE_DIR}/thirdparty/silver-bun/silver-bun)

    set(silver-bun_FOUND
        1
        PARENT_SCOPE
        )
endif()

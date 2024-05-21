if(NOT rapidjson_FOUND)
    check_init_submodule(${PROJECT_SOURCE_DIR}/thirdparty/rapidjson)

    # set(RAPIDJSON_BUILD_DOC OFF)
    # set(RAPIDJSON_BUILD_EXAMPLES OFF)
    # set(RAPIDJSON_BUILD_TESTS OFF)
    # set(RAPIDJSON_ENABLE_INSTRUMENTATION_OPT OFF)
    # add_subdirectory(${PROJECT_SOURCE_DIR}/thirdparty/rapidjson rapidjson)

    add_library(RapidJSON INTERFACE IMPORTED)
    target_include_directories(RapidJSON INTERFACE ${PROJECT_SOURCE_DIR}/thirdparty/rapidjson/include)

    set(rapidjson_FOUND 1)
endif()

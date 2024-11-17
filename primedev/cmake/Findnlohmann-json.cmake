if(NOT nlohmann-json_FOUND)
    check_init_submodule(${PROJECT_SOURCE_DIR}/primedev/thirdparty/nlohmann-json)

    add_subdirectory(${PROJECT_SOURCE_DIR}/primedev/thirdparty/nlohmann-json nlohmann-json)
    set(nlohmann-json_FOUND 1)
endif()

if(NOT nlohmann_json_FOUND)
    check_init_submodule(${PROJECT_SOURCE_DIR}/thirdparty/json)

    add_subdirectory(${PROJECT_SOURCE_DIR}/thirdparty/json nlohmann_json)

    set(nlohmann_json_FOUND 1)
endif()

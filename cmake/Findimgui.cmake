if(NOT imgui_FOUND)
    add_subdirectory(${PROJECT_SOURCE_DIR}/thirdparty/imgui imgui)
    
    set(spdlog_FOUND 1)
endif()

# loader_wsock32_proxy

find_package(minhook REQUIRED)

add_library(
    loader_wsock32_proxy SHARED
    "dllmain.cpp"
    "loader.cpp"
    "loader.h"
    "wsock32.def"
    )

target_link_libraries(
    loader_wsock32_proxy
    PRIVATE 
    -static
    minhook
    Shlwapi.lib
    )

target_precompile_headers(
    loader_wsock32_proxy
    PRIVATE
    pch.h
    )

target_compile_definitions(loader_wsock32_proxy PRIVATE UNICODE _UNICODE)

set_target_properties(
    loader_wsock32_proxy
    PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${NS_BINARY_DIR}/bin/x64_retail
               OUTPUT_NAME wsock32
    )

add_custom_command(TARGET loader_wsock32_proxy POST_BUILD
    COMMAND $<$<CONFIG:release>:${CMAKE_STRIP}> $<$<CONFIG:release>:${NS_BINARY_DIR}/bin/x64_retail/wsock32.dll>)

if( ipo_supported )
    set_property(TARGET loader_wsock32_proxy PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()
# NorthstarLauncher

add_executable(NorthstarLauncher "primelauncher/main.cpp" "primelauncher/resources.rc")

target_compile_definitions(NorthstarLauncher PRIVATE UNICODE _UNICODE)

target_link_libraries(
    NorthstarLauncher
    PRIVATE -static
        winpthread
        stdc++
        Shlwapi.lib
        Ws2_32.lib
    )

set_target_properties(NorthstarLauncher PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${NS_BINARY_DIR})

target_compile_options(NorthstarLauncher PRIVATE 
-finput-charset=utf-8 
-fexec-charset=utf-8
)

target_link_options(NorthstarLauncher PUBLIC "LINKER:--stack,8000000")

add_custom_command(TARGET NorthstarLauncher POST_BUILD
    COMMAND $<$<CONFIG:release>:${CMAKE_STRIP}> $<$<CONFIG:release>:${NS_BINARY_DIR}/NorthstarLauncher.exe>)

if( ipo_supported )
    set_property(TARGET NorthstarLauncher PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()
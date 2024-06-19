cmake_minimum_required(VERSION 3.15)

add_library(imgui STATIC
    "imgui_demo.cpp"
    "imgui_draw.cpp"
    "imgui_tables.cpp"
    "imgui_widgets.cpp"
    "imgui.cpp"
    "implot_demo.cpp"
    "implot_items.cpp"
    "implot.cpp"
    "backends/imgui_impl_dx11.cpp"
    "backends/imgui_impl_win32.cpp")

target_include_directories(imgui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(imgui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/backends)

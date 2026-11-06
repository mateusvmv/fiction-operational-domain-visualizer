cmake_minimum_required(VERSION 3.20)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

project(my_fiction_project VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# raylib
add_subdirectory(vendor/raylib-6.2)

# imgui
# add_library(imgui STATIC
#     vendor/imgui-1.92.7/imgui.cpp
#     vendor/imgui-1.92.7/imgui_demo.cpp
#     vendor/imgui-1.92.7/imgui_draw.cpp
#     vendor/imgui-1.92.7/imgui_tables.cpp
#     vendor/imgui-1.92.7/imgui_widgets.cpp)
# target_include_directories(imgui SYSTEM PUBLIC 
#     vendor/imgui-1.92.7)
add_library(imgui STATIC
    vendor/imgui-docking/imgui.cpp
    vendor/imgui-docking/imgui_demo.cpp
    vendor/imgui-docking/imgui_draw.cpp
    vendor/imgui-docking/imgui_tables.cpp
    vendor/imgui-docking/imgui_widgets.cpp)
target_include_directories(imgui SYSTEM PUBLIC 
    vendor/imgui-docking)

# rlimgui
add_library(rlimgui STATIC
    vendor/rlImGui-Raylib_6_0/rlImGui.cpp)
target_include_directories(rlimgui SYSTEM PUBLIC 
    vendor/rlImGui-Raylib_6_0)
target_link_libraries(rlimgui PUBLIC imgui raylib)

# implot
add_library(implot STATIC
    vendor/implot-1.1/implot.cpp
    vendor/implot-1.1/implot_demo.cpp
    vendor/implot-1.1/implot_items.cpp
)
target_include_directories(implot SYSTEM PUBLIC 
    vendor/implot-1.1)
target_link_libraries(implot PRIVATE imgui)

# fiction
add_subdirectory(fiction)

# main exe
add_executable(tester
    src/main.cpp
    src/experiment.cpp
    src/operational_data.cpp
    src/save.cpp
    src/app/file_browser.cpp
    src/app/results.cpp
    src/app/plot.cpp
    src/app/control_panel.cpp
    src/window/save_modals.cpp
    src/window/setup.cpp
    src/window/toolbar.cpp)
target_link_libraries(tester PRIVATE libfiction raylib imgui rlimgui implot)
target_include_directories(tester PRIVATE ${img_SOURCE_DIR})

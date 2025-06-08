cmake_minimum_required(VERSION 4.0)
project(input)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

function(set_default_compile_options target)
    target_compile_options(${target} PUBLIC
        -Wno-missing-field-initializers
        -Wno-comment
        )
    target_compile_features(${target} PUBLIC cxx_std_26)
endfunction()

include(FetchContent)
set(VENDOR_DIR ".build/3rdparty")

# backward-cpp

FetchContent_Declare(backward-cpp
    GIT_REPOSITORY https://github.com/bombela/backward-cpp.git
    GIT_TAG master)
FetchContent_MakeAvailable(backward-cpp)

# # glfw

# FetchContent_Declare(glfw
#     GIT_REPOSITORY https://github.com/glfw/glfw.git
#     GIT_TAG master)
# FetchContent_MakeAvailable(glfw)

# # imgui

# FetchContent_Declare(imgui
#     GIT_REPOSITORY https://github.com/ocornut/imgui.git
#     GIT_TAG docking)
# FetchContent_MakeAvailable(imgui)

# add_library(imgui)
# target_sources(imgui PRIVATE
#     ${imgui_SOURCE_DIR}/imgui.cpp
#     ${imgui_SOURCE_DIR}/imgui_demo.cpp
#     ${imgui_SOURCE_DIR}/imgui_tables.cpp
#     ${imgui_SOURCE_DIR}/imgui_draw.cpp
#     ${imgui_SOURCE_DIR}/imgui_widgets.cpp
#     ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
#     ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp)
# target_include_directories(imgui PUBLIC
#     ${imgui_SOURCE_DIR}
#     ${imgui_SOURCE_DIR}/backends)
# target_link_libraries(imgui PUBLIC
#     glfw)

# evdev

add_library(libevdev INTERFACE)
target_include_directories(libevdev INTERFACE /usr/include/libevdev-1.0)
target_link_libraries(libevdev INTERFACE evdev)

# input

add_executable(input)
set_default_compile_options(input)
target_sources(input PUBLIC
    src/example/example-joystick.cpp
    src/example/example-mouse.cpp
    src/example/example-udev-watch.cpp
    src/example/example.cpp

    src/input/fd_event_bus.cpp
    src/input/udev_subsystem.cpp
    src/input/evdev_subsystem.cpp
    )
target_include_directories(input PUBLIC src)
target_link_libraries(input PUBLIC stdc++exp)
target_link_libraries(input PUBLIC libevdev udev)
target_link_libraries(input PUBLIC Backward::Object)
# target_link_libraries(input PUBLIC glfw imgui GL)

cmake_minimum_required(VERSION 3.22)
project(injection CXX)

set(CMAKE_CXX_STANDARD 17)
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)

add_library(injection SHARED
    imgui_unity_plugin.cpp
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(injection PRIVATE
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

target_link_libraries(injection
    android
    log
    EGL
    GLESv3
    dl
    # shadowhook   ← uncomment after adding bytedance/android-inline-hook
)

target_compile_definitions(injection PRIVATE
    IMGUI_IMPL_OPENGL_ES3
    IMGUI_IMPL_OPENGL_LOADER_CUSTOM
)

set_target_properties(injection PROPERTIES
    PREFIX ""
    OUTPUT_NAME "libinjection"
    POSITION_INDEPENDENT_CODE ON
)

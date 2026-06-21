cmake_minimum_required(VERSION 3.20)
project(CyberBiology LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------
# Third-party: Dear ImGui + ImPlot + gl3w compiled as one static library.
# GLFW is consumed as a prebuilt static lib shipped under third_party/glfw.
# ---------------------------------------------------------------------------
set(TP "${CMAKE_SOURCE_DIR}/third_party")

add_library(thirdparty STATIC
    ${TP}/imgui/imgui.cpp
    ${TP}/imgui/imgui_draw.cpp
    ${TP}/imgui/imgui_tables.cpp
    ${TP}/imgui/imgui_widgets.cpp
    ${TP}/imgui/imgui_impl_glfw.cpp
    ${TP}/imgui/imgui_impl_opengl3.cpp
    ${TP}/imgui/implot.cpp
    ${TP}/imgui/implot_items.cpp
    ${TP}/glfw/gl3w.c
)
target_include_directories(thirdparty PUBLIC
    ${TP}
    ${TP}/imgui
    ${TP}/glfw
)

# Prebuilt GLFW (static, /MD). Match the executable's runtime library.
target_link_libraries(thirdparty PUBLIC
    ${TP}/glfw/glfw3.lib
    opengl32
    gdi32
    shell32
)

# ---------------------------------------------------------------------------
# Application
# ---------------------------------------------------------------------------
add_executable(CyberBiology
    src/app/main.cpp
    src/sim/WorldState.cpp
    src/sim/Simulation.cpp
    src/sim/GpuSimulation.cpp
    src/io/WorldIO.cpp
    src/platform/Helpers.cpp
)

target_include_directories(CyberBiology PRIVATE
    src/sim
    src/io
    src/platform
    src/app
)

target_link_libraries(CyberBiology PRIVATE thirdparty)

if(MSVC)
    target_compile_options(CyberBiology PRIVATE /utf-8)
    set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT CyberBiology)
endif()

# Copy runtime assets next to the executable so it can be launched in-place.
add_custom_command(TARGET CyberBiology POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/fonts" "$<TARGET_FILE_DIR:CyberBiology>/fonts"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:CyberBiology>/saves"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:CyberBiology>/bots"
)

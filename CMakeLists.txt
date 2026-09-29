cmake_minimum_required(VERSION 3.24)

project(Rebuild
    VERSION 0.1.0
    DESCRIPTION "From-scratch rebuild of the mini engine - D5/D6: swap chain, render pass, shaders"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()
find_package(Vulkan REQUIRED)
include(FetchContent)

set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)

FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE)

FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.5
    GIT_SHALLOW    TRUE)

FetchContent_MakeAvailable(glfw imgui)

if(Vulkan_GLSLC_EXECUTABLE)
    set(GLSLC_EXECUTABLE "${Vulkan_GLSLC_EXECUTABLE}")
else()
    find_program(GLSLC_EXECUTABLE glslc
        HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")
endif()

if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR
        "glslc was not found. Install the Vulkan SDK from https://vulkan.lunarg.com, "
        "make sure the VULKAN_SDK environment variable is set, then re-run CMake.")
endif()

set(SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${SHADER_OUTPUT_DIR}")

set(SHADER_NAMES
    triangle.vert
    triangle.frag
    cube.vert
    cube.frag
    cube_mvp.vert
    cube_mvp.frag)

set(SHADER_SPIRV_FILES)
foreach(shader_name IN LISTS SHADER_NAMES)
    set(spirv_path "${SHADER_OUTPUT_DIR}/${shader_name}.spv")
    add_custom_command(
        OUTPUT  "${spirv_path}"
        COMMAND "${GLSLC_EXECUTABLE}" "${SHADER_SOURCE_DIR}/${shader_name}" -o "${spirv_path}"
        DEPENDS "${SHADER_SOURCE_DIR}/${shader_name}"
        COMMENT "Compiling ${shader_name} -> SPIR-V"
        VERBATIM)
    list(APPEND SHADER_SPIRV_FILES "${spirv_path}")
endforeach()

add_custom_target(RebuildShaders ALL DEPENDS ${SHADER_SPIRV_FILES})
set(IMGUI_SOURCES
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp")

add_executable(Rebuild
    src/rhi/vulkan/vulkanRenderer.cpp
    src/main.cpp
    src/rhi/RendererFactory.cpp
    src/scene/CubeMesh.cpp
    ${IMGUI_SOURCES})

add_dependencies(Rebuild RebuildShaders)

target_include_directories(Rebuild PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${imgui_SOURCE_DIR}"
    "${imgui_SOURCE_DIR}/backends")

target_compile_definitions(Rebuild PRIVATE
    SHADER_DIR="${SHADER_OUTPUT_DIR}"
    GLM_FORCE_RADIANS
    GLM_FORCE_DEPTH_ZERO_TO_ONE)

target_link_libraries(Rebuild PRIVATE
    glfw
    Vulkan::Vulkan)

if(MSVC)
    target_compile_options(Rebuild PRIVATE /utf-8)

    target_compile_options(Rebuild PRIVATE /W4)
    set_source_files_properties(${IMGUI_SOURCES} PROPERTIES COMPILE_OPTIONS "/w")
endif()

cmake_minimum_required(VERSION 3.24)

project(Rebuild
    VERSION 0.1.0
    DESCRIPTION "From-scratch rebuild of the mini engine - D4: Vulkan instance, layers, surface, device"
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

FetchContent_MakeAvailable(glfw)
add_executable(Rebuild
    src/vulkanTools.cpp
    src/main.cpp)

target_include_directories(Rebuild PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_link_libraries(Rebuild PRIVATE
    glfw
    Vulkan::Vulkan)

if(MSVC)
    target_compile_options(Rebuild PRIVATE /utf-8)

    target_compile_options(Rebuild PRIVATE /W4)
endif()

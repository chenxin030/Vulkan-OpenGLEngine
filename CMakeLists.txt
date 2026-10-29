cmake_minimum_required(VERSION 3.24)

project(VulkanOpenGLEngine
    VERSION 0.1.0
    DESCRIPTION "基于 Vulkan / OpenGL 的三维渲染引擎"
    LANGUAGES C CXX)


set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()
find_package(Vulkan REQUIRED)
find_package(OpenGL REQUIRED)
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
    GIT_TAG        v1.91.5-docking
    GIT_SHALLOW    TRUE)

FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        2c980bb59875b0d32144a71867fbdebb2f77cd20)
FetchContent_Declare(vma
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG        v3.1.0
    GIT_SHALLOW    TRUE)

FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE)

FetchContent_Declare(tinygltf
    GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
    GIT_TAG        v3.0.1
    GIT_SHALLOW    TRUE)

set(TINYGLTF3_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(TINYGLTF3_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw imgui stb vma nlohmann_json tinygltf)

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
    mesh.vert
    mesh.frag
    tonemap.vert
    tonemap.frag
    shadow.vert
    shadow.frag)

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

add_custom_target(VulkanOpenGLEngineShaders ALL DEPENDS ${SHADER_SPIRV_FILES})
set(IMGUI_SOURCES
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp")

add_executable(VulkanOpenGLEngine
    src/industrial/IndustrialSystem.cpp
    src/industrial/IndustrialScene.cpp
    src/player/PlayerLayer.cpp
    src/scene/SceneRuntime.cpp
    src/scene/SceneSystem.cpp
    src/scene/AnimationSystem.cpp
    src/core/LaunchOptions.cpp
    src/core/Application.cpp
    src/core/LayerStack.cpp
    src/platform/GlfwEvents.cpp
    src/editor/EditorLayer.cpp
    src/editor/Editor.cpp
    src/editor/EditorLayout.cpp
    src/editor/AssetBrowser.cpp
    src/editor/ProjectAssets.cpp
    src/editor/SceneDocument.cpp
    src/platform/FileDialogs.cpp
    src/editor/EditorGeometry.cpp
    third_party/ImGuizmo/ImGuizmo.cpp
    src/rhi/opengl/openglRenderer.cpp
    src/rhi/vulkan/vulkan_memory.cpp
    src/rhi/vulkan/vulkanRenderer.cpp
    src/main.cpp
    src/rhi/RendererFactory.cpp
    src/platform/FileWatch.cpp
    src/platform/GlfwInput.cpp
    src/scene/CameraController.cpp
    src/scene/TangentSpace.cpp
    third_party/MikkTSpace/mikktspace.c
    src/scene/MeshLibrary.cpp
    src/scene/Environment.cpp
    src/scene/MaterialLibrary.cpp
    src/scene/ModelImporter.cpp
    src/scene/Registry.cpp
    src/scene/RenderSnapshot.cpp
    src/scene/SceneSerializer.cpp
    "${tinygltf_SOURCE_DIR}/tiny_gltf_v3.c"
    src/scene/Scene.cpp
    src/scene/Systems.cpp
    ${IMGUI_SOURCES})


file(GLOB_RECURSE ENGINE_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp")
file(GLOB SHADER_SOURCES CONFIGURE_DEPENDS
    "${SHADER_SOURCE_DIR}/*.vert"
    "${SHADER_SOURCE_DIR}/*.frag")
set_source_files_properties(${SHADER_SOURCES} PROPERTIES HEADER_FILE_ONLY TRUE)

set(IMGUI_HEADERS
    "${imgui_SOURCE_DIR}/imgui.h"
    "${imgui_SOURCE_DIR}/imgui_internal.h"
    "${imgui_SOURCE_DIR}/imconfig.h"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.h"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.h"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.h")
set(MIKKTSPACE_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/third_party/MikkTSpace/mikktspace.h")
set(TINYGLTF_HEADER "${tinygltf_SOURCE_DIR}/tiny_gltf_v3.h")
target_sources(VulkanOpenGLEngine PRIVATE ${ENGINE_HEADERS} ${SHADER_SOURCES} ${IMGUI_HEADERS}
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ImGuizmo/ImGuizmo.h"
    ${MIKKTSPACE_HEADER} ${TINYGLTF_HEADER})


function(organize_project_sources target)
    get_target_property(target_source_files ${target} SOURCES)
    foreach(source_file IN LISTS target_source_files)
        get_filename_component(source_path "${source_file}" ABSOLUTE
            BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        file(RELATIVE_PATH relative_source "${CMAKE_CURRENT_SOURCE_DIR}" "${source_path}")
        if(relative_source MATCHES "^src/")
            source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "Engine" FILES "${source_path}")
        elseif(relative_source MATCHES "^shaders/")
            source_group(TREE "${SHADER_SOURCE_DIR}" PREFIX "Shaders" FILES "${source_path}")
        elseif(relative_source MATCHES "^third_party/")
            source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/third_party" PREFIX "ThirdParty" FILES "${source_path}")
        endif()
    endforeach()
endfunction()

organize_project_sources(VulkanOpenGLEngine)
source_group(TREE "${imgui_SOURCE_DIR}" PREFIX "ThirdParty/ImGui" FILES ${IMGUI_SOURCES} ${IMGUI_HEADERS})
source_group("ThirdParty/TinyGLTF" FILES "${tinygltf_SOURCE_DIR}/tiny_gltf_v3.c" ${TINYGLTF_HEADER})

add_dependencies(VulkanOpenGLEngine VulkanOpenGLEngineShaders)

if(WIN32)
    add_custom_target(DemoTexture
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/assets/make-demo-texture.ps1"
        COMMENT "Generating the demo texture into assets/textures/"
        VERBATIM)
endif()

target_include_directories(VulkanOpenGLEngine PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ImGuizmo"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${stb_SOURCE_DIR}"
    "${vma_SOURCE_DIR}/include"
    "${imgui_SOURCE_DIR}"
    "${imgui_SOURCE_DIR}/backends")
target_include_directories(VulkanOpenGLEngine PRIVATE "${tinygltf_SOURCE_DIR}" "${stb_SOURCE_DIR}")

target_compile_definitions(VulkanOpenGLEngine PRIVATE
    SHADER_DIR="${SHADER_OUTPUT_DIR}"
    SHADER_SOURCE_DIR="${SHADER_SOURCE_DIR}"
    IBL_CACHE_DIR="${CMAKE_CURRENT_BINARY_DIR}/ibl-cache"
    ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
    PROJECT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
    EDITOR_LAYOUT_PATH="${CMAKE_CURRENT_BINARY_DIR}/editor-layout.ini"
    GLM_FORCE_RADIANS
    GLM_FORCE_DEPTH_ZERO_TO_ONE)

target_link_libraries(VulkanOpenGLEngine PRIVATE
    glfw
    nlohmann_json::nlohmann_json
    OpenGL::GL
    Vulkan::Vulkan)
if(WIN32)
    target_link_libraries(VulkanOpenGLEngine PRIVATE comdlg32)
endif()

if(MSVC)
    target_compile_options(VulkanOpenGLEngine PRIVATE /utf-8)

    target_compile_options(VulkanOpenGLEngine PRIVATE /W4)
    set_source_files_properties(${IMGUI_SOURCES} PROPERTIES COMPILE_OPTIONS "/w")
endif()


cmake_minimum_required(VERSION 3.24)

project(Rebuild
    VERSION 0.1.0
    DESCRIPTION "From-scratch rebuild of the mini engine - D5/D6: swap chain, render pass, shaders"
    LANGUAGES C CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()
find_package(Vulkan REQUIRED)
# OpenGL 后端用（D41–D42）：Windows 上它给的是 opengl32，只需要链接即可 ——
# 1.1 以上的入口点在运行时由我们自己的加载器取（见 src/rhi/opengl/openglFunctions.h）。
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
    GIT_TAG        v1.91.5
    GIT_SHALLOW    TRUE)

FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        2c980bb59875b0d32144a71867fbdebb2f77cd20)
# stb 故意不写 GIT_SHALLOW：浅克隆只能取分支或 tag 的尖端，取不到任意 commit 哈希。
# stb 很小，全量克隆的代价不值得优化。

# VulkanMemoryAllocator：header-only，实现在我们自己的一个 TU 里
# （src/rhi/vulkan/vulkan_memory.cpp），所以没有 target 可以 link —— 只有一个 include 目录要指。
#
# 这个 tag 找不到时，先列出有哪些 tag 再钉一个：
#   git ls-remote --tags https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
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
    triangle.vert
    triangle.frag
    cube.vert
    cube.frag
    cube_mvp.vert
    cube_mvp.frag
    cube_tex.vert
    cube_tex.frag
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

add_custom_target(RebuildShaders ALL DEPENDS ${SHADER_SPIRV_FILES})
set(IMGUI_SOURCES
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp"
    # 第二个后端的 ImGui 半边（D41 接上；它自带一个最小的 GL 加载器，不需要额外依赖）。
    "${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp")

add_executable(Rebuild
    src/editor/Editor.cpp
    src/editor/EditorGeometry.cpp
    third_party/ImGuizmo/ImGuizmo.cpp
    # 第二个后端。它存在的意义是检验 Renderer 接口是不是真的与后端无关 ——
    # 所以它和第一个并列放在这里。
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
    src/scene/SceneUpdater.cpp
    src/scene/Systems.cpp
    ${IMGUI_SOURCES})

if(BUILD_TESTING)
    add_executable(RebuildTests
        tests/RegistryTests.cpp
        tests/Stage6Tests.cpp
        tests/Stage7Tests.cpp
        tests/Stage8Tests.cpp
        tests/Stage9Tests.cpp
        src/editor/EditorGeometry.cpp
        src/scene/TangentSpace.cpp
        third_party/MikkTSpace/mikktspace.c
        src/scene/MeshLibrary.cpp
        src/scene/Environment.cpp
        src/scene/MaterialLibrary.cpp
        src/scene/ModelImporter.cpp
        src/scene/Registry.cpp)
    target_sources(RebuildTests PRIVATE
        src/scene/RenderSnapshot.cpp
        src/scene/Scene.cpp
        src/scene/SceneSerializer.cpp
        "${tinygltf_SOURCE_DIR}/tiny_gltf_v3.c"
        src/scene/SceneUpdater.cpp
        src/scene/Systems.cpp)
    target_link_libraries(RebuildTests PRIVATE nlohmann_json::nlohmann_json)
    target_include_directories(RebuildTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_include_directories(RebuildTests PRIVATE "${tinygltf_SOURCE_DIR}" "${stb_SOURCE_DIR}")
    target_compile_definitions(RebuildTests PRIVATE TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}" IBL_CACHE_DIR="${CMAKE_CURRENT_BINARY_DIR}/ibl-cache")
    add_test(NAME Rebuild.Registry COMMAND RebuildTests)
endif()

# Visual Studio 的虚拟文件夹按现有职责目录组织，.h 与 .cpp 放在同一组。
# 这里只自动发现用于浏览的头文件/GLSL；参与编译的 C/C++ 仍由上面的显式列表决定。
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
target_sources(Rebuild PRIVATE ${ENGINE_HEADERS} ${SHADER_SOURCES} ${IMGUI_HEADERS}
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ImGuizmo/ImGuizmo.h"
    ${MIKKTSPACE_HEADER} ${TINYGLTF_HEADER})

# 测试项目展示它使用的场景/编辑器几何头文件，不混入渲染后端的头文件。
if(BUILD_TESTING)
    set(TEST_HEADERS ${ENGINE_HEADERS})
    list(FILTER TEST_HEADERS INCLUDE REGEX "/src/(scene/|editor/EditorGeometry\\.h$)")
    target_sources(RebuildTests PRIVATE ${TEST_HEADERS} ${MIKKTSPACE_HEADER} ${TINYGLTF_HEADER})
endif()

# 用法：调用 organize_project_sources(目标名)，从该目标的 SOURCES 生成 IDE 分组。
# 不移动磁盘文件，不改变 include 路径或链接关系；新增头文件会触发 CMake 自动重新生成。
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
        elseif(relative_source MATCHES "^tests/")
            source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/tests" PREFIX "Tests" FILES "${source_path}")
        elseif(relative_source MATCHES "^third_party/")
            source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/third_party" PREFIX "ThirdParty" FILES "${source_path}")
        endif()
    endforeach()
endfunction()

organize_project_sources(Rebuild)
if(BUILD_TESTING)
    organize_project_sources(RebuildTests)
endif()
source_group(TREE "${imgui_SOURCE_DIR}" PREFIX "ThirdParty/ImGui" FILES ${IMGUI_SOURCES} ${IMGUI_HEADERS})
source_group("ThirdParty/TinyGLTF" FILES "${tinygltf_SOURCE_DIR}/tiny_gltf_v3.c" ${TINYGLTF_HEADER})

add_dependencies(Rebuild RebuildShaders)

if(WIN32)
    add_custom_target(DemoTexture
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File "${CMAKE_CURRENT_SOURCE_DIR}/tools/make-demo-texture.ps1"
        COMMENT "Generating the demo texture into assets/"
        VERBATIM)
endif()

target_include_directories(Rebuild PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ImGuizmo"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${stb_SOURCE_DIR}"
    "${vma_SOURCE_DIR}/include"
    "${imgui_SOURCE_DIR}"
    "${imgui_SOURCE_DIR}/backends")
target_include_directories(Rebuild PRIVATE "${tinygltf_SOURCE_DIR}" "${stb_SOURCE_DIR}")

target_compile_definitions(Rebuild PRIVATE
    SHADER_DIR="${SHADER_OUTPUT_DIR}"
    # OpenGL 在**运行时**自己编译 GLSL，所以它要的是源码目录，而不是 .spv 目录。
    # 两个后端，两个答案：GLSL 什么时候变成机器码。
    SHADER_SOURCE_DIR="${SHADER_SOURCE_DIR}"
    IBL_CACHE_DIR="${CMAKE_CURRENT_BINARY_DIR}/ibl-cache"
    ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
    PROJECT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
    GLM_FORCE_RADIANS
    GLM_FORCE_DEPTH_ZERO_TO_ONE)

target_link_libraries(Rebuild PRIVATE
    glfw
    nlohmann_json::nlohmann_json
    OpenGL::GL
    Vulkan::Vulkan)

if(MSVC)
    target_compile_options(Rebuild PRIVATE /utf-8)
    if(BUILD_TESTING)
        target_compile_options(RebuildTests PRIVATE /utf-8)
    endif()

    target_compile_options(Rebuild PRIVATE /W4)
    set_source_files_properties(${IMGUI_SOURCES} PROPERTIES COMPILE_OPTIONS "/w")
endif()

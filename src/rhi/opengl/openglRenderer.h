#pragma once

#include "openglFunctions.h"
#include "platform/FileWatch.h"
#include "rhi/Renderer.h"
#include "scene/Image.h"
#include "scene/MeshLibrary.h"

#include <cstdint>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// 渲染器的 OpenGL 后端。
//
// 每个对象的变换（Model 矩阵）。Vulkan 使用 push constant；OpenGL 没有这个概念，而是在每次绘制前使用普通的 uniform。
// 相机数据。Vulkan 使用带有每帧 uniform buffer 的描述符集；OpenGL 使用绑定一次的 uniform block，通过 glBindBufferBase。
// Y 轴翻转。Vulkan 的 NDC Y 向下，其投影在后端被翻转。OpenGL 的 Y 向上，没有翻转，因此两个着色器在这方面不同。
// 线程。OpenGL 是在调用线程上的状态机，有一个“当前上下文”；Vulkan 都没有。
// ---------------------------------------------------------------------------
class OpenGLRenderer : public Renderer
{
public:
    OpenGLRenderer() = default;
    ~OpenGLRenderer() override = default;

    OpenGLRenderer(const OpenGLRenderer&) = delete;
    OpenGLRenderer& operator=(const OpenGLRenderer&) = delete;

    // Renderer
    void ConfigureWindowHints() const override;
    void Init(GLFWwindow* window) override;
    void DrawFrame(const Scene& scene) override;
    void WaitIdle() override;
    void Cleanup() override;

private:
    // 每个网格一个顶点数组对象。
    // VAO 是没有 Vulkan 对应部分的：核心配置的 OpenGL 要求属性布局记录到绑定的 VAO 中，而 Vulkan 则在管线对象中声明它。
    struct GlMesh
    {
        GLuint vertexArray = 0;
        GLuint vertexBuffer = 0;
        GLuint indexBuffer = 0;
        GLuint indexCount = 0;
    };

    void InitImGui();
    void ShutdownImGui();
    void BuildImGuiFrame();

    void EnsureMeshesUploaded(const MeshLibrary& meshes);
    void UploadMesh(const MeshData& mesh, uint32_t meshIndex);
    void DestroyMeshes();

    void CreateShaderProgram();
    GLuint CompileShader(GLenum stage, const std::string& source, const std::string& path) const;

    void CreateTexture();
    void UploadTexture(const Image& image);
    void DestroyTexture();
    void ReloadTexture();

    void UpdateCameraUniforms(const Scene& scene);

    GLFWwindow* m_window = nullptr;

    GlFunctions m_gl;
    std::string m_rendererName;
    std::string m_versionString;

    bool m_imguiInitialized = false;

    std::vector<GlMesh> m_meshes;

    GLuint m_shaderProgram = 0;
    GLint m_modelLocation = -1;
    GLint m_textureLocation = -1;

    GLuint m_cameraUniformBuffer = 0;

    GLuint m_texture = 0;
    uint32_t m_textureWidth = 0;
    uint32_t m_textureHeight = 0;

    // 和 Vulkan 一样的热重载。但不通用，不同后端根据自身引擎特性实现。
    FileWatch m_textureWatch;
};

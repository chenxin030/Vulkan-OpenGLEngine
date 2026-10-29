#pragma once

#include "openglFunctions.h"
#include "platform/FileWatch.h"
#include "rhi/Renderer.h"
#include "scene/MaterialLibrary.h"
#include "scene/MeshLibrary.h"
#include "scene/Environment.h"
#include <array>

#include <cstdint>
#include <string>
#include <vector>

// 渲染器的 OpenGL 后端。
//
// 每个对象的变换（Model 矩阵）。Vulkan 使用 push constant；
// OpenGL 没有这个概念，而是在每次绘制前使用普通的 uniform。
// 相机数据。Vulkan 使用带有每帧 uniform buffer 的描述符集；OpenGL 使用绑定一次的 uniform block，通过 glBindBufferBase。
// Y 轴翻转。Vulkan 的 NDC Y 向下，其投影在后端被翻转。OpenGL 的 Y 向上，没有翻转，因此两个着色器在这方面不同。
// 线程。OpenGL 是在调用线程上的状态机，有一个“当前上下文”；Vulkan 都没有。
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
    void DrawFrame(const RenderSnapshot& snapshot,
        const std::function<void()>& drawEditorUi) override;
    ViewportImage GetViewportImage() const override { return m_viewportImage; }
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

    struct GlTexture
    {
        GLuint handle = 0;
        uint32_t width = 0;
        uint32_t height = 0;
    };

    void InitImGui();
    void ShutdownImGui();
    void BuildImGuiFrame(const std::function<void()>& drawEditorUi);

    void EnsureMeshesUploaded(const MeshLibrary& meshes);
    void EnsureMaterialTexturesUploaded(const MaterialLibrary& materials);
    void DestroyMaterialTextures();
    void UploadMesh(const MeshData& mesh, uint32_t meshIndex);
    void DestroyMeshes();

    void CreateShaderProgram();
    GLuint CompileShader(GLenum stage, const std::string& source, const std::string& path) const;

    void EnsureEnvironmentUploaded(std::shared_ptr<const EnvironmentData> data);
    GLuint UploadFloatTexture(const FloatTexture& texture);
    std::shared_ptr<const EnvironmentData> m_uploadedEnvironment;
    std::array<GLuint, 4> m_environmentTextures{};

    void CreateTexture();
    void UploadTexture(const Image& image);
    void DestroyTexture();
    void ReloadTexture();

    void UpdateCameraUniforms(const RenderSnapshot& snapshot);
    void CreateShadowResources();
    void DestroyShadowResources();
    void ResizeHdrTarget(int width, int height);
    void DestroyHdrTarget();

    GLFWwindow* m_window = nullptr;

    GlFunctions m_gl;
    std::string m_rendererName;
    std::string m_versionString;

    bool m_imguiInitialized = false;
    bool m_glLoaded = false;
    bool m_imguiContextCreated = false;
    bool m_imguiGlfwInitialized = false;

    std::vector<GlMesh> m_meshes;
    std::vector<GlTexture> m_materialTextures;
    uint64_t m_meshLibraryIdentity = 0;
    uint64_t m_meshLibraryRevision = UINT64_MAX;
    uint64_t m_materialLibraryIdentity = 0;
    uint64_t m_materialLibraryRevision = UINT64_MAX;

    GLuint m_shaderProgram = 0;
    GLint m_modelLocation = -1;
    GLint m_tintLocation = -1;
    GLint m_metallicLocation = -1;
    GLint m_textureLocation = -1;
    GLint m_emissive = -1;
    GLint m_normalInfoLocation = -1;
    GLint m_normalSamplerLocation = -1;
    GLuint m_flatNormalTexture = 0;

    GLuint m_cameraUniformBuffer = 0;
    GLuint m_shadowFramebuffer = 0;
    GLuint m_shadowDepthTexture = 0;
    GLuint m_shadowProgram = 0;
    GLint m_shadowModelLocation = -1;
    GLint m_shadowLightViewProjectionLocation = -1;
    GLint m_shadowSamplerLocation = -1;

    std::string m_viewportStatus;
    ViewportImage m_viewportImage;
    ViewportExtent m_failedViewportExtent;
    GLuint m_ldrFramebuffer = 0, m_ldrColorTexture = 0;
    int m_msaaSamples = 1;
    GLuint m_msaaFramebuffer = 0, m_msaaColorTexture = 0;
    GLuint m_hdrFramebuffer = 0;
    GLuint m_hdrColorTexture = 0;
    GLuint m_hdrDepthTexture = 0;
    int m_hdrWidth = 0;
    int m_hdrHeight = 0;
    GLuint m_tonemapProgram = 0;
    GLint m_hdrColorLocation = -1;
    GLuint m_fullscreenVertexArray = 0;

    GLuint m_texture = 0;
    uint32_t m_textureWidth = 0;
    uint32_t m_textureHeight = 0;

    // 和 Vulkan 一样的热重载。但不通用，不同后端根据自身引擎特性实现。
    FileWatch m_textureWatch;
};

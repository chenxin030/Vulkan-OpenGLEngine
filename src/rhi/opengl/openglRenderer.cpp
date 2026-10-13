#include "openglRenderer.h"

#include "scene/MaterialLibrary.h"
#include "scene/RenderSnapshot.h"
#include "scene/Vertex.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <glm/gtc/type_ptr.hpp>

#include <cstddef>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

    constexpr int kContextVersionMajor = 4;
    constexpr int kContextVersionMinor = 5;

    struct GlCameraUniforms
    {
        glm::mat4 view;
        glm::mat4 proj;
        glm::vec4 cameraPosition;
        glm::vec4 lightDirection;
        glm::vec4 lightColor;
        glm::mat4 lightViewProjection;
        glm::mat4 inverseViewProjection;
        glm::vec4 environment;
        glm::vec4 lightingTerms;
        glm::vec4 framebufferSize;

    };
    static_assert(offsetof(GlCameraUniforms, environment) == 304 && sizeof(GlCameraUniforms) == 352,
        "environment camera UBO must match std140 shader offsets");

    const char* GlStringOrUnknown(const unsigned char* value)
    {
        return value != nullptr ? reinterpret_cast<const char*>(value) : "<unknown>";
    }

    std::string ReadTextFile(const std::string& path)
    {
        std::ifstream file(path, std::ios::ate | std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("failed to open '" + path + "'");
        }

        const auto size = static_cast<std::size_t>(file.tellg());
        std::string text(size, '\0');

        file.seekg(0);
        file.read(text.data(), static_cast<std::streamsize>(size));

        return text;
    }

    std::string ShaderSourcePath(const char* fileName)
    {
        return std::string(SHADER_SOURCE_DIR) + "/" + fileName;
    }

    std::string TextureAssetPath()
    {
        return std::string(ASSET_DIR) + "/checkerboard.ppm";
    }

}  // namespace

void OpenGLRenderer::ConfigureWindowHints() const
{
    // 和 Vulkan 相反
    // GLFW 在这里创建上下文并拥有缓冲区交换，而 Vulkan 后端只向 GLFW 要一个窗口和表面，并自己处理显示。
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, kContextVersionMajor);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, kContextVersionMinor);

    // 核心配置，向前兼容：没有已弃用的固定功能入口点可用。
    // 兼容性配置会让这个后端悄悄依赖 Vulkan 没有的 API，这样两个后端之间的比较就没有意义了。
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);

    // 一个支持 sRGB 的默认帧缓冲，这样下面启用 GL_FRAMEBUFFER_SRGB 时才会生效。
    // 没有它的话，后台会渲染到普通 8 位缓冲区的线性空间，看起来会比 Vulkan 的渲染明显淡很多。
    glfwWindowHint(GLFW_SRGB_CAPABLE, GLFW_TRUE);
}

void OpenGLRenderer::Init(GLFWwindow* window)
{
    m_window = window;

    // 在Vulkan这边没有这一步的等价操作：OpenGL是一个附加在调用线程上的状态机，
    // 所以在加载或查询任何东西之前，必须先让上下文变为当前的。Vulkan没有这样的概念——一个设备就是一个设备。
    glfwMakeContextCurrent(window);

    // 入口点只能针对当前上下文解析，这就是为什么它不是在启动时全局初始化。
    if (!m_gl.Load()) {
        throw std::runtime_error(std::string("failed to load OpenGL entry point: ") +
            m_gl.FirstMissing());
    }

    m_versionString = GlStringOrUnknown(m_gl.glGetString(GL_VERSION));
    m_rendererName = GlStringOrUnknown(m_gl.glGetString(GL_RENDERER));

    // 说明 Vulkan 后端在这里的管线对象内部表示是全局的。
    // 深度测试和剔除只需启用一次并保持启用；在 Vulkan 中，它们已经被烘焙进管线，不会在绘制时被忘记。
    m_gl.glEnable(GL_DEPTH_TEST);
    m_gl.glEnable(GL_CULL_FACE);
    m_gl.glCullFace(GL_BACK);

    // OpenGL 默认是 GL_CCW

    // 写入时线性->sRGB。配合纹理上的 GL_SRGB8_ALPHA8，这就是 OpenGL 对 Vulkan 后端从 sRGB 交换链格式加上 sRGB 纹理格式获得的效果的表述。只做其中一个是第二个后端看起来几乎正确的经典原因。
    m_gl.glEnable(GL_FRAMEBUFFER_SRGB);

    // 和 Vulkan 后端用**同一个背景色**（那边是 VkClearValue）。改一处就要改另一处 ——
    // 这个值现在没有单一归属，理由见 Vulkan 那处的注释。
    m_gl.glClearColor(0.05f, 0.06f, 0.09f, 1.0f);

    // GL 异步捕捉错误并报告，在上面状态设置无效枚举，否则会在后面以视觉错误的形式表现出来。
    // 在这里读回它可以把原因和症状保持在同一个地方。
    if (const GLenum error = m_gl.glGetError(); error != GL_NO_ERROR) {
        std::cerr << "[opengl] GL error 0x" << std::hex << error << std::dec
            << " during state initialisation" << std::endl;
    }

    // 一个统一缓冲区，只绑定一次。Vulkan 因为没有全局绑定统一块的概念，为每个飞行中的帧分配一个缓冲区，并通过描述符集绑定。
    m_gl.glGenBuffers(1, &m_cameraUniformBuffer);
    m_gl.glBindBuffer(GL_UNIFORM_BUFFER, m_cameraUniformBuffer);
    m_gl.glBufferData(GL_UNIFORM_BUFFER, sizeof(GlCameraUniforms), nullptr, GL_STATIC_DRAW);
    m_gl.glBindBufferBase(GL_UNIFORM_BUFFER, 0, m_cameraUniformBuffer);
    m_gl.glBindBuffer(GL_UNIFORM_BUFFER, 0);

    CreateShaderProgram();
    const GLuint toneVertex = CompileShader(GL_VERTEX_SHADER,
        ReadTextFile(ShaderSourcePath("gl_tonemap.vert")), ShaderSourcePath("gl_tonemap.vert"));
    const GLuint toneFragment = CompileShader(GL_FRAGMENT_SHADER,
        ReadTextFile(ShaderSourcePath("gl_tonemap.frag")), ShaderSourcePath("gl_tonemap.frag"));
    m_tonemapProgram = m_gl.glCreateProgram();
    m_gl.glAttachShader(m_tonemapProgram, toneVertex);
    m_gl.glAttachShader(m_tonemapProgram, toneFragment);
    m_gl.glLinkProgram(m_tonemapProgram);
    m_gl.glDeleteShader(toneVertex);
    m_gl.glDeleteShader(toneFragment);
    GLint toneLinked = GL_FALSE;
    m_gl.glGetProgramiv(m_tonemapProgram, GL_LINK_STATUS, &toneLinked);
    if (toneLinked != GL_TRUE) throw std::runtime_error("failed to link tone-map shader");
    m_hdrColorLocation = m_gl.glGetUniformLocation(m_tonemapProgram, "uHdrColor");
    if (m_hdrColorLocation < 0) throw std::runtime_error("tone-map shader is missing uHdrColor");
    m_gl.glGenVertexArrays(1, &m_fullscreenVertexArray);
    CreateTexture();
    // 即使没有法线图也绑定有效的线性 fallback；无图的材质仍直接使用 Nbase。
    m_gl.glGenTextures(1, &m_flatNormalTexture);
    m_gl.glBindTexture(GL_TEXTURE_2D, m_flatNormalTexture);
    const uint8_t flatNormal[] = {128,128,255,255};
    m_gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, flatNormal);
    m_gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    m_gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    CreateShadowResources();
    int framebufferWidth = 0, framebufferHeight = 0;
    glfwGetFramebufferSize(m_window, &framebufferWidth, &framebufferHeight);
    ResizeHdrTarget(framebufferWidth, framebufferHeight);
    InitImGui();
}

void OpenGLRenderer::DrawFrame(const RenderSnapshot& snapshot,
    const std::function<void()>& drawEditorUi)
{
    // 热重载，每个后端按自己的方式引擎特性的实现。
    if (m_textureWatch.ConsumeChanged()) {
        ReloadTexture();
    }

    EnsureEnvironmentUploaded(snapshot.environmentData ? snapshot.environmentData : FallbackEnvironment());
    EnsureMeshesUploaded(*snapshot.meshes);
    EnsureMaterialTexturesUploaded(*snapshot.materials);
    UpdateCameraUniforms(snapshot);

    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetFramebufferSize(m_window, &framebufferWidth, &framebufferHeight);
    if (framebufferWidth == 0 || framebufferHeight == 0) return;
    if (framebufferWidth != m_hdrWidth || framebufferHeight != m_hdrHeight)
        ResizeHdrTarget(framebufferWidth, framebufferHeight);

    // Render the opaque scene from the directional light into a fixed-size depth map.
    m_gl.glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFramebuffer);
    m_gl.glViewport(0, 0, 1024, 1024);
    m_gl.glClear(GL_DEPTH_BUFFER_BIT);
    m_gl.glUseProgram(m_shadowProgram);
    m_gl.glUniformMatrix4fv(m_shadowLightViewProjectionLocation, 1, GL_FALSE,
        glm::value_ptr(snapshot.lightViewProjection));
    for (const DrawItem& item : snapshot.drawItems) {
        if (item.unlit) continue;
        m_gl.glFrontFace(item.mirrored ? GL_CW : GL_CCW);
        const GlMesh& mesh = m_meshes.at(item.meshIndex);
        m_gl.glUniformMatrix4fv(m_shadowModelLocation, 1, GL_FALSE,
            glm::value_ptr(item.worldTransform));
        m_gl.glBindVertexArray(mesh.vertexArray);
        m_gl.glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.indexCount), GL_UNSIGNED_SHORT, nullptr);
    }
    m_gl.glBindVertexArray(0);
    m_gl.glUseProgram(0);
    m_gl.glBindFramebuffer(GL_FRAMEBUFFER, m_hdrFramebuffer);
    m_gl.glViewport(0, 0, framebufferWidth, framebufferHeight);
    m_gl.glClearColor(0.05f, 0.06f, 0.09f, 0.0f);
    m_gl.glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    m_gl.glFrontFace(GL_CCW);
    // --- scene --------------------------------------------------------------
    m_gl.glUseProgram(m_shaderProgram);
    for (int i = 0; i < 4; ++i) {
        m_gl.glActiveTexture(GL_TEXTURE0 + 2 + i);
        m_gl.glBindTexture(i == 3 ? GL_TEXTURE_2D : GL_TEXTURE_CUBE_MAP, m_environmentTextures[i]);
    }
    m_gl.glActiveTexture(GL_TEXTURE0);
    m_gl.glActiveTexture(GL_TEXTURE0 + 1);
    m_gl.glBindTexture(GL_TEXTURE_2D, m_shadowDepthTexture);
    m_gl.glActiveTexture(GL_TEXTURE0);
    m_gl.glBindBufferBase(GL_UNIFORM_BUFFER, 0, m_cameraUniformBuffer);

    for (const DrawItem& item : snapshot.drawItems) {
        const GlMesh& mesh = m_meshes[item.meshIndex];

        // 每次绘制一个统一变量，Vulkan push constant。
        // 两者都是“小型数据，每次绘制都会变化且不需要描述符”；只是写法不同。
        m_gl.glUniformMatrix4fv(m_modelLocation, 1, GL_FALSE,
            glm::value_ptr(item.worldTransform));
        m_gl.glUniform3fv(m_tintLocation, 1, glm::value_ptr(item.tint));
        m_gl.glFrontFace(item.mirrored ? GL_CW : GL_CCW);
        const glm::vec4 material(item.metallicFactor, item.roughnessFactor,
            item.unlit ? 1.0f : 0.0f, item.normalScale);
        m_gl.glUniform4fv(m_metallicLocation, 1, glm::value_ptr(material));
        const glm::vec3 normalInfo(snapshot.normalMapping && item.normalTextureIndex != UINT32_MAX ? 1.0f : 0.0f,
            float(snapshot.normalDebug), 0.0f);
        m_gl.glUniform3fv(m_normalInfoLocation, 1, glm::value_ptr(normalInfo));
        m_gl.glActiveTexture(GL_TEXTURE0 + 6);
        m_gl.glBindTexture(GL_TEXTURE_2D, item.normalTextureIndex == UINT32_MAX
            ? m_flatNormalTexture : m_materialTextures.at(item.normalTextureIndex).handle);
        m_gl.glActiveTexture(GL_TEXTURE0);
        m_gl.glUniform3fv(m_emissive, 1, glm::value_ptr(item.emissive));

        const GLuint texture = item.textureIndex == 0xFFFFFFFFu
            ? m_texture : m_materialTextures.at(item.textureIndex).handle;
        m_gl.glBindTexture(GL_TEXTURE_2D, texture);

        m_gl.glBindVertexArray(mesh.vertexArray);
        m_gl.glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.indexCount),
            GL_UNSIGNED_SHORT, nullptr);
    }

    m_gl.glBindVertexArray(0);
    m_gl.glUseProgram(0);

    // 恢复默认绕序，避免最后一个镜像 draw 把全屏三角形也剔除。
    m_gl.glFrontFace(GL_CCW);
    // Tone-map the linear RGBA16F scene target into the sRGB window, then draw UI on top.
    m_gl.glBindFramebuffer(GL_FRAMEBUFFER, 0);
    m_gl.glDrawBuffer(GL_BACK);
    m_gl.glReadBuffer(GL_BACK);
    m_gl.glViewport(0, 0, framebufferWidth, framebufferHeight);
    m_gl.glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    m_gl.glDisable(GL_DEPTH_TEST);
    m_gl.glUseProgram(m_tonemapProgram);
    m_gl.glUniform1i(m_hdrColorLocation, 0);
    m_gl.glActiveTexture(GL_TEXTURE0);
    m_gl.glBindTexture(GL_TEXTURE_2D, m_hdrColorTexture);
    m_gl.glBindVertexArray(m_fullscreenVertexArray);
    m_gl.glDrawArrays(GL_TRIANGLES, 0, 3);
    m_gl.glBindVertexArray(0);
    m_gl.glUseProgram(0);
    m_gl.glEnable(GL_DEPTH_TEST);

    // --- overlay ------------------------------------------------------------
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    BuildImGuiFrame(drawEditorUi);
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    glfwSwapBuffers(m_window);
}

void OpenGLRenderer::WaitIdle()
{
    // glFinish 是 OpenGL 中最接近 vkDeviceWaitIdle 的东西：它会阻塞，直到所有提交的命令都完成为止。

    // 它的开销并不等同。vkDeviceWaitIdle 是等待队列中的工作完成；而 glFinish 则会阻塞管线。
    // 正确的是在退出的时候等待，但每帧这样做就是错的,而且接口上没有任何提示。
    m_gl.glFinish();
}

void OpenGLRenderer::Cleanup()
{
    ShutdownImGui();

    DestroyTexture();
    DestroyMaterialTextures();
    m_gl.glDeleteTextures(1, &m_flatNormalTexture);
    m_flatNormalTexture = 0;
    m_gl.glDeleteTextures(4, m_environmentTextures.data());
    m_uploadedEnvironment.reset();
    DestroyMeshes();
    DestroyShadowResources();
    DestroyHdrTarget();
    if (m_tonemapProgram != 0) {
        m_gl.glDeleteProgram(m_tonemapProgram);
        m_tonemapProgram = 0;
    }
    if (m_fullscreenVertexArray != 0) {
        m_gl.glDeleteVertexArrays(1, &m_fullscreenVertexArray);
        m_fullscreenVertexArray = 0;
    }

    if (m_cameraUniformBuffer != 0) {
        m_gl.glDeleteBuffers(1, &m_cameraUniformBuffer);
        m_cameraUniformBuffer = 0;
    }
    if (m_shaderProgram != 0) {
        m_gl.glDeleteProgram(m_shaderProgram);
        m_shaderProgram = 0;
    }

    // 和 Vulkan 不同，没有其他东西需要释放：
    // 上下文和交换链属于 GLFW，GLFW 会一直拥有它们直到 glfwDestroyWindow。
}

// ---------------------------------------------------------------------------
// Meshes
// ---------------------------------------------------------------------------

GLuint OpenGLRenderer::UploadFloatTexture(const FloatTexture& texture)
{
    GLuint handle = 0;
    m_gl.glGenTextures(1, &handle);
    const GLenum target = texture.layers == 6 ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
    m_gl.glActiveTexture(GL_TEXTURE0 + 2);
    m_gl.glBindTexture(target, handle);
    for (uint32_t mip = 0; mip < texture.mips.size(); ++mip) {
        const auto& level = texture.mips[mip];
        const size_t facePixels = size_t(level.width) * level.height * 4;
        for (uint32_t face = 0; face < texture.layers; ++face)
            m_gl.glTexImage2D(texture.layers == 6 ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + face : GL_TEXTURE_2D,
                mip, GL_RGBA32F, level.width, level.height, 0, GL_RGBA, GL_FLOAT,
                level.pixels.data() + face * facePixels);
    }
    // 输出 mip 已经按 GGX 计算，绝不能再调用 glGenerateMipmap 覆盖。
    m_gl.glTexParameteri(target, GL_TEXTURE_MAX_LEVEL, GLint(texture.mips.size() - 1));
    m_gl.glTexParameteri(target, GL_TEXTURE_MIN_FILTER, texture.mips.size() > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    m_gl.glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    m_gl.glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    m_gl.glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (texture.layers == 6) m_gl.glTexParameteri(target, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    if (m_gl.glGetError() != GL_NO_ERROR) {
        m_gl.glDeleteTextures(1, &handle);
        throw std::runtime_error("failed to upload floating-point environment texture");
    }
    return handle;
}

void OpenGLRenderer::EnsureEnvironmentUploaded(std::shared_ptr<const EnvironmentData> data)
{
    if (m_uploadedEnvironment == data) return;
    std::array<GLuint, 4> uploaded{};
    try {
        uploaded[0] = UploadFloatTexture(data->radiance);
        uploaded[1] = UploadFloatTexture(data->irradiance);
        uploaded[2] = UploadFloatTexture(data->prefiltered);
        uploaded[3] = UploadFloatTexture(*data->brdfLut);
    } catch (...) { m_gl.glDeleteTextures(4, uploaded.data()); throw; }
    WaitIdle();
    m_gl.glDeleteTextures(4, m_environmentTextures.data());
    m_environmentTextures = uploaded;
    m_uploadedEnvironment = std::move(data);
    m_gl.glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
    m_gl.glActiveTexture(GL_TEXTURE0);
}

void OpenGLRenderer::EnsureMeshesUploaded(const MeshLibrary& meshes)
{
    if (m_meshLibraryIdentity != meshes.Identity() || m_meshLibraryRevision != meshes.Revision()) {
        DestroyMeshes();
        m_meshLibraryIdentity = meshes.Identity();
        m_meshLibraryRevision = meshes.Revision();
    }
    const uint32_t target = meshes.Count();
    if (m_meshes.size() >= target) {
        return;
    }

    const uint32_t firstNew = static_cast<uint32_t>(m_meshes.size());
    m_meshes.resize(target);

    for (uint32_t index = firstNew; index < target; ++index) {
        UploadMesh(meshes.Get(index), index);
    }
}

void OpenGLRenderer::EnsureMaterialTexturesUploaded(const MaterialLibrary& materials)
{
    if (m_materialLibraryIdentity == materials.Identity() &&
        m_materialLibraryRevision == materials.Revision()) return;

    DestroyMaterialTextures();
    m_materialLibraryIdentity = materials.Identity();
    m_materialLibraryRevision = materials.Revision();
    m_materialTextures.resize(materials.TextureCount());
    for (uint32_t index = 0; index < materials.TextureCount(); ++index) {
        const Image& image = materials.GetTexture(index);
        GlTexture& texture = m_materialTextures[index];
        texture.width = image.width;
        texture.height = image.height;
        m_gl.glGenTextures(1, &texture.handle);
        m_gl.glBindTexture(GL_TEXTURE_2D, texture.handle);
        m_gl.glTexImage2D(GL_TEXTURE_2D, 0, image.srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8,
            static_cast<GLsizei>(image.width), static_cast<GLsizei>(image.height), 0,
            GL_RGBA, GL_UNSIGNED_BYTE, image.pixels.data());
        m_gl.glGenerateMipmap(GL_TEXTURE_2D);
        m_gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        m_gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        m_gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        m_gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    }
    m_gl.glBindTexture(GL_TEXTURE_2D, 0);
}

void OpenGLRenderer::DestroyMaterialTextures()
{
    for (GlTexture& texture : m_materialTextures) {
        if (texture.handle != 0) m_gl.glDeleteTextures(1, &texture.handle);
    }
    m_materialTextures.clear();
}

void OpenGLRenderer::UploadMesh(const MeshData& mesh, uint32_t meshIndex)
{
    GlMesh& glMesh = m_meshes[meshIndex];

    // 核心配置要求属性状态属于绑定的 VAO。
    // 相比之下，Vulkan 是把它放在管线对象里，所以这种类型在 vulkan 没有对应的东西。
    m_gl.glGenVertexArrays(1, &glMesh.vertexArray);
    m_gl.glBindVertexArray(glMesh.vertexArray);

    m_gl.glGenBuffers(1, &glMesh.vertexBuffer);
    m_gl.glBindBuffer(GL_ARRAY_BUFFER, glMesh.vertexBuffer);
    m_gl.glBufferData(GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(sizeof(Vertex) * mesh.vertices.size()),
        mesh.vertices.data(), GL_STATIC_DRAW);

    // element buffer 绑定被记录在 VAO 中，这就是为什么在绘制时它不需要重新绑定
    // 不像 Vulkan ，每次绘制时都会显式绑定它的索引缓冲区。
    m_gl.glGenBuffers(1, &glMesh.indexBuffer);
    m_gl.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, glMesh.indexBuffer);
    m_gl.glBufferData(GL_ELEMENT_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(sizeof(uint16_t) * mesh.indices.size()),
        mesh.indices.data(), GL_STATIC_DRAW);

    const auto stride = static_cast<GLsizei>(sizeof(Vertex));

    m_gl.glEnableVertexAttribArray(0);
    m_gl.glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride,
        reinterpret_cast<const void*>(offsetof(Vertex, position)));

    m_gl.glEnableVertexAttribArray(1);
    m_gl.glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
        reinterpret_cast<const void*>(offsetof(Vertex, color)));

    m_gl.glEnableVertexAttribArray(2);
    m_gl.glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride,
        reinterpret_cast<const void*>(offsetof(Vertex, texCoord)));

    m_gl.glEnableVertexAttribArray(4);
    m_gl.glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, stride,
        reinterpret_cast<const void*>(offsetof(Vertex, tangent)));

    m_gl.glEnableVertexAttribArray(3);
    m_gl.glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, stride,
        reinterpret_cast<const void*>(offsetof(Vertex, normal)));

    m_gl.glBindVertexArray(0);

    glMesh.indexCount = static_cast<GLuint>(mesh.indices.size());
}

void OpenGLRenderer::DestroyMeshes()
{
    for (GlMesh& mesh : m_meshes) {
        if (mesh.vertexArray != 0) {
            m_gl.glDeleteVertexArrays(1, &mesh.vertexArray);
        }
        if (mesh.vertexBuffer != 0) {
            m_gl.glDeleteBuffers(1, &mesh.vertexBuffer);
        }
        if (mesh.indexBuffer != 0) {
            m_gl.glDeleteBuffers(1, &mesh.indexBuffer);
        }
        mesh = GlMesh{};
    }
    m_meshes.clear();
}

// ---------------------------------------------------------------------------
// Shaders
// ---------------------------------------------------------------------------

GLuint OpenGLRenderer::CompileShader(GLenum stage, const std::string& source,
    const std::string& path) const
{
    const GLuint shader = m_gl.glCreateShader(stage);

    // glShaderSource 接收一个字符串数组和一个长度数组；如果只有一个字符串且长度数组为 null，就表示“以 null 结尾”，正好对应 std::string 的默认状态。
    const GLchar* sourcePointer = source.c_str();
    m_gl.glShaderSource(shader, 1, &sourcePointer, nullptr);
    m_gl.glCompileShader(shader);

    GLint status = GL_FALSE;
    m_gl.glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status == GL_TRUE) {
        return shader;
    }

    GLint logLength = 0;
    m_gl.glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);

    std::string log(static_cast<std::size_t>(logLength > 1 ? logLength : 1), '\0');
    m_gl.glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());

    m_gl.glDeleteShader(shader);

    throw std::runtime_error("failed to compile '" + path + "':\n" + log);
}

void OpenGLRenderer::CreateShaderProgram()
{
    const std::string vertexPath = ShaderSourcePath("gl_mesh.vert");
    const std::string fragmentPath = ShaderSourcePath("gl_mesh.frag");

    const GLuint vertexShader =
        CompileShader(GL_VERTEX_SHADER, ReadTextFile(vertexPath), vertexPath);
    const GLuint fragmentShader =
        CompileShader(GL_FRAGMENT_SHADER, ReadTextFile(fragmentPath), fragmentPath);

    m_shaderProgram = m_gl.glCreateProgram();
    m_gl.glAttachShader(m_shaderProgram, vertexShader);
    m_gl.glAttachShader(m_shaderProgram, fragmentShader);
    m_gl.glLinkProgram(m_shaderProgram);

    m_gl.glDeleteShader(vertexShader);
    m_gl.glDeleteShader(fragmentShader);

    GLint status = GL_FALSE;
    m_gl.glGetProgramiv(m_shaderProgram, GL_LINK_STATUS, &status);
    if (status != GL_TRUE) {
        GLint logLength = 0;
        m_gl.glGetProgramiv(m_shaderProgram, GL_INFO_LOG_LENGTH, &logLength);

        std::string log(static_cast<std::size_t>(logLength > 1 ? logLength : 1), '\0');
        m_gl.glGetProgramInfoLog(m_shaderProgram, static_cast<GLsizei>(log.size()),
            nullptr, log.data());

        throw std::runtime_error("failed to link the mesh shader program:\n" + log);
    }

    // 缓存一次：glGetUniformLocation 是字符串查找，每帧在绘制循环里调用它是有成本的，而且没有任何好处。
    m_modelLocation = m_gl.glGetUniformLocation(m_shaderProgram, "uModel");
    m_tintLocation = m_gl.glGetUniformLocation(m_shaderProgram, "uTint");
    m_metallicLocation = m_gl.glGetUniformLocation(m_shaderProgram, "uMaterial");
    m_textureLocation = m_gl.glGetUniformLocation(m_shaderProgram, "uTexture");
    m_shadowSamplerLocation = m_gl.glGetUniformLocation(m_shaderProgram, "uShadowMap");
    m_emissive = m_gl.glGetUniformLocation(m_shaderProgram, "uEmissive");
    m_normalInfoLocation = m_gl.glGetUniformLocation(m_shaderProgram, "uNormalInfo");
    m_normalSamplerLocation = m_gl.glGetUniformLocation(m_shaderProgram, "uNormalMap");

    if (m_modelLocation < 0 || m_tintLocation < 0 || m_metallicLocation < 0 ||
        m_textureLocation < 0 || m_shadowSamplerLocation < 0 || m_emissive < 0 ||
        m_normalInfoLocation < 0 || m_normalSamplerLocation < 0) {
        throw std::runtime_error(
            "the mesh shader program is missing uModel or uTexture. A linker removes "
            "uniforms the shader does not actually use, so this usually means the "
            "shader stopped referencing one.");
    }

    // 一个采样器 uniform 拥有的是纹理单元，而不是纹理对象，这和 Vulkan 有区别
    // Vulkan 通过描述符集明确显示，而 OpenGL 则隐藏在一个整数后面。
    m_gl.glUseProgram(m_shaderProgram);
    m_gl.glUniform1i(m_textureLocation, 0);
    m_gl.glUniform1i(m_shadowSamplerLocation, 1);
    m_gl.glUniform1i(m_normalSamplerLocation, 6);
    m_gl.glUseProgram(0);
}

void OpenGLRenderer::CreateShadowResources()
{
    constexpr GLsizei kShadowSize = 1024;
    m_gl.glGenTextures(1, &m_shadowDepthTexture);
    m_gl.glBindTexture(GL_TEXTURE_2D, m_shadowDepthTexture);
    m_gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, kShadowSize, kShadowSize,
        0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    m_gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    m_gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    m_gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    m_gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    m_gl.glGenFramebuffers(1, &m_shadowFramebuffer);
    m_gl.glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFramebuffer);
    m_gl.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
        m_shadowDepthTexture, 0);
    m_gl.glDrawBuffer(GL_NONE);
    m_gl.glReadBuffer(GL_NONE);
    if (m_gl.glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error("OpenGL directional shadow framebuffer is incomplete");
    m_gl.glBindFramebuffer(GL_FRAMEBUFFER, 0);

    const GLuint vertex = CompileShader(GL_VERTEX_SHADER,
        ReadTextFile(ShaderSourcePath("gl_shadow.vert")), ShaderSourcePath("gl_shadow.vert"));
    const GLuint fragment = CompileShader(GL_FRAGMENT_SHADER,
        ReadTextFile(ShaderSourcePath("gl_shadow.frag")), ShaderSourcePath("gl_shadow.frag"));
    m_shadowProgram = m_gl.glCreateProgram();
    m_gl.glAttachShader(m_shadowProgram, vertex);
    m_gl.glAttachShader(m_shadowProgram, fragment);
    m_gl.glLinkProgram(m_shadowProgram);
    m_gl.glDeleteShader(vertex);
    m_gl.glDeleteShader(fragment);
    GLint linked = GL_FALSE;
    m_gl.glGetProgramiv(m_shadowProgram, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) throw std::runtime_error("failed to link directional shadow shader");
    m_shadowModelLocation = m_gl.glGetUniformLocation(m_shadowProgram, "uModel");
    m_shadowLightViewProjectionLocation = m_gl.glGetUniformLocation(m_shadowProgram, "uLightViewProjection");
    if (m_shadowModelLocation < 0 || m_shadowLightViewProjectionLocation < 0)
        throw std::runtime_error("directional shadow shader is missing required uniforms");
}

void OpenGLRenderer::DestroyShadowResources()
{
    if (m_shadowProgram != 0) m_gl.glDeleteProgram(m_shadowProgram);
    if (m_shadowFramebuffer != 0) m_gl.glDeleteFramebuffers(1, &m_shadowFramebuffer);
    if (m_shadowDepthTexture != 0) m_gl.glDeleteTextures(1, &m_shadowDepthTexture);
    m_shadowProgram = m_shadowFramebuffer = m_shadowDepthTexture = 0;
}

void OpenGLRenderer::ResizeHdrTarget(int width, int height)
{
    if (width <= 0 || height <= 0) return;
    if (m_hdrFramebuffer == 0) m_gl.glGenFramebuffers(1, &m_hdrFramebuffer);
    if (m_hdrColorTexture == 0) m_gl.glGenTextures(1, &m_hdrColorTexture);
    if (m_hdrDepthTexture == 0) m_gl.glGenTextures(1, &m_hdrDepthTexture);

    m_gl.glBindTexture(GL_TEXTURE_2D, m_hdrColorTexture);
    m_gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0,
        GL_RGBA, GL_HALF_FLOAT, nullptr);
    m_gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    m_gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    m_gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    m_gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    m_gl.glBindTexture(GL_TEXTURE_2D, m_hdrDepthTexture);
    m_gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width, height, 0,
        GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    m_gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    m_gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    m_gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    m_gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    m_gl.glBindFramebuffer(GL_FRAMEBUFFER, m_hdrFramebuffer);
    m_gl.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D, m_hdrColorTexture, 0);
    m_gl.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
        GL_TEXTURE_2D, m_hdrDepthTexture, 0);
    if (m_gl.glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error("OpenGL RGBA16F HDR framebuffer is incomplete");
    m_gl.glBindFramebuffer(GL_FRAMEBUFFER, 0);
    m_hdrWidth = width;
    m_hdrHeight = height;
}

void OpenGLRenderer::DestroyHdrTarget()
{
    if (m_hdrFramebuffer != 0) m_gl.glDeleteFramebuffers(1, &m_hdrFramebuffer);
    if (m_hdrColorTexture != 0) m_gl.glDeleteTextures(1, &m_hdrColorTexture);
    if (m_hdrDepthTexture != 0) m_gl.glDeleteTextures(1, &m_hdrDepthTexture);
    m_hdrFramebuffer = m_hdrColorTexture = m_hdrDepthTexture = 0;
    m_hdrWidth = m_hdrHeight = 0;
}

// ---------------------------------------------------------------------------
// Texture
// ---------------------------------------------------------------------------

void OpenGLRenderer::CreateTexture()
{
    const std::string path = TextureAssetPath();

    const Image image = LoadImage(path);
    if (!image.IsValid()) {
        throw std::runtime_error("decoded image is empty: " + path);
    }

    UploadTexture(image);

    m_textureWatch.Reset(path);
}

void OpenGLRenderer::UploadTexture(const Image& image)
{
    m_textureWidth = image.width;
    m_textureHeight = image.height;

    m_gl.glGenTextures(1, &m_texture);
    m_gl.glActiveTexture(GL_TEXTURE0);
    m_gl.glBindTexture(GL_TEXTURE_2D, m_texture);

    // 内部格式是 sRGB；像素数据仍然是普通的 RGBA8。二者不同
    // 采样器在读取时会转换，而 stb_image 根本不用知道渲染器想要哪种格式。
    m_gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8,
        static_cast<GLsizei>(image.width),
        static_cast<GLsizei>(image.height),
        0, GL_RGBA, GL_UNSIGNED_BYTE, image.pixels.data());

    m_gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    m_gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    m_gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    m_gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    // 整个七级 blit 链是 Vulkan 后端手动构建的，一次调用完成。
    // 不是因为 OpenGL “更简单”，而是因为驱动自己管理内存，已经知道如何进行下采样。
    // Vulkan 把这个责任交还给引擎。
    m_gl.glGenerateMipmap(GL_TEXTURE_2D);

    m_gl.glBindTexture(GL_TEXTURE_2D, 0);
}

void OpenGLRenderer::DestroyTexture()
{
    if (m_texture != 0) {
        m_gl.glDeleteTextures(1, &m_texture);
        m_texture = 0;
    }

    m_textureWidth = 0;
    m_textureHeight = 0;
}

void OpenGLRenderer::ReloadTexture()
{
    // 在拆除任何东西之前先解码，像 Vulkan 那样：可能的失败是文件只写了一半，而对这种情况的正确处理方式是继续显示已经能用的纹理。
    Image image;
    try {
        image = LoadImage(TextureAssetPath());
    }
    catch (const std::exception& exception) {
        std::cerr << "[hotreload] " << exception.what() << std::endl;
        std::cerr << "[hotreload] keeping the previously loaded texture" << std::endl;
        return;
    }

    DestroyTexture();
    UploadTexture(image);

    // 注意这里和 Vulkan 后端相比缺少什么：没有要重写的描述符集。
    // 采样器 uniform 保存了一个纹理单元，并且绑定到该单元的纹理对象被就地替换
    // 所以 Vulkan 明确的间接操作被 GL 的绑定模型吸收了。
    std::cout << "[hotreload] texture reloaded: " << m_textureWidth << "x" << m_textureHeight
        << " (OpenGL)" << std::endl;
}

// ---------------------------------------------------------------------------
// Camera
// ---------------------------------------------------------------------------

void OpenGLRenderer::UpdateCameraUniforms(const RenderSnapshot& snapshot)
{
    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetFramebufferSize(m_window, &framebufferWidth, &framebufferHeight);

    GlCameraUniforms uniforms;
    uniforms.view = snapshot.camera.ViewMatrix();

    const float aspect = framebufferHeight > 0
        ? static_cast<float>(framebufferWidth) / static_cast<float>(framebufferHeight)
        : 1.0f;
    uniforms.proj = snapshot.camera.ProjectionMatrix(aspect);
    uniforms.cameraPosition = glm::vec4(snapshot.camera.position, 1.0f);
    uniforms.lightDirection = glm::vec4(snapshot.lightDirection, 0.0f);
    uniforms.lightColor = glm::vec4(snapshot.lightColor, 1.0f);
    uniforms.lightViewProjection = snapshot.lightViewProjection;
    uniforms.inverseViewProjection = glm::inverse(uniforms.proj * uniforms.view);
    uniforms.environment = glm::vec4(snapshot.environment.intensity,
        snapshot.environmentData && snapshot.environment.enabled ? 1.0f : 0.0f,
        snapshot.environmentData && snapshot.environment.background ? 1.0f : 0.0f,
        float(m_uploadedEnvironment->prefiltered.mips.size() - 1));
    uniforms.lightingTerms = glm::vec4(snapshot.lightingTerms, 0.0f);
    uniforms.framebufferSize = glm::vec4(framebufferWidth, framebufferHeight, 0, 0);

    // 注意：这里没有Y轴翻转。

    // Vulkan后端会翻转proj[1][1]，因为Vulkan的归一化设备坐标（NDC）中Y轴向下。
    // OpenGL的Y轴向上，所以摄像机的投影矩阵直接按原样使用。
    // 不用翻转，否则渲染的场景会颠倒。
    m_gl.glBindBuffer(GL_UNIFORM_BUFFER, m_cameraUniformBuffer);
    m_gl.glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(uniforms), &uniforms);
    m_gl.glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

// ---------------------------------------------------------------------------
// Dear ImGui overlay
// ---------------------------------------------------------------------------

void OpenGLRenderer::InitImGui()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(m_window, true);
    ImGui_ImplOpenGL3_Init("#version 450");

    m_imguiInitialized = true;
}

void OpenGLRenderer::ShutdownImGui()
{
    if (m_imguiInitialized) {
        ImGui_ImplOpenGL3_Shutdown();
        m_imguiInitialized = false;
    }

    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void OpenGLRenderer::BuildImGuiFrame(const std::function<void()>& drawEditorUi)
{
    ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(340.0f, 0.0f), ImGuiCond_FirstUseEver);

    ImGui::Begin("Renderer Diagnostics - OpenGL");

    const ImGuiIO& io = ImGui::GetIO();
    if (io.Framerate > 0.0f) {
        ImGui::Text("Frame: %.2f ms  (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
    }
    else {
        ImGui::TextUnformatted("Frame: -- (warming up)");
    }

    ImGui::Separator();
    ImGui::Text("GPU: %s", m_rendererName.c_str());
    ImGui::Text("GL: %s", m_versionString.c_str());
    ImGui::Text("Meshes: %zu uploaded", m_meshes.size());
    ImGui::Text("Texture: %u x %u", m_textureWidth, m_textureHeight);

    ImGui::Separator();
    ImGui::TextWrapped(
        "The same scene, drawn by an unrelated API, through the same Renderer "
        "interface. main.cpp does not know which backend it got.");

    ImGui::End();

    drawEditorUi();
}

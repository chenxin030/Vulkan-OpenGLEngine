#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include <string>

struct ViewportExtent
{
    uint32_t width = 0, height = 0;
    bool operator==(const ViewportExtent&) const = default;
    bool Valid() const { return width > 0 && height > 0; }
    float Aspect() const { return Valid() ? float(width) / float(height) : 0.0f; }
};

// 场景视口的大小
struct ViewportRequest
{
    ViewportExtent extent{640, 360};
    bool visible = true;
};

// 为了让Renderer和Editor分开，用这个作为传递信息的工具。
// 本帧提供图像的具体信息以及去哪里拿，不同图形API有不同的内容，
struct ViewportImage
{
    uint64_t textureId = 0;         // 告诉 ImGui 显示哪张图，GL 为 texture name，Vulkan 为 ImGui 注册的 descriptor set
    ViewportExtent extent;
    glm::vec2 uv0{0, 0}, uv1{1, 1}; // 保证图像朝向正确，OpenGL (0,1) -> (1,0)，Vulkan (0,0) → (1,1)。Editor 使用 Renderer 给出的 UV，两个后端的画面就都能正向显示，不需要在 Editor 中判断后端。
    std::string status;             // 显示目标创建或 resize 失败的信息
    bool Valid() const { return textureId != 0 && extent.Valid(); }
};


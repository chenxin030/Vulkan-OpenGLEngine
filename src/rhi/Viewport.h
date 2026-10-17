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

// UI 只请求下一帧尺寸；GPU 分配/销毁由 Renderer 在 DrawFrame 入口完成。
// 表示“我希望渲染多大”
struct ViewportRequest
{
    ViewportExtent extent{640, 360};
    bool visible = true;
};

// 相当于 Renderer 和 Editor 的接口，把 Editor 需要的 Renderer 的信息从 Redenerer 获取给到 Editor，让二者分开，互相不知道对方
// 表示“本帧实际提供了什么图像”。
struct ViewportImage
{
    uint64_t textureId = 0;         // 告诉 ImGui 显示哪张图，GL 为 texture name，Vulkan 为 ImGui 注册的 descriptor set
    ViewportExtent extent;
    glm::vec2 uv0{0, 0}, uv1{1, 1}; // 保证图像朝向正确，OpenGL 提供 (0,1) → (1,0)，Vulkan 提供 (0,0) → (1,1)。Editor 使用 Renderer 给出的 UV，两个后端的画面就都能正向显示，不需要在 Editor 中判断后端。
    std::string status;             // 显示目标创建或 resize 失败的信息
    bool Valid() const { return textureId != 0 && extent.Valid(); }
};


#pragma once

#include "scene/Scene.h"
#include <optional>

// 编辑器的纯几何计算：不依赖 GLFW、ImGui 或任何 GPU 后端，便于直接测试。
namespace EditorGeometry {
struct Trs
{
    glm::vec3 position{0.0f};
    glm::vec3 rotationDegrees{0.0f}; // XYZ 欧拉角，合成顺序 T * Rz * Ry * Rx * S。
    glm::vec3 scale{1.0f};
};

glm::mat4 Compose(const Trs& value);
// 仅接受有限的、正缩放的仿射 TRS。失败时保留原矩阵，不把剪切/镜像强行变成 TRS。
std::optional<Trs> Decompose(const glm::mat4& matrix);
Entity RootEntity(const Scene& scene, Entity entity);

// mouse/size 为 Image 内的逻辑坐标；aspect 来自实际离屏 pixel size。
// 返回最近可见三角形所属的顶层实体。屏幕 Y 向下，几何计算的 NDC Y 向上。
Entity Pick(const Scene& scene, glm::vec2 mouse, glm::vec2 size, float aspect, const Camera* renderCamera = nullptr);
glm::mat4 Projection(const Camera& camera, float aspect);
}

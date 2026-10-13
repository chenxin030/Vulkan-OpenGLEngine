#include "editor/EditorGeometry.h"
#include "scene/Components.h"

#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

namespace EditorGeometry {
namespace {
bool Finite(const glm::mat4& matrix)
{
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            if (!std::isfinite(matrix[c][r])) return false;
    return true;
}
}

glm::mat4 Compose(const Trs& value)
{
    return glm::translate(glm::mat4(1.0f), value.position) *
        glm::mat4_cast(glm::quat(glm::radians(value.rotationDegrees))) *
        glm::scale(glm::mat4(1.0f), value.scale);
}

std::optional<Trs> Decompose(const glm::mat4& matrix)
{
    if (!Finite(matrix) || std::abs(matrix[3][3] - 1.0f) > 1e-5f ||
        std::abs(matrix[0][3]) > 1e-5f || std::abs(matrix[1][3]) > 1e-5f ||
        std::abs(matrix[2][3]) > 1e-5f) return std::nullopt;
    Trs result;
    result.position = glm::vec3(matrix[3]);
    glm::mat3 basis(matrix);
    for (int i = 0; i < 3; ++i) {
        result.scale[i] = glm::length(basis[i]);
        if (result.scale[i] < 0.001f || result.scale[i] > 1000.0f) return std::nullopt;
        basis[i] /= result.scale[i];
    }
    if (std::abs(glm::dot(basis[0], basis[1])) > 1e-4f ||
        std::abs(glm::dot(basis[0], basis[2])) > 1e-4f ||
        std::abs(glm::dot(basis[1], basis[2])) > 1e-4f ||
        glm::determinant(basis) < 0.0f) return std::nullopt;
    // Rz*Ry*Rx 的显式反解。用 atan2(sinY, |cosY|) 避免 asin 在 ±90° 附近放大误差。
    const float cosY = std::hypot(basis[0][0], basis[0][1]);
    const float y = std::atan2(-basis[0][2], cosY);
    // 接近万向节锁时 X/Z 不再能独立可靠反解；固定 Z=0，保留等价的组合角。
    const float x = cosY > 1e-4f ? std::atan2(basis[1][2], basis[2][2]) :
        std::atan2(-basis[2][1], basis[1][1]);
    const float z = cosY > 1e-4f ? std::atan2(basis[0][1], basis[0][0]) : 0.0f;
    result.rotationDegrees = glm::degrees(glm::vec3(x, y, z));
    // 欧拉角的奇异位置也必须能回建原矩阵，否则显示只读，避免第一次编辑改变姿态。
    const glm::mat4 rebuilt = Compose(result);
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            if (std::abs(rebuilt[c][r] - matrix[c][r]) >
                2e-4f * std::max(1.0f, std::abs(matrix[c][r]))) return std::nullopt;
    return result;
}

glm::mat4 Projection(const Camera& camera, float aspect)
{
    return camera.ProjectionMatrix(aspect);
}

Entity RootEntity(const Scene& scene, Entity entity)
{
    // 以实际 Parent 链为准，不靠名称猜实例身份；有环/死句柄时不选择。
    for (size_t remaining = scene.registry.EntityCount(); remaining > 0; --remaining) {
        if (!scene.registry.IsAlive(entity)) return {};
        const Parent* parent = scene.registry.TryGet<Parent>(entity);
        if (!parent) return entity;
        entity = parent->entity;
    }
    return {};
}

Entity Pick(const Scene& scene, glm::vec2 mouse, glm::vec2 size, float aspect)
{
    if (!(size.x > 0 && size.y > 0 && aspect > 0) || !std::isfinite(aspect) ||
        !std::isfinite(mouse.x) || !std::isfinite(mouse.y) ||
        mouse.x < 0 || mouse.y < 0 || mouse.x >= size.x || mouse.y >= size.y) return {};
    const glm::mat4 viewProjection = Projection(scene.camera, aspect) * scene.camera.ViewMatrix();
    if (!Finite(viewProjection) || std::abs(glm::determinant(viewProjection)) < 1e-12f) return {};
    const glm::mat4 inverse = glm::inverse(viewProjection);
    const glm::vec2 ndc(mouse.x / size.x * 2 - 1, 1 - mouse.y / size.y * 2);
    const glm::vec4 nearH = inverse * glm::vec4(ndc, 0, 1);
    const glm::vec4 farH = inverse * glm::vec4(ndc, 1, 1);
    if (std::abs(nearH.w) < 1e-7f || std::abs(farH.w) < 1e-7f) return {};
    const glm::vec3 start = glm::vec3(nearH) / nearH.w;
    const glm::vec3 segment = glm::vec3(farH) / farH.w - start;
    // 使用近/远裁面之间的线段，t∈[0,1]，不能选中裁剪范围之外的对象。
    float nearest = std::numeric_limits<float>::max();
    Entity hit;
    scene.registry.ForEachEntity([&](Entity entity) {
        const Transform* transform = scene.registry.TryGet<Transform>(entity);
        if (!transform || !Finite(transform->world)) return;
        const auto intersect = [&](uint32_t meshIndex) {
            if (meshIndex >= scene.meshes.Count()) return;
            const MeshData& mesh = scene.meshes.Get(meshIndex);
            for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
                if (mesh.indices[i] >= mesh.vertices.size() || mesh.indices[i+1] >= mesh.vertices.size() ||
                    mesh.indices[i+2] >= mesh.vertices.size()) continue;
                const auto point = [&](uint16_t index) {
                    return glm::vec3(transform->world * glm::vec4(mesh.vertices[index].position, 1));
                };
                const glm::vec3 a = point(mesh.indices[i]);
                const glm::vec3 e1 = point(mesh.indices[i+1]) - a;
                const glm::vec3 e2 = point(mesh.indices[i+2]) - a;
                const glm::vec3 p = glm::cross(segment, e2);
                const float determinant = glm::dot(e1, p);
                // Möller–Trumbore：解 start + t*segment = a + u*e1 + v*e2。
                // u/v 是三角形重心坐标；它们决定是否落在真实三角形内，而非仅落在包围盒内。
                // 两后端主 pass 都剔除背面。世界空间检测也自然处理镜像后的绕序。
                const float tolerance = 1e-7f * glm::length(e1) * glm::length(e2) * glm::length(segment);
                if (!std::isfinite(determinant) || determinant <= tolerance) continue;
                const glm::vec3 delta = start - a;
                const float u = glm::dot(delta, p) / determinant;
                const glm::vec3 q = glm::cross(delta, e1);
                const float v = glm::dot(segment, q) / determinant;
                const float t = glm::dot(e2, q) / determinant;
                if (u >= 0 && v >= 0 && u + v <= 1 && t >= 0 && t <= 1 && t < nearest) {
                    nearest = t;
                    hit = entity;
                }
            }
        };
        if (const MeshRenderer* mesh = scene.registry.TryGet<MeshRenderer>(entity)) intersect(mesh->meshIndex);
        if (scene.registry.Has<SpriteRenderer>(entity)) intersect(MeshLibrary::kQuad);
    });
    return RootEntity(scene, hit);
}
}

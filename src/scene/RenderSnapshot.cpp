#include "RenderSnapshot.h"

#include "Scene.h"
#include "scene/Components.h"
#include <cmath>
#include <optional>

namespace {
// 拒绝非有限矩阵、退化矩阵及逆矩阵溢出；返回行列式以复用镜像判断。
std::optional<float> RenderableDeterminant(const glm::mat4& world)
{
    for (int c=0;c<4;++c)
        for (int r=0;r<4;++r)
            if (!std::isfinite(world[c][r])) return std::nullopt;

    const glm::mat3 basis(world);
    const float determinant=glm::determinant(basis);
    if (!std::isfinite(determinant) || determinant==0.0f) return std::nullopt;

    const glm::mat3 inverse = glm::inverse(basis);
    for (int c=0;c<3;++c)
        for (int r=0;r<3;++r)
            if (!std::isfinite(inverse[c][r])) return std::nullopt;

    return determinant;
}

template <typename Enumerate>
RenderSnapshot ExtractSnapshot(const Scene& scene, std::size_t entitiesCount, Enumerate enumerate)
{
    RenderSnapshot snapshot;
    snapshot.camera = scene.camera;
    snapshot.environment = scene.environment;
    snapshot.environmentData = scene.environmentData;
    snapshot.lightingTerms = glm::vec3(scene.directLighting, scene.diffuseIbl, scene.specularIbl);
    snapshot.normalMapping = scene.normalMapping;
    snapshot.normalDebug = scene.normalDebug;
    snapshot.meshes = &scene.meshes;
    snapshot.materials = &scene.materials;
    snapshot.drawItems.reserve(entitiesCount);

    enumerate([&snapshot, &scene](Entity entity) {
        const Transform* transform = scene.registry.TryGet<Transform>(entity);
        const MeshRenderer* mesh = scene.registry.TryGet<MeshRenderer>(entity);
        if (!transform || !mesh) return;
        // 只验证绘制节点，并复用行列式判断镜像绕序。
        const auto determinant = RenderableDeterminant(transform->world);
        if (!determinant) {
            ++snapshot.skippedDegenerateDraws;
            return;
        }
        const SurfaceMaterial& material = scene.materials.GetMaterial(mesh->materialIndex);
        snapshot.drawItems.push_back(DrawItem{ transform->world, mesh->meshIndex,
            glm::vec3(material.baseColorFactor), material.baseColorTexture,
            material.metallicFactor, material.roughnessFactor, material.emissive,
            mesh->materialIndex, material.normalTexture, material.normalScale,
            *determinant < 0.0f });
        });

    return snapshot;
}
}

RenderSnapshot BuildRenderSnapshot(const Scene& scene)
{
    return ExtractSnapshot(scene, scene.registry.EntityCount(), [&](auto visit) { scene.registry.ForEachEntity(visit); });
}

RenderSnapshot BuildRenderSnapshot(const Scene& scene, std::span<const Entity> entities)
{
    return ExtractSnapshot(scene, entities.size(), [&](auto visit) { for (auto entity : entities) visit(entity); });
}

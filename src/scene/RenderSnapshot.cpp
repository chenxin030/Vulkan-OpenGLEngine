#include "RenderSnapshot.h"

#include "Scene.h"
#include "scene/Components.h"
#include <cmath>

bool IsRenderableTransform(const glm::mat4& world)
{
    for (int c=0;c<4;++c)
        for (int r=0;r<4;++r)
            if (!std::isfinite(world[c][r])) return false;

    const glm::mat3 basis(world);
    const float determinant=glm::determinant(basis);
    if (!std::isfinite(determinant) || determinant==0.0f) return false;

    const glm::mat3 inverse = glm::inverse(basis);
    for (int c=0;c<3;++c)
        for (int r=0;r<3;++r)
            if (!std::isfinite(inverse[c][r])) return false;

    return true;
}

RenderSnapshot BuildRenderSnapshot(const Scene& scene)
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
    snapshot.drawItems.reserve(scene.registry.EntityCount());

    scene.registry.ForEachEntity([&snapshot, &scene](Entity entity) {
        const Transform* transform = scene.registry.TryGet<Transform>(entity);
        // 逆转置对退化矩阵没用。只跳过本次绘制，不修改实体或保存的矩阵。
        if (transform != nullptr && !IsRenderableTransform(transform->world)) {
            snapshot.skippedDegenerateDraws += scene.registry.Has<MeshRenderer>(entity) ? 1u : 0u;
            snapshot.skippedDegenerateDraws += scene.registry.Has<SpriteRenderer>(entity) ? 1u : 0u;
            return;
        }
        const MeshRenderer* mesh = scene.registry.TryGet<MeshRenderer>(entity);
        if (transform != nullptr && mesh != nullptr) {
            const SurfaceMaterial& material = scene.materials.GetMaterial(mesh->materialIndex);
            snapshot.drawItems.push_back(DrawItem{ transform->world, mesh->meshIndex,
                glm::vec3(material.baseColorFactor), material.baseColorTexture,
                material.metallicFactor, material.roughnessFactor, material.emissive, false,
                mesh->materialIndex, material.normalTexture, material.normalScale,
                glm::determinant(glm::mat3(transform->world)) < 0.0f });
        }
        const SpriteRenderer* sprite = scene.registry.TryGet<SpriteRenderer>(entity);
        if (transform != nullptr && sprite != nullptr) {
            snapshot.drawItems.push_back(DrawItem{ transform->world, MeshLibrary::kQuad, sprite->tint,
                0xFFFFFFFFu, 0.0f, 1.0f, {0,0,0}, true, 0, 0xFFFFFFFFu, 1.0f,
                false }); // 保留现有 Sprite 绕序行为；镜像正面修正只用于 3D Mesh。
        }
        });

    return snapshot;
}

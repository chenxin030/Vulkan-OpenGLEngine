#include "RenderSnapshot.h"

#include "Scene.h"
#include "scene/Components.h"

RenderSnapshot BuildRenderSnapshot(const Scene& scene)
{
    RenderSnapshot snapshot;
    snapshot.camera = scene.camera;
    snapshot.environment = scene.environment;
    snapshot.environmentData = scene.environmentData;
    snapshot.lightingTerms = glm::vec3(scene.directLighting, scene.diffuseIbl, scene.specularIbl);
    snapshot.meshes = &scene.meshes;
    snapshot.materials = &scene.materials;
    snapshot.drawItems.reserve(scene.registry.EntityCount());

    scene.registry.ForEachEntity([&snapshot, &scene](Entity entity) {
        const Transform* transform = scene.registry.TryGet<Transform>(entity);
        const MeshRenderer* mesh = scene.registry.TryGet<MeshRenderer>(entity);
        if (transform != nullptr && mesh != nullptr) {
            const SurfaceMaterial& material = scene.materials.GetMaterial(mesh->materialIndex);
            snapshot.drawItems.push_back(DrawItem{ transform->world, mesh->meshIndex,
                glm::vec3(material.baseColorFactor), material.baseColorTexture,
                material.metallicFactor, material.roughnessFactor, material.emissive, false });
        }
        const SpriteRenderer* sprite = scene.registry.TryGet<SpriteRenderer>(entity);
        if (transform != nullptr && sprite != nullptr) {
            snapshot.drawItems.push_back(DrawItem{ transform->world, MeshLibrary::kQuad, sprite->tint,
                0xFFFFFFFFu, 0.0f, 1.0f, {0,0,0}, true });
        }
        });

    return snapshot;
}

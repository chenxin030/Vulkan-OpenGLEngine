#include "Scene.h"

#include "scene/Components.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

uint32_t Scene::FindModelAssetIndex(const std::string& id) const
{
    for (uint32_t i = 0; i < modelAssets.size(); ++i) if (modelAssets[i].id == id) return i;
    throw std::runtime_error("unknown model asset ID: " + id);
}

std::string Scene::AllocateModelAssetId()
{
    for (;;) {
        if (m_nextModelAssetId == std::numeric_limits<uint64_t>::max())
            throw std::runtime_error("Scene is out of model asset IDs");
        std::string id = "model-" + std::to_string(m_nextModelAssetId++);
        const bool used = std::any_of(modelAssets.begin(), modelAssets.end(),
            [&](const ModelAsset& asset) { return asset.id == id; });
        if (!used) return id;
    }
}

void Scene::DestroyEntitySubtree(Entity root)
{
    std::vector<Entity> subtree{ root };
    for (size_t parentIndex = 0; parentIndex < subtree.size(); ++parentIndex) {
        registry.ForEachEntity([&](Entity candidate) {
            const Parent* parent = registry.TryGet<Parent>(candidate);
            if (parent && parent->entity == subtree[parentIndex] &&
                std::find(subtree.begin(), subtree.end(), candidate) == subtree.end()) {
                subtree.push_back(candidate);
            }
        });
    }
    for (auto it = subtree.rbegin(); it != subtree.rend(); ++it) registry.Destroy(*it);
}


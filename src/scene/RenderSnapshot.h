#pragma once

#include "scene/Camera.h"
#include "scene/MeshLibrary.h"
#include "scene/MaterialLibrary.h"
#include "scene/Lighting.h"
#include "scene/Environment.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>
#include <span>
#include "scene/Entity.h"

struct DrawItem
{
    glm::mat4 worldTransform{ 1.0f };
    uint32_t meshIndex = 0;
    glm::vec3 tint{ 1.0f };
    uint32_t textureIndex = 0xFFFFFFFFu;
    float metallicFactor = 0.0f;
    float roughnessFactor = 1.0f;
    glm::vec3 emissive{ 0.0f };
    uint32_t materialIndex = 0;
    uint32_t normalTextureIndex = 0xFFFFFFFFu;
    float normalScale = 1.0f;
    bool mirrored = false;
};

// 渲染一帧所需的不可变、后端中立的数据。
struct RenderSnapshot
{
    Camera camera;
    EnvironmentSettings environment;
    std::shared_ptr<const EnvironmentData> environmentData;
    glm::vec3 lightingTerms{1.0f};
    bool normalMapping = true;
    int normalDebug = 0; // 0=光照，1=未扰动法线，2=扰动法线。
    uint32_t skippedDegenerateDraws = 0;
    glm::vec3 lightDirection = DirectionalLightDirection();
    glm::vec3 lightColor = DirectionalLightColor();
    glm::mat4 lightViewProjection = DirectionalLightViewProjection();
    const MeshLibrary* meshes = nullptr;
    const MaterialLibrary* materials = nullptr;
    std::vector<DrawItem> drawItems;
};

RenderSnapshot BuildRenderSnapshot(const struct Scene& scene);
RenderSnapshot BuildRenderSnapshot(const struct Scene& scene, std::span<const Entity> entities);

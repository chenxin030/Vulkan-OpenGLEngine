#pragma once

#include <glm/glm.hpp>
#include <vector>
#include "Camera.h"
#include "scene/Registry.h"
#include "MeshLibrary.h"

struct DrawItem
{
    glm::mat4 worldTransform{ 1.0f };
    uint32_t meshIndex = 0;
};

struct Scene
{
    Registry registry;
    Camera camera;

    MeshLibrary meshes;

    // 世界时钟：需要"绝对时间"的系统（如 BobSystem）读它。
    // 它和 Camera 一样是**整个世界一份**的状态，不是每实体一份 —— 所以不进 Registry。
    float elapsed = 0.0f;

    std::vector<DrawItem> drawItems;

    void Update(float deltaSeconds);
};

Scene BuildDemoScene();

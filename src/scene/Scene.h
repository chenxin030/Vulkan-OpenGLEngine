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

    std::vector<DrawItem> drawItems;

    void Update(float deltaSeconds);
};

Scene BuildDemoScene();

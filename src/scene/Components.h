#pragma once

#include <glm/glm.hpp>

#include <cstdint>

struct Transform
{
    glm::mat4 local{ 1.0f };
    glm::mat4 world{ 1.0f };
};

struct MeshRenderer
{
    uint32_t meshIndex = 0;   // index to mesh type, such as cube/plane/sphere
};

struct Spin
{
    glm::vec3 axis{ 0.0f, 1.0f, 0.0f };
    float speed = 0.0f;   // radians per second
    float angle = 0.0f;   // accumulated
};

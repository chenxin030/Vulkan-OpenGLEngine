#pragma once

#include <glm/glm.hpp>

struct Vertex
{
    glm::vec3 position;
    glm::vec3 color;
    glm::vec2 texCoord;
    glm::vec3 normal{ 0.0f, 0.0f, 1.0f };
    // 模型空间切线；w 为 ±1，B = w * cross(N, T)。无 normalTexture 时不使用。
    glm::vec4 tangent{ 1.0f, 0.0f, 0.0f, 1.0f };
};

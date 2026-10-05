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

// 上下浮动。和 Spin 的关键差别：这里不累加自己的状态，而是由**世界时钟**算绝对位移
// （sin(elapsed * speed + phase)）—— 结果只依赖 t，不会因为某一帧卡顿而漂移。
struct Bob
{
    float amplitude = 0.25f;   // 世界空间竖直浮动的半幅
    float speed = 1.5f;        // radians per second
    float phase = 0.0f;        // 让不同实体的相位错开
};

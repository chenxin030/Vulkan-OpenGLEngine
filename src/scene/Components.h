#pragma once

#include <glm/glm.hpp>
#include "scene/Entity.h"

#include <cstdint>
#include <string>

struct Transform
{
    glm::mat4 local{ 1.0f };
    glm::mat4 world{ 1.0f };
};

struct MeshRenderer
{
    uint32_t meshIndex = 0;   // index to mesh type, such as cube/plane/sphere
    uint32_t materialIndex = 0;
};

struct Name
{
    std::string value;
};

struct Parent
{
    Entity entity;
};

struct Spin
{
    glm::vec3 axis{ 0.0f, 1.0f, 0.0f };
    float speed = 0.0f;   // radians per second
    float angle = 0.0f;   // accumulated
};

// 上下浮动。和 Spin 的差别：这里不累加自己的状态，而是由世界时钟算绝对位移
// （sin(elapsed * speed + phase)）—— 结果只依赖 t，不会因为某一帧卡顿而漂移。
struct Bob
{
    float amplitude = 0.25f;   // 世界空间竖直浮动的半幅
    float speed = 1.5f;        // radians per second
    float phase = 0.0f;        // 让不同实体的相位错开
};

// 刚性程序运动，从播放时保存的静止变换中评估。
// 0: 旋转；1: 往复；2: 固定抓取/放置机械臂；3: 成对工件。
struct MachineMotion
{
    int kind = 0;
    glm::vec3 axis{0, 1, 0};
    float amplitude = 1.0f, speed = 1.0f, phase = 0.0f;
};

// 这个实体本身是出发站；目标是另一个场景实体。
struct CargoRoute
{
    Entity target;
    std::string assetId;
    uint32_t capacity = 64;
    float speed = 4.0f, interval = 0.08f, scale = 0.15f;
    float altitude = 3.0f;
};

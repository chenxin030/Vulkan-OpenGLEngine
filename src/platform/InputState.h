#pragma once

#include <glm/glm.hpp>

// 记录用户在这一帧的输入
// 让 Camera 和 glfw 解耦，用户输入——GLFW——InputState——Camera。
struct InputState
{
    // 在相机本地坐标轴上的移动意图，每个分量的范围为 [-1, 1]。
    // x = 向右平移, y = 世界向上, z = 向前。
    glm::vec3 moveAxis{0.0f};

    // 自上一帧以来的鼠标移动量，以像素为单位。
    // 是一个增量而不是位置，因为摄像机关心的是鼠标移动了多远，而不是它在屏幕上的位置。
    glm::vec2 lookDelta{0.0f};

    // 当按住右键，且使用本窗口时    时生效。
    bool lookActive = false;

    // // 冲刺键
    bool sprint = false;
};

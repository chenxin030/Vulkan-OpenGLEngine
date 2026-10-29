#pragma once

#include <glm/glm.hpp>

// 记录用户在这一帧的输入
// 让 Camera 和 glfw 解耦，用户输入——GLFW——InputState——Camera。
struct InputState
{
    // 在相机本地坐标轴上的移动意图，每个分量的范围为 [-1, 1]。
    // x = 向右平移, y = 世界向上, z = 向前。
    glm::vec3 moveAxis{0.0f};

    // 自上一帧以来的鼠标移动增量，以像素为单位。
    glm::vec2 lookDelta{0.0f};

    // 当按住右键，且使用本窗口时生效。
    bool lookActive = false;

    glm::vec2 cursorPosition{0.0f};

    // // 冲刺键
    bool sprint = false;

    bool quitRequested = false;
};

struct EditorInputCapture
{
    bool keyboard = false;
    bool mouse = false;
};

inline void FilterCapturedEditorInput(InputState& input, const EditorInputCapture& capture)
{
    if (capture.keyboard) {
        input.moveAxis = glm::vec3(0.0f);
        input.sprint = false;
        input.quitRequested = false;
    }

    if (capture.mouse) {
        input.lookActive = false;
        input.lookDelta = glm::vec2(0.0f);
    }
}

#pragma once

#include "platform/InputState.h"

#include <glm/glm.hpp>

struct GLFWwindow;

// ---------------------------------------------------------------------------
// 用户输入——GLFW——InputState——Camera。

// 唯一一个在 main.cpp 外引用 GLFW 的文件，输入的特定平台部分只存在于一个地方，其余部分都使用 InputState。

// 轮询（不是回调）
// 每一帧轮询 glfwGetKey / glfwGetCursorPos，读取当前状态，而不是从队列取样，防止丢失事件。
//
// 回调需要一个积累位置，要么是 file-static，要么是 glfwSetWindowUserPointer——而 ImGui 的 GLFW 后端可能会用到用户指针;
// 由两个库安装的回调必须正确链式调用，如果搞错了，只有两个同时活跃时才会暴露 bug;
//
// 代价是输入每帧采样一次，而不是立即响应。
// ---------------------------------------------------------------------------
class GlfwInput
{
public:
    explicit GlfwInput(GLFWwindow* window);

    GlfwInput(const GlfwInput&) = delete;
    GlfwInput& operator=(const GlfwInput&) = delete;

    // 对当前状态进行采样并推进存储的光标位置。
    InputState Sample();

private:
    GLFWwindow* m_window = nullptr;

    glm::vec2 m_lastCursorPosition{0.0f};
    bool m_hasLastCursorPosition = false;
};

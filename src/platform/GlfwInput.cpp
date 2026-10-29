#include "platform/GlfwInput.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

GlfwInput::GlfwInput(GLFWwindow* window)
    : m_window(window)
{
}

InputState GlfwInput::Sample(const EditorInputCapture& effectiveCapture)
{
    InputState input;

    if (glfwGetWindowAttrib(m_window, GLFW_FOCUSED) != GLFW_TRUE) {
        ResetCursorBaseline();
        return input;
    }

    // --- movement -----------------------------------------------------------
    //  Q/E 垂直移动：
    if (glfwGetKey(m_window, GLFW_KEY_W) == GLFW_PRESS) {
        input.moveAxis.z += 1.0f;
    }
    if (glfwGetKey(m_window, GLFW_KEY_S) == GLFW_PRESS) {
        input.moveAxis.z -= 1.0f;
    }
    if (glfwGetKey(m_window, GLFW_KEY_D) == GLFW_PRESS) {
        input.moveAxis.x += 1.0f;
    }
    if (glfwGetKey(m_window, GLFW_KEY_A) == GLFW_PRESS) {
        input.moveAxis.x -= 1.0f;
    }
    if (glfwGetKey(m_window, GLFW_KEY_E) == GLFW_PRESS) {
        input.moveAxis.y += 1.0f;
    }
    if (glfwGetKey(m_window, GLFW_KEY_Q) == GLFW_PRESS) {
        input.moveAxis.y -= 1.0f;
    }

    input.sprint = glfwGetKey(m_window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;
    input.quitRequested = glfwGetKey(m_window, GLFW_KEY_ESCAPE) == GLFW_PRESS;

    // --- look ---------------------------------------------------------------
    // 按住右键才能移动视角
    input.lookActive =
        glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS &&
        glfwGetWindowAttrib(m_window, GLFW_FOCUSED) == GLFW_TRUE;

    double cursorX = 0.0;
    double cursorY = 0.0;
    glfwGetCursorPos(m_window, &cursorX, &cursorY);

    const glm::vec2 cursorPosition(static_cast<float>(cursorX), static_cast<float>(cursorY));
    input.cursorPosition = cursorPosition;

    if (input.lookActive && m_hasLastCursorPosition) {
        input.lookDelta = cursorPosition - m_lastCursorPosition;
    }

    // 即使在视角不活动时，也要始终推进存储的位置。跳过这一步会导致摄像机在鼠标按下的瞬间，瞬移到鼠标移动过的距离。
    m_lastCursorPosition = cursorPosition;
    m_hasLastCursorPosition = true;

    // 与事件分发使用调用方计算的同一份捕获状态。
    FilterCapturedEditorInput(input, effectiveCapture);

    return input;
}

EditorInputCapture GlfwInput::EffectiveCapture(EditorInputCapture capture) const
{
    // UI 捕获来自上一帧；左键首帧也优先编辑，避免同时按 WASD 时相机先移动一帧。
    if (glfwGetWindowAttrib(m_window, GLFW_FOCUSED) != GLFW_TRUE ||
        glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) // (窗口没有焦点 || 当前左键按下)
        return { true, true };  // 同时阻挡键盘和鼠标输入
    return capture;
}

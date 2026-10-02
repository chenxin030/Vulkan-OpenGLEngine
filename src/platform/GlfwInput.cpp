#include "platform/GlfwInput.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

GlfwInput::GlfwInput(GLFWwindow* window)
    : m_window(window)
{
}

InputState GlfwInput::Sample()
{
    InputState input;

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

    // --- look ---------------------------------------------------------------
    // 按住右键才能移动视角
    input.lookActive =
        glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS &&
        glfwGetWindowAttrib(m_window, GLFW_FOCUSED) == GLFW_TRUE;

    double cursorX = 0.0;
    double cursorY = 0.0;
    glfwGetCursorPos(m_window, &cursorX, &cursorY);

    const glm::vec2 cursorPosition(static_cast<float>(cursorX), static_cast<float>(cursorY));

    if (input.lookActive && m_hasLastCursorPosition) {
        input.lookDelta = cursorPosition - m_lastCursorPosition;
    }

    // 即使在视角不活动时，也要始终推进存储的位置。跳过这一步会导致摄像机在鼠标按下的瞬间，瞬移到鼠标移动过的距离。
    m_lastCursorPosition = cursorPosition;
    m_hasLastCursorPosition = true;

    return input;
}

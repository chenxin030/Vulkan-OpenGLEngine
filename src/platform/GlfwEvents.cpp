#include "platform/GlfwEvents.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <new>
#include <utility>

GlfwEvents& GlfwEvents::From(GLFWwindow* window)
{
    return *static_cast<GlfwEvents*>(glfwGetWindowUserPointer(window));
}

GlfwEvents::GlfwEvents(GLFWwindow* window) : m_window(window)
{
    glfwSetWindowUserPointer(window, this);
    glfwSetWindowCloseCallback(window, [](GLFWwindow* w) { From(w).Record({EventType::WindowClose}); });
    glfwSetFramebufferSizeCallback(window, [](GLFWwindow* w, int width, int height) {
        Event event{EventType::FramebufferResize}; event.width = width; event.height = height;
        From(w).Record(event);
    });
    glfwSetWindowFocusCallback(window, [](GLFWwindow* w, int focused) {
        Event event{EventType::WindowFocus}; event.focused = focused == GLFW_TRUE;
        From(w).Record(event);
    });
    glfwSetKeyCallback(window, [](GLFWwindow* w, int key, int, int action, int mods) {
        Event event{action == GLFW_RELEASE ? EventType::KeyReleased : EventType::KeyPressed};
        event.code = key; event.modifiers = mods; event.repeat = action == GLFW_REPEAT;
        From(w).Record(event);
    });
    glfwSetMouseButtonCallback(window, [](GLFWwindow* w, int button, int action, int mods) {
        Event event{action == GLFW_RELEASE ? EventType::MouseButtonReleased : EventType::MouseButtonPressed};
        event.code = button; event.modifiers = mods; From(w).Record(event);
    });
}

GlfwEvents::~GlfwEvents()
{
    // 在 ImGui_ImplGlfw_Shutdown 恢复应用回调之后、窗口销毁之前运行。
    glfwSetWindowCloseCallback(m_window, nullptr);
    glfwSetFramebufferSizeCallback(m_window, nullptr);
    glfwSetWindowFocusCallback(m_window, nullptr);
    glfwSetKeyCallback(m_window, nullptr);
    glfwSetMouseButtonCallback(m_window, nullptr);
    glfwSetWindowUserPointer(m_window, nullptr);
}

void GlfwEvents::Record(Event event) noexcept
{
    try { m_events.push_back(event); }
    catch (...) { m_recordFailed = true; }
}

std::vector<Event> GlfwEvents::Drain()
{
    if (m_recordFailed) 
        throw std::bad_alloc();
    return std::exchange(m_events, {});
}

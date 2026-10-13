#pragma once

#include "core/Event.h"
#include <vector>

struct GLFWwindow;

// 在 ImGui GLFW 初始化前安装：ImGui 保存并链式调用这些回调，Shutdown 时恢复它们。
// 单窗口桥拥有 user pointer；本地 ImGui 后端使用自己的 backend data，不占该指针。
class GlfwEvents
{
public:
    explicit GlfwEvents(GLFWwindow* window);
    ~GlfwEvents();
    GlfwEvents(const GlfwEvents&) = delete;
    GlfwEvents& operator=(const GlfwEvents&) = delete;

    // 返回现有的事件槽然后清空
    std::vector<Event> Drain();
private:
    static GlfwEvents& From(GLFWwindow* window);

    // C 回调不能抛出 C++ 异常；分配失败等 Drain() 在主循环报告。
    void Record(Event event) noexcept;

    GLFWwindow* m_window;
    std::vector<Event> m_events;
    bool m_recordFailed = false;
};

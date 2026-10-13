#pragma once

// 回调只记录事实；Handled 仅阻止继续传给应用 Layer，不影响 ImGui 收到原始输入。
enum class EventType { WindowClose, FramebufferResize, WindowFocus, KeyPressed, KeyReleased,
    MouseButtonPressed, MouseButtonReleased };

struct Event
{
    EventType type;
    bool handled = false;
    int width = 0, height = 0; // framebuffer 像素，不是 UI 逻辑坐标。
    bool focused = false;
    int code = 0, modifiers = 0;
    bool repeat = false;

    bool IsKeyboard() const { return type == EventType::KeyPressed || type == EventType::KeyReleased; }
    bool IsMouse() const { return type == EventType::MouseButtonPressed || type == EventType::MouseButtonReleased; }
};

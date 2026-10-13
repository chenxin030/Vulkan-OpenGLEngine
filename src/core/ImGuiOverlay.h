#pragma once

#include "core/Layer.h"

// 逻辑 Overlay；不创建 Context、不调用 NewFrame、不拥有 GPU 字体/管线。
// 捕获状态由 Application 在分发前设置，与 GlfwInput::Sample 使用同一份。
class ImGuiOverlay : public Layer
{
public:
    void SetCapture(EditorInputCapture capture) { m_capture = capture; }
    void OnEvent(Event& event) override
    {
        if ((event.IsKeyboard() && m_capture.keyboard) || (event.IsMouse() && m_capture.mouse))
            event.handled = true;
    }
private:
    EditorInputCapture m_capture;
};

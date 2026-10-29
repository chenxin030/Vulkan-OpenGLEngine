#pragma once

#include "core/Event.h"
#include "core/FrameContext.h"

class Layer
{
public:
    virtual ~Layer() = default;
    virtual void OnAttach() {}
    // noexcept：退出与异常回收时也要卸载。失败挂接的层同样必须能清理已取得的资源。
    virtual void OnDetach() noexcept {}
    virtual void OnEvent(Event&) {}
    virtual void OnUpdate(FrameContext&) {}
    virtual void OnImGuiRender(const FrameContext&) {}
    virtual void OnFrameEnd() {}
    virtual EditorInputCapture InputCapture() const { return {}; }
};

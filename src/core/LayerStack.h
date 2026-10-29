#pragma once

#include "core/Layer.h"
#include <memory>
#include <vector>

class Renderer;

// 普通 Layer 在 [0, insertIndex)，Overlay 在其后。所有层由栈唯一拥有。
// 按调用约定在运行前安装层；运行后 Clear，层回调中不修改层栈。
class LayerStack
{
public:
    ~LayerStack();
    Layer& PushLayer(std::unique_ptr<Layer> layer);
    Layer& PushOverlay(std::unique_ptr<Layer> layer);
    void Clear();
    void Dispatch(Event& event);
    EditorInputCapture InputCapture() const;
    void RunFrame(Renderer& renderer, FrameContext& frame);

private:
    Layer& Push(std::unique_ptr<Layer> layer, bool overlay);
    std::vector<std::unique_ptr<Layer>> m_layers;
    size_t m_insertIndex = 0;
};

#pragma once

#include "core/Layer.h"
#include <memory>
#include <vector>

class Renderer;

// 普通 Layer 在 [0, insertIndex)，Overlay 在其后。所有层由栈唯一拥有。
// 启动安装 → Seal → 运行 → Clear；运行中不允许 Push，遍历期间不允许改变容器。
class LayerStack
{
public:
    ~LayerStack();
    Layer& PushLayer(std::unique_ptr<Layer> layer);
    Layer& PushOverlay(std::unique_ptr<Layer> layer);
    void Seal() { m_sealed = true; }
    void Clear();
    void Dispatch(Event& event);
    EditorInputCapture InputCapture() const;
    void RunFrame(Renderer& renderer, FrameContext& frame);

private:
    Layer& Push(std::unique_ptr<Layer> layer, bool overlay);
    std::vector<std::unique_ptr<Layer>> m_layers;
    size_t m_insertIndex = 0;
    bool m_sealed = false;      // 层的安装阶段已经结束
    bool m_traversing = false;  // 当前正在操作或调用栈里的层，防止回调中途改变栈。
};

#include "core/LayerStack.h"
#include "rhi/Renderer.h"
#include <stdexcept>

namespace {
// 用法：生命周期覆盖遍历 LayerStack 的整个过程，函数退出时自动取消“正在遍历”的标记，即使退出原因是异常。
struct TraversalGuard
{
    bool& flag;
    explicit TraversalGuard(bool& value) : flag(value)
    {
        if (flag) throw std::logic_error("LayerStack traversal cannot be nested");
        flag = true;
    }
    ~TraversalGuard() { flag = false; }
};
}

LayerStack::~LayerStack() { Clear(); }
Layer& LayerStack::PushLayer(std::unique_ptr<Layer> layer) { return Push(std::move(layer), false); }
Layer& LayerStack::PushOverlay(std::unique_ptr<Layer> layer) { return Push(std::move(layer), true); }

Layer& LayerStack::Push(std::unique_ptr<Layer> layer, bool overlay)
{
    if (m_sealed || m_traversing) throw std::logic_error("Install layers before Application::Run");

    if (!layer) throw std::invalid_argument("Cannot install a null layer");

    // 先保证容器分配成功，再取得层资源。失败时该层清理，之前成功挂接的层仍由栈拥有。
    m_layers.reserve(m_layers.size() + 1);
    TraversalGuard guard(m_traversing);

    try { layer->OnAttach(); }
    catch (...) { layer->OnDetach(); throw; }

    Layer& result = *layer;
    if (overlay) m_layers.push_back(std::move(layer));
    else {
        m_layers.insert(m_layers.begin() + m_insertIndex, std::move(layer));
        ++m_insertIndex;
    }
    return result;
}

void LayerStack::Clear()
{
    // 帧内 Clear 是调用错误，不能让正在使用的 Layer 消失。
    if (m_traversing) throw std::logic_error("Cannot clear layers during traversal");
    m_traversing = true;

    while (!m_layers.empty()) {
        m_layers.back()->OnDetach();
        m_layers.pop_back();
    }
    m_insertIndex = 0;
    m_traversing = false;
}

void LayerStack::Dispatch(Event& event)
{
    TraversalGuard guard(m_traversing);
    for (auto it = m_layers.rbegin(); it != m_layers.rend() && !event.handled; ++it)
        (*it)->OnEvent(event);
}

EditorInputCapture LayerStack::InputCapture() const
{
    EditorInputCapture combined;
    for (const auto& layer : m_layers) {
        const auto capture = layer->InputCapture();
        combined.keyboard |= capture.keyboard;
        combined.mouse |= capture.mouse;
    }
    return combined;
}

void LayerStack::RunFrame(Renderer& renderer, FrameContext& frame)
{
    TraversalGuard guard(m_traversing);
    try {
        for (auto& layer : m_layers) layer->OnUpdate(frame);
        if (!frame.m_snapshot) throw std::logic_error("A scene layer must submit a snapshot");
        // DrawFrame 后同步执行这个回调；允许因为 resize/out-of-date 跳帧而不调用。
        renderer.DrawFrame(*frame.m_snapshot, [&] {
            for (auto& layer : m_layers) layer->OnImGuiRender(frame);
        });
    } catch (...) {
        frame.m_snapshot.reset();
        throw;
    }
    frame.m_snapshot.reset();
    for (auto& layer : m_layers) layer->OnFrameEnd();
}

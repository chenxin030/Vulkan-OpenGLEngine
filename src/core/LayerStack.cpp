#include "core/LayerStack.h"
#include "rhi/Renderer.h"
#include <stdexcept>

LayerStack::~LayerStack() { Clear(); }
Layer& LayerStack::PushLayer(std::unique_ptr<Layer> layer) { return Push(std::move(layer), false); }
Layer& LayerStack::PushOverlay(std::unique_ptr<Layer> layer) { return Push(std::move(layer), true); }

Layer& LayerStack::Push(std::unique_ptr<Layer> layer, bool overlay)
{
    if (!layer) 
        throw std::invalid_argument("Cannot install a null layer");

    m_layers.reserve(m_layers.size() + 1);

    try { layer->OnAttach(); }
    catch (...) { layer->OnDetach(); throw; }

    Layer& result = *layer;
    if (overlay) 
        m_layers.push_back(std::move(layer));
    else {
        m_layers.insert(m_layers.begin() + m_insertIndex, std::move(layer));
        ++m_insertIndex;
    }
    return result;
}

void LayerStack::Clear()
{
    while (!m_layers.empty()) {
        m_layers.back()->OnDetach();
        m_layers.pop_back();
    }
    m_insertIndex = 0;
}

void LayerStack::Dispatch(Event& event)
{
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

void LayerStack::RunFrame(Renderer& renderer, FrameContext& frameContext)
{
    try {
        for (auto& layer : m_layers) 
            layer->OnUpdate(frameContext);

        if (!frameContext.m_snapshot) 
            throw std::logic_error("A scene layer must submit a snapshot");

        frameContext.renderCamera = frameContext.m_snapshot->camera;

        renderer.SetViewportRequest(frameContext.renderViewportRequest);
        renderer.DrawFrame(*frameContext.m_snapshot, [&] {
            frameContext.uiViewportImage = renderer.GetViewportImage();// 不同图形API有不同的Imgui数据
            for (auto& layer : m_layers) 
                layer->OnImGuiRender(frameContext);
        });
    } catch (...) {
        frameContext.m_snapshot.reset();
        throw;
    }
    frameContext.m_snapshot.reset();
    for (auto& layer : m_layers) 
        layer->OnFrameEnd();
}

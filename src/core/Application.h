#pragma once

#include "core/LayerStack.h"
#include "core/ProjectInfo.h"
#include <memory>

class Renderer;
class GlfwEvents;
class GlfwInput;
class ImGuiOverlay;
struct GLFWwindow;

struct ApplicationSpecification
{
    int width = 720, height = 440;
    const char* title = ProjectInfo::Name;
    bool pixelSizedWindow = false; // 展示窗口使用指定像素尺寸，不随显示器缩放。
};

// 单线程、单窗口；应用拥有传入的 Renderer，不依赖全局实例。
class Application
{
public:
    explicit Application(std::unique_ptr<Renderer> renderer, ApplicationSpecification spec = {});
    ~Application();
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    Layer& PushLayer(std::unique_ptr<Layer> layer);
    Layer& PushOverlay(std::unique_ptr<Layer> layer);

    void InstallImGuiOverlay();
    void Run();
    void Close() noexcept;
private:
    void Shutdown() noexcept;
    void DispatchEvents(EditorInputCapture capture);

    std::unique_ptr<Renderer> m_renderer;

    GLFWwindow* m_window = nullptr;
    std::unique_ptr<GlfwEvents> m_events;
    std::unique_ptr<GlfwInput> m_input;

    LayerStack m_layerStack;
    ImGuiOverlay* m_imguiOverlay = nullptr; // 借用，直到 LayerStack::Clear。
    bool m_glfwInitialized = false;
    bool m_rendererInitAttempted = false;
    bool m_rendererReady = false;
    bool m_running = false;
};

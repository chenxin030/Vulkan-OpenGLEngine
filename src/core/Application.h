#pragma once

#include "core/LayerStack.h"
#include <memory>

class Renderer;
class GlfwEvents;
class GlfwInput;
class ImGuiOverlay;
struct GLFWwindow;

struct ApplicationSpecification
{
    int width = 720, height = 440;
    const char* title = "Engine";
    bool visible = true; // 隐藏窗口仅供生命周期检查，默认启动行为不变。
};

// 单线程、单窗口；无全局实例。Renderer 注入让应用生命周期可用无 GPU 的替身验证。
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
    bool m_hasRun = false;
};

#include "core/Application.h"
#include "core/ImGuiOverlay.h"
#include "platform/GlfwEvents.h"
#include "platform/GlfwInput.h"
#include "rhi/Renderer.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <iostream>
#include <stdexcept>

namespace {
// helper
void GlfwErrorCallback(int error, const char* description)
{
    std::cerr << "[glfw] (" << error << ") " << description << std::endl;
}
}

Application::Application(std::unique_ptr<Renderer> renderer, ApplicationSpecification spec)
    : m_renderer(std::move(renderer))
{
    if (!m_renderer) throw std::invalid_argument("Application requires a Renderer");
    // 构造失败不会调用 ~Application；显式回收已取得的部分资源，再保留原异常。

    try {
        glfwSetErrorCallback(GlfwErrorCallback);
        if (!glfwInit()) throw std::runtime_error("ERROR: GLFW init");
        m_glfwInitialized = true;
        glfwDefaultWindowHints();
        m_renderer->ConfigureWindowHints();
        glfwWindowHint(GLFW_VISIBLE, spec.visible ? GLFW_TRUE : GLFW_FALSE);
        m_window = glfwCreateWindow(spec.width, spec.height, spec.title, nullptr, nullptr);
        if (!m_window) throw std::runtime_error("ERROR: GLFW window create");

        m_events = std::make_unique<GlfwEvents>(m_window);
        m_input = std::make_unique<GlfwInput>(m_window);
        m_rendererInitAttempted = true;
        m_renderer->Init(m_window);
        m_rendererReady = true;
    } catch (...) { Shutdown(); throw; }
}

Application::~Application() { Shutdown(); }
Layer& Application::PushLayer(std::unique_ptr<Layer> layer) { return m_layerStack.PushLayer(std::move(layer)); }
Layer& Application::PushOverlay(std::unique_ptr<Layer> layer) { return m_layerStack.PushOverlay(std::move(layer)); }

void Application::InstallImGuiOverlay()
{
    if (m_imguiOverlay) throw std::logic_error("ImGuiOverlay is already installed");
    auto overlay = std::make_unique<ImGuiOverlay>();
    auto* observer = overlay.get();
    m_layerStack.PushOverlay(std::move(overlay));
    m_imguiOverlay = observer;
}

void Application::Close() noexcept
{
    m_running = false;
    if (m_window) glfwSetWindowShouldClose(m_window, GLFW_TRUE);
}

void Application::DispatchEvents(EditorInputCapture capture)
{
    if (m_imguiOverlay) m_imguiOverlay->SetCapture(capture);
    for (auto& event : m_events->Drain()) {
        if (event.type == EventType::WindowClose) {
            Close(); event.handled = true;
        } else if (event.type == EventType::WindowFocus) {
            m_input->ResetCursorBaseline();
        }
        // resize 只通知层；Renderer 在安全的 DrawFrame 入口检查 framebuffer 并重建资源。
        m_layerStack.Dispatch(event);
    }
}

void Application::Run()
{
    if (m_hasRun) throw std::logic_error("Application::Run may only be called once");
    m_hasRun = true;
    m_layerStack.Seal();
    m_running = true;

    double previousTime = glfwGetTime();
    bool paused = false;
    while (m_running && !glfwWindowShouldClose(m_window)) {
        glfwPollEvents();

        // 保留现有上一帧 UI capture；左键当前状态的优先过滤在 GlfwInput 内完成。
        const auto capture = m_input->EffectiveCapture(m_layerStack.InputCapture());
        DispatchEvents(capture);
        if (!m_running || glfwWindowShouldClose(m_window)) break;

        int width = 0, height = 0;
        glfwGetFramebufferSize(m_window, &width, &height);
        if (width == 0 || height == 0) {
            paused = true;
            m_input->ResetCursorBaseline();
            glfwWaitEventsTimeout(0.1); // 仍响应关闭，两个后端都不空转或推进场景。
            previousTime = glfwGetTime();
            continue;
        }

        const double now = glfwGetTime();
        FrameContext frame;
        frame.deltaSeconds = paused ? 0.0f : static_cast<float>(now - previousTime);
        previousTime = now;
        paused = false;
        frame.framebufferSize = glm::vec2(width, height);
        frame.input = m_input->Sample(capture);
        // Escape 的唯一退出入口：键盘 capture 过滤之后才检查，不在 KeyPressed 重复处理。
        if (frame.input.quitRequested) { Close(); break; }
        m_layerStack.RunFrame(*m_renderer, frame);
    }
}

void Application::Shutdown() noexcept
{
    m_running = false;
    // WaitIdle 只在 Init 成功后调用；部分 Init 的 Cleanup 自己按资源状态回收。
    if (m_rendererReady) {
        try { m_renderer->WaitIdle(); } catch (...) { std::cerr << "Renderer WaitIdle failed during shutdown\n"; }
        m_rendererReady = false;
    }
    m_layerStack.Clear();
    m_imguiOverlay = nullptr;
    if (m_rendererInitAttempted) {
        try { m_renderer->Cleanup(); } catch (...) { std::cerr << "Renderer Cleanup failed during shutdown\n"; }
        m_rendererInitAttempted = false;
    }
    m_renderer.reset();
    m_input.reset();
    m_events.reset();
    if (m_window) { glfwDestroyWindow(m_window); m_window = nullptr; }
    if (m_glfwInitialized) { glfwTerminate(); m_glfwInitialized = false; }
}

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
void GlfwErrorCallback(int error, const char* description)
{
    std::cerr << "[glfw] (" << error << ") " << description << std::endl;
}
}

Application::Application(std::unique_ptr<Renderer> renderer, ApplicationSpecification spec)
    : m_renderer(std::move(renderer))
{
    // 构造失败不会调用 ~Application；
    // 且显式回收已取得的部分资源，再保留原异常。
    if (!m_renderer) 
        throw std::invalid_argument("Application requires a Renderer");
    try {
        glfwSetErrorCallback(GlfwErrorCallback);
        if (!glfwInit()) throw std::runtime_error("ERROR: GLFW init");
        m_glfwInitialized = true;
        glfwDefaultWindowHints();
        m_renderer->ConfigureWindowHints();
        if (spec.pixelSizedWindow) {
            glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_FALSE);
            glfwWindowHint(GLFW_SCALE_FRAMEBUFFER, GLFW_FALSE);
        }
        m_window = glfwCreateWindow(spec.width, spec.height, spec.title, nullptr, nullptr);
        if (!m_window) throw std::runtime_error("ERROR: GLFW window create");

        m_events = std::make_unique<GlfwEvents>(m_window);
        m_input = std::make_unique<GlfwInput>(m_window);
        m_rendererInitAttempted = true;
        m_renderer->Init(m_window);
        m_rendererReady = true;
    } catch (...) { 
        Shutdown(); 
        throw; 
    }
}

Application::~Application() { Shutdown(); }
Layer& Application::PushLayer(std::unique_ptr<Layer> layer) { return m_layerStack.PushLayer(std::move(layer)); }
Layer& Application::PushOverlay(std::unique_ptr<Layer> layer) { return m_layerStack.PushOverlay(std::move(layer)); }

void Application::InstallImGuiOverlay()
{
    auto overlay = std::make_unique<ImGuiOverlay>();
    auto* observer = overlay.get();
    m_layerStack.PushOverlay(std::move(overlay));
    m_imguiOverlay = observer;
}

void Application::Close() noexcept
{
    m_running = false;
    if (m_window) 
        glfwSetWindowShouldClose(m_window, GLFW_TRUE);
}

void Application::DispatchEvents(EditorInputCapture capture)
{
    if (m_imguiOverlay) 
        m_imguiOverlay->SetCapture(capture);
    for (auto& event : m_events->Drain()) {
        if (event.type == EventType::WindowClose) {
            Close(); 
            event.handled = true;
        } else if (event.type == EventType::WindowFocus) {
            m_input->ResetCursorBaseline();
        }
        m_layerStack.Dispatch(event);
    }
}

void Application::Run()
{
    m_running = true;

    double previousTime = glfwGetTime();
    bool paused = false;
    while (m_running && !glfwWindowShouldClose(m_window)) {
        glfwPollEvents();

        // 统一计算一次捕获状态，供事件分发和后续输入采样共用。
        const auto effectiveCapture = m_input->EffectiveCapture(m_layerStack.InputCapture());
        DispatchEvents(effectiveCapture);
        if (!m_running || glfwWindowShouldClose(m_window)) 
            break;

        int width = 0, height = 0;
        glfwGetFramebufferSize(m_window, &width, &height);
        if (width == 0 || height == 0) {
            paused = true;
            m_input->ResetCursorBaseline();
            glfwWaitEventsTimeout(0.1);
            previousTime = glfwGetTime();
            continue;
        }

        const double now = glfwGetTime();
        FrameContext frame;
        frame.deltaSeconds = paused ? 0.0f : static_cast<float>(now - previousTime);
        frame.windowFramebufferExtent = glm::vec2(width, height);
        frame.input = m_input->Sample(effectiveCapture);
        if (frame.input.quitRequested) { 
            Close(); 
            break; 
        }

        previousTime = now;
        paused = false;

        m_layerStack.RunFrame(*m_renderer, frame);
    }
}

void Application::Shutdown() noexcept
{
    m_running = false;
    if (m_rendererReady) {
        try { m_renderer->WaitIdle(); } 
        catch (...) { std::cerr << "Renderer WaitIdle failed during shutdown\n"; }
        m_rendererReady = false;
    }
    m_layerStack.Clear();
    m_imguiOverlay = nullptr;
    if (m_rendererInitAttempted) {
        try { m_renderer->Cleanup(); } 
        catch (...) { std::cerr << "Renderer Cleanup failed during shutdown\n"; }
        m_rendererInitAttempted = false;
    }
    m_renderer.reset();
    m_input.reset();
    m_events.reset();
    if (m_window) { glfwDestroyWindow(m_window); m_window = nullptr; }
    if (m_glfwInitialized) { glfwTerminate(); m_glfwInitialized = false; }
}

#include "core/Application.h"
#include "core/ImGuiOverlay.h"
#include "core/LayerStack.h"
#include "core/ScopeExit.h"
#include "platform/GlfwInput.h"
#include "rhi/Renderer.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <iostream>
#include <string>
#include <vector>

namespace {
using Log = std::vector<std::string>;
template<class F> bool Throws(F&& f) { try { f(); } catch (const std::exception&) { return true; } return false; }

// 替身仍使用真实隐藏 GLFW 窗口，但不创建 GL/Vulkan 设备；测试的是应用边界和调用顺序。
struct FakeRenderer : Renderer
{
    Log& log;
    GLFWwindow* window = nullptr;
    bool failInit = false, failDraw = false, skipUi = false;
    bool* windowAliveAtCleanup = nullptr;
    explicit FakeRenderer(Log& value) : log(value) {}
    ~FakeRenderer() override { log.push_back("renderer.destroy"); }
    void ConfigureWindowHints() const override { glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); }
    void Init(GLFWwindow* w) override
    {
        window = w; log.push_back("renderer.init");
        if (failInit) throw std::runtime_error("injected init failure");
    }
    void DrawFrame(const RenderSnapshot&, const std::function<void()>& ui) override
    {
        log.push_back("renderer.draw");
        if (failDraw) throw std::runtime_error("injected draw failure");
        if (!skipUi) ui();
    }
    void WaitIdle() override { log.push_back("renderer.wait"); }
    void Cleanup() override
    {
        log.push_back("renderer.cleanup");
        if (windowAliveAtCleanup) *windowAliveAtCleanup = glfwGetWindowAttrib(window, GLFW_CLIENT_API) == GLFW_NO_API;
    }
};

struct TraceLayer : Layer
{
    Log& log;
    std::string name;
    bool consumes = false, submits = false, failAttach = false, failUpdate = false;
    FrameContext* lastFrame = nullptr;
    Application* closeAtEnd = nullptr;
    bool* releasedBeforeEnd = nullptr;
    LayerStack* mutationTarget = nullptr;
    bool* mutationsRejected = nullptr;
    EditorInputCapture capture;
    TraceLayer(Log& value, std::string label) : log(value), name(std::move(label)) {}
    ~TraceLayer() override { log.push_back(name + ".destroy"); }
    void OnAttach() override
    {
        log.push_back(name + ".attach");
        if (failAttach) throw std::runtime_error("injected attach failure");
    }
    void OnDetach() noexcept override { log.push_back(name + ".detach"); }
    void OnEvent(Event& event) override { log.push_back(name + ".event"); event.handled |= consumes; }
    void OnUpdate(FrameContext& frame) override
    {
        lastFrame = &frame;
        log.push_back(name + ".update");
        if (mutationsRejected) *mutationsRejected =
            Throws([&] { mutationTarget->PushLayer(std::make_unique<Layer>()); }) &&
            Throws([&] { mutationTarget->Clear(); });
        if (submits) frame.Submit({});
        if (failUpdate) throw std::runtime_error("injected update failure");
    }
    void OnImGuiRender(const FrameContext&) override { log.push_back(name + ".ui"); }
    void OnFrameEnd() override
    {
        log.push_back(name + ".end");
        if (releasedBeforeEnd) *releasedBeforeEnd = !lastFrame->HasSnapshot();
        if (closeAtEnd) closeAtEnd->Close();
    }
    EditorInputCapture InputCapture() const override { return capture; }
};
}

bool Stage10Checks(int& assertions)
{
#define CHECK10(condition) do { ++assertions; if (!(condition)) { \
    std::cerr << "Stage10 CHECK failed at " << __LINE__ << ": " #condition << '\n'; return false; } } while(false)
    {
        Log log;
        LayerStack stack;
        auto a = std::make_unique<TraceLayer>(log, "a"); a->submits = true; a->capture.keyboard = true;
        bool released = false, rejected = false; a->releasedBeforeEnd = &released;
        a->mutationTarget = &stack; a->mutationsRejected = &rejected;
        stack.PushLayer(std::move(a));
        stack.PushOverlay(std::make_unique<TraceLayer>(log, "o1"));
        auto b = std::make_unique<TraceLayer>(log, "b"); b->capture.mouse = true;
        stack.PushLayer(std::move(b));
        auto o2 = std::make_unique<TraceLayer>(log, "o2"); auto* observer = o2.get();
        stack.PushOverlay(std::move(o2));
        CHECK10((log == Log{"a.attach", "o1.attach", "b.attach", "o2.attach"}));
        CHECK10(stack.InputCapture().keyboard && stack.InputCapture().mouse);
        log.clear(); Event event{EventType::KeyPressed}; stack.Dispatch(event);
        CHECK10((log == Log{"o2.event", "o1.event", "b.event", "a.event"}));
        CHECK10(!event.handled);
        observer->consumes = true; log.clear(); stack.Dispatch(event);
        CHECK10((log == Log{"o2.event"})); CHECK10(event.handled);
        log.clear(); stack.Dispatch(event); CHECK10(log.empty());
        stack.Seal();
        CHECK10(Throws([&] { stack.PushLayer(std::make_unique<Layer>()); }));
        log.clear(); FakeRenderer renderer(log); FrameContext frame; stack.RunFrame(renderer, frame);
        CHECK10((log == Log{"a.update","b.update","o1.update","o2.update","renderer.draw",
            "a.ui","b.ui","o1.ui","o2.ui","a.end","b.end","o1.end","o2.end"}));
        CHECK10(released && !frame.HasSnapshot());
        CHECK10(rejected);
        renderer.skipUi = true; log.clear(); stack.RunFrame(renderer, frame);
        CHECK10((log == Log{"a.update","b.update","o1.update","o2.update","renderer.draw",
            "a.end","b.end","o1.end","o2.end"}));
        renderer.failDraw = true; log.clear(); CHECK10(Throws([&] { stack.RunFrame(renderer, frame); }));
        CHECK10(!frame.HasSnapshot());
        CHECK10((log == Log{"a.update","b.update","o1.update","o2.update","renderer.draw"}));
        log.clear(); stack.Clear();
        CHECK10((log == Log{"o2.detach","o2.destroy","o1.detach","o1.destroy","b.detach","b.destroy","a.detach","a.destroy"}));
        log.clear(); stack.Clear(); CHECK10(log.empty());
    }
    {
        ImGuiOverlay overlay;
        overlay.SetCapture({true, false});
        Event key{EventType::KeyPressed}; overlay.OnEvent(key); CHECK10(key.handled);
        Event release{EventType::KeyReleased}; overlay.OnEvent(release); CHECK10(release.handled);
        Event mouse{EventType::MouseButtonPressed}; overlay.OnEvent(mouse); CHECK10(!mouse.handled);
        overlay.SetCapture({false, true}); overlay.OnEvent(mouse); CHECK10(mouse.handled);
        Event close{EventType::WindowClose}; overlay.OnEvent(close); CHECK10(!close.handled);
        Event resize{EventType::FramebufferResize}; overlay.OnEvent(resize); CHECK10(!resize.handled);
        Event focus{EventType::WindowFocus}; overlay.OnEvent(focus); CHECK10(!focus.handled);
        overlay.SetCapture({}); Event freeKey{EventType::KeyPressed}; overlay.OnEvent(freeKey); CHECK10(!freeKey.handled);
    }
    {
        Log log; LayerStack stack;
        CHECK10(Throws([&] { stack.PushLayer(nullptr); }));
        auto bad = std::make_unique<TraceLayer>(log, "bad"); bad->failAttach = true;
        CHECK10(Throws([&] { stack.PushLayer(std::move(bad)); }));
        CHECK10((log == Log{"bad.attach","bad.detach","bad.destroy"}));
        auto badUpdate = std::make_unique<TraceLayer>(log, "update"); badUpdate->submits = true; badUpdate->failUpdate = true;
        stack.PushLayer(std::move(badUpdate));
        FakeRenderer renderer(log); FrameContext frame;
        CHECK10(Throws([&] { stack.RunFrame(renderer, frame); })); CHECK10(!frame.HasSnapshot());
        stack.Clear(); CHECK10(log.back() == "update.destroy");
    }
    {
        Log log; LayerStack empty; FakeRenderer renderer(log); FrameContext frame;
        CHECK10(Throws([&] { empty.RunFrame(renderer, frame); })); CHECK10(log.empty());
        frame.Submit({}); CHECK10(Throws([&] { frame.Submit({}); }));
    }
    {
        int releases = 0;
        { ScopeExit guard([&] { ++releases; }); }
        CHECK10(releases == 1);
        CHECK10(Throws([&] { ScopeExit guard([&] { ++releases; }); throw std::runtime_error("scope"); }));
        CHECK10(releases == 2);
    }
    const ApplicationSpecification hidden{720, 440, "Stage10 lifecycle", false};
    {
        Log log; bool alive = false;
        auto renderer = std::make_unique<FakeRenderer>(log); renderer->failInit = true; renderer->windowAliveAtCleanup = &alive;
        CHECK10(Throws([&] { Application app(std::move(renderer), hidden); }));
        CHECK10(alive); CHECK10((log == Log{"renderer.init","renderer.cleanup","renderer.destroy"}));
    }
    {
        Log log;
        {
            Application app(std::make_unique<FakeRenderer>(log), hidden);
            app.PushLayer(std::make_unique<TraceLayer>(log, "good"));
            auto bad = std::make_unique<TraceLayer>(log, "bad"); bad->failAttach = true;
            CHECK10(Throws([&] { app.PushLayer(std::move(bad)); }));
        }
        CHECK10((log == Log{"renderer.init","good.attach","bad.attach","bad.detach","bad.destroy",
            "renderer.wait","good.detach","good.destroy","renderer.cleanup","renderer.destroy"}));
    }
    {
        Log log; bool alive = false;
        CHECK10(Throws([&] {
            auto renderer = std::make_unique<FakeRenderer>(log); renderer->failDraw = true; renderer->windowAliveAtCleanup = &alive;
            Application app(std::move(renderer), hidden);
            auto layer = std::make_unique<TraceLayer>(log, "scene"); layer->submits = true;
            app.PushLayer(std::move(layer)); app.Run();
        }));
        CHECK10(alive);
        CHECK10((log == Log{"renderer.init","scene.attach","scene.update","renderer.draw","renderer.wait",
            "scene.detach","scene.destroy","renderer.cleanup","renderer.destroy"}));
    }
    {
        Log log;
        {
            Application app(std::make_unique<FakeRenderer>(log), hidden);
            auto layer = std::make_unique<TraceLayer>(log, "scene"); layer->submits = true; layer->closeAtEnd = &app;
            app.PushLayer(std::move(layer)); app.InstallImGuiOverlay();
            CHECK10(Throws([&] { app.InstallImGuiOverlay(); }));
            app.Run(); CHECK10(Throws([&] { app.Run(); }));
            CHECK10(Throws([&] { app.PushLayer(std::make_unique<Layer>()); }));
        }
        CHECK10((log == Log{"renderer.init","scene.attach","scene.update","renderer.draw","scene.ui","scene.end",
            "renderer.wait","scene.detach","scene.destroy","renderer.cleanup","renderer.destroy"}));
    }
    {
        Log log;
        {
            auto renderer = std::make_unique<FakeRenderer>(log); auto* observer = renderer.get();
            Application app(std::move(renderer), hidden);
            auto layer = std::make_unique<TraceLayer>(log, "scene"); layer->submits = true; layer->closeAtEnd = &app;
            app.PushLayer(std::move(layer));
            auto key = glfwSetKeyCallback(observer->window, nullptr);
            glfwSetKeyCallback(observer->window, key); CHECK10(key != nullptr);
            key(observer->window, GLFW_KEY_W, 0, GLFW_PRESS, 0);
            key(observer->window, GLFW_KEY_W, 0, GLFW_RELEASE, 0);
            GlfwInput input(observer->window);
            CHECK10(input.EffectiveCapture({}).keyboard && input.EffectiveCapture({}).mouse);
            const auto sampled = input.Sample({});
            CHECK10(sampled.moveAxis == glm::vec3(0) && !sampled.lookActive && !sampled.quitRequested);
            app.Run();
            CHECK10((log == Log{"renderer.init","scene.attach","scene.event","scene.event",
                "scene.update","renderer.draw","scene.ui","scene.end"}));
        }
        CHECK10(log.back() == "renderer.destroy");
    }
    {
        Log log;
        {
            auto renderer = std::make_unique<FakeRenderer>(log); auto* observer = renderer.get();
            Application app(std::move(renderer), hidden);
            auto layer = std::make_unique<TraceLayer>(log, "scene"); layer->submits = true; layer->closeAtEnd = &app;
            app.PushLayer(std::move(layer)); app.InstallImGuiOverlay();
            // 调用实际安装的窗口关闭桥；Application 必须先消费它，不能交给 UI 阻挡。
            auto callback = glfwSetWindowCloseCallback(observer->window, nullptr);
            glfwSetWindowCloseCallback(observer->window, callback); CHECK10(callback != nullptr);
            callback(observer->window);
            app.Run();
            CHECK10((log == Log{"renderer.init","scene.attach"}));
        }
        CHECK10(log.back() == "renderer.destroy");
    }
#undef CHECK10
    return true;
}

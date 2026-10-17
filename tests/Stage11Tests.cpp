#include "editor/ViewportInteraction.h"
#include "core/LayerStack.h"
#include "rhi/Renderer.h"
#include "editor/EditorGeometry.h"
#include "scene/SceneUpdater.h"
#include <iostream>
#include <limits>

namespace {
bool Near(glm::vec2 a, glm::vec2 b) { return glm::length(a - b) < 0.001f; }
struct ViewRenderer : Renderer
{
    bool skip = false;
    ViewportExtent received;
    void ConfigureWindowHints() const override {}
    void Init(GLFWwindow*) override {}
    void DrawFrame(const RenderSnapshot&, const std::function<void()>& ui) override
    {
        received = m_viewportRequest.extent;
        if (!skip) ui();
    }
    ViewportImage GetViewportImage() const override { return {123, received, {0, 1}, {1, 0}, {}}; }
    void WaitIdle() override {}
    void Cleanup() override {}
};
struct ViewLayer : Layer
{
    bool sawImage = false, frameEnded = false;
    FrameContext* context = nullptr;
    void OnUpdate(FrameContext& frame) override
    {
        context = &frame;
        frame.viewportRequest = {{750, 350}, true};
        RenderSnapshot snapshot; snapshot.camera.position = {3, 4, 5};
        frame.Submit(std::move(snapshot));
    }
    void OnImGuiRender(const FrameContext& frame) override
    {
        sawImage = frame.viewportImage.textureId == 123 && frame.viewportImage.extent == ViewportExtent{750, 350} &&
            frame.viewportImage.uv0.y == 1 && frame.renderCamera.position == glm::vec3(3, 4, 5);
    }
    void OnFrameEnd() override { frameEnded = !context->HasSnapshot(); }
};
}

bool Stage11Checks(int& assertions)
{
#define CHECK11(...) do { ++assertions; if (!(__VA_ARGS__)) { \
    std::cerr << "Stage11 check failed at " << __LINE__ << ": " #__VA_ARGS__ << '\n'; return false; } } while(false)
    CHECK11(ViewportPixels({400, 200}, {1.5f, 2}) == ViewportExtent{600, 400});
    CHECK11(ViewportPixels({100.2f, 100.8f}, {1, 1}) == ViewportExtent{100, 101});
    CHECK11(ViewportPixels({0.1f, 0.2f}, {1, 1}) == ViewportExtent{1, 1});
    CHECK11(ViewportPixels({300, 200}, {2, 2}, 256) == ViewportExtent{256, 256});
    CHECK11(!ViewportPixels({0, 200}, {1, 1}).Valid());
    CHECK11(!ViewportPixels({-1, 200}, {1, 1}).Valid());
    CHECK11(!ViewportPixels({200, 200}, {0, 1}).Valid());
    CHECK11(!ViewportPixels({200, 200}, {1, -1}).Valid());
    CHECK11(!ViewportPixels({200, 200}, {1, 1}, 0).Valid());
    CHECK11(!ViewportPixels({std::numeric_limits<float>::infinity(), 200}, {1, 1}).Valid());
    CHECK11(!ViewportPixels({200, 200}, {1, std::numeric_limits<float>::quiet_NaN()}).Valid());
    CHECK11(ViewportPixels({std::numeric_limits<float>::max(), 200}, {2, 1}).width == 16384);
    CHECK11(ViewportExtent{}.Aspect() == 0);
    CHECK11(ViewportExtent{600, 400}.Aspect() == 1.5f);
    CHECK11(!ViewportImage{}.Valid());
    CHECK11((ViewportImage{7, {600, 400}}).Valid());

    auto rect = FitViewport({100, 50}, {800, 600}, {800, 400}, {1, 1});
    CHECK11(Near(rect.min, {100, 150}) && Near(rect.size, {800, 400}));
    CHECK11(rect.Contains({100, 150}));
    CHECK11(!rect.Contains({900, 150}) && !rect.Contains({100, 550}));
    CHECK11(!rect.Contains({300, 100})); // 留边不能拾取。
    auto highDpi = FitViewport({100, 50}, {800, 600}, {1600, 800}, {2, 2});
    CHECK11(Near(highDpi.min, rect.min) && Near(highDpi.size, rect.size));
    auto unequalDpi = FitViewport({100, 50}, {800, 600}, {1600, 800}, {2, 1});
    CHECK11(Near(unequalDpi.min, {200, 50}) && Near(unequalDpi.size, {600, 600}));
    CHECK11(!FitViewport({0, 0}, {0, 100}, {400, 200}, {1, 1}).Contains({0, 0}));

    InputState input; input.moveAxis = {1, 0, 1}; input.lookDelta = {20, 30};
    input.lookActive = true; input.sprint = true; input.quitRequested = true;
    input.cursorPosition = {300, 300};
    bool admitted = false;
    auto filtered = FilterViewportCamera(input, rect, true, true, false, admitted);
    CHECK11(admitted && filtered.lookActive && filtered.moveAxis == input.moveAxis);
    CHECK11(filtered.lookDelta == glm::vec2(0)); // 新手势第一帧不跳。
    filtered = FilterViewportCamera(input, rect, true, true, false, admitted);
    CHECK11(filtered.lookDelta == input.lookDelta);
    for (int reason = 0; reason < 5; ++reason) {
        InputState current = input;
        if (reason == 3) current.cursorPosition = {50, 300};
        if (reason == 4) current.lookActive = false;
        filtered = FilterViewportCamera(current, rect, reason != 0, reason != 1, reason == 2, admitted);
        CHECK11(!admitted && !filtered.lookActive && filtered.lookDelta == glm::vec2(0));
        CHECK11(filtered.moveAxis == glm::vec3(0) && !filtered.sprint);
        CHECK11(filtered.quitRequested); // 相机边界不改 Application 退出意图。
    }
    filtered = FilterViewportCamera(input, rect, true, true, false, admitted);
    CHECK11(filtered.lookDelta == glm::vec2(0));

    // 非零 Image 原点 + 不同目标 aspect；2D/透视都沿真实三角形求交。
    Scene scene = BuildDemo2DScene(); SceneUpdater updater; updater.Update(scene, 0);
    for (auto projection : {CameraProjection::Orthographic, CameraProjection::Perspective}) {
        Camera rendered = scene.camera; rendered.projection = projection;
        for (ViewportExtent extent : {ViewportExtent{900, 300}, ViewportExtent{300, 600}}) {
            const auto imageRect = FitViewport({173, 91}, {900, 600}, extent, {1, 1});
            for (int i = 0; i < 3; ++i) {
                const glm::vec4 clip = rendered.ProjectionMatrix(extent.Aspect()) * rendered.ViewMatrix() *
                    glm::vec4(float(i - 1) * 2, 0, 0, 1);
                const glm::vec2 local{(clip.x / clip.w + 1) * imageRect.size.x / 2,
                    (1 - clip.y / clip.w) * imageRect.size.y / 2};
                if (local.x < 0 || local.x >= imageRect.size.x) continue;
                const glm::vec2 screen = local + imageRect.min;
                CHECK11(imageRect.Contains(screen));
                const auto picked = EditorGeometry::Pick(scene, screen - imageRect.min, imageRect.size,
                    extent.Aspect(), &rendered);
                CHECK11(picked.IsValid() && picked.index == uint32_t(i));
            }
        }
    }
    Camera rendered = scene.camera;
    scene.camera.position.x += 100; // UI 相机字段与当前已提交快照不同也应按本帧相机拾取。
    CHECK11(EditorGeometry::Pick(scene, {400, 200}, {800, 400}, 2, &rendered).index == 1);
    CHECK11(!EditorGeometry::Pick(scene, {400, 200}, {800, 400}, 2).IsValid());

    ViewRenderer renderer; LayerStack layers;
    auto owner = std::make_unique<ViewLayer>(); auto* layer = owner.get();
    layers.PushLayer(std::move(owner)); layers.Seal(); FrameContext frame;
    layers.RunFrame(renderer, frame);
    CHECK11(renderer.received == ViewportExtent{750, 350});
    CHECK11(layer->sawImage && layer->frameEnded);
    renderer.skip = true; layer->sawImage = false; layer->frameEnded = false;
    FrameContext skipped; layers.RunFrame(renderer, skipped);
    CHECK11(!layer->sawImage && layer->frameEnded && !skipped.viewportImage.Valid());
#undef CHECK11
    return true;
}

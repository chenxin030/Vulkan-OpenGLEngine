#include "player/PlayerLayer.h"
#include "scene/SceneSerializer.h"
#include "scene/SceneRuntime.h"
#include <imgui.h>
#include <cstdio>
#include <utility>

PlayerLayer::PlayerLayer(std::filesystem::path scene, std::string backend)
    : m_scenePath(std::move(scene)), m_backend(std::move(backend)) {}

void PlayerLayer::OnAttach()
{
    m_scene = SceneSerializer::Load(m_scenePath);
    m_runtime = CreateSceneRuntime(m_scene);
    m_skipDelta = true;
}

void PlayerLayer::OnDetach() noexcept
{
    m_runtime.reset();
}

void PlayerLayer::OnUpdate(FrameContext& frame)
{
    m_lastFrameMs = frame.deltaSeconds * 1000;
    m_camera.Update(m_scene.camera, frame.input, frame.deltaSeconds);
    m_runtime->Update(m_skipDelta ? 0 : frame.deltaSeconds);
    m_skipDelta = false;
    frame.SubmitRenderSnapShot(m_runtime->Snapshot());
    frame.renderViewportRequest = {{uint32_t(frame.windowFramebufferExtent.x), uint32_t(frame.windowFramebufferExtent.y)}, true};
}

void PlayerLayer::OnImGuiRender(const FrameContext& frame)
{
    const auto size = ImGui::GetIO().DisplaySize;
    const auto& image = frame.uiViewportImage;
    if (image.Valid()) ImGui::GetBackgroundDrawList()->AddImage(ImTextureID(image.textureId), {0, 0}, size,
        {image.uv0.x, image.uv0.y}, {image.uv1.x, image.uv1.y});
    char line[300];
    std::snprintf(line, sizeof(line), "PLAYER | %s | %.2f ms | RMB + WASD / QE", m_backend.c_str(), m_lastFrameMs);
    std::string status(line);
    for (const auto& statistic : m_runtime->Statistics()) {
        status += " | " + statistic.name + " " + std::to_string(statistic.value);
    }
    ImGui::GetForegroundDrawList()->AddText({12, 12}, IM_COL32_WHITE, status.c_str());
}

#pragma once

#include "core/Layer.h"
#include "scene/CameraController.h"
#include "scene/Scene.h"
#include "scene/SceneRuntime.h"
#include <filesystem>
#include <memory>
#include <string>

class PlayerLayer : public Layer
{
public:
    PlayerLayer(std::filesystem::path scene, std::string backend);
    void OnAttach() override;
    void OnDetach() noexcept override;
    void OnUpdate(FrameContext& frame) override;
    void OnImGuiRender(const FrameContext& frame) override;

private:
    std::filesystem::path m_scenePath;
    Scene m_scene;
    std::string m_backend;
    std::unique_ptr<SceneRuntime> m_runtime;
    CameraController m_camera;
    bool m_skipDelta = true;
    double m_lastFrameMs = 0;
};

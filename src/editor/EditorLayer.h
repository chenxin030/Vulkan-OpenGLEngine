#pragma once

#include "core/Layer.h"
#include "editor/Editor.h"
#include "scene/CameraController.h"
#include "scene/SceneRuntime.h"
#include <memory>

class EditorLayer : public Layer
{
public:
    explicit EditorLayer(std::filesystem::path initialScene)
        : m_initialScene(std::move(initialScene)) {}
    void OnAttach() override;
    void OnDetach() noexcept override;
    void OnEvent(Event& event) override;
    void OnUpdate(FrameContext& frame) override;
    void OnImGuiRender(const FrameContext& frame) override;
    void OnFrameEnd() override;
    EditorInputCapture InputCapture() const override { return m_editor.InputCapture(); }
private:
    Scene& ActiveScene();

    CameraController m_cameraController;
    Editor m_editor;

    std::filesystem::path m_initialScene;
    std::optional<Scene> m_editScene;
    std::optional<Scene> m_runtimeScene;
    std::unique_ptr<SceneRuntime> m_runtime;
};

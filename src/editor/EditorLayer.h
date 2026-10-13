#pragma once

#include "core/Layer.h"
#include "editor/Editor.h"
#include "scene/CameraController.h"
#include "scene/SceneUpdater.h"

// 编辑会话的所有者；Application 不知道 Registry、Play 或场景加载规则。
class EditorLayer : public Layer
{
public:
    void OnAttach() override;
    void OnDetach() noexcept override;
    void OnUpdate(FrameContext& frame) override;
    void OnImGuiRender(const FrameContext& frame) override;
    void OnFrameEnd() override;
    EditorInputCapture InputCapture() const override { return m_editor.InputCapture(); }
private:
    std::optional<Scene> m_editScene;
    std::optional<Scene> m_runtimeScene;
    SceneUpdater m_editUpdater;
    SceneUpdater m_runtimeUpdater;
    CameraController m_cameraController;
    Editor m_editor;
};

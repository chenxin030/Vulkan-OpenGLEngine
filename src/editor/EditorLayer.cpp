#include "editor/EditorLayer.h"

void EditorLayer::OnAttach()
{
    m_editor.InitializeLayout();
    m_editScene.emplace(SceneDocument::EmptyScene());
}

void EditorLayer::OnDetach() noexcept
{
    // 层卸载早于 Renderer/ImGui Context 清理，最后一次布局仍可序列化。
    m_editor.SaveLayout();
    m_runtimeScene.reset();
    m_editScene.reset();
}

void EditorLayer::OnEvent(Event& event)
{
    // Application 在零 framebuffer 时仍分发事件；不能只依靠下一次 OnUpdate 清手势。
    if (event.type == EventType::WindowFocus && !event.focused) 
        m_editor.CancelViewportInput();
}

void EditorLayer::OnUpdate(FrameContext& frame)
{
    if (m_editor.ConsumeDialogPause()) frame.deltaSeconds = 0;
    Scene& activeScene = m_editor.ActiveScene(*m_editScene, m_runtimeScene);
    m_cameraController.Update(activeScene.camera, m_editor.CameraInput(frame.input), frame.deltaSeconds);
    frame.viewportRequest = m_editor.Viewport();
    // Edit 仍运行 Spin/Bob 预览；Play 使用独立 updater 的累计时间。
    (m_runtimeScene ? m_runtimeUpdater : m_editUpdater).Update(activeScene, frame.deltaSeconds);
    frame.Submit(BuildRenderSnapshot(activeScene));
}

void EditorLayer::OnImGuiRender(const FrameContext& frame)
{
    Scene& activeScene = m_editor.ActiveScene(*m_editScene, m_runtimeScene);
    // 投影宽高比取实际像素尺寸；UI 鼠标坐标仍使用 ImGui 的逻辑坐标。
    // Renderer 给出实际目标尺寸和本帧借用句柄；请求尺寸在下一帧入口生效。
    m_editor.Draw(activeScene, m_runtimeScene.has_value(), frame.viewportImage, frame.renderCamera);
}

void EditorLayer::OnFrameEnd()
{
    m_editor.UpdateLayoutPersistence();
    const bool wasPlaying = m_runtimeScene.has_value();
    m_editor.ApplyPending(*m_editScene, m_runtimeScene);
    if (m_editor.ConsumeSceneChange()) m_editUpdater = SceneUpdater{};
    if (wasPlaying != m_runtimeScene.has_value()) m_runtimeUpdater = SceneUpdater{};
}

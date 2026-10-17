#include "editor/EditorLayer.h"

void EditorLayer::OnAttach() { m_editScene.emplace(BuildDemo2DScene()); }

void EditorLayer::OnDetach() noexcept
{
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
    const bool wasPlaying = m_runtimeScene.has_value();
    m_editor.ApplyPending(*m_editScene, m_runtimeScene);
    if (wasPlaying != m_runtimeScene.has_value()) m_runtimeUpdater = SceneUpdater{};
}

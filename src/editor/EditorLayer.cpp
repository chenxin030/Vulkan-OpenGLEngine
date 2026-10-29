#include "editor/EditorLayer.h"
#include "scene/Systems.h"
#include "scene/SceneRuntime.h"

void EditorLayer::OnAttach()
{
    m_editor.InitializeLayout();
    m_editScene.emplace(SceneDocument::EmptyScene());
    if (!m_initialScene.empty()) 
        m_editor.RequestInitialScene(std::filesystem::absolute(m_initialScene));
}

void EditorLayer::OnDetach() noexcept
{
    // 层卸载早于 Renderer/ImGui Context 清理，最后一次布局仍可序列化。
    m_editor.SaveLayout();
    m_editor.SetRuntimeEntities(std::nullopt);
    m_runtime.reset();
    m_runtimeScene.reset();
    m_editScene.reset();
}

void EditorLayer::OnEvent(Event& event)
{
    // 取消相机输入，防止恢复窗口后沿用旧的输入状态
    if (event.type == EventType::WindowFocus && !event.focused) 
        m_editor.CancelViewportInput();
}

void EditorLayer::OnUpdate(FrameContext& frame)
{
    if (m_editor.ConsumeDialogPause()) 
        frame.deltaSeconds = 0;
    Scene& activeScene = ActiveScene();
    m_cameraController.Update(activeScene.camera, m_editor.CameraInput(frame.input), frame.deltaSeconds);
    frame.renderViewportRequest = m_editor.Viewport();
    if (m_runtime) {
        m_runtime->Update(frame.deltaSeconds);
        frame.SubmitRenderSnapShot(m_runtime->Snapshot());
    } else {
        TransformSystem(activeScene.registry);  // 只计算场景中物体的父子相对位置
        frame.SubmitRenderSnapShot(BuildRenderSnapshot(activeScene));
    }
}

void EditorLayer::OnImGuiRender(const FrameContext& frame)
{
    Scene& activeScene = ActiveScene();
    // 投影宽高比取实际像素尺寸；UI 鼠标坐标仍使用 ImGui 的逻辑坐标。
    // Renderer 给出实际目标尺寸和本帧借用句柄；请求尺寸在下一帧入口生效。
    m_editor.SetRuntimeEntities(m_runtime ? std::optional(m_runtime->VisibleEntities()) : std::nullopt);
    m_editor.Draw(activeScene, m_runtimeScene.has_value(), frame.uiViewportImage, frame.renderCamera);
}

Scene& EditorLayer::ActiveScene()
{
    return m_runtimeScene ? *m_runtimeScene : *m_editScene;
}

void EditorLayer::OnFrameEnd()
{
    m_editor.UpdateLayoutPersistence();
    // ApplyPending 可能销毁运行场景，先释放借用它的运行时与实体视图。
    if (m_editor.StopRequested()) {
        m_editor.SetRuntimeEntities(std::nullopt);
        m_runtime.reset();
    }
    m_editor.ApplyPending(*m_editScene, m_runtimeScene);
    if (m_runtimeScene && !m_runtime) {
        try {
            m_runtime = CreateSceneRuntime(*m_runtimeScene);
        }
        catch (const std::exception& e) { 
            m_runtimeScene.reset(); 
            m_editor.SetStatus(e.what()); 
        }
    }
}

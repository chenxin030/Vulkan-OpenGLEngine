#pragma once

#include "platform/InputState.h"
#include "editor/ViewportInteraction.h"
#include "editor/EditorLayout.h"
#include "editor/AssetBrowser.h"
#include "editor/SceneDocument.h"
#include <utility>
#include "scene/Entity.h"
#include "scene/Scene.h"

#include <optional>
#include <string>

class Editor
{
public:
    void Draw(Scene& scene, bool playing, const ViewportImage& image, const Camera& renderCamera);
    void InitializeLayout();
    bool ConsumeDialogPause() { return std::exchange(m_pauseAfterDialog, false); }
    bool ConsumeSceneChange() { return std::exchange(m_sceneChanged, false); }
    void SaveLayout() noexcept { m_layout.Save(); }
    void UpdateLayoutPersistence() noexcept { m_layout.SaveIfRequested(); }
    ViewportRequest Viewport() const { return m_viewportRequest; }
    InputState CameraInput(InputState input);
    // 原生失焦事件在最小化暂停更新时仍会到达；这里只取消状态，不调用 ImGui。
    void CancelViewportInput() noexcept
    {
        m_cameraGesture = m_cameraAdmitted = m_viewportFocused = m_viewportHovered = false;
    }
    void ApplyPending(Scene& editScene, std::optional<Scene>& runtimeScene);

    EditorInputCapture InputCapture() const;
    bool HasSelection() const;
    Entity SelectedEntity() const;
    void Select(Entity entity);
    Scene& ActiveScene(Scene& editScene, std::optional<Scene>& runtimeScene);

private:
    enum class FileCommand { None, New, Open, Save, SaveAs };
    void DrawMenu(bool playing);
    void DrawDocumentDialogs();
    void BeforeFileDialog();
    std::optional<std::filesystem::path> ChooseSavePath();
    void RefreshAssets();
    void SceneChanged(const Scene& scene);

    EditorLayout m_layout;
    ProjectAssets m_assets;
    AssetBrowser m_assetBrowser;
    SceneDocument m_document;
    FileCommand m_fileCommand = FileCommand::None;
    bool m_requestCreateEmpty = false, m_requestRefresh = false;
    bool m_requestPickImport = false, m_openImportPopup = false;
    bool m_pauseAfterDialog = false, m_nativeInputGuard = false, m_sceneChanged = false;
    std::filesystem::path m_importSource;
    char m_importName[256]{};
    ViewportRect m_previousViewportRect;
    Entity m_selectedEntity{};
    bool m_hasSelection = false;
    bool m_requestCreate = false;
    bool m_requestDelete = false;
    bool m_requestPlay = false;
    bool m_requestStop = false;
    bool m_requestImport = false;
    bool m_requestEnvironment = false, m_requestClearEnvironment = false, m_requestIblDemo = false;
    char m_environmentPath[1024] = "assets/studio.hdr";
    std::optional<std::filesystem::path> m_requestInstance; // 项目模型路径，使用时才加载到当前 Scene。
    ViewportRequest m_viewportRequest;
    ViewportRect m_viewportRect;
    bool m_viewportFocused = false, m_cameraGesture = false, m_cameraAdmitted = false;
    bool m_viewportHovered = false;
    bool m_gizmoCaptured = false;
    int m_gizmoOperation = 0; // 0 Move / 1 Rotate / 2 Scale；避免公共头文件依赖 ImGuizmo。
    bool m_gizmoLocal = true;
    std::string m_status;
};

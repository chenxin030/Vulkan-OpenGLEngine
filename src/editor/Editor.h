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
#include <span>
#include <functional>
#include <string>

class Editor
{
public:
    void Draw(Scene& scene, bool playing, const ViewportImage& image, const Camera& renderCamera);
    void InitializeLayout();
    bool ConsumeDialogPause() { return std::exchange(m_pauseAfterDialog, false); }
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
    bool StopRequested() const { return m_requestStop; }
    void RequestInitialScene(const std::filesystem::path& path) { m_document.RequestOpen(path); }
    void SetStatus(std::string text) { m_status = std::move(text); }
    void SetRuntimeEntities(std::optional<std::span<const Entity>> entities) { m_runtimeEntities = entities; }
    void ForEachDisplayed(const Scene& scene, const std::function<void(Entity)>& visit) const {
        if (m_runtimeEntities) { for (auto e : *m_runtimeEntities) visit(e); }
        else scene.registry.ForEachEntity(visit);
    }

    EditorInputCapture InputCapture() const;
    bool HasSelection() const;
    Entity SelectedEntity() const;
    void Select(Entity entity);

private:
    enum class FileCommand { None, New, Open, Save, SaveAs };
    void DrawMenu(Scene& scene, bool playing);
    void DrawDocumentDialogs();
    void BeforeFileDialog();
    std::optional<std::filesystem::path> ChooseSavePath();
    void RefreshAssets();
    void SceneChanged(const Scene& scene);

    std::optional<std::span<const Entity>> m_runtimeEntities;
    EditorLayout m_layout;
    ProjectAssets m_assets;
    AssetBrowser m_assetBrowser;
    SceneDocument m_document;
    FileCommand m_fileCommand = FileCommand::None;
    bool m_requestCreateEmpty = false, m_requestRefresh = false;
    bool m_requestPickImport = false, m_openImportPopup = false;
    bool m_pauseAfterDialog = false, m_nativeInputGuard = false;
    std::filesystem::path m_importSource;
    char m_importName[256]{};
    ViewportRect m_previousViewportRect;
    Entity m_selectedEntity{};
    bool m_requestDelete = false;
    bool m_requestPlay = false;
    bool m_requestStop = false;
    bool m_requestImport = false;
    bool m_requestEnvironment = false, m_requestClearEnvironment = false;
    char m_environmentPath[1024] = "assets/environments/studio.hdr";
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

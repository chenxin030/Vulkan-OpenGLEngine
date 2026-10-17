#pragma once

#include "platform/InputState.h"
#include "editor/ViewportInteraction.h"
#include "scene/Entity.h"
#include "scene/Scene.h"

#include <optional>
#include <string>

class Editor
{
public:
    void Draw(Scene& scene, bool playing, const ViewportImage& image, const Camera& renderCamera);
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
    Entity m_selectedEntity{};
    bool m_hasSelection = false;
    bool m_requestCreate = false;
    bool m_requestDelete = false;
    bool m_requestPlay = false;
    bool m_requestStop = false;
    bool m_requestSave = false;
    bool m_requestLoad = false;
    bool m_requestImport = false;
    bool m_requestEnvironment = false, m_requestClearEnvironment = false, m_requestIblDemo = false;
    char m_environmentPath[1024] = "assets/studio.hdr";
    std::optional<std::string> m_requestInstance; // Add Instance 与拖放共用资产 ID。
    ViewportRequest m_viewportRequest;
    ViewportRect m_viewportRect;
    bool m_viewportFocused = false, m_cameraGesture = false, m_cameraAdmitted = false;
    bool m_viewportHovered = false;
    bool m_gizmoCaptured = false;
    int m_gizmoOperation = 0; // 0 Move / 1 Rotate / 2 Scale；避免公共头文件依赖 ImGuizmo。
    bool m_gizmoLocal = true;
    char m_scenePath[260] = "assets/phase8-ibl.scene.json";
    char m_modelPath[260] = "tests/data/model-import-test.gltf";
    std::string m_status;
};

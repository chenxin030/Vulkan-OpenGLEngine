#pragma once

#include "platform/InputState.h"
#include "scene/Entity.h"
#include "scene/Scene.h"

#include <optional>
#include <string>

class Editor
{
public:
    void Draw(Scene& scene, bool playing, glm::vec2 framebufferSize);
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
    bool m_viewportHovered = false;
    bool m_gizmoCaptured = false;
    int m_gizmoOperation = 0; // 0 Move / 1 Rotate / 2 Scale；避免公共头文件依赖 ImGuizmo。
    bool m_gizmoLocal = true;
    char m_scenePath[260] = "assets/phase8-ibl.scene.json";
    char m_modelPath[260] = "tests/data/model-import-test.gltf";
    std::string m_status;
};

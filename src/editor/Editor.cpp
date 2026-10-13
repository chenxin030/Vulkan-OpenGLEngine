#include "editor/Editor.h"

#include "editor/EditorGeometry.h"
#include "scene/Components.h"
#include "scene/Scene.h"
#include "scene/SceneSerializer.h"
#include "scene/ModelImporter.h"
#include "scene/RenderSnapshot.h"

#include <imgui.h>
#include <ImGuizmo.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <vector>
#include <cstring>

namespace {

std::filesystem::path ResolveEditorPath(const char* text)
{
    const std::string utf8(text);
    const std::u8string encoded(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size());
    const std::filesystem::path input(encoded);
    if (input.is_absolute()) return input.lexically_normal();

    const std::filesystem::path fromWorkingDirectory =
        (std::filesystem::current_path() / input).lexically_normal();
    if (std::filesystem::exists(fromWorkingDirectory)) return fromWorkingDirectory;
    return (std::filesystem::path(PROJECT_ROOT) / input).lexically_normal();
}

std::optional<std::string> DrawViewport(Editor& editor, Scene& scene, const ImVec2& displaySize,
    bool playing, bool& hovered, float aspect, int& operation, bool& local, bool& gizmoCaptured)
{
    const float left = 360.0f;
    const float right = std::max(left + 160.0f, displaySize.x - 270.0f);
    const float top = 8.0f;
    const float bottom = std::max(top + 100.0f, displaySize.y - 8.0f);

    ImGui::SetNextWindowPos(ImVec2(left, top), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(right - left, bottom - top), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::Begin("Scene View", nullptr,
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavInputs |
        ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoSavedSettings);

    hovered = ImGui::IsWindowHovered();
    std::optional<std::string> requested;
    // Dummy 只登记拖放目标，不抢 ActiveId；InvisibleButton 会阻止 ImGuizmo 开始拖动。
    ImGui::Dummy(ImGui::GetContentRegionAvail());
    if (!playing && ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MODEL_ASSET")) {
            const char* id = static_cast<const char*>(payload->Data);
            if (payload->DataSize > 1 && payload->DataSize <= 129 &&
                id[payload->DataSize - 1] == '\0' &&
                std::strlen(id) == static_cast<size_t>(payload->DataSize - 1)) requested = id;
        }
        ImGui::EndDragDropTarget();
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetWindowPos();
    const ImVec2 max(min.x + ImGui::GetWindowSize().x, min.y + ImGui::GetWindowSize().y);
    drawList->AddRect(min, max, IM_COL32(115, 145, 180, 170));
    drawList->AddText(ImVec2(min.x + 12.0f, min.y + 10.0f), IM_COL32(225, 230, 240, 230),
        "Viewport - scene is rendered to the application surface");

    gizmoCaptured = false;
    const Entity selected = editor.SelectedEntity();
    Transform* transform = scene.registry.TryGet<Transform>(selected);
    // 首版只编辑顶层实体。节点保留导入矩阵；子节点可在 Inspector 跳转到整体根。
    const bool editable = editor.HasSelection() && transform && !scene.registry.Has<Parent>(selected) &&
        EditorGeometry::Decompose(transform->local).has_value();
    const bool dragging = ImGui::GetDragDropPayload() != nullptr;
    const bool cameraLook = ImGui::IsMouseDown(ImGuiMouseButton_Right);
    // 一个固定 widget ID：Enable(false) 在换场景/Play 时也能取消上一帧拖动。
    ImGuizmo::PushID("Scene transform");
    ImGuizmo::Enable(!playing && editable && !dragging && !cameraLook);
    if (editable && !playing && aspect > 0 && displaySize.x > 0 && displaySize.y > 0) {
        const glm::mat4 view = scene.camera.ViewMatrix();
        const glm::mat4 projection = EditorGeometry::Projection(scene.camera, aspect);
        glm::mat4 candidate = transform->local;
        ImGuizmo::SetDrawlist(drawList);
        // 矩阵对应整窗画面，SetRect 必须也是整窗；绘制另按透明交互区裁剪。
        ImGuizmo::SetRect(0, 0, displaySize.x, displaySize.y);
        ImGuizmo::SetOrthographic(scene.camera.projection == CameraProjection::Orthographic);
        const ImGuizmo::OPERATION ops[] = { ImGuizmo::TRANSLATE, ImGuizmo::ROTATE, ImGuizmo::SCALE };
        drawList->PushClipRect(min, max, true);
        if (ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(projection), ops[operation],
            local || operation == 2 ? ImGuizmo::LOCAL : ImGuizmo::WORLD, glm::value_ptr(candidate))) {
            if (EditorGeometry::Decompose(candidate)) transform->local = candidate;
        }
        drawList->PopClipRect();
        // 使用当前操作的即时命中，不能用 IsOver() 的上一帧缓存判定本次选择。
        gizmoCaptured = !cameraLook && !dragging &&
            (ImGuizmo::IsUsing() || (hovered && ImGuizmo::IsOver(ops[operation])));
    }
    ImGuizmo::PopID();
    if (hovered && !dragging && !cameraLook && !gizmoCaptured &&
        !ImGui::IsAnyItemActive() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        editor.Select(EditorGeometry::Pick(scene, { mouse.x, mouse.y },
            { displaySize.x, displaySize.y }, aspect));
    }

    ImGui::End();

    // 在视口上下文中保持 scene 参数，这样它的状态就可以保持可见
    // 同时不会在这个共享编辑器中引入特定渲染器的依赖。
    ImGui::SetNextWindowPos(ImVec2(left + 12.0f, top + 34.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.55f);
    ImGui::Begin("Viewport Status", nullptr,
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings);
    ImGui::Text("Entities: %zu", scene.registry.EntityCount());
    std::size_t drawItemCount = 0, skippedTransforms = 0;
    scene.registry.ForEachEntity([&scene, &drawItemCount, &skippedTransforms](Entity entity) {
        if (scene.registry.TryGet<Transform>(entity) != nullptr &&
            (scene.registry.TryGet<MeshRenderer>(entity) != nullptr ||
             scene.registry.TryGet<SpriteRenderer>(entity) != nullptr)) {
            if (!IsRenderableTransform(scene.registry.TryGet<Transform>(entity)->world)) {
                ++skippedTransforms;
                return;
            }
            ++drawItemCount;
        }
    });
    ImGui::Text("Draw items: %zu", drawItemCount);
    if (skippedTransforms) ImGui::Text("Skipped invalid transforms: %zu", skippedTransforms);
    ImGui::Text("Right mouse + WASD: move camera");
    ImGui::Text("Drag a model here to add it at the origin");
    ImGui::BeginDisabled(playing || ImGuizmo::IsUsingAny());
    if (ImGui::RadioButton("Move", operation == 0)) operation = 0;
    ImGui::SameLine();
    if (ImGui::RadioButton("Rotate", operation == 1)) operation = 1;
    ImGui::SameLine();
    if (ImGui::RadioButton("Scale", operation == 2)) operation = 2;
    if (operation == 2) ImGui::TextUnformatted("Scale uses local axes");
    else ImGui::Checkbox("Local axes", &local);
    ImGui::EndDisabled();
    ImGui::End();
    return requested;
}

void DrawHierarchy(Editor& editor, const Scene& scene)
{
    const float availableHeight = std::max(80.0f, ImGui::GetIO().DisplaySize.y - 410.0f);
    ImGui::SetNextWindowPos(ImVec2(12.0f, 210.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(240.0f, availableHeight), ImGuiCond_Always);
    ImGui::Begin("Scene Hierarchy");

    scene.registry.ForEachEntity([&editor, &scene](Entity entity) {
        ImGui::PushID(static_cast<int>(entity.index));
        ImGui::PushID(static_cast<int>(entity.generation));

        char label[160]{};
        const Name* name = scene.registry.TryGet<Name>(entity);
        std::snprintf(label, sizeof(label), "%s%u:%u %s",
            scene.registry.TryGet<Parent>(entity) ? "  " : "",
            entity.index, entity.generation, name ? name->value.c_str() : "Entity");
        // 比较完整 Entity（index 和 generation），判断这一行是否是当前选中的 Entity 。
        const bool selected = editor.HasSelection() && editor.SelectedEntity() == entity;
        // selected 控制这一行的选中高亮；Selectable 返回 true 表示用户点击了这一行。
        if (ImGui::Selectable(label, selected)) {
            // 将点击的 Entity 记录到 Editor，供 Inspector 显示以及 Delete 等操作使用。
            editor.Select(entity);
        }

        ImGui::PopID();
        ImGui::PopID();
    });

    ImGui::End();
}

std::optional<std::string> DrawModelAssets(const Scene& scene, bool playing)
{
    const float top = std::max(300.0f, ImGui::GetIO().DisplaySize.y - 190.0f);
    ImGui::SetNextWindowPos(ImVec2(12.0f, top), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(330.0f, std::max(100.0f,
        ImGui::GetIO().DisplaySize.y - top - 8.0f)), ImGuiCond_Always);
    ImGui::Begin("Model Assets");
    std::optional<std::string> requested;
    if (scene.modelAssets.empty()) ImGui::TextWrapped("Import a model to add it here.");
    for (uint32_t i = 0; i < scene.modelAssets.size(); ++i) {
        ImGui::PushID(scene.modelAssets[i].id.c_str());
        ImGui::BeginDisabled(playing);
        if (ImGui::Button("Add Instance")) requested = scene.modelAssets[i].id;
        ImGui::EndDisabled();
        ImGui::SameLine();
        const std::string& path = scene.modelAssets[i].sourcePath;
        const size_t separator = path.find_last_of("/\\");
        ImGui::Selectable(path.c_str() + (separator == std::string::npos ? 0 : separator + 1));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", path.c_str());
        if (!playing && ImGui::BeginDragDropSource()) {
            const std::string& id = scene.modelAssets[i].id;
            ImGui::SetDragDropPayload("MODEL_ASSET", id.c_str(), id.size() + 1);
            ImGui::TextUnformatted("Add model instance at the origin");
            ImGui::EndDragDropSource();
        }
        ImGui::PopID();
    }
    ImGui::End();
    return requested;
}

void DrawEnvironment(Scene& scene, bool playing, char* path, size_t pathSize,
    bool& load, bool& clear, bool& demo)
{
    ImGui::SeparatorText("Environment");
    ImGui::BeginDisabled(playing);
    ImGui::InputText("HDR", path, pathSize);
    if (ImGui::Button("Load / Reload HDR")) load = true;
    ImGui::SameLine();
    if (ImGui::Button("Clear")) clear = true;
    if (ImGui::Button("IBL demo")) demo = true;
    ImGui::BeginDisabled(!scene.environmentData);
    ImGui::Checkbox("IBL", &scene.environment.enabled);
    ImGui::Checkbox("Skybox", &scene.environment.background);
    ImGui::DragFloat("Intensity", &scene.environment.intensity, 0.02f, 0.0f, 10000.0f,
        "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::EndDisabled();
    ImGui::Checkbox("Direct light", &scene.directLighting);
    ImGui::Checkbox("Diffuse IBL", &scene.diffuseIbl);
    ImGui::Checkbox("Specular IBL", &scene.specularIbl);
    ImGui::SeparatorText("Surface normals");
    ImGui::Checkbox("Normal mapping", &scene.normalMapping);
    ImGui::Combo("View", &scene.normalDebug, "Lighting\0Base normal\0Mapped normal\0");
    ImGui::EndDisabled();
}

void DrawInspector(Editor& editor, Scene& scene, bool playing, char* path, size_t pathSize,
    bool& load, bool& clear, bool& demo)
{
    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(std::max(12.0f, displaySize.x - 258.0f), 12.0f),
        ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(246.0f, std::max(180.0f, displaySize.y - 24.0f)),
        ImGuiCond_Always);
    ImGui::Begin("Inspector");

    const Entity selected = editor.SelectedEntity();
    if (!editor.HasSelection() || !scene.registry.IsAlive(selected)) {
        ImGui::TextUnformatted("Select an entity.");
        DrawEnvironment(scene, playing, path, pathSize, load, clear, demo);
        ImGui::End();
        return;
    }

    ImGui::Text("Entity %u:%u", selected.index, selected.generation);

    if (Transform* transform = scene.registry.TryGet<Transform>(selected)) {
        ImGui::SeparatorText("Transform");
        if (scene.registry.Has<Parent>(selected)) {
            ImGui::TextWrapped("Child entity: edit the root to transform the whole model.");
            if (ImGui::Button("Select root")) editor.Select(EditorGeometry::RootEntity(scene, selected));
        } else if (auto value = EditorGeometry::Decompose(transform->local)) {
            // 不操作时完全不回写；点击 Position 不再抹去 X/Y 旋转和 Z 缩放。
            ImGui::BeginDisabled(playing || ImGuizmo::IsUsingAny());
            const bool positionChanged = ImGui::DragFloat3("Position", &value->position.x, 0.05f);
            const bool rotationChanged = ImGui::DragFloat3("Rotation", &value->rotationDegrees.x, 0.5f);
            const bool scaleChanged = ImGui::DragFloat3("Scale XYZ", &value->scale.x, 0.01f, 0.001f, 1000.0f,
                "%.3f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::EndDisabled();
            ImGui::TextWrapped("XYZ axes; rotation in degrees");
            if (!playing && (positionChanged || rotationChanged || scaleChanged)) {
                glm::mat4 candidate = transform->local;
                // 单独改位置不必经过欧拉角；单独缩放只改变列长，保留原旋转基底。
                if (rotationChanged) candidate = EditorGeometry::Compose(*value);
                else if (scaleChanged) {
                    for (int axis = 0; axis < 3; ++axis)
                        candidate[axis] *= value->scale[axis] / glm::length(glm::vec3(candidate[axis]));
                }
                candidate[3] = glm::vec4(value->position, 1.0f);
                if (EditorGeometry::Decompose(candidate)) transform->local = candidate;
            }
        } else {
            ImGui::TextWrapped("Read-only: shear, reflection, degenerate or unsupported TRS. Original matrix preserved.");
        }
    }

    if (const MeshRenderer* mesh = scene.registry.TryGet<MeshRenderer>(selected)) {
        ImGui::SeparatorText("Mesh Renderer");
        ImGui::Text("Mesh index: %u", mesh->meshIndex);
    }

    if (SpriteRenderer* sprite = scene.registry.TryGet<SpriteRenderer>(selected)) {
        ImGui::SeparatorText("Sprite Renderer");
        if (!playing) ImGui::ColorEdit3("Tint", &sprite->tint.x);
        else ImGui::Text("Tint: %.2f, %.2f, %.2f", sprite->tint.x, sprite->tint.y, sprite->tint.z);
    }

    if (const Spin* spin = scene.registry.TryGet<Spin>(selected)) {
        ImGui::SeparatorText("Spin");
        ImGui::Text("Speed: %.2f rad/s", spin->speed);
    }

    if (const Bob* bob = scene.registry.TryGet<Bob>(selected)) {
        ImGui::SeparatorText("Bob");
        ImGui::Text("Amplitude: %.2f", bob->amplitude);
        ImGui::Text("Speed: %.2f rad/s", bob->speed);
    }

    DrawEnvironment(scene, playing, path, pathSize, load, clear, demo);
    ImGui::End();
}

} // namespace

void Editor::Draw(Scene& scene, bool playing, glm::vec2 framebufferSize)
{
    ImGuizmo::BeginFrame();
    if (!scene.registry.IsAlive(m_selectedEntity)) {
        m_hasSelection = false;
    }

    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(330.0f, 184.0f), ImGuiCond_Always);
    ImGui::Begin("Scene");
    ImGui::InputText("File", m_scenePath, sizeof(m_scenePath));
    ImGui::InputText("Model", m_modelPath, sizeof(m_modelPath));
    ImGui::BeginDisabled(playing);
    if (ImGui::Button("New Sprite")) m_requestCreate = true;
    ImGui::SameLine();
    if (ImGui::Button("Delete")) m_requestDelete = true;
    ImGui::SameLine();
    if (ImGui::Button("Save")) m_requestSave = true;
    ImGui::SameLine();
    if (ImGui::Button("Load")) m_requestLoad = true;
    if (ImGui::Button("Import glTF / GLB")) m_requestImport = true;
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (playing) {
        if (ImGui::Button("Stop")) m_requestStop = true;
    } else if (ImGui::Button("Play")) m_requestPlay = true;
    if (!m_status.empty()) ImGui::TextWrapped("%s", m_status.c_str());
    ImGui::End();
    const float aspect = framebufferSize.y > 0 ? framebufferSize.x / framebufferSize.y : 0;
    if (const auto requested = DrawViewport(*this, scene, displaySize, playing, m_viewportHovered,
        aspect, m_gizmoOperation, m_gizmoLocal, m_gizmoCaptured))
        m_requestInstance = requested;
    DrawHierarchy(*this, scene);
    if (const auto requested = DrawModelAssets(scene, playing)) m_requestInstance = requested;
    DrawInspector(*this, scene, playing, m_environmentPath, sizeof(m_environmentPath),
        m_requestEnvironment, m_requestClearEnvironment, m_requestIblDemo);
}

void Editor::ApplyPending(Scene& editScene, std::optional<Scene>& runtimeScene)
{
    try {
        if (m_requestPlay && !runtimeScene) runtimeScene.emplace(editScene);
        if (m_requestStop) runtimeScene.reset();
        if (m_requestCreate && !runtimeScene) {
            Entity entity = editScene.registry.Create();
            Transform transform;
            transform.local = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f));
            transform.world = transform.local;
            editScene.registry.Add<Transform>(entity, transform);
            editScene.registry.Add<SpriteRenderer>(entity);
            Select(entity);
        }
        if (m_requestDelete && !runtimeScene && m_hasSelection) {
            editScene.DestroyEntitySubtree(m_selectedEntity);
            m_hasSelection = false;
        }
        if (m_requestSave && !runtimeScene) SceneSerializer::Save(editScene, ResolveEditorPath(m_scenePath));
        if (m_requestLoad && !runtimeScene) {
            // 直接造一个新的 Scene
            Scene loaded = SceneSerializer::Load(ResolveEditorPath(m_scenePath));
            editScene = std::move(loaded);
            m_hasSelection = false;
            m_requestInstance.reset(); // 资产 ID 属于原 Scene，换场景后清除旧请求。
        }
        if (m_requestIblDemo && !runtimeScene) {
            Scene loaded = SceneSerializer::Load(ResolveEditorPath("assets/phase8-ibl.scene.json"));
            editScene = std::move(loaded);
            m_hasSelection = false;
            m_requestInstance.reset();
        }
        if (m_requestEnvironment && !runtimeScene) {
            const auto source = ResolveEditorPath(m_environmentPath);
            // 先读/烘焙完整候选数据，成功后才替换。失败不污染现有 Scene。
            const auto loaded = LoadEnvironment(source);
            const auto encoded = source.generic_u8string();
            editScene.environment.sourcePath.assign(reinterpret_cast<const char*>(encoded.data()), encoded.size());
            editScene.environmentData = loaded.data;
            editScene.environment.enabled = true;
            editScene.environment.background = true;
        }
        if (m_requestClearEnvironment && !runtimeScene) {
            editScene.environment = {};
            editScene.environmentData.reset();
        }
        if ((m_requestLoad || m_requestIblDemo) && !runtimeScene && !editScene.environment.sourcePath.empty())
            std::snprintf(m_environmentPath, sizeof(m_environmentPath), "%s", editScene.environment.sourcePath.c_str());
        if (m_requestImport && !runtimeScene) {
            // 原基础上新增 Import 内容
            Scene candidate(editScene);
            const std::vector<Entity> roots = ModelImporter::Import(candidate, ResolveEditorPath(m_modelPath));
            editScene = std::move(candidate);
            if (!roots.empty()) Select(roots.front());
        }
        if (m_requestInstance && !runtimeScene) {
            Scene candidate(editScene);
            const std::vector<Entity> roots = ModelImporter::InstantiateAsset(candidate,
                candidate.FindModelAssetIndex(*m_requestInstance));
            editScene = std::move(candidate);
            Select(roots.front());
        }
        if (m_requestPlay || m_requestStop || m_requestCreate || m_requestDelete || m_requestSave || m_requestLoad || m_requestImport || m_requestInstance ||
            m_requestEnvironment || m_requestClearEnvironment || m_requestIblDemo)
            m_status = "Scene operation completed";
    } catch (const std::exception& error) {
        m_status = error.what();
    }
    m_requestCreate = m_requestDelete = m_requestPlay = m_requestStop = false;
    m_requestSave = m_requestLoad = false;
    m_requestImport = false;
    m_requestEnvironment = m_requestClearEnvironment = m_requestIblDemo = false;
    m_requestInstance.reset();
}

Scene& Editor::ActiveScene(Scene& editScene, std::optional<Scene>& runtimeScene)
{
    // 放在 Editor 中，是为了把“编辑 / Play 场景切换”的规则与 ApplyPending 放在一起：
    // Play 时 ApplyPending 复制 editScene 到 runtimeScene，Stop 时销毁运行副本。
    // 主循环通过这里取得当前场景，供相机、系统更新、渲染和编辑器 UI 使用。
    // 两份 Scene 实际由 EditorLayer 持有；本方法返回引用，不复制场景，也不改变播放状态。
    // 这里没有读取 Editor 成员，因此它并非必须是成员方法，也可以是静态或自由函数。
    // 当前只是按职责归组；以后若 play 生命周期变复杂，可交给专门的应用/ play 控制器。
    return runtimeScene ? *runtimeScene : editScene;
}

EditorInputCapture Editor::InputCapture() const
{
    if (ImGui::GetCurrentContext() == nullptr) {
        return {};
    }

    const ImGuiIO& io = ImGui::GetIO();
    if (m_gizmoCaptured || ImGuizmo::IsUsingAny()) return { true, true };
    const bool viewportInput = m_viewportHovered && !ImGui::IsAnyItemActive() &&
        ImGui::GetDragDropPayload() == nullptr;
    // 空闲 UI 的导航焦点也会捕获键盘；在未编辑或拖放的视口中让 WASD 通行。
    return EditorInputCapture{ io.WantCaptureKeyboard && !viewportInput,
        io.WantCaptureMouse && !viewportInput };
}

bool Editor::HasSelection() const
{
    return m_hasSelection;
}

Entity Editor::SelectedEntity() const
{
    return m_selectedEntity;
}

void Editor::Select(Entity entity)
{
    m_selectedEntity = entity;
    m_hasSelection = entity.IsValid();
}

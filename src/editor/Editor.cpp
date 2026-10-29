#include "editor/Editor.h"

#include "editor/EditorGeometry.h"
#include "scene/Components.h"
#include "scene/Scene.h"
#include "scene/SceneSerializer.h"
#include "scene/ModelImporter.h"
#include "platform/FileDialogs.h"

#include <imgui.h>
#include <ImGuizmo.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <cstring>

namespace {

void CancelSceneGizmo()
{
    ImGuizmo::PushID("Scene transform");
    ImGuizmo::Enable(false);
    ImGuizmo::PopID();
}

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

std::optional<ProjectAssetEntry> DrawViewport(Editor& editor, Scene& scene, bool playing,
    bool& hovered, bool& focused, bool& cameraGesture, ViewportRect& rect,
    ViewportRequest& request, const ViewportImage& image, const Camera& renderCamera,
    int& operation, bool& local, bool& gizmoCaptured, ViewportRect& previousRect, bool& modified)
{
    hovered = false; focused = false; gizmoCaptured = false;
    if (!ImGui::Begin("Scene View")) {
        request.visible = false; rect = {}; cameraGesture = false;
        CancelSceneGizmo();
        ImGui::End(); return {};
    }
    ImGui::Text("Target: %u x %u pixels", image.extent.width, image.extent.height);
    ImGui::TextUnformatted("Right mouse + WASD: camera | Drag model: instance");
    ImGui::BeginDisabled(playing || ImGuizmo::IsUsingAny());
    if (ImGui::RadioButton("Move", operation == 0)) operation = 0;
    ImGui::SameLine();
    if (ImGui::RadioButton("Rotate", operation == 1)) operation = 1;
    ImGui::SameLine();
    if (ImGui::RadioButton("Scale", operation == 2)) operation = 2;
    if (operation == 2) ImGui::TextUnformatted("Scale uses local axes");
    else ImGui::Checkbox("Local axes", &local);
    ImGui::EndDisabled();
    if (!image.status.empty()) ImGui::TextWrapped("%s", image.status.c_str());
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const ImVec2 framebufferScale = ImGui::GetIO().DisplayFramebufferScale;
    request.extent = ViewportPixels({available.x, available.y}, {framebufferScale.x, framebufferScale.y});
    request.visible = request.extent.Valid();
    rect = FitViewport({origin.x, origin.y}, {available.x, available.y}, image.extent,
        {framebufferScale.x, framebufferScale.y});
    if (!request.visible || !image.Valid() || rect.size.x <= 0 || rect.size.y <= 0) {
        rect = {}; cameraGesture = false;
        CancelSceneGizmo();
        ImGui::End(); return {};
    }
    // Dock/resize 改变坐标时停止旧手势；不能用旧 Image 上开始的拖动修改新布局。
    const bool geometryChanged = rect.min != previousRect.min || rect.size != previousRect.size;
    previousRect = rect;
    if (geometryChanged) {
        cameraGesture = false;
        CancelSceneGizmo();
    }
    ImGui::SetCursorScreenPos(ImVec2(rect.min.x, rect.min.y));
    // Image 和 Dummy 一样不抢 ActiveId，因此可同时作为拖放目标及操纵轴背景。
    ImGui::Image(static_cast<ImTextureID>(image.textureId), ImVec2(rect.size.x, rect.size.y),
        ImVec2(image.uv0.x, image.uv0.y), ImVec2(image.uv1.x, image.uv1.y));
    hovered = ImGui::IsItemHovered();
    if (hovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
        ImGui::IsMouseClicked(ImGuiMouseButton_Right))) ImGui::SetWindowFocus();
    focused = ImGui::IsWindowFocused();
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        cameraGesture = hovered && focused && !ImGui::IsAnyItemActive() &&
            ImGui::GetDragDropPayload() == nullptr;
    if (!hovered || !focused || !ImGui::IsMouseDown(ImGuiMouseButton_Right)) cameraGesture = false;
    const float aspect = image.extent.Aspect();
    std::optional<ProjectAssetEntry> requested;
    if (!playing && ImGui::BeginDragDropTarget()) {
        for (const auto kind : {ProjectAssetKind::Model, ProjectAssetKind::Scene}) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
                kind == ProjectAssetKind::Model ? "PROJECT_MODEL" : "PROJECT_SCENE")) {
                const char* path = static_cast<const char*>(payload->Data);
                if (payload->DataSize > 1 && payload->DataSize <= 32768 &&
                    path[payload->DataSize - 1] == '\0' &&
                    std::strlen(path) == static_cast<size_t>(payload->DataSize - 1))
                    requested = ProjectAssetEntry{AssetPathFromUtf8(path), kind};
            }
        }
        ImGui::EndDragDropTarget();
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 min(rect.min.x, rect.min.y);
    const ImVec2 max(rect.min.x + rect.size.x, rect.min.y + rect.size.y);
    drawList->AddRect(min, max, IM_COL32(115, 145, 180, 170));

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
    // 被其他窗口遮挡时不能开始操纵轴拖动；已开始的拖动允许离开 Image 继续。
    ImGuizmo::Enable(!playing && editable && !dragging && !cameraLook && focused &&
        (hovered || ImGuizmo::IsUsing()));
    if (editable && !playing && aspect > 0 && rect.size.x > 0 && rect.size.y > 0) {
        const glm::mat4 view = renderCamera.ViewMatrix();
        const glm::mat4 projection = EditorGeometry::Projection(renderCamera, aspect);
        glm::mat4 candidate = transform->local;
        ImGuizmo::SetDrawlist(drawList);
        // 与 Image 使用同一逻辑矩形；Vulkan 投影翻转只留在 GPU 后端。
        ImGuizmo::SetRect(rect.min.x, rect.min.y, rect.size.x, rect.size.y);
        ImGuizmo::SetOrthographic(renderCamera.projection == CameraProjection::Orthographic);
        const ImGuizmo::OPERATION ops[] = { ImGuizmo::TRANSLATE, ImGuizmo::ROTATE, ImGuizmo::SCALE };
        drawList->PushClipRect(min, max, true);
        if (ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(projection), ops[operation],
            local || operation == 2 ? ImGuizmo::LOCAL : ImGuizmo::WORLD, glm::value_ptr(candidate))) {
            if (EditorGeometry::Decompose(candidate) && candidate != transform->local) {
                transform->local = candidate;
                modified = true;
            }
        }
        drawList->PopClipRect();
        // 使用当前操作的即时命中，不能用 IsOver() 的上一帧缓存判定本次选择。
        gizmoCaptured = !cameraLook && !dragging &&
            (ImGuizmo::IsUsing() || (hovered && ImGuizmo::IsOver(ops[operation])));
    }
    ImGuizmo::PopID();
    if (!playing && hovered && !dragging && !cameraLook && !gizmoCaptured &&
        !ImGui::IsAnyItemActive() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        editor.Select(EditorGeometry::Pick(scene, { mouse.x - rect.min.x, mouse.y - rect.min.y },
            rect.size, aspect, &renderCamera));
    }

    ImGui::End();

    return requested;
}

void DrawHierarchy(Editor& editor, const Scene& scene, bool playing,
    bool& createEmpty, bool& deleteEntity)
{
    if (!ImGui::Begin("Scene Hierarchy")) { ImGui::End(); return; }
    ImGui::BeginDisabled(playing);
    if (ImGui::Button("Create")) ImGui::OpenPopup("CreateEntity");
    if (ImGui::BeginPopup("CreateEntity")) {
        if (ImGui::MenuItem("Empty Entity")) createEmpty = true;
        ImGui::EndPopup();
    }
    ImGui::EndDisabled();

    static ImGuiTextFilter hierarchyFilter;
    hierarchyFilter.Draw("Filter");
    editor.ForEachDisplayed(scene, [&editor, &scene, playing, &deleteEntity](Entity entity) {
        ImGui::PushID(static_cast<int>(entity.index));
        ImGui::PushID(static_cast<int>(entity.generation));

        char label[160]{};
        const Name* name = scene.registry.TryGet<Name>(entity);
        if (!hierarchyFilter.PassFilter(name ? name->value.c_str() : "Entity")) {
            ImGui::PopID(); ImGui::PopID(); return;
        }
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

        if (ImGui::BeginPopupContextItem("EntityActions")) {
            editor.Select(entity);
            ImGui::BeginDisabled(playing);
            if (ImGui::MenuItem("Delete")) deleteEntity = true;
            ImGui::EndDisabled();
            ImGui::EndPopup();
        }
        ImGui::PopID();
        ImGui::PopID();
    });

    ImGui::End();
}

void DrawEnvironment(Scene& scene, bool playing, char* path, size_t pathSize,
    bool& load, bool& clear, bool& modified)
{
    ImGui::SeparatorText("Environment");
    ImGui::BeginDisabled(playing);
    ImGui::InputText("HDR", path, pathSize);
    if (ImGui::Button("Load / Reload HDR")) load = true;
    ImGui::SameLine();
    if (ImGui::Button("Clear")) clear = true;
    ImGui::BeginDisabled(!scene.environmentData);
    if (ImGui::Checkbox("IBL", &scene.environment.enabled)) modified = true;
    if (ImGui::Checkbox("Skybox", &scene.environment.background)) modified = true;
    if (ImGui::DragFloat("Intensity", &scene.environment.intensity, 0.02f, 0.0f, 10000.0f,
        "%.2f", ImGuiSliderFlags_AlwaysClamp)) modified = true;
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
    bool& load, bool& clear, bool& modified)
{
    ImGui::Begin("Inspector");

    const Entity selected = editor.SelectedEntity();
    if (!editor.HasSelection() || !scene.registry.IsAlive(selected)) {
        ImGui::TextUnformatted("Select an entity.");
        DrawEnvironment(scene, playing, path, pathSize, load, clear, modified);
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
                if (EditorGeometry::Decompose(candidate) && candidate != transform->local) {
                    transform->local = candidate; modified = true;
                }
            }
        } else {
            ImGui::TextWrapped("Read-only: shear, reflection, degenerate or unsupported TRS. Original matrix preserved.");
        }
    }

    if (const MeshRenderer* mesh = scene.registry.TryGet<MeshRenderer>(selected)) {
        ImGui::SeparatorText("Mesh Renderer");
        ImGui::Text("Mesh index: %u", mesh->meshIndex);
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

    DrawEnvironment(scene, playing, path, pathSize, load, clear, modified);
    ImGui::End();
}

} // namespace

void Editor::InitializeLayout()
{
    m_layout.Initialize(std::filesystem::path(u8"" EDITOR_LAYOUT_PATH));
    m_assets.Initialize(std::filesystem::path(u8"" ASSET_DIR));
}

void Editor::DrawMenu(Scene& scene, bool playing)
{
    if (!ImGui::BeginMainMenuBar()) return;
    if (ImGui::BeginMenu("File")) {
        ImGui::BeginDisabled(playing || m_document.HasPending() || !m_importSource.empty());
        if (ImGui::MenuItem("New Scene")) m_fileCommand = FileCommand::New;
        if (ImGui::MenuItem("Open Scene...")) m_fileCommand = FileCommand::Open;
        ImGui::Separator();
        if (ImGui::MenuItem("Save")) m_fileCommand = FileCommand::Save;
        if (ImGui::MenuItem("Save As...")) m_fileCommand = FileCommand::SaveAs;
        ImGui::EndDisabled();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Scene Systems")) {
        ImGui::BeginDisabled(playing || m_document.HasPending() || !m_importSource.empty());
        const auto contains = [&](SceneSystemKind kind) {
            return std::find(scene.systems.begin(), scene.systems.end(), kind) != scene.systems.end();
        };
        bool industrial = contains(SceneSystemKind::Industrial);
        bool animation = contains(SceneSystemKind::Animation);
        if (ImGui::MenuItem("Industrial", nullptr, industrial)) {
            if (industrial) std::erase(scene.systems, SceneSystemKind::Industrial);
            else {
                // 显式选择工业逻辑时，同时选择其依赖并维持执行顺序。
                std::erase(scene.systems, SceneSystemKind::Animation);
                scene.systems.push_back(SceneSystemKind::Industrial);
                scene.systems.push_back(SceneSystemKind::Animation);
            }
            m_document.MarkModified();
        }
        industrial = contains(SceneSystemKind::Industrial);
        animation = contains(SceneSystemKind::Animation);
        if (ImGui::MenuItem("Animation", nullptr, animation, !industrial)) {
            if (animation) std::erase(scene.systems, SceneSystemKind::Animation);
            else scene.systems.push_back(SceneSystemKind::Animation);
            m_document.MarkModified();
        }
        if (industrial) ImGui::TextDisabled("Industrial requires Animation after it.");
        ImGui::EndDisabled();
        ImGui::EndMenu();
    }
    m_layout.DrawMenu();
    ImGui::BeginDisabled(m_document.HasPending() || !m_importSource.empty() || m_fileCommand != FileCommand::None);
    if (ImGui::Button(playing ? "Stop" : "Play")) {
        if (playing) m_requestStop = true;
        else m_requestPlay = true;
    }
    ImGui::EndDisabled();
    ImGui::Text("%s%s", m_document.Path().empty() ? "Untitled" : AssetPathUtf8(m_document.Path().filename()).c_str(),
        m_document.Dirty() ? " *" : "");
    if (!m_status.empty()) {
        ImGui::TextDisabled("| %s", m_status.c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", m_status.c_str());
    }
    ImGui::EndMainMenuBar();
}

void Editor::DrawDocumentDialogs()
{
    if (m_document.NeedsConfirmation() && !ImGui::IsPopupOpen("Unsaved Scene"))
        ImGui::OpenPopup("Unsaved Scene");
    if (ImGui::BeginPopupModal("Unsaved Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Save changes before opening or creating a scene?");
        if (ImGui::Button("Save")) { m_document.Decide(SceneDecision::Save); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button("Don't Save")) { m_document.Decide(SceneDecision::Discard); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) { m_document.Decide(SceneDecision::Cancel); ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
    if (m_openImportPopup) { ImGui::OpenPopup("Import Model"); m_openImportPopup = false; }
    if (ImGui::BeginPopupModal("Import Model", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Source: %s", AssetPathUtf8(m_importSource.filename()).c_str());
        ImGui::InputText("Model folder", m_importName, sizeof(m_importName));
        bool available = false;
        try { available = m_assets.ImportNameAvailable(m_importName); }
        catch (const std::exception& error) { m_status = error.what(); }
        ImGui::TextUnformatted("Copy model and dependencies into assets/models/<folder>.");
        if (!available) ImGui::TextUnformatted("Name exists or is invalid. Choose a different name, or Cancel.");
        ImGui::BeginDisabled(!available);
        if (ImGui::Button("Import")) { m_requestImport = true; ImGui::CloseCurrentPopup(); }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) { m_importSource.clear(); ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
}

void Editor::Draw(Scene& scene, bool playing, const ViewportImage& image, const Camera& renderCamera)
{
    ImGuizmo::BeginFrame();
    // 原生对话框的 Enter/Escape 不能泄漏成应用快捷键；至少经过一次新的 ImGui 输入帧。
    if (m_nativeInputGuard) {
        bool held = false;
        for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; ++key)
            held |= ImGui::IsKeyDown(static_cast<ImGuiKey>(key));
        for (int button = 0; button < 5; ++button) held |= ImGui::IsMouseDown(button);
        if (!held) m_nativeInputGuard = false;
    }
    if (!scene.registry.IsAlive(m_selectedEntity)) m_selectedEntity = {};
    DrawMenu(scene, playing);
    if (m_layout.DrawDockSpace()) {
        CancelViewportInput();
        CancelSceneGizmo();
    }
    bool modified = false;
    if (const auto dropped = DrawViewport(*this, scene, playing, m_viewportHovered,
        m_viewportFocused, m_cameraGesture, m_viewportRect, m_viewportRequest, image, renderCamera,
        m_gizmoOperation, m_gizmoLocal, m_gizmoCaptured, m_previousViewportRect, modified)) {
        if (dropped->kind == ProjectAssetKind::Model) m_requestInstance = dropped->relativePath;
        else {
            try { m_document.RequestOpen(m_assets.Resolve(dropped->relativePath, ProjectAssetKind::Scene)); }
            catch (const std::exception& error) { m_status = error.what(); }
        }
    }
    DrawHierarchy(*this, scene, playing, m_requestCreateEmpty, m_requestDelete);
    const auto actions = m_assetBrowser.Draw(m_assets, playing);
    m_requestPickImport |= actions.importModel;
    m_requestRefresh |= actions.refresh;
    if (actions.openScene) {
        try { m_document.RequestOpen(m_assets.Resolve(*actions.openScene, ProjectAssetKind::Scene)); }
        catch (const std::exception& error) { m_status = error.what(); }
    }
    DrawInspector(*this, scene, playing, m_environmentPath, sizeof(m_environmentPath),
        m_requestEnvironment, m_requestClearEnvironment, modified);
    if (modified && !playing) m_document.MarkModified();
    DrawDocumentDialogs();
}

void Editor::BeforeFileDialog()
{
    CancelViewportInput();
    CancelSceneGizmo();
    m_pauseAfterDialog = true;
    m_nativeInputGuard = true;
}
std::optional<std::filesystem::path> Editor::ChooseSavePath()
{
    const auto directory = m_document.Path().empty() ? m_assets.Root() / "scenes" : m_document.Path().parent_path();
    if (!IsAssetPathWithin(directory, m_assets.Root())) throw std::runtime_error("Scene directory is outside assets");
    std::filesystem::create_directories(directory);
    BeforeFileDialog();
    return FileDialogs::SaveScene(directory, m_document.Path().empty() ? std::filesystem::path("Untitled.scene.json") : m_document.Path().filename());
}
void Editor::RefreshAssets()
{
    try { m_assets.Refresh(); }
    catch (const std::exception& error) { m_status = std::string("Assets refresh failed: ") + error.what(); }
}
void Editor::SceneChanged(const Scene& scene)
{
    m_selectedEntity = {};
    // 旧流程：资产 ID 属于原 Scene，换场景后清除旧请求。现在传项目路径，仍取消旧操作。
    m_requestInstance.reset();
    m_pauseAfterDialog = true; // 新场景不推进同步加载期间的墙钟时间。
    CancelViewportInput();
    CancelSceneGizmo();
    std::snprintf(m_environmentPath, sizeof(m_environmentPath), "%s", scene.environment.sourcePath.c_str());
    if (!m_document.Path().empty()) m_assetBrowser.Reveal(m_assets, m_document.Path());
}

void Editor::ApplyPending(Scene& editScene, std::optional<Scene>& runtimeScene)
{
    try {
        if (m_requestStop) runtimeScene.reset();
        if (m_requestRefresh) { m_status = "Assets refreshed"; RefreshAssets(); }
        if (!runtimeScene) {
            if (m_requestCreateEmpty) {
                Entity entity = editScene.registry.Create();
                Transform transform;
                transform.local = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f));
                transform.world = transform.local;
                editScene.registry.Add<Transform>(entity, transform);
                editScene.registry.Add<Name>(entity, Name{"Entity"});
                Select(entity); m_document.MarkModified();
                m_status = "Entity created";
            }
            if (m_requestDelete && HasSelection() && editScene.registry.IsAlive(m_selectedEntity)) {
                editScene.DestroyEntitySubtree(m_selectedEntity);
                m_selectedEntity = {}; m_document.MarkModified(); m_status = "Entity deleted";
            }
            if (m_requestEnvironment) {
                const auto source = ResolveEditorPath(m_environmentPath);
                // 先读/烘焙完整候选数据，成功后才替换。失败不污染现有 Scene。
                const auto loaded = LoadEnvironment(source);
                const auto encoded = source.generic_u8string();
                editScene.environment.sourcePath.assign(reinterpret_cast<const char*>(encoded.data()), encoded.size());
                editScene.environmentData = loaded.data;
                editScene.environment.enabled = true; editScene.environment.background = true;
                m_document.MarkModified(); m_status = "Environment loaded";
            }
            if (m_requestClearEnvironment) {
                const bool changed = !editScene.environment.sourcePath.empty() || editScene.environment.enabled ||
                    editScene.environment.background || editScene.environment.intensity != 1.0f;
                editScene.environment = {}; editScene.environmentData.reset();
                if (changed) m_document.MarkModified();
                m_status = "Environment cleared";
            }
            if (m_requestInstance) {
                m_pauseAfterDialog = true;
                const auto path = m_assets.Resolve(*m_requestInstance, ProjectAssetKind::Model);
                // 原基础上新增 Import 内容：仅模型拖放创建实例，Import 按钮不修改场景。
                Scene candidate(editScene);
                const auto roots = ModelImporter::InstantiateAsset(candidate, ModelImporter::LoadAsset(candidate, path));
                editScene = std::move(candidate);
                if (!roots.empty()) Select(roots.front());
                m_document.MarkModified(); m_status = "Model instance created";
            }
            if (m_requestPickImport) {
                BeforeFileDialog();
                if (const auto source = FileDialogs::OpenModel(m_assets.Root())) {
                    m_importSource = *source;
                    const auto name = AssetPathUtf8(source->stem());
                    std::snprintf(m_importName, sizeof(m_importName), "%s", name.c_str());
                    m_openImportPopup = true;
                }
            }
            if (m_requestImport && !m_importSource.empty()) {
                m_pauseAfterDialog = true;
                // 成功后只刷新项目资源，不向 editScene 追加资源或实体。
                const auto imported = m_assets.ImportModel(m_importSource, m_importName);
                m_importSource.clear();
                m_status = "Model imported into project";
                RefreshAssets(); m_assetBrowser.Reveal(m_assets, imported);
            }
            if (m_fileCommand == FileCommand::New) m_document.RequestNew();
            if (m_fileCommand == FileCommand::Open) {
                BeforeFileDialog();
                if (const auto path = FileDialogs::OpenScene(m_assets.Root()))
                    m_document.RequestOpen(m_assets.Resolve(*path, ProjectAssetKind::Scene));
            }
            if (m_fileCommand == FileCommand::Save || m_fileCommand == FileCommand::SaveAs) {
                const auto path = m_fileCommand == FileCommand::SaveAs || m_document.Path().empty()
                    ? ChooseSavePath() : std::optional(m_document.Path());
                if (m_document.Save(editScene, m_assets, path)) {
                    m_status = "Scene saved"; RefreshAssets(); m_assetBrowser.Reveal(m_assets, m_document.Path());
                } else m_status = "Save canceled";
            }
            if (m_document.HasPending() && !m_document.NeedsConfirmation()) {
                bool replaced = false;
                try { replaced = m_document.Advance(editScene, m_assets, [this] { return ChooseSavePath(); }); }
                catch (...) { RefreshAssets(); throw; }
                RefreshAssets();
                if (replaced) { SceneChanged(editScene); m_status = m_document.Path().empty() ? "New scene" : "Scene opened"; }
                else m_status = "Scene switch canceled";
            }
            if (m_requestPlay && !m_document.HasPending() && m_importSource.empty()) runtimeScene.emplace(editScene);
        }
    } catch (const std::exception& error) {
        m_status = error.what();
        // 导入失败关闭命名框，下一次 Import 可重新选择；候选目录由 ProjectAssets 回收。
        if (m_requestImport) m_importSource.clear();
    }
    m_requestCreateEmpty = m_requestDelete = m_requestPlay = m_requestStop = false;
    m_requestImport = m_requestPickImport = m_requestRefresh = false;
    m_requestEnvironment = m_requestClearEnvironment = false;
    m_fileCommand = FileCommand::None;
    m_requestInstance.reset();
}


EditorInputCapture Editor::InputCapture() const
{
    if (ImGui::GetCurrentContext() == nullptr) {
        return {};
    }

    if (m_nativeInputGuard || m_document.NeedsConfirmation() || !m_importSource.empty()) return {true, true};
    const ImGuiIO& io = ImGui::GetIO();
    if (m_gizmoCaptured || ImGuizmo::IsUsingAny()) return { true, true };
    const bool viewportInput = m_viewportHovered && m_viewportFocused && !ImGui::IsAnyItemActive() &&
        ImGui::GetDragDropPayload() == nullptr;
    // 空闲 UI 的导航焦点也会捕获键盘；在未编辑或拖放的视口中让 WASD 通行。
    return EditorInputCapture{ io.WantCaptureKeyboard && !viewportInput,
        io.WantCaptureMouse && !viewportInput };
}

InputState Editor::CameraInput(InputState input)
{
    const bool blocked = m_nativeInputGuard || m_document.HasPending() || !m_importSource.empty() ||
        m_gizmoCaptured || ImGuizmo::IsUsingAny() ||
        ImGui::IsAnyItemActive() || ImGui::GetDragDropPayload() != nullptr;
    input = FilterViewportCamera(input, m_viewportRect, m_viewportFocused,
        m_cameraGesture, blocked, m_cameraAdmitted);
    if (!m_cameraAdmitted) m_cameraGesture = false;
    return input;
}

bool Editor::HasSelection() const
{
    return m_selectedEntity.IsValid();
}

Entity Editor::SelectedEntity() const
{
    return m_selectedEntity;
}

void Editor::Select(Entity entity)
{
    m_selectedEntity = entity;
}

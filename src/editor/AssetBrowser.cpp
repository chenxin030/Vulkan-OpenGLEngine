#include "editor/AssetBrowser.h"
#include <imgui.h>
#include <algorithm>

void AssetBrowser::Reveal(const ProjectAssets& assets, const std::filesystem::path& path)
{
    m_selected = path.lexically_relative(assets.Root());
    m_directory = m_selected.parent_path();
    m_resetScroll = true;
}
void AssetBrowser::DrawDirectory(const ProjectAssets& assets, const std::filesystem::path& directory)
{
    const auto id = directory.empty() ? std::string("assets") : AssetPathUtf8(directory);
    bool hasChildren = false;
    for (const auto& entry : assets.Entries())
        if (entry.kind == ProjectAssetKind::Directory && entry.relativePath.parent_path() == directory) hasChildren = true;
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (m_directory == directory) flags |= ImGuiTreeNodeFlags_Selected;
    if (directory.empty()) flags |= ImGuiTreeNodeFlags_DefaultOpen;
    if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf;
    const auto label = directory.empty() ? std::string("assets") : AssetPathUtf8(directory.filename());
    const bool open = ImGui::TreeNodeEx(id.c_str(), flags, "%s", label.c_str());
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) { m_directory = directory; m_selected.clear(); m_resetScroll = true; }
    if (open) {
        for (const auto& entry : assets.Entries())
            if (entry.kind == ProjectAssetKind::Directory && entry.relativePath.parent_path() == directory)
                DrawDirectory(assets, entry.relativePath);
        ImGui::TreePop();
    }
}

AssetBrowserActions AssetBrowser::Draw(const ProjectAssets& assets, bool playing)
{
    AssetBrowserActions actions;
    if (!ImGui::Begin("Assets")) { ImGui::End(); return actions; }
    ImGui::BeginDisabled(playing);
    actions.importModel = ImGui::Button("Import");
    ImGui::EndDisabled();
    ImGui::SameLine(); actions.refresh = ImGui::Button("Refresh");
    ImGui::SameLine();
    if (ImGui::Button("Up") && !m_directory.empty()) { m_directory = m_directory.parent_path(); m_selected.clear(); m_resetScroll = true; }
    // Refresh 后目录不再存在于缓存时退回根目录。
    if (!m_directory.empty() && std::none_of(assets.Entries().begin(), assets.Entries().end(),
        [this](const auto& entry) { return entry.kind == ProjectAssetKind::Directory && entry.relativePath == m_directory; })) {
        m_directory.clear(); m_selected.clear(); m_resetScroll = true;
    }
    ImGui::SameLine(); ImGui::TextUnformatted(("assets/" + AssetPathUtf8(m_directory)).c_str());
    if (ImGui::BeginTable("AssetBrowserColumns", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("Folders", ImGuiTableColumnFlags_WidthFixed, 180.0f);
        ImGui::TableSetupColumn("Files", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextColumn();
        ImGui::BeginChild("AssetFolders", ImVec2(0, 0));
        DrawDirectory(assets, {});
        ImGui::EndChild();
        ImGui::TableNextColumn();
        ImGui::BeginChild("AssetFiles", ImVec2(0, 0));
        if (m_resetScroll) { ImGui::SetScrollY(0); m_resetScroll = false; }
        bool any = false;
        // 缓存只在启动/显式刷新/文件操作成功后替换，绘制期间不遍历磁盘或修改容器。
        for (const auto& entry : assets.Entries()) {
            if (entry.relativePath.parent_path() != m_directory) continue;
            any = true;
            const auto key = AssetPathUtf8(entry.relativePath);
            const char* type = entry.kind == ProjectAssetKind::Directory ? "[Folder] " :
                entry.kind == ProjectAssetKind::Model ? "[Model] " : "[Scene] ";
            const auto label = std::string(type) + AssetPathUtf8(entry.relativePath.filename());
            ImGui::PushID(key.c_str());
            if (ImGui::Selectable(label.c_str(), m_selected == entry.relativePath, ImGuiSelectableFlags_AllowDoubleClick)) {
                m_selected = entry.relativePath;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    if (entry.kind == ProjectAssetKind::Directory) { m_directory = entry.relativePath; m_selected.clear(); m_resetScroll = true; }
                    else if (entry.kind == ProjectAssetKind::Scene && !playing) actions.openScene = entry.relativePath;
                }
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", key.c_str());
            if (!playing && entry.kind != ProjectAssetKind::Directory && ImGui::BeginDragDropSource()) {
                ImGui::SetDragDropPayload(entry.kind == ProjectAssetKind::Model ? "PROJECT_MODEL" : "PROJECT_SCENE",
                    key.c_str(), key.size() + 1);
                ImGui::Text("%s: %s", entry.kind == ProjectAssetKind::Model ? "Create instance at origin" : "Open scene", label.c_str());
                ImGui::EndDragDropSource();
            }
            ImGui::PopID();
        }
        if (!any) ImGui::TextUnformatted("No models or scenes in this directory.");
        ImGui::EndChild();
        ImGui::EndTable();
    }
    ImGui::End();
    return actions;
}

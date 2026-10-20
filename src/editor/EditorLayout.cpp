#include "editor/EditorLayout.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <fstream>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace {
ImGuiID WorkspaceId() { return ImHashStr("RebuildEditorDockSpace.Assets.v1"); }

void BuildDefaultLayout(ImGuiViewport* viewport)
{
    const ImGuiID root = WorkspaceId();
    ImGui::DockBuilderRemoveNode(root);
    ImGui::DockBuilderAddNode(root, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodePos(root, viewport->WorkPos);
    ImGui::DockBuilderSetNodeSize(root, viewport->WorkSize);
    ImGuiID center = root;
    const ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.25f, nullptr, &center);
    const ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.23f, nullptr, &center);
    // 比例以原始全宽为基准：从剩余 75% 中取 22%。
    ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.22f / 0.75f, nullptr, &center);
    ImGui::DockBuilderDockWindow("Scene Hierarchy", left);
    ImGui::DockBuilderDockWindow("Scene View", center);
    ImGui::DockBuilderDockWindow("Inspector", right);
    ImGui::DockBuilderDockWindow("Assets", bottom);
    ImGui::DockBuilderDockWindow("Renderer Diagnostics", bottom);
    ImGui::DockBuilderFinish(root);
}
}

void EditorLayout::Initialize(const std::filesystem::path& path)
{
    m_path = path;
    m_initialized = true;
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    try {
        if (!std::filesystem::exists(path)) return;
        // 只用于本机布局；限制读取大小，错误不妨碍编辑场景。
        const auto size = std::filesystem::file_size(path);
        if (size > 1024 * 1024) throw std::runtime_error("layout file exceeds 1 MiB");
        std::ifstream stream(path, std::ios::binary);
        if (!stream) throw std::runtime_error("cannot open layout file");
        std::string data(static_cast<size_t>(size), '\0');
        if (size && !stream.read(data.data(), static_cast<std::streamsize>(size)))
            throw std::runtime_error("cannot read layout file");
        if (!data.empty()) ImGui::LoadIniSettingsFromMemory(data.data(), data.size());
    } catch (const std::exception& error) {
        m_status = std::string("Layout load failed: ") + error.what();
    }
}

void EditorLayout::DrawMenu()
{
    if (ImGui::BeginMenu("Layout")) {
        if (ImGui::MenuItem("Save Layout")) m_saveRequested = true;
        if (ImGui::MenuItem("Reset Layout")) m_resetRequested = true;
        if (!m_status.empty()) { ImGui::Separator(); ImGui::TextUnformatted(m_status.c_str()); }
        ImGui::EndMenu();
    }
}

bool EditorLayout::DrawDockSpace()
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImGuiDockNode* node = ImGui::DockBuilderGetNode(WorkspaceId());
    const bool rebuild = m_resetRequested || node == nullptr || !node->IsDockSpace();
    if (rebuild) {
        BuildDefaultLayout(viewport);
        m_resetRequested = false;
        m_saveRequested = true;
    }
    // 每帧保持宿主存在，并且早于全部可停靠窗口提交。中心由 Scene View 占据。
    ImGui::DockSpaceOverViewport(WorkspaceId(), viewport);
    return rebuild;
}

void EditorLayout::Save() noexcept
{
    if (!m_initialized || !ImGui::GetCurrentContext()) return;
    const auto temporary = std::filesystem::path(m_path).concat(".tmp");
    try {
        size_t size = 0;
        const char* data = ImGui::SaveIniSettingsToMemory(&size);
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) throw std::runtime_error("cannot create temporary layout file");
        stream.write(data, static_cast<std::streamsize>(size));
        stream.flush();
        if (!stream) throw std::runtime_error("cannot write layout file");
        stream.close();
        if (!stream) throw std::runtime_error("cannot close layout file");
        // 写完再替换：写失败不截断原布局。路径使用 filesystem/wide API 支持中文。
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), m_path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("cannot replace layout file");
#else
        std::filesystem::rename(temporary, m_path);
#endif
        m_status = "Layout saved";
    } catch (const std::exception& error) {
        m_status = std::string("Layout save failed: ") + error.what();
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
    }
    // 失败后不逐帧重试；用户可手动保存，后续布局修改仍可触发自动保存。
    ImGui::GetIO().WantSaveIniSettings = false;
    m_saveRequested = false;
}

void EditorLayout::SaveIfRequested() noexcept
{
    if (m_initialized && ImGui::GetCurrentContext() &&
        (m_saveRequested || ImGui::GetIO().WantSaveIniSettings)) Save();
}

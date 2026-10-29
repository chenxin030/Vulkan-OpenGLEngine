#pragma once

#include <filesystem>
#include <string>

// 编辑器工作区，不持有 Scene/GPU 对象；ImGui Context 仍归 Renderer。
// Initialize 在首个 NewFrame 前加载，Save 在 Context 销毁前执行。
class EditorLayout
{
public:
    void Initialize(const std::filesystem::path& path);
    void DrawMenu();
    bool DrawDockSpace(); // true：本帧重建了布局，调用者需取消旧交互。
    void Save() noexcept;
    void SaveIfRequested() noexcept;
private:
    std::filesystem::path m_path;
    std::string m_status;
    bool m_initialized = false;
    bool m_resetRequested = false;
    bool m_saveRequested = false;
};

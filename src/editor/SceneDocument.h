#pragma once

#include <filesystem>
#include <functional>
#include <optional>

struct Scene;
class ProjectAssets;
enum class SceneDecision { Save, Discard, Cancel };

// 编辑文档的路径/修改标记/待切换目标。不拥有场景，不读取 ImGui 或 GLFW。
class SceneDocument
{
public:
    bool Dirty() const { return m_dirty; }
    void MarkModified() { m_dirty = true; }
    const std::filesystem::path& Path() const { return m_path; }
    bool HasPending() const { return m_pending.has_value(); }
    bool NeedsConfirmation() const { return m_dirty && m_pending && !m_decision; }
    void RequestNew();
    void RequestOpen(const std::filesystem::path& path);
    void Decide(SceneDecision decision);
    // 用户取消 Save As 返回 false。失败不改变文档路径/dirty，也不截断已有文件。
    bool Save(const Scene& scene, const ProjectAssets& assets,
        const std::optional<std::filesystem::path>& target);
    // 只在快照释放后的帧末调用。true 表示完整候选已经替换当前场景。
    bool Advance(Scene& scene, const ProjectAssets& assets,
        const std::function<std::optional<std::filesystem::path>()>& chooseSavePath);

    static Scene EmptyScene();

private:
    struct Transition { 
        bool newScene; 
        std::filesystem::path path; 
    };
    std::filesystem::path m_path;
    bool m_dirty = false;
    std::optional<Transition> m_pending;
    std::optional<SceneDecision> m_decision;
};

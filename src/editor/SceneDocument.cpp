#include "editor/SceneDocument.h"
#include "editor/ProjectAssets.h"
#include "scene/Scene.h"
#include "scene/SceneSerializer.h"
#include <stdexcept>
#include <utility>
#include <atomic>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

Scene SceneDocument::EmptyScene() { return Scene{}; }

void SceneDocument::RequestNew()
{
    if (!m_pending) { m_pending = Transition{true, {}}; m_decision.reset(); }
}
void SceneDocument::RequestOpen(const std::filesystem::path& path)
{
    if (!m_pending) { m_pending = Transition{false, path}; m_decision.reset(); }
}
void SceneDocument::Decide(SceneDecision decision)
{
    if (!m_pending) return;
    if (decision == SceneDecision::Cancel) { m_pending.reset(); m_decision.reset(); }
    else m_decision = decision;
}

bool SceneDocument::Save(const Scene& scene, const ProjectAssets& assets,
    const std::optional<std::filesystem::path>& target)
{
    if (!target) return false;
    const auto path = assets.Resolve(*target, ProjectAssetKind::Scene);
    static std::atomic<uint64_t> nextSave{0};
    std::filesystem::path temporary;
    do { temporary = std::filesystem::path(path).concat(".saving-" + std::to_string(++nextSave)); }
    while (std::filesystem::exists(std::filesystem::symlink_status(temporary)));
    try {
        if (!IsAssetPathWithin(temporary, assets.Root())) throw std::runtime_error("Save temporary path escapes assets");
        // 临时文件和最终文件具有相同父目录，v3 相对资源路径不会因临时文件名变化。
        SceneSerializer::Save(scene, temporary);
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Cannot replace scene file; current scene is still unsaved");
#else
        std::filesystem::rename(temporary, path);
#endif
    } catch (...) {
        std::error_code ignored;
        if (IsAssetPathWithin(temporary, assets.Root())) std::filesystem::remove(temporary, ignored);
        throw;
    }
    m_path = path;
    m_dirty = false;
    return true;
}

bool SceneDocument::Advance(Scene& scene, const ProjectAssets& assets,
    const std::function<std::optional<std::filesystem::path>()>& chooseSavePath)
{
    if (!m_pending || NeedsConfirmation()) return false;
    // 消费一次：保存取消、保存失败、目标加载失败均终止本次切换，不在下一帧重复。
    const Transition transition = std::move(*m_pending);
    const auto decision = m_decision;
    m_pending.reset(); m_decision.reset();
    if (m_dirty && decision == SceneDecision::Save) {
        const auto target = m_path.empty() ? chooseSavePath() : std::optional(m_path);
        if (!Save(scene, assets, target)) return false;
    }
    const auto target = transition.newScene ? std::filesystem::path{} : assets.Resolve(transition.path, ProjectAssetKind::Scene);
    // 直接造一个新的 Scene；完整候选准备成功后，才替换当前编辑场景。
    Scene candidate = transition.newScene ? EmptyScene() : SceneSerializer::Load(target);
    scene = std::move(candidate);
    m_path = target;
    m_dirty = false;
    return true;
}

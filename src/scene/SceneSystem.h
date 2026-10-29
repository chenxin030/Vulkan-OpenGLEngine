#pragma once

#include "scene/Entity.h"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <vector>

struct Scene;

// Runtime 的活动实体集合，记录“这次运行中，哪些实体当前参与动画更新和展示”。
// Registry：实体是否存在，以及实体有哪些组件
// RuntimeEntities：已存在的实体是否处于运行活动集合中
// 例如工业系统的货物池：预先创建的货物都存在于 Registry，但闲置货物不应显示，也不应推进动画。
    // 货物激活 → Show(entity) → 加入活动集合
    // 货物回收 → Hide(entity) → 移出活动集合，实体仍然存在
class RuntimeEntities
{
public:
    void Show(Entity entity);
    void Hide(Entity entity);
    bool Contains(Entity entity) const;
    std::span<const Entity> View() const { return m_entities; }

private:
    static constexpr std::size_t Missing = std::numeric_limits<std::size_t>::max();
    std::vector<Entity> m_entities;
    std::vector<std::size_t> m_positions;
};

// SceneRuntime 传给各个 System 的上下文：把系统执行时需要的场景、活动实体和模拟时间放在一起。
struct SceneSystemContext
{
    Scene& scene;
    RuntimeEntities& entities;
    uint64_t tick = 0;
    double elapsedSeconds = 0;
};

struct RuntimeStatistic
{
    std::string name;
    uint64_t value = 0;
};

// 按 System 注册顺序。先执行所有 FixedUpdate，再执行所有 AfterFixedUpdate，解决 System 之间的数据依赖。
// 比如 IndustrialSystem 更新机械运动、回收到期货物；AnimationSystem 推进 Spin、求解包含 Spin / Bob 的世界变换
// 然后 IndustrialSystem 生成货物、更新运输位置、重算货物世界变换
// Start 只调用一次；Update 每渲染帧调用。系统不得保存跨帧的组件引用或实体 View。
class SceneSystem
{
public:
    virtual ~SceneSystem() = default;
    virtual void Start(SceneSystemContext&) {}
    virtual void FixedUpdate(SceneSystemContext&, double) {}
    virtual void AfterFixedUpdate(SceneSystemContext&, double) {}
    virtual void Update(SceneSystemContext&, double) {}
    virtual void AppendStatistics(std::vector<RuntimeStatistic>&) const {}
};

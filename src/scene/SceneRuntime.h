#pragma once

#include "core/FixedStepClock.h"
#include "scene/RenderSnapshot.h"
#include "scene/SceneSystem.h"
#include <memory>
// 借用独立的运行场景；先注册系统再 Start，本对象先于场景销毁。
// 运行期间的结构修改由系统负责，同时维护 context.entities。
class SceneRuntime
{
public:
    explicit SceneRuntime(Scene& scene);
    ~SceneRuntime();
    SceneRuntime(const SceneRuntime&) = delete;
    SceneRuntime& operator=(const SceneRuntime&) = delete;

    void AddSystem(std::unique_ptr<SceneSystem> system);
    void Start();
    void Update(double deltaSeconds);
    RenderSnapshot Snapshot() const;
    std::span<const Entity> VisibleEntities() const;
    std::vector<RuntimeStatistic> Statistics() const;

private:
    enum class State { Configuring, Starting, Running, Failed };
    void RequireRunning() const;
    RuntimeEntities m_entities;
    SceneSystemContext m_context;
    FixedStepClock m_clock;
    std::vector<std::unique_ptr<SceneSystem>> m_systems;
    State m_state = State::Configuring;
};

// 按场景的显式 system 清单校验、创建并注册系统，返回已启动的运行时。
std::unique_ptr<SceneRuntime> CreateSceneRuntime(Scene& scene);


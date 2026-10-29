#include "scene/SceneRuntime.h"
#include "scene/Scene.h"
#include "industrial/IndustrialSystem.h"
#include "scene/AnimationSystem.h"
#include "scene/Systems.h"
#include <cmath>
#include <stdexcept>

namespace {
// 未启用动画时仍维护基础世界变换，不解释 Spin/Bob 数据。
class WorldTransformSystem final : public SceneSystem
{
public:
    void Start(SceneSystemContext& context) override { TransformSystem(context.scene.registry); }
    void Update(SceneSystemContext& context, double) override { TransformSystem(context.scene.registry); }
};

std::unique_ptr<SceneSystem> CreateSystem(SceneSystemKind kind)
{
    switch (kind) {
    case SceneSystemKind::Industrial: return std::make_unique<IndustrialSystem>();
    case SceneSystemKind::Animation: return std::make_unique<AnimationSystem>();
    }
    throw std::runtime_error("Unknown scene system kind");
}
}

SceneRuntime::SceneRuntime(Scene& scene) : m_context{scene, m_entities} {}
SceneRuntime::~SceneRuntime() = default;

void SceneRuntime::AddSystem(std::unique_ptr<SceneSystem> system)
{
    if (m_state != State::Configuring) throw std::logic_error("Register systems before Start");
    if (!system) throw std::invalid_argument("Cannot register a null scene system");
    m_systems.push_back(std::move(system));
}

void SceneRuntime::Start()
{
    if (m_state != State::Configuring) throw std::logic_error("Scene runtime can only start once");
    m_state = State::Starting;
    try {
        m_context.scene.registry.ForEachEntity([&](Entity e) { m_entities.Show(e); });
        for (auto& system : m_systems) system->Start(m_context);
        m_state = State::Running;
    } catch (...) {
        m_state = State::Failed;
        throw;
    }
}

void SceneRuntime::RequireRunning() const
{
    if (m_state != State::Running) 
        throw std::logic_error("Scene runtime is not running");
}

void SceneRuntime::Update(double deltaSeconds)
{
    RequireRunning();
    if (!std::isfinite(deltaSeconds) || deltaSeconds < 0)
        throw std::invalid_argument("Invalid frame delta");
    const auto steps = m_clock.Advance(deltaSeconds);
    try {
        for (uint32_t i = 0; i < steps; ++i) {
            ++m_context.tick;
            m_context.elapsedSeconds = double(m_context.tick) * FixedStepClock::Step;
            for (auto& system : m_systems) system->FixedUpdate(m_context, FixedStepClock::Step);
            for (auto& system : m_systems) system->AfterFixedUpdate(m_context, FixedStepClock::Step);
        }
        for (auto& system : m_systems) 
            system->Update(m_context, deltaSeconds);
    } catch (...) {
        m_state = State::Failed;
        throw;
    }
}

RenderSnapshot SceneRuntime::Snapshot() const
{
    RequireRunning();
    return BuildRenderSnapshot(m_context.scene, m_entities.View());
}

std::span<const Entity> SceneRuntime::VisibleEntities() const
{
    RequireRunning();
    return m_entities.View();
}

std::vector<RuntimeStatistic> SceneRuntime::Statistics() const
{
    RequireRunning();
    std::vector<RuntimeStatistic> statistics{{"tick", m_context.tick}};
    for (const auto& system : m_systems) system->AppendStatistics(statistics);
    return statistics;
}

std::unique_ptr<SceneRuntime> CreateSceneRuntime(Scene& scene)
{
    ValidateSceneSystems(scene.systems);
    auto runtime = std::make_unique<SceneRuntime>(scene);
    bool hasAnimation = false;
    for (auto kind : scene.systems) {
        runtime->AddSystem(CreateSystem(kind));
        hasAnimation |= kind == SceneSystemKind::Animation;
    }
    if (!hasAnimation)
        runtime->AddSystem(std::make_unique<WorldTransformSystem>());
    runtime->Start();
    return runtime;
}

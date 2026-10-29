#include "industrial/IndustrialSystem.h"
#include "scene/Components.h"
#include "scene/ModelImporter.h"
#include "scene/Systems.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

void ValidateIndustrialRules(const Scene& scene)
{
    const auto& r = scene.registry;
    uint64_t total = 0;
    r.ForEach<MachineMotion>([&](Entity e, const MachineMotion& m) {
        if (!r.Has<Transform>(e) || m.kind < 0 || m.kind > 3 ||
            !std::isfinite(m.axis.x) || !std::isfinite(m.axis.y) || !std::isfinite(m.axis.z) ||
            glm::length(m.axis) < 0.001f || !std::isfinite(glm::length(m.axis)) ||
            !std::isfinite(m.amplitude) || !std::isfinite(m.speed) || !std::isfinite(m.phase) ||
            std::abs(m.amplitude) > 100 || std::abs(m.speed) > 100)
            throw std::runtime_error("Invalid MachineMotion rule");
        });
    r.ForEach<CargoRoute>([&](Entity e, const CargoRoute& c) {
        total += c.capacity;
        (void)scene.FindModelAssetIndex(c.assetId); // 不存在的资产由查找接口抛出错误。
        if (!r.Has<Transform>(e) || !r.IsAlive(c.target) || !r.Has<Transform>(c.target) || e == c.target ||
            c.capacity == 0 || total > 8192 ||
            !std::isfinite(c.speed) || c.speed < 0.1f || c.speed > 100 ||
            !std::isfinite(c.interval) || c.interval < 1.0f / 60 || c.interval > 3600 ||
            !std::isfinite(c.scale) || c.scale <= 0 || c.scale > 100 ||
            !std::isfinite(c.altitude) || std::abs(c.altitude) > 100)
            throw std::runtime_error("Invalid CargoRoute rule or missing station/model (total pool <= 8192)");
        });
}

void IndustrialSystem::Start(SceneSystemContext& context)
{
    auto& scene = context.scene;
    ValidateIndustrialRules(scene);
    auto& registry = scene.registry;
    registry.ForEach<MachineMotion>([&](Entity e, const MachineMotion&) {
        m_motions.push_back({ e, registry.TryGet<Transform>(e)->local });
        });
    registry.ForEach<CargoRoute>([&](Entity e, const CargoRoute&) { m_routes.push_back({ e,{}, 1 }); });
    if (m_motions.empty() && m_routes.empty()) return;
    if (scene.industrialRuntimeOnly) throw std::runtime_error("Industrial runtime already initialized");
    scene.industrialRuntimeOnly = true;
    EvaluateAnimatedTransforms(registry, context.entities.View(), context.elapsedSeconds);
    for (const auto& route : m_routes) {
        const auto& rule = *registry.TryGet<CargoRoute>(route.entity);
        const float distance = glm::distance(glm::vec3(registry.TryGet<Transform>(route.entity)->world[3]),
            glm::vec3(registry.TryGet<Transform>(rule.target)->world[3]));
        if (!std::isfinite(distance) || distance < 0.001f || distance > 10000)
            throw std::runtime_error("Industrial stations need a finite separation within 0.001..10000 units");
    }
    for (uint32_t routeIndex = 0; routeIndex < m_routes.size(); ++routeIndex) {
        auto& route = m_routes[routeIndex];
        // 在实例化资源前复制：结构性更改会使组件引用失效。
        const auto rule = (*registry.TryGet<CargoRoute>(route.entity));
        const auto asset = scene.FindModelAssetIndex(rule.assetId);
        route.idle.reserve(rule.capacity);
        for (uint32_t i = 0; i < rule.capacity; ++i) {
            Instance instance;
            ModelImporter::InstantiateAsset(scene, asset, &instance.nodes);
            route.idle.push_back(uint32_t(m_instances.size()));
            m_instances.push_back(std::move(instance));
            m_owner.push_back(routeIndex);
        }
    }
    m_active.reserve(m_instances.size());
    size_t maximumActiveNodes = 0;
    for (const auto& instance : m_instances) maximumActiveNodes += instance.nodes.size();
    m_activeNodes.reserve(maximumActiveNodes);
}

void IndustrialSystem::FixedUpdate(SceneSystemContext& context, double)
{
    auto& registry = context.scene.registry;
    const double time = context.elapsedSeconds;
    for (const auto& motion : m_motions) {
        const auto& rule = (*registry.TryGet<MachineMotion>(motion.entity));
        const float phase = float(time * rule.speed + rule.phase);
        const auto axis = glm::normalize(rule.axis);
        glm::mat4 offset(1);
        if (rule.kind == 0) offset = glm::rotate(offset, phase * rule.amplitude, axis);
        else if (rule.kind == 1) offset = glm::translate(offset, axis * (std::sin(phase) * rule.amplitude));
        else {
            // Fixed pick -> carry -> place -> empty return. The replacement workpiece
            // resets at the next pickup; this is a visual cycle, not production inventory.
            const float u = phase / 6.2831853f - std::floor(phase / 6.2831853f);
            auto ease = [](float x) { x = std::clamp(x, 0.0f, 1.0f); return x * x * (3 - 2 * x); };
            float sweep = u < 0.12f ? -1 : u < 0.45f ? -1 + 2 * ease((u - 0.12f) / 0.33f) :
                u < 0.62f ? 1 : u < 0.95f ? 1 - 2 * ease((u - 0.62f) / 0.33f) : -1;
            if (rule.kind == 3 && u >= 0.5f && u < 0.95f) sweep = 1;
            offset = glm::rotate(offset, sweep * rule.amplitude, axis);
            if (rule.kind == 3) {
                (*registry.TryGet<Transform>(motion.entity)).local = offset * motion.base;
                continue;
            }
        }
        (*registry.TryGet<Transform>(motion.entity)).local = motion.base * offset;
    }
    // 先隐藏本步到期的实例，动画系统不推进闲置池中的 Spin/Bob。
    for (size_t i = m_active.size(); i-- > 0;) {
        const uint32_t id = m_active[i];
        if (context.tick - m_instances[id].born >= m_instances[id].duration) {
            for (Entity e : m_instances[id].nodes) context.entities.Hide(e);
            m_routes[m_owner[id]].idle.push_back(id);
            m_active[i] = m_active.back(); m_active.pop_back();
        }
    }
}

void IndustrialSystem::AfterFixedUpdate(SceneSystemContext& context, double deltaSeconds)
{
    auto& registry = context.scene.registry;
    for (auto& route : m_routes) {
        if (context.tick < route.next) continue;
        const auto& rule = (*registry.TryGet<CargoRoute>(route.entity));
        route.next = context.tick + std::max(uint64_t(1), uint64_t(std::ceil(double(rule.interval) * 60 - 1e-6)));
        if (route.idle.empty()) continue;
        const auto id = route.idle.back(); route.idle.pop_back();
        auto& instance = m_instances[id];
        instance.start = glm::vec3((*registry.TryGet<Transform>(route.entity)).world[3]) + glm::vec3(0, rule.altitude, 0);
        instance.end = glm::vec3((*registry.TryGet<Transform>(rule.target)).world[3]) + glm::vec3(0, rule.altitude, 0);
        const float distance = glm::distance(instance.start, instance.end);
        if (!std::isfinite(distance) || distance < 0.001f || distance > 10000) {
            route.idle.push_back(id); continue; // animated stations may temporarily coincide
        }
        instance.born = context.tick;
        instance.duration = std::max(uint64_t(1), uint64_t(std::ceil(double(distance) / rule.speed * 60)));
        auto& root = (*registry.TryGet<Transform>(instance.nodes.front()));
        const auto direction = instance.end - instance.start;
        root.local = glm::rotate(glm::mat4(1), std::atan2(direction.x, direction.z), glm::vec3(0, 1, 0));
        root.local = glm::scale(root.local, glm::vec3(rule.scale));
        m_active.push_back(id);
        // 新激活节点错过了本步动画阶段，只为这些节点推进一次 Spin。
        SpinSystem(registry, instance.nodes, static_cast<float>(deltaSeconds));
        for (Entity e : instance.nodes) context.entities.Show(e);
    }
    m_activeNodes.clear();
    for (const auto id : m_active) {
        const auto& instance = m_instances[id];
        const float progress = float(context.tick - instance.born) / float(instance.duration);
        auto& root = (*registry.TryGet<Transform>(instance.nodes.front()));
        root.local[3] = glm::vec4(glm::mix(instance.start, instance.end, progress), 1);
        m_activeNodes.insert(m_activeNodes.end(), instance.nodes.begin(), instance.nodes.end());
    }
    // 位置变化后重算活跃实例的 world，不再推进动画时间。
    EvaluateAnimatedTransforms(registry, m_activeNodes, context.elapsedSeconds);
}

void IndustrialSystem::AppendStatistics(std::vector<RuntimeStatistic>& statistics) const
{
    if (m_motions.empty() && m_routes.empty()) return;
    statistics.push_back({ "active", m_active.size() });
    statistics.push_back({ "idle", m_instances.size() - m_active.size() });
    statistics.push_back({ "pool", m_instances.size() });
}

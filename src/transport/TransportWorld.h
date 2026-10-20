#pragma once

#include "scene/Scene.h"
#include <array>
#include <cstdint>

// Runtime-only demo component. Not part of the v3 document format.
struct CargoFlight
{
    uint32_t route = 0;
    uint64_t bornTick = 0, sequence = 0;
};
struct TransportRoute
{
    glm::vec3 from{}, to{};
    uint64_t travelTicks = 0;
};
class TransportWorld
{
public:
    explicit TransportWorld(uint32_t limit, double interval = 0);
    void Tick();
    void Reset();
    Scene& GetScene() { return m_scene; }
    const Scene& GetScene() const { return m_scene; }
    uint32_t Active() const { return m_active; }
    uint32_t Limit() const { return m_limit; }
    uint64_t Ticks() const { return m_tick; }
    uint64_t Spawned() const { return m_spawned; }
    uint64_t Arrived() const { return m_arrived; }
    bool Stable() const { return m_tick >= m_stableTick; }
    double Interval() const { return double(m_intervalTicks) / 60.0; }
    const auto& Routes() const { return m_routes; }
    static constexpr double Step = 1.0 / 60.0;
private:
    void BuildScene();
    void Spawn(uint32_t route);
    Scene m_scene;
    std::array<TransportRoute, 16> m_routes;
    std::array<uint32_t, 4> m_materials{};
    std::array<uint64_t, 16> m_nextDispatch{};
    uint32_t m_limit = 0, m_active = 0, m_firstRoute = 0;
    uint64_t m_tick = 0, m_spawned = 0, m_arrived = 0;
    uint64_t m_intervalTicks = 1, m_stableTick = 0;
};

#include "transport/TransportWorld.h"
#include "scene/Components.h"
#include "core/Profiling.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace {
Image Pattern(uint32_t type)
{
    const std::array<glm::ivec3, 4> colors{{{40,130,245},{245,135,35},{60,205,95},{185,65,230}}};
    Image image{64, 64, std::vector<uint8_t>(64 * 64 * 4), true};
    for (uint32_t y = 0; y < 64; ++y) for (uint32_t x = 0; x < 64; ++x) {
        const bool dark = type == 0 ? (x / 8) % 2 == 0 :
            type == 1 ? ((x / 8 + y / 8) % 2 == 0) :
            type == 2 ? (x % 16 < 3 || y % 16 < 3) : ((x + y) % 16 < 5);
        for (int c = 0; c < 3; ++c) image.pixels[(y * 64 + x) * 4 + c] =
            uint8_t(dark ? colors[type][c] / 3 : colors[type][c]);
        image.pixels[(y * 64 + x) * 4 + 3] = 255;
    }
    return image;
}
glm::mat4 Model(glm::vec3 position, glm::vec3 scale, float yaw = 0)
{
    return glm::scale(glm::rotate(glm::translate(glm::mat4(1), position), yaw, {0,1,0}), scale);
}
void AddMesh(Scene& scene, glm::vec3 position, glm::vec3 scale, uint32_t mesh, uint32_t material)
{
    const auto entity = scene.registry.Create();
    const auto model = Model(position, scale);
    scene.registry.Add<Transform>(entity, Transform{model, model});
    scene.registry.Add<MeshRenderer>(entity, MeshRenderer{mesh, material});
}
}

TransportWorld::TransportWorld(uint32_t limit, double interval) : m_limit(limit)
{
    if (!limit || limit > 100000 || !std::isfinite(interval) || interval < 0 || interval > 3600)
        throw std::invalid_argument("Transport limit must be 1..100000; interval must be 0..3600 seconds");
    // A default batch consists of one dispatch per route. Target saturation before the first arrival.
    m_intervalTicks = interval > 0 ? uint64_t(std::max(1.0, std::round(interval * 60))) :
        std::max<uint64_t>(1, uint64_t(16 * 3 * 60) / limit);
    BuildScene();
    uint64_t longest = 0;
    for (const auto& route : m_routes) longest = std::max(longest, route.travelTicks);
    m_stableTick = longest * 2 + m_intervalTicks;
}
void TransportWorld::BuildScene()
{
    ZoneScopedN("Transport.BuildScene");
    for (uint32_t i = 0; i < 4; ++i) {
        SurfaceMaterial material;
        material.baseColorTexture = m_scene.materials.AddTexture(Pattern(i));
        material.roughnessFactor = 0.65f;
        m_materials[i] = m_scene.materials.AddMaterial(material);
    }
    std::array<glm::vec3, 8> towers;
    for (uint32_t i = 0; i < 8; ++i) {
        const float angle = float(i) * 6.283185307f / 8;
        towers[i] = {std::cos(angle) * 15, 0, std::sin(angle) * 15};
        AddMesh(m_scene, towers[i] + glm::vec3(0,1.5f,0), {0.65f,1.5f,0.65f}, MeshLibrary::kCube, m_materials[i%4]);
        AddMesh(m_scene, towers[i] + glm::vec3(0,3.4f,0), {1,0.4f,1}, MeshLibrary::kPyramid, m_materials[i%4]);
    }
    SurfaceMaterial ground;
    ground.baseColorFactor = {0.12f,0.14f,0.16f,1};
    ground.roughnessFactor = 1;
    AddMesh(m_scene, {0,-0.25f,0}, {38,0.2f,38}, MeshLibrary::kCube, m_scene.materials.AddMaterial(ground));
    for (uint32_t i = 0; i < 16; ++i) {
        const uint32_t source = i / 2, target = (source + (i % 2 ? 3 : 1)) % 8;
        const float height = 2.0f + float(i % 4) * 0.5f;
        const auto from = towers[source] + glm::vec3(0,height,0);
        const auto to = towers[target] + glm::vec3(0,height,0);
        const double speed = 2.0 + (i % 4) * 0.3;
        m_routes[i] = {from, to, uint64_t(std::ceil(glm::length(to - from) / speed * 60))};
    }
    m_scene.camera.position = {0,24,30};
    m_scene.camera.yaw = -1.570796327f;
    m_scene.camera.pitch = -0.72f;
    m_scene.camera.fovDegrees = 40;
    m_scene.camera.farPlane = 200;
}
void TransportWorld::Spawn(uint32_t routeIndex)
{
    const auto entity = m_scene.registry.Create();
    m_scene.registry.Add<CargoFlight>(entity, CargoFlight{routeIndex, m_tick, m_spawned});
    const auto& route = m_routes[routeIndex];
    const auto direction = route.to - route.from;
    const float size = 0.24f + float(m_spawned % 4) * 0.04f;
    const auto matrix = Model(route.from, {size,size * 0.7f,size * 1.4f}, std::atan2(direction.x, direction.z));
    m_scene.registry.Add<Transform>(entity, Transform{matrix, matrix});
    m_scene.registry.Add<MeshRenderer>(entity, MeshRenderer{MeshLibrary::kCube, m_materials[m_spawned % 4]});
    ++m_active; ++m_spawned;
}
void TransportWorld::Tick()
{
    ZoneScopedN("Transport.FixedTick");
    ++m_tick;
    std::vector<Entity> arrived;
    {
        ZoneScopedN("Transport.Move");
        m_scene.registry.ForEach<CargoFlight, Transform>([&](Entity entity, CargoFlight& flight, Transform& transform) {
            const auto& route = m_routes[flight.route];
            const uint64_t age = m_tick - flight.bornTick;
            if (age >= route.travelTicks) { arrived.push_back(entity); return; }
            const glm::vec3 position = glm::mix(route.from, route.to, float(double(age) / double(route.travelTicks)));
            transform.local[3] = glm::vec4(position, 1);
            transform.world = transform.local;
        });
    }
    {
        ZoneScopedN("Transport.DestroyArrivals");
        // Registry traversal has ended. No RenderSnapshot has been published yet.
        for (const auto entity : arrived) { m_scene.registry.Destroy(entity); --m_active; ++m_arrived; }
    }
    {
        ZoneScopedN("Transport.Dispatch");
        for (uint32_t n = 0; n < 16; ++n) {
            const uint32_t route = (m_firstRoute + n) % 16;
            if (m_tick < m_nextDispatch[route]) continue;
            // A full station skips this dispatch; it does not accumulate a burst backlog.
            m_nextDispatch[route] = m_tick + m_intervalTicks;
            if (m_active < m_limit) Spawn(route);
        }
        m_firstRoute = (m_firstRoute + 1) % 16;
    }
}
void TransportWorld::Reset()
{
    std::vector<Entity> cargo;
    m_scene.registry.ForEach<CargoFlight>([&](Entity entity, CargoFlight&) { cargo.push_back(entity); });
    for (const auto entity : cargo) m_scene.registry.Destroy(entity);
    m_tick = m_spawned = m_arrived = 0;
    m_active = m_firstRoute = 0;
    m_nextDispatch.fill(0);
}

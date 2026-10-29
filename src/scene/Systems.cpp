#include "scene/Systems.h"

#include "scene/Components.h"
#include "scene/Registry.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <functional>
#include <unordered_map>
#include <stdexcept>

namespace {
void ResolveWorldTransform(Registry& registry, Entity entity,
    std::unordered_map<uint32_t, uint8_t>& states, const double* elapsedSeconds = nullptr)
{
    uint8_t& state = states[entity.index];
    if (state == 2) return;
    if (state == 1) throw std::runtime_error("entity transform hierarchy contains a cycle");
    Transform* transform = registry.TryGet<Transform>(entity);
    if (!transform) return;
    state = 1;
    transform->world = transform->local;
    if (elapsedSeconds) {
        if (const auto* spin = registry.TryGet<Spin>(entity))
            transform->world *= glm::rotate(glm::mat4(1), spin->angle, spin->axis);
    }
    if (const Parent* parent = registry.TryGet<Parent>(entity)) {
        if (!registry.IsAlive(parent->entity))
            throw std::runtime_error("entity transform hierarchy references a dead parent");
        ResolveWorldTransform(registry, parent->entity, states, elapsedSeconds);
        const Transform* parentTransform = registry.TryGet<Transform>(parent->entity);
        if (!parentTransform)
            throw std::runtime_error("entity parent has no Transform component");
        transform->world = parentTransform->world * transform->world;
    }
    if (elapsedSeconds) {
        if (const auto* bob = registry.TryGet<Bob>(entity)) {
            const float offset = std::sin(float(*elapsedSeconds) * bob->speed + bob->phase) * bob->amplitude;
            transform->world = glm::translate(glm::mat4(1), glm::vec3(0, offset, 0)) * transform->world;
        }
    }
    state = 2;
}
}

void SpinSystem(Registry& registry, std::span<const Entity> entities, float deltaSeconds)
{
    for (Entity entity : entities) {
        if (auto* spin = registry.TryGet<Spin>(entity)) spin->angle += spin->speed * deltaSeconds;
    }
}

void EvaluateAnimatedTransforms(Registry& registry, std::span<const Entity> entities, double elapsedSeconds)
{
    std::unordered_map<uint32_t, uint8_t> states;
    states.reserve(entities.size());
    for (Entity entity : entities)
        ResolveWorldTransform(registry, entity, states, &elapsedSeconds);
}

void TransformSystem(Registry& registry)
{
    std::unordered_map<uint32_t, uint8_t> states;
    registry.ForEachEntity([&](Entity entity) {
        if (registry.TryGet<Transform>(entity))
            ResolveWorldTransform(registry, entity, states);
    });
}

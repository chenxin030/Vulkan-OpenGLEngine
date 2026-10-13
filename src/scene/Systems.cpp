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
    std::unordered_map<uint32_t, uint8_t>& states)
{
    uint8_t& state = states[entity.index];
    if (state == 2) return;
    if (state == 1) throw std::runtime_error("entity transform hierarchy contains a cycle");
    Transform* transform = registry.TryGet<Transform>(entity);
    if (!transform) return;
    state = 1;
    transform->world = transform->local;
    if (const Parent* parent = registry.TryGet<Parent>(entity)) {
        if (!registry.IsAlive(parent->entity))
            throw std::runtime_error("entity transform hierarchy references a dead parent");
        ResolveWorldTransform(registry, parent->entity, states);
        const Transform* parentTransform = registry.TryGet<Transform>(parent->entity);
        if (!parentTransform)
            throw std::runtime_error("entity parent has no Transform component");
        transform->world = parentTransform->world * transform->local;
    }
    state = 2;
}
}

void SpinSystem(Registry& registry, float deltaSeconds)
{
    // 无需判断某个 Entity 能否旋转
    // 
    // deltaSeconds 是显式捕获的，而不是通过 [&]：lambda 直接传给 ForEach，
    // 不会在这个调用之外存在，显式捕获说明了它实际依赖什么;
    // 如果用空捕获列表会导致编译错误，而不是静默的 bug。
    registry.ForEach<Spin>([deltaSeconds](Entity, Spin& spin) {
        spin.angle += spin.speed * deltaSeconds;
    });
}

void TransformSystem(Registry& registry)
{
    // 两趟，而不是一趟带分支：pass 1 覆盖全体（每个 Entity 都有 Transform），循环体里
    // 没有任何查询、也没有数据相关的分支；"有没有 Spin"的判断只留在第二次遍历里。
    // 一趟带分支会让每个 Entity 都为少数会命中的情况付一次判断。
    //
    // pass 1 的无条件重置是后面每一趟的前提：从 pass 2 起都在 world 上"继续组合"，
    // 而不是从 local 重算 —— 从 local 重算的话，每加一个 Component 要么组合爆炸
    // （ForEach<Transform, Spin, Bob>），要么让"趟序"变得敏感。
    std::unordered_map<uint32_t, uint8_t> states;
    registry.ForEachEntity([&](Entity entity) {
        if (registry.TryGet<Transform>(entity)) ResolveWorldTransform(registry, entity, states);
    });

    // 自转是局部空间的变换 → 右乘（world * R）。乘错一侧，物体会绕世界原点公转，
    // 而不是原地自转。
    registry.ForEach<Transform, Spin>([](Entity, Transform& transform, const Spin& spin) {
        transform.world = transform.world *
            glm::rotate(glm::mat4(1.0f), spin.angle, spin.axis);
    });
}

void BobSystem(Registry& registry, float elapsedSeconds)
{
    // 浮动是世界空间的位移 → 左乘（T * world）。乘错一侧，浮动方向会被自转带着转。
    //
    // BobSystem 和上面 pass 2 谁先跑，结果都一样：矩阵乘法满足结合律
    // （T * (M * R) == (T * M) * R）。
    // 决定结果的是乘在哪一侧（左乘 = 世界空间，右乘 = 局部空间）。
    //
    // 左乘/右乘都不幂等（同一趟跑两次就叠加两次），
    // 所以它依赖 TransformSystem 的 pass 1 每次都把 world 重置成 local。
    registry.ForEach<Transform, Bob>([elapsedSeconds](Entity, Transform& transform, const Bob& bob) {
        const float offset = std::sin(elapsedSeconds * bob.speed + bob.phase) * bob.amplitude;
        transform.world =
            glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, offset, 0.0f)) * transform.world;
    });
}

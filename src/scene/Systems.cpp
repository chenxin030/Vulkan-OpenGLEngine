#include "scene/Systems.h"

#include "scene/Components.h"
#include "scene/Registry.h"

#include <glm/gtc/matrix_transform.hpp>

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
    // 两趟，而不是一趟带分支：pass 1 覆盖全体（每个实体都有 Transform），循环体里
    // 没有任何查询、也没有数据相关的分支；"有没有 Spin"的判断只留在第二次遍历里。
    // 一趟带分支会让每个实体都为少数会命中的情况付一次判断。
    registry.ForEach<Transform>([](Entity, Transform& transform) {
        transform.world = transform.local;
    });

    registry.ForEach<Transform, Spin>([](Entity, Transform& transform, const Spin& spin) {
        transform.world = transform.local *
            glm::rotate(glm::mat4(1.0f), spin.angle, spin.axis);
    });
}

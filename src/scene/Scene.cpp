#include "Scene.h"

#include "scene/Components.h"
#include "scene/Systems.h"

#include <cmath>
#include <glm/ext/matrix_transform.hpp>

namespace {

constexpr float kPi = 3.14159265358979323846f;

}  // namespace

void Scene::Update(float deltaSeconds)
{
    // 顺序在这里写死，而且重要：先推进 angle，再由 angle 组出 world；反过来画面慢一帧。
    // 显式三行（而不是一张系统注册表）也是它能被 grep 到的原因。
    SpinSystem(registry, deltaSeconds);
    TransformSystem(registry);

    // 一般来说实体动了就只改列表里对应那一项更快，但要求每一条修改世界的路径都记得同步列表
    // 现在规模小，几次到几十次矩阵拷贝就可以防止出问题
    drawItems.clear();

    registry.ForEach<Transform, MeshRenderer>(
        [this](Entity, const Transform& transform, const MeshRenderer& mesh) {
        drawItems.push_back(DrawItem{ transform.world, mesh.meshIndex });
    });
}

Scene BuildDemoScene()
{
    Scene scene;

    // --- ground ---------------------------------------------------------------
    {
        const Entity ground = scene.registry.Create();

        Transform transform;
        transform.local = glm::scale(
            glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.75f, 0.0f)),
            glm::vec3(8.0f, 0.05f, 8.0f));
        transform.world = transform.local;
        scene.registry.Add<Transform>(ground, transform);

        scene.registry.Add<MeshRenderer>(ground, MeshRenderer{ 0 });
    }

    constexpr int kObjectCount = 5;
    constexpr float kRingRadius = 1.9f;

    for (int i = 0; i < kObjectCount; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kObjectCount);
        const float angle = t * 2.0f * kPi;

        // 交替高度，所以一个失败的深度测试会显示为方块相互穿透，而不是表现得很微妙。
        const float height = (i % 2 == 0) ? 0.0f : 0.45f;

        glm::mat4 placement = glm::translate(
            glm::mat4(1.0f),
            glm::vec3(std::cos(angle) * kRingRadius, height, std::sin(angle) * kRingRadius));

        placement = glm::scale(placement, glm::vec3(0.55f + 0.15f * static_cast<float>(i % 3)));

        const Entity entity = scene.registry.Create();

        Transform transform;
        transform.local = placement;
        transform.world = placement;
        scene.registry.Add<Transform>(entity, transform);

        const uint32_t mesh = 0;
        scene.registry.Add<MeshRenderer>(entity, MeshRenderer{ mesh });

        const bool spins = (i < 2) || (i >= 4);
        if (spins) {
            Spin spin;
            spin.axis = glm::normalize(
                glm::vec3(0.35f, 1.0f, 0.2f + 0.15f * static_cast<float>(i)));
            spin.speed = 0.5f + 0.25f * static_cast<float>(i);
            spin.angle = angle;
            scene.registry.Add<Spin>(entity, spin);
        }
    }

    scene.camera.position = glm::vec3(0.0f, 3.5f, 9.0f);
    scene.camera.yaw = -1.5707963f;   // looking along -Z, at the ring
    scene.camera.pitch = -0.25f;      // angled slightly down

    return scene;
}

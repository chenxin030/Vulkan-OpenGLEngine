#include "Scene.h"

#include "scene/Components.h"
#include "scene/Systems.h"
#include "MeshLibrary.h"

#include <cmath>
#include <glm/ext/matrix_transform.hpp>

namespace {

constexpr float kPi = 3.14159265358979323846f;

}  // namespace

void Scene::Update(float deltaSeconds)
{
    elapsed += deltaSeconds;

    // 顺序在这里写死，而且重要：
    //   SpinSystem      推进 angle（下一行要读它）
    //   TransformSystem pass 1 把 world 重置成 local，pass 2 右乘自转
    //   BobSystem       在 world 上左乘世界空间的竖直位移 —— 必须晚于 pass 1 的重置
    // 显式几行（而不是一张系统注册表）也是它能被 grep 到的原因。
    SpinSystem(registry, deltaSeconds);
    TransformSystem(registry);
    BobSystem(registry, elapsed);

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
        // 平面网格是 2x2（立方体是 1x1），所以 4 给出和 D32 同样的 8x8 地板；
        // 立方体时代那个 (8, 0.05, 8) 的压扁参数对没有厚度的平面没有意义。
        transform.local = glm::scale(
            glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.75f, 0.0f)),
            glm::vec3(4.0f));
        transform.world = transform.local;
        scene.registry.Add<Transform>(ground, transform);

        scene.registry.Add<MeshRenderer>(ground, MeshRenderer{ MeshLibrary::kPlane });
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

        // 三种图元轮流用 —— 加一种图元在这里就是改这一行，渲染器完全不知道区别
        // （meshIndex 是真句柄之后，这就是"加图元"的全部成本）。
        const uint32_t mesh = (i % 3 == 0) ? MeshLibrary::kCube
            : (i % 3 == 1) ? MeshLibrary::kPyramid
            : MeshLibrary::kCylinder;
        scene.registry.Add<MeshRenderer>(entity, MeshRenderer{ mesh });

        // 故意把四种组合都摆在场景里：
        //   i=0 只有 Spin · i=1 Spin+Bob · i=2 什么都不加 · i=3 只有 Bob · i=4 只有 Spin
        const bool spins = (i < 2) || (i >= 4);
        if (spins) {
            Spin spin;
            spin.axis = glm::normalize(
                glm::vec3(0.35f, 1.0f, 0.2f + 0.15f * static_cast<float>(i)));
            spin.speed = 0.5f + 0.25f * static_cast<float>(i);
            spin.angle = angle;
            scene.registry.Add<Spin>(entity, spin);
        }

        const bool bobs = (i == 1) || (i == 3);
        if (bobs) {
            Bob bob;
            bob.amplitude = 0.20f + 0.10f * static_cast<float>(i % 2);
            bob.speed = 1.2f + 0.4f * static_cast<float>(i);
            bob.phase = angle;   // 相位错开，免得整排一起上下
            scene.registry.Add<Bob>(entity, bob);
        }
    }

    {
        const Entity pivot = scene.registry.Create();

        Transform transform;
        transform.local = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f));
        transform.world = transform.local;
        scene.registry.Add<Transform>(pivot, transform);
    }

    scene.camera.position = glm::vec3(0.0f, 3.5f, 9.0f);
    scene.camera.yaw = -1.5707963f;   // looking along -Z, at the ring
    scene.camera.pitch = -0.25f;      // angled slightly down

    return scene;
}

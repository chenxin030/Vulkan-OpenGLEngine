#include "Scene.h"

#include <cmath>
#include <glm/ext/matrix_transform.hpp>

namespace {

constexpr float kPi = 3.14159265358979323846f;

}  // namespace

void Scene::Update(float deltaSeconds)
{
    for (SceneObject& object : objects) {
        object.spinAngle += object.spinSpeed * deltaSeconds;
    }
}

glm::mat4 Scene::WorldTransform(const SceneObject& object)
{
    // base * rotate：旋转先作用在顶点上，所以物体绕自己的中心转，
    // 再由 baseTransform 把它摆到位置上。两个顺序换一下，
    // 立方体就会绕世界原点公转。
    return object.baseTransform *
        glm::rotate(glm::mat4(1.0f), object.spinAngle, object.spinAxis);
}

Scene BuildDemoScene()
{
    Scene scene;

    // --- ground ---------------------------------------------------------------
    {
        glm::mat4 transform;
        transform = glm::scale(
            glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.75f, 0.0f)),
            glm::vec3(8.0f, 0.05f, 8.0f));
        scene.objects.push_back(SceneObject{ .baseTransform = transform });
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

        // 尺寸各不相同，这样立方体在视觉上容易区分，而且转换顺序的错误也能立即被发现。
        placement = glm::scale(placement, glm::vec3(0.55f + 0.15f * static_cast<float>(i % 3)));

        const bool spins = (i < 2) || (i >= 4);
        if (spins) {
            glm::vec3 axis = glm::normalize(
                glm::vec3(0.35f, 1.0f, 0.2f + 0.15f * static_cast<float>(i)));
            float speed = 0.5f + 0.25f * static_cast<float>(i);
            scene.objects.push_back(SceneObject{ .baseTransform = placement, .spinAxis = axis,
                .spinSpeed = speed, .spinAngle = angle });
        }
        else {
            scene.objects.push_back(SceneObject{ .baseTransform = placement });
        }
    }

    return scene;
}

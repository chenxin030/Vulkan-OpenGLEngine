#include "scene/CameraController.h"

#include "scene/Camera.h"

#include <algorithm>
#include <cmath>

namespace {

// 89度。在正好±90度时，Forward() 会与世界向上方向平行，
// glm::lookAt 无法选择一个基础方向，所以视图矩阵会退化，图像会翻转或消失。
constexpr float kPitchLimit = 1.5533431f;

}  // namespace

CameraController::CameraController(Settings settings)
    : m_settings(settings)
{
}

void CameraController::Update(Camera& camera, const InputState& input, float deltaSeconds) const
{
    // 先看。下面的移动是用相机本地坐标表示的，所以必须使用这一帧的方向，而不是上一帧的方向——否则相机会总是比鼠标慢一帧转向。
    if (input.lookActive) {
        camera.yaw += input.lookDelta.x * m_settings.lookSensitivity;
        // 反向：向下移动鼠标应该看向下，屏幕的Y坐标向下增长。
        camera.pitch -= input.lookDelta.y * m_settings.lookSensitivity;

        camera.pitch = std::clamp(camera.pitch, -kPitchLimit, kPitchLimit);
    }

    glm::vec3 direction = camera.Forward() * input.moveAxis.z +
                          camera.Right() * input.moveAxis.x +
                          glm::vec3(0.0f, 1.0f, 0.0f) * input.moveAxis.y;

    // 在缩放之前先归一化。如果不这样做，同时按下W和D移动会比单独按W快√2倍
    const float lengthSquared = glm::dot(direction, direction);
    if (lengthSquared > 0.0001f) {
        direction /= std::sqrt(lengthSquared);
    }

    const float speed = m_settings.moveSpeed *
                        (input.sprint ? m_settings.sprintMultiplier : 1.0f);

    camera.position += direction * speed * deltaSeconds;
}

#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

// 摄像机
// 存储为位置和偏航/俯仰。
// 后端中性，就像 scene/ 里的其他东西一样。它产生矩阵；它并不知道谁在使用这些矩阵。

struct Camera
{
    glm::vec3 position{0.0f, 3.5f, 9.0f};

    // 弧度。偏航绕世界向上旋转；俯仰被控制器限制，刚好不到垂直，所以视角基础永不坍塌。
    float yaw = -1.5707963f;      // -90°: 看向-Z
    float pitch = -0.25f;

    float fovDegrees = 50.0f;
    float nearPlane = 0.1f;
    float farPlane = 100.0f;

    // 相机观察方向的单位向量，包括俯仰角。
    glm::vec3 Forward() const
    {
        const float cosPitch = std::cos(pitch);
        return glm::vec3(cosPitch * std::cos(yaw),
                         std::sin(pitch),
                         cosPitch * std::sin(yaw));
    }

    // 仅由偏航角导出：如果用 Forward() 和 up 叉乘，当摄像机俯仰时，横向移动会在垂直方向漂移。
    glm::vec3 Right() const
    {
        return glm::vec3(-std::sin(yaw), 0.0f, std::cos(yaw));
    }

    glm::mat4 ViewMatrix() const
    {
        return glm::lookAt(position, position + Forward(), glm::vec3(0.0f, 1.0f, 0.0f));
    }

    // aspect：宽度 / 高度，作为参数传入而不是存储：相机不拥有窗口，所以当窗口大小变化时不需要通知相机让相机改数值，增加耦合度。
    // 用的时候把 `aspect` 传进去，相机不应该持有它 
    glm::mat4 ProjectionMatrix(float aspect) const
    {
        return glm::perspective(glm::radians(fovDegrees), aspect, nearPlane, farPlane);
    }
};

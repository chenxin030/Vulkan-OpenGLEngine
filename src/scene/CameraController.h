#pragma once

#include "platform/InputState.h"

struct Camera;

// 将输入意图转换为相机移动。
// 与相机分开，因为相机是状态，而这是策略：视角转动有多快、移动速度多快、是否存在冲刺。
// 将它们分开意味着不同的策略——如轨道装置、剧情动画轨道、观众相机——不必存在于相机类型中。
// 也与平台层分开，这样可以使用合成的输入状态而完全不依赖窗口来测试映射。
class CameraController
{
public:
    struct Settings
    {
        float moveSpeed = 4.0f;             
        float sprintMultiplier = 3.0f;
        float lookSensitivity = 0.0025f;    // 鼠标移动每像素的弧度
    };

    CameraController() = default;
    explicit CameraController(Settings settings);

    void Update(Camera& camera, const InputState& input, float deltaSeconds) const;

private:
    Settings m_settings;
};

#pragma once

#include "scene/SceneSystem.h"

// 在修改 local 变换的系统之后注册。只更新可见实体，并将父节点动画传给子节点。
class AnimationSystem : public SceneSystem
{
public:
    void Start(SceneSystemContext& context) override;
    void FixedUpdate(SceneSystemContext& context, double deltaSeconds) override;
};

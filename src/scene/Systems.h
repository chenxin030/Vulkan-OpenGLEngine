#pragma once

#include "scene/Entity.h"
#include <span>

class Registry;

// 以下是按实体批量执行的算法；SceneSystem 在阶段级别调度，不为每个实体调用虚函数。

void SpinSystem(Registry& registry, std::span<const Entity> entities, float deltaSeconds);

// 从 local 重算 world，包含 Spin/Bob；递归先求解父节点，不推进动画时间。
void EvaluateAnimatedTransforms(Registry& registry, std::span<const Entity> entities, double elapsedSeconds);

// 仅从 local 和父子关系重算 world，不应用或推进动画。
void TransformSystem(Registry& registry);

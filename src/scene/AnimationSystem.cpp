#include "scene/AnimationSystem.h"
#include "scene/Scene.h"
#include "scene/Systems.h"

void AnimationSystem::Start(SceneSystemContext& context)
{
    EvaluateAnimatedTransforms(context.scene.registry, context.entities.View(), context.elapsedSeconds);
}

void AnimationSystem::FixedUpdate(SceneSystemContext& context, double deltaSeconds)
{
    SpinSystem(context.scene.registry, context.entities.View(), static_cast<float>(deltaSeconds));
    EvaluateAnimatedTransforms(context.scene.registry, context.entities.View(), context.elapsedSeconds);
}

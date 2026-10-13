#include "SceneUpdater.h"

#include "Scene.h"
#include "scene/Components.h"
#include "scene/Systems.h"

void SceneUpdater::Update(Scene& scene, float deltaSeconds)
{
    m_elapsed += deltaSeconds;

    SpinSystem(scene.registry, deltaSeconds);
    TransformSystem(scene.registry);
    BobSystem(scene.registry, m_elapsed);
}

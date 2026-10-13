#pragma once

struct Scene;

class SceneUpdater
{
public:
    void Update(Scene& scene, float deltaSeconds);

private:
    float m_elapsed = 0.0f;
};

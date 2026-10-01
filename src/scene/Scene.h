#pragma once

#include <glm/glm.hpp>
#include <vector>

struct SceneObject
{
    glm::mat4 baseTransform{ 1.0f }; 

    glm::vec3 spinAxis{ 0.0f, 1.0f, 0.0f };
    float spinSpeed = 0.0f;
    float spinAngle = 0.0f;
};

struct Scene
{
    std::vector<SceneObject> objects;

    void Update(float deltaSeconds);

    static glm::mat4 WorldTransform(const SceneObject& object);
};

Scene BuildDemoScene();

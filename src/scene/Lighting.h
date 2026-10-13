#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

inline glm::vec3 DirectionalLightDirection()
{
    return glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f));
}

inline glm::vec3 DirectionalLightColor()
{
    const float intensity = 8.0f;
    return glm::vec3(1.0f, 0.95f, 0.8833333f) * intensity;
}

inline glm::mat4 DirectionalLightViewProjection()
{
    const glm::vec3 direction = DirectionalLightDirection();
    const glm::mat4 view = glm::lookAt(-direction * 12.0f, glm::vec3(0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 projection = glm::ortho(-12.0f, 12.0f, -12.0f, 12.0f, 0.1f, 40.0f);
    return projection * view;
}

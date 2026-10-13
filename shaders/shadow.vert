#version 450

layout(binding = 0) uniform UniformBufferObject
{
    mat4 view;
    mat4 proj;
    vec4 cameraPosition;
    vec4 lightDirection;
    vec4 lightColor;
    mat4 lightViewProjection;
} ubo;

layout(push_constant) uniform ShadowPushConstants
{
    mat4 model;
} push;

layout(location = 0) in vec3 inPosition;

void main()
{
    gl_Position = ubo.lightViewProjection * push.model * vec4(inPosition, 1.0);
}

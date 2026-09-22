#version 450

// Per-frame camera data. Mirrors UniformBufferObject in vulkan_renderer.h (std140).
// Two mat4 = 128 bytes with no padding.
layout(binding = 0) uniform UniformBufferObject
{
    mat4 view;
    mat4 proj;
} ubo;

// Per-object data. A push constant, so it arrives through the pipeline layout
// rather than a descriptor set - see the note on PushConstants in
// vulkan_renderer.h for why that is the right call for a single matrix.
layout(push_constant) uniform PushConstants
{
    mat4 model;
} push;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;

void main()
{
    gl_Position = ubo.proj * ubo.view * push.model * vec4(inPosition, 1.0);
    fragColor = inColor;
    fragTexCoord = inTexCoord;
}

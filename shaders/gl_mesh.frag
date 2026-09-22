#version 450

// OpenGL counterpart of shaders/mesh.frag.
//
// The body is identical to the Vulkan version. Only the binding of the sampler
// differs: Vulkan needs layout(binding = 1) to match the descriptor set layout,
// while OpenGL has no descriptor sets at all and uses a plain sampler uniform whose
// texture unit is assigned with glUniform1i.

uniform sampler2D uTexture;

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

void main()
{
    vec3 texel = texture(uTexture, fragTexCoord).rgb;
    outColor = vec4(fragColor * texel, 1.0);
}

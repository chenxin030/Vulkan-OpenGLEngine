#version 450

// Matches binding 1 of the descriptor set layout in vulkan_renderer.cpp.
layout(binding = 1) uniform sampler2D texSampler;

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

void main()
{
    // The texture is deliberately greyscale: modulating it by the per-face vertex
    // colour keeps the six faces distinguishable, so a wrong UV assignment or a
    // flipped V axis stays visible instead of being averaged away into mush.
    //
    // The procedural checkerboard that used to live here is gone - it was a
    // placeholder for exactly this sampler.
    vec3 texel = texture(texSampler, fragTexCoord).rgb;
    outColor = vec4(fragColor * texel, 1.0);
}

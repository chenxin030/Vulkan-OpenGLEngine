#version 450

uniform sampler2D uTexture;

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

void main()
{
    vec3 texel = texture(uTexture, fragTexCoord).rgb;
    outColor = vec4(fragColor * texel, 1.0);
}

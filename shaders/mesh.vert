#version 450
layout(binding = 0) uniform UniformBufferObject
{
    mat4 view;
    mat4 proj;
    vec4 cameraPosition;
    vec4 lightDirection;
    vec4 lightColor;
    mat4 lightViewProjection;
    mat4 inverseViewProjection;
    vec4 environment; // intensity, enabled, background, max specular LOD
    vec4 lightingTerms; // direct, diffuse IBL, specular IBL
    vec4 framebufferSize;

} ubo;

layout(push_constant) uniform PushConstants
{
    mat4 model;
    vec4 tint;
    vec4 material;
} push;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inNormal;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragWorldPosition;
layout(location = 3) out vec3 fragWorldNormal;
layout(location = 4) out vec4 fragLightSpacePosition;

void main()
{
    vec4 worldPosition = push.model * vec4(inPosition, 1.0);
    gl_Position = ubo.proj * ubo.view * worldPosition;
    fragColor = inColor * push.tint.rgb;
    fragTexCoord = inTexCoord;
    fragWorldPosition = worldPosition.xyz;
    fragWorldNormal = normalize(transpose(inverse(mat3(push.model))) * inNormal);
    fragLightSpacePosition = ubo.lightViewProjection * worldPosition;
}

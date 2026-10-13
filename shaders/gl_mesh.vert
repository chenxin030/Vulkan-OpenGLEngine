#version 450

layout(std140, binding = 0) uniform Camera
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

} camera;

uniform mat4 uModel;
uniform vec3 uTint;
uniform vec3 uMaterial;

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
    vec4 worldPosition = uModel * vec4(inPosition, 1.0);
    gl_Position = camera.proj * camera.view * worldPosition;
    // 相机使用 ZO 深度；GL 默认裁剪区间为 [-w,w]。重映射后两后端近裁面一致。
    gl_Position.z = 2.0 * gl_Position.z - gl_Position.w;
    fragColor = inColor * uTint;
    fragTexCoord = inTexCoord;
    fragWorldPosition = worldPosition.xyz;
    fragWorldNormal = normalize(transpose(inverse(mat3(uModel))) * inNormal);
    fragLightSpacePosition = camera.lightViewProjection * worldPosition;
}

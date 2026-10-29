#version 450

uniform sampler2D uHdrColor;
layout(location = 0) out vec4 outColor;

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
layout(binding = 2) uniform samplerCube environmentMap;


void main()
{
    vec4 scene = texture(uHdrColor, gl_FragCoord.xy / camera.framebufferSize.xy);
    vec3 background = vec3(0.05, 0.06, 0.09);
    if (camera.environment.z > 0.5) {
        vec2 ndc = gl_FragCoord.xy / camera.framebufferSize.xy * 2.0 - 1.0;
        vec4 a = camera.inverseViewProjection * vec4(ndc, 0, 1);
        vec4 b = camera.inverseViewProjection * vec4(ndc, 1, 1);
        vec3 direction = normalize(b.xyz / b.w - a.xyz / a.w);
        background = textureLod(environmentMap, direction, 0).rgb * camera.environment.x;
    }
    vec3 hdr = scene.rgb + background * (1.0 - scene.a);
    vec3 mapped = clamp((hdr * (2.51 * hdr + 0.03)) /
        (hdr * (2.43 * hdr + 0.59) + 0.14), 0.0, 1.0);
    outColor = vec4(mapped, 1);
}

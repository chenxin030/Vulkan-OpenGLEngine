#version 450

layout(input_attachment_index = 0, binding = 0) uniform subpassInput hdrColor;
layout(location = 0) out vec4 outColor;

layout(std140, set = 1, binding = 4) uniform Camera
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
layout(set = 1, binding = 0) uniform samplerCube environmentMap;

void main()
{
    vec4 sceneColor = subpassLoad(hdrColor);
    vec3 hdr = sceneColor.rgb;
    // HDR alpha=0 标记没有几何覆盖的背景；前景写 1。环境同样在 tone map 之前求值。
    if (sceneColor.a < 0.5 && camera.environment.z > 0.5) {
        vec2 ndc = gl_FragCoord.xy / camera.framebufferSize.xy * 2.0 - 1.0;
        vec4 nearPoint = camera.inverseViewProjection * vec4(ndc, 0.0, 1.0);
        vec4 farPoint = camera.inverseViewProjection * vec4(ndc, 1.0, 1.0);
        vec3 direction = normalize(farPoint.xyz / farPoint.w - nearPoint.xyz / nearPoint.w);
        hdr = textureLod(environmentMap, direction, 0.0).rgb * camera.environment.x;
    }
    vec3 mapped = clamp((hdr * (2.51 * hdr + 0.03)) /
        (hdr * (2.43 * hdr + 0.59) + 0.14), 0.0, 1.0);
    outColor = vec4(mapped, 1.0);
}

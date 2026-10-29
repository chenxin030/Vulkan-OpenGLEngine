#version 450

uniform sampler2D uTexture;
uniform sampler2D uShadowMap;
uniform vec4 uMaterial;
uniform vec3 uNormalInfo; // normal enabled, debug mode, base color texture enabled
uniform sampler2D uNormalMap;
uniform vec3 uEmissive;

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

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragWorldPosition;
layout(location = 3) in vec3 fragWorldNormal;
layout(location = 4) in vec4 fragLightSpacePosition;
layout(location = 5) in vec4 fragWorldTangent;

layout(location = 0) out vec4 outColor;

layout(binding = 3) uniform samplerCube irradianceMap;
layout(binding = 4) uniform samplerCube prefilteredMap;
layout(binding = 5) uniform sampler2D brdfLut;

void main()
{
    vec3 albedo = fragColor * (uNormalInfo.z > 0.5 ? texture(uTexture, fragTexCoord).rgb : vec3(1.0));
    const float PI = 3.14159265359;
    vec3 Nbase = normalize(fragWorldNormal);
    vec3 N = Nbase;
    if (uNormalInfo.x > 0.5 && uMaterial.w != 0.0) {
        // 插值破坏单位长度/正交关系，在片元端重建世界空间 TBN。
        vec3 tangent = fragWorldTangent.xyz - Nbase * dot(Nbase, fragWorldTangent.xyz);
        vec3 nTS = texture(uNormalMap, fragTexCoord).rgb * 2.0 - 1.0;
        nTS.xy *= uMaterial.w;
        // 先按最大分量缩放，避免合法但很大的 normalScale 使长度平方溢出。
        float magnitude = max(abs(nTS.x), max(abs(nTS.y), abs(nTS.z)));
        if (magnitude > 0.0) nTS /= magnitude;
        if (dot(tangent, tangent) > 1e-12 && dot(nTS, nTS) > 1e-12) {
            vec3 T = normalize(tangent);
            vec3 B = sign(fragWorldTangent.w) * cross(Nbase, T);
            N = normalize(mat3(T, B, Nbase) * nTS);
        }
    }
    // 诊断颜色仍经过现有 HDR/ACES 显示链，实际法线值由上面的 N 表示。
    if (uNormalInfo.y > 0.5) {
        outColor = vec4((uNormalInfo.y < 1.5 ? Nbase : N) * 0.5 + 0.5, 1.0);
        return;
    }
    vec3 V = normalize(camera.cameraPosition.xyz - fragWorldPosition);
    vec3 L = normalize(-camera.lightDirection.xyz);
    vec3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.001);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);
    float metallic = clamp(uMaterial.x, 0.0, 1.0);
    float roughness = clamp(uMaterial.y, 0.045, 1.0);
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    float D = a2 / max(PI * denom * denom, 0.001);
    float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    float Gv = NdotV / (NdotV * (1.0 - k) + k);
    float Gl = NdotL / (NdotL * (1.0 - k) + k);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 F = F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);
    vec3 specular = D * Gv * Gl * F / max(4.0 * NdotV * max(NdotL, 0.001), 0.001);
    vec3 diffuse = (1.0 - F) * (1.0 - metallic) * albedo / PI;
    vec3 shadowNdc = fragLightSpacePosition.xyz / fragLightSpacePosition.w;
    // GLM is configured for zero-to-one projection depth; OpenGL's viewport maps that
    // clip depth into its [0,1] depth attachment with the usual half-scale transform.
    vec3 shadowCoord = vec3(shadowNdc.xy * 0.5 + 0.5, shadowNdc.z * 0.5 + 0.5);
    // Compute derivatives before the bounds branch: neighbouring fragments may
    // straddle the shadow frustum. Predict receiver depth at each sampled texel.
    vec3 shadowDx = dFdx(shadowCoord);
    vec3 shadowDy = dFdy(shadowCoord);
    float jacobian = shadowDx.x * shadowDy.y - shadowDx.y * shadowDy.x;
    vec2 receiverSlope = vec2(0.0);
    if (abs(jacobian) > 1e-12) {
        receiverSlope = vec2(shadowDy.y * shadowDx.z - shadowDx.y * shadowDy.z,
            shadowDx.x * shadowDy.z - shadowDy.x * shadowDx.z) / jacobian;
    }
    float visibility = 1.0;
    if (all(greaterThanEqual(shadowCoord, vec3(0.0))) &&
        all(lessThanEqual(shadowCoord, vec3(1.0)))) {
        // 阴影 bias 描述网格表面，不跟随 normalTexture 的微观扰动。
        float bias = max(0.0012 * (1.0 - max(dot(Nbase, L), 0.0)), 0.00035);
        vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0));
        visibility = 0.0;
        for (int y = -1; y <= 1; ++y)
            for (int x = -1; x <= 1; ++x) {
                vec2 sampleUv = shadowCoord.xy + vec2(x, y) * texel;
                vec2 sampleCenter = (floor(sampleUv / texel) + 0.5) * texel;
                float receiverDepth = shadowCoord.z + dot(receiverSlope, sampleCenter - shadowCoord.xy);
                float storedDepth = texture(uShadowMap, sampleUv).r;
                visibility += receiverDepth - bias <= storedDepth ? 1.0 : 0.0;
            }
        visibility /= 9.0;
    }
    vec3 direct = (diffuse + specular) * camera.lightColor.rgb * NdotL * visibility;
    // split-sum: 环境预过滤沿 R 查询，LUT 补回 NoV 与粗糙度的 BRDF 积分。
    vec3 R = reflect(-V, N);
    vec2 AB = texture(brdfLut, vec2(NdotV, roughness)).rg;
    vec3 reflected = textureLod(prefilteredMap, R, roughness * camera.environment.w).rgb;
    vec3 specularIbl = reflected * (F0 * AB.x + AB.y);
    // 直接光的 F 使用 VoH；环境漫反射的能量分配使用 NoV 的近似，不能直接复用 F。
    vec3 Fenv = F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - NdotV, 5.0);
    vec3 diffuseIbl = (1.0 - Fenv) * (1.0 - metallic) * albedo / PI * texture(irradianceMap, N).rgb;
    vec3 ambient = (diffuseIbl * camera.lightingTerms.y + specularIbl * camera.lightingTerms.z)
        * camera.environment.x * camera.environment.y;
    vec3 hdrColor = direct * camera.lightingTerms.x + ambient + uEmissive;
    outColor = vec4(hdrColor, 1.0);
}

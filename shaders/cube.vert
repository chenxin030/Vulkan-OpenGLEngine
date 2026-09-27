#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 texCoords;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoords;

// ---------------------------------------------------------------------------
// TEMPORARY (D11 only) - a hardcoded rotation.
//
// With no matrix at all the cube projects to one flat square: the +X/-X/+Y/-Y
// faces are edge-on (screen area 0), the -Z half sits at z < 0 and is clipped
// away, and only the +Z face survives. This block exists so D11's acceptance
// ("the cube appears") is something you can actually see.
//
// D12 deletes this whole block and replaces it with ubo.proj * ubo.view *
// push.model. Do not build on it.
//
// No projection on purpose: adding one flips the screen-space winding, so
// rasterizer.frontFace would have to change from CLOCKWISE to
// COUNTER_CLOCKWISE in the same edit (see the comment in CreateGraphicsPipeline).
//
// The scale and the z offset keep the rotated cube inside Vulkan's [0,1] depth
// range. A rotated unit cube reaches |z| = 0.5*sqrt(3) ~ 0.87, which is outside
// it; scaled by 0.5 that is ~0.43, and shifting by +0.5 lands it at
// z in [0.08, 0.92] (measured: see the verification note in the commit message).
// ---------------------------------------------------------------------------
const float kAngleX = radians(25.0);
const float kAngleY = radians(35.0);
const float kCx = cos(kAngleX);
const float kSx = sin(kAngleX);
const float kCy = cos(kAngleY);
const float kSy = sin(kAngleY);

// Ry(kAngleY) * Rx(kAngleX). GLSL fills mat4 column by column, so the first four
// values are column 0, the next four column 1, and so on.
const mat4 kModel = mat4(
    kCy,        0.0,  -kSy,        0.0,
    kSy * kSx,  kCx,   kCy * kSx,  0.0,
    kSy * kCx, -kSx,   kCy * kCx,  0.0,
    0.0,        0.0,   0.0,        1.0);

const float kScale = 0.5;
const vec3 kOffset = vec3(0.0, 0.0, 0.5);

void main() {
    // GLSL has no mat4 * vec3 (GLM does), so the offset goes in as a vec4 with w = 0.
    gl_Position = kModel * vec4(inPosition * kScale, 1.0) + vec4(kOffset, 0.0);
    fragColor = inColor;
    fragTexCoords = texCoords;
}

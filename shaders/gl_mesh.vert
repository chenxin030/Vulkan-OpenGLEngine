#version 450

// OpenGL counterpart of shaders/mesh.vert.
//
// Why this is a separate file rather than a shared one
// ---------------------------------------------------
// The two shaders differ in a way that no amount of sharing hides:
//
//   * Vulkan passes the per-object model matrix as a push constant, because that is
//     what a push constant is FOR - a small blob of per-draw data that needs no
//     descriptor. OpenGL has no such concept, so here it is a plain uniform set with
//     glUniformMatrix4fv before each draw. Same idea, different mechanism.
//
//   * Vulkan's shader emits proj * view with a flipped Y built into proj, because
//     Vulkan's NDC has +Y pointing down. OpenGL's NDC has +Y up, so nothing is
//     flipped here. The flip lives in the Vulkan backend, not in a shared shader -
//     which is the entire payoff of that decision.
//
// A single GLSL file with #ifdefs could paper over both, and would be worse: every
// reader would have to hold two APIs in their head to understand one shader.

// std140 matches the layout of GlCameraUniforms in opengl_renderer.cpp. The binding
// is declared in the shader (GLSL 4.2+) rather than assigned with
// glUniformBlockBinding, so the two sides cannot disagree about the index.
layout(std140, binding = 0) uniform Camera
{
    mat4 view;
    mat4 proj;
} camera;

// Per-object, set before each draw.
uniform mat4 uModel;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;

void main()
{
    gl_Position = camera.proj * camera.view * uModel * vec4(inPosition, 1.0);
    fragColor = inColor;
    fragTexCoord = inTexCoord;
}

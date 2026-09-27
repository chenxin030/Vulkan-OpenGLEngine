#include "scene/CubeMesh.h"

#include <array>

namespace {

constexpr float kCubeHalfExtent = 0.5f;

// Per-face frame: u and v are in-plane axes chosen so that u x v == normal.
struct FaceAxes
{
    glm::vec3 normal;
    glm::vec3 u;
    glm::vec3 v;
    glm::vec3 color;
};

const std::array<FaceAxes, 6> kCubeFaces = { {
    //   normal                  u                        v                       colour
    { {  0.0f,  0.0f,  1.0f }, {  1.0f,  0.0f,  0.0f }, {  0.0f,  1.0f,  0.0f }, { 0.90f, 0.30f, 0.30f } },  // +Z
    { {  0.0f,  0.0f, -1.0f }, { -1.0f,  0.0f,  0.0f }, {  0.0f,  1.0f,  0.0f }, { 0.30f, 0.80f, 0.35f } },  // -Z
    { {  1.0f,  0.0f,  0.0f }, {  0.0f,  0.0f, -1.0f }, {  0.0f,  1.0f,  0.0f }, { 0.30f, 0.50f, 0.95f } },  // +X
    { { -1.0f,  0.0f,  0.0f }, {  0.0f,  0.0f,  1.0f }, {  0.0f,  1.0f,  0.0f }, { 0.95f, 0.80f, 0.25f } },  // -X
    { {  0.0f,  1.0f,  0.0f }, {  1.0f,  0.0f,  0.0f }, {  0.0f,  0.0f, -1.0f }, { 0.75f, 0.40f, 0.90f } },  // +Y
    { {  0.0f, -1.0f,  0.0f }, {  1.0f,  0.0f,  0.0f }, {  0.0f,  0.0f,  1.0f }, { 0.30f, 0.85f, 0.85f } },  // -Y
} };

// Corner offsets in the (u, v) frame, ordered so the winding comes out CCW.
const float kCornerSigns[4][2] = {
    { -1.0f, -1.0f },
    {  1.0f, -1.0f },
    {  1.0f,  1.0f },
    { -1.0f,  1.0f },
};

const glm::vec2 kCornerUvs[4] = {
    { 0.0f, 0.0f },
    { 1.0f, 0.0f },
    { 1.0f, 1.0f },
    { 0.0f, 1.0f },
};

}  // namespace

std::vector<Vertex> BuildCubeVertices()
{
    std::vector<Vertex> vertices;
    vertices.reserve(kCubeFaces.size() * 4);

    for (const FaceAxes& face : kCubeFaces) {
        const glm::vec3 center = face.normal * kCubeHalfExtent;

        for (int corner = 0; corner < 4; ++corner) {
            Vertex vertex{};
            vertex.position = center
                            + face.u * (kCornerSigns[corner][0] * kCubeHalfExtent)
                            + face.v * (kCornerSigns[corner][1] * kCubeHalfExtent);
            vertex.color = face.color;
            vertex.texCoord = kCornerUvs[corner];
            vertices.push_back(vertex);
        }
    }

    return vertices;
}

std::vector<uint16_t> BuildCubeIndices()
{
    std::vector<uint16_t> indices;
    indices.reserve(kCubeFaces.size() * 6);

    for (uint16_t face = 0; face < static_cast<uint16_t>(kCubeFaces.size()); ++face) {
        const uint16_t base = static_cast<uint16_t>(face * 4);
        indices.push_back(static_cast<uint16_t>(base + 0));
        indices.push_back(static_cast<uint16_t>(base + 1));
        indices.push_back(static_cast<uint16_t>(base + 2));
        indices.push_back(static_cast<uint16_t>(base + 0));
        indices.push_back(static_cast<uint16_t>(base + 2));
        indices.push_back(static_cast<uint16_t>(base + 3));
    }

    return indices;
}

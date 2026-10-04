#include "scene/MeshLibrary.h"

#include <glm/glm.hpp>

#include <array>
#include <stdexcept>

namespace {

    constexpr float kHalfExtent = 0.5f;

    // 在面自身的 (u, v) 坐标系中的角偏移，绕向是逆时针。
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

    // 立方体每个面的框架：u 和 v 的选择使得 u x v == 法线。
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

    MeshData BuildCube()
    {
        MeshData mesh;
        mesh.vertices.reserve(kCubeFaces.size() * 4);
        mesh.indices.reserve(kCubeFaces.size() * 6);

        for (uint32_t face = 0; face < static_cast<uint32_t>(kCubeFaces.size()); ++face) {
            const FaceAxes& axes = kCubeFaces[face];
            const glm::vec3 center = axes.normal * kHalfExtent;
            const uint16_t base = static_cast<uint16_t>(mesh.vertices.size());

            for (int corner = 0; corner < 4; ++corner) {
                Vertex vertex{};
                vertex.position = center
                    + axes.u * (kCornerSigns[corner][0] * kHalfExtent)
                    + axes.v * (kCornerSigns[corner][1] * kHalfExtent);
                vertex.color = axes.color;
                vertex.texCoord = kCornerUvs[corner];
                mesh.vertices.push_back(vertex);
            }

            mesh.indices.push_back(static_cast<uint16_t>(base + 0));
            mesh.indices.push_back(static_cast<uint16_t>(base + 1));
            mesh.indices.push_back(static_cast<uint16_t>(base + 2));
            mesh.indices.push_back(static_cast<uint16_t>(base + 0));
            mesh.indices.push_back(static_cast<uint16_t>(base + 2));
            mesh.indices.push_back(static_cast<uint16_t>(base + 3));
        }

        return mesh;
    }

    // 四角锥
    MeshData BuildPyramid()
    {
        // 底面在 y = -0.5，顶点在 y = 0.5，底面半边长为 0.5。
        const float h = 0.5f;
        const glm::vec3 apex(0.0f, h, 0.0f);    // 上面的点

        // 底面的 4 个点，从下往上看是逆时针
        const glm::vec3 corners[4] = {
            { -h, -h, -h },
            {  h, -h, -h },
            {  h, -h,  h },
            { -h, -h,  h },
        };

        // 对于每个侧面通过 u x v == 法线 推导出来。
        // A + B + 顶点 组成一个面，从侧面看是逆时针
        const int sideBaseA[4] = { 3, 2, 1, 0 };
        const int sideBaseB[4] = { 2, 1, 0, 3 };

        const glm::vec3 sideColors[4] = {
            { 0.90f, 0.35f, 0.30f },   // toward +Z
            { 0.35f, 0.75f, 0.90f },   // toward +X
            { 0.90f, 0.75f, 0.30f },   // toward -Z
            { 0.45f, 0.85f, 0.45f },   // toward -X
        };

        MeshData mesh;
        mesh.vertices.reserve(16);
        mesh.indices.reserve(18);

        // --- 四个面，每个都是一个三角形：底角A，底角B，顶点 -------
        for (int side = 0; side < 4; ++side) {
            const uint16_t base = static_cast<uint16_t>(mesh.vertices.size());

            const glm::vec3& a = corners[sideBaseA[side]];
            const glm::vec3& b = corners[sideBaseB[side]];

            Vertex va{};
            va.position = a;
            va.color = sideColors[side];
            va.texCoord = glm::vec2(0.0f, 0.0f);

            Vertex vb{};
            vb.position = b;
            vb.color = sideColors[side];
            vb.texCoord = glm::vec2(1.0f, 0.0f);

            Vertex vApex{};
            vApex.position = apex;
            vApex.color = sideColors[side];
            vApex.texCoord = glm::vec2(0.5f, 1.0f);

            mesh.vertices.push_back(va);
            mesh.vertices.push_back(vb);
            mesh.vertices.push_back(vApex);

            mesh.indices.push_back(static_cast<uint16_t>(base + 0));
            mesh.indices.push_back(static_cast<uint16_t>(base + 1));
            mesh.indices.push_back(static_cast<uint16_t>(base + 2));
        }

        // --- 底座，一个四边形面朝 -Y -----------------------------------------
        {
            const uint16_t base = static_cast<uint16_t>(mesh.vertices.size());
            const glm::vec3 baseColor(0.55f, 0.55f, 0.62f);

            for (int corner = 0; corner < 4; ++corner) {
                Vertex vertex{};
                vertex.position = corners[corner];
                vertex.color = baseColor;
                vertex.texCoord = kCornerUvs[corner];
                mesh.vertices.push_back(vertex);
            }

            mesh.indices.push_back(static_cast<uint16_t>(base + 0));
            mesh.indices.push_back(static_cast<uint16_t>(base + 1));
            mesh.indices.push_back(static_cast<uint16_t>(base + 2));
            mesh.indices.push_back(static_cast<uint16_t>(base + 0));
            mesh.indices.push_back(static_cast<uint16_t>(base + 2));
            mesh.indices.push_back(static_cast<uint16_t>(base + 3));
        }

        return mesh;
    }

    MeshData BuildPlane()
    {
        // A 2x2 quad in the XZ plane, facing +Y.
        //
        // u = +X && v = -Z,  X x (-Z) = +Y. 
        const float h = 1.0f;
        const glm::vec3 center(0.0f);

        const glm::vec3 u(1.0f, 0.0f, 0.0f);
        const glm::vec3 v(0.0f, 0.0f, -1.0f);

        const glm::vec3 color(0.35f, 0.42f, 0.35f);

        MeshData mesh;
        mesh.vertices.reserve(4);
        mesh.indices.reserve(6);

        for (int corner = 0; corner < 4; ++corner) {
            Vertex vertex{};
            vertex.position = center
                + u * (kCornerSigns[corner][0] * h)
                + v * (kCornerSigns[corner][1] * h);
            vertex.color = color;
            vertex.texCoord = kCornerUvs[corner];
            mesh.vertices.push_back(vertex);
        }

        mesh.indices = { 0, 1, 2, 0, 2, 3 };

        return mesh;
    }

}  // namespace

MeshLibrary::MeshLibrary()
{
    m_meshes.reserve(kBuiltinCount);
    m_meshes.push_back(BuildCube());
    m_meshes.push_back(BuildPyramid());
    m_meshes.push_back(BuildPlane());
}

const MeshData& MeshLibrary::Get(uint32_t index) const
{
    if (index >= m_meshes.size()) {
        throw std::runtime_error("MeshLibrary::Get called with an out-of-range index");
    }
    return m_meshes[index];
}

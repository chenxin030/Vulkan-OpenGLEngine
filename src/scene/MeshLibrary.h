#pragma once

#include "scene/Vertex.h"

#include <cstdint>
#include <vector>
#include <cstddef>

struct MeshData
{
    std::vector<Vertex> vertices;
    std::vector<uint16_t> indices;

    bool IsValid() const { return !vertices.empty() && !indices.empty(); }
};

class MeshLibrary
{
public:
    static constexpr uint32_t kCube = 0;
    static constexpr uint32_t kPyramid = 1;
    static constexpr uint32_t kPlane = 2;
    static constexpr uint32_t kCylinder = 3;
    static constexpr uint32_t kQuad = 4;
    static constexpr uint32_t kBuiltinCount = 5;

    MeshLibrary();

    const MeshData& Get(uint32_t index) const;
    uint32_t Append(MeshData mesh);

    uint32_t Count() const { return static_cast<uint32_t>(m_meshes.size()); }
    std::uint64_t Identity() const { return m_identity; }
    std::uint64_t Revision() const { return m_revision; }

private:
    std::vector<MeshData> m_meshes;
    std::uint64_t m_identity = 0;
    std::uint64_t m_revision = 0;
};

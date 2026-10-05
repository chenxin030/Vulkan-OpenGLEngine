#pragma once

#include "scene/MeshData.h"

#include <cstdint>
#include <vector>

class MeshLibrary
{
public:
    static constexpr uint32_t kCube = 0;
    static constexpr uint32_t kPyramid = 1;
    static constexpr uint32_t kPlane = 2;
    static constexpr uint32_t kCylinder = 3;
    static constexpr uint32_t kBuiltinCount = 4;

    MeshLibrary();

    const MeshData& Get(uint32_t index) const;

    uint32_t Count() const { return static_cast<uint32_t>(m_meshes.size()); }

private:
    std::vector<MeshData> m_meshes;
};

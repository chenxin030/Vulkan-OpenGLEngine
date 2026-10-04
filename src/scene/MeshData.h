#pragma once

#include "scene/Vertex.h"

#include <cstdint>
#include <vector>

struct MeshData
{
    std::vector<Vertex> vertices;
    std::vector<uint16_t> indices;

    bool IsValid() const { return !vertices.empty() && !indices.empty(); }
};

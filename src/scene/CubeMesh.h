#pragma once

#include "scene/Vertex.h"

#include <cstdint>
#include <vector>

// ---------------------------------------------------------------------------
// 立方体的几何数据 —— 它是**资产**，既不是"场景的事实"，也不是某个图形 API 的编码方式。
//
//   · 不归后端：抄到 OpenGL 里照样成立（后端只关心 VkVertexInputBindingDescription 那类东西）。
//   · 不归 Scene：Scene 描述"世界里有什么"，不负责把顶点算出来。
//
// D33 的 MeshLibrary 会把它收成库里的第一个条目；在那之前它自己一个文件。
// ---------------------------------------------------------------------------
std::vector<Vertex> BuildCubeVertices();

std::vector<uint16_t> BuildCubeIndices();

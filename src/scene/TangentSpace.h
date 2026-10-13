#pragma once
#include "MeshLibrary.h"

// glTF 缺少 NORMAL：生成逐面的平面法线并按需拆点，保留颜色和 UV。
// 应在切线生成、MeshLibrary::Append 和 GPU 上传之前调用。
void GenerateFlatNormals(MeshData& mesh);

// 校验并正交化导出的 TANGENT；要求单位法线、有限非零切线、w=±1。
// 同一三角形必须采用同一手性，否则该三角形的切线空间无定义。
void ValidateTangents(MeshData& mesh);

// 使用默认 MikkTSpace，从 POSITION/NORMAL/UV 生成逐角点切线。
// 内部重新建立索引；可能拆分顶点，超过 16 位容量或基底无效时抛异常。
// 算法内部无需作为教学主线；重点是输入契约和角点输出不能覆盖共享顶点。
void GenerateTangents(MeshData& mesh);

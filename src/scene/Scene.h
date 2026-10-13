#pragma once

#include <glm/glm.hpp>
#include <vector>
#include "Camera.h"
#include "scene/Registry.h"
#include "MeshLibrary.h"
#include "MaterialLibrary.h"
#include "ModelAsset.h"
#include "Environment.h"
#include <string>
#include <unordered_map>

struct Scene
{
    Registry registry;
    Camera camera;
    EnvironmentSettings environment;
    std::shared_ptr<const EnvironmentData> environmentData;
    // 分别观察 direct / diffuse IBL / specular IBL。
    bool directLighting = true, diffuseIbl = true, specularIbl = true;
    // 学习诊断不持久化；Play 副本拥有独立开关，Stop 恢复编辑态。
    bool normalMapping = true;
    int normalDebug = 0;

    MeshLibrary meshes;
    MaterialLibrary materials;
    std::vector<ModelAsset> modelAssets;
    // 运行时资源索引到资产/源子资源的映射，保存时据此生成稳定引用。
    // key（uint32_t）：meshes 中的运行时网格索引，即 MeshRenderer.meshIndex。
    // value（ModelMeshSource）：记录 {assetId, sourceMesh, primitive}。
    //   assetId → modelAssets 中 id 相同的模型资产；
    //   sourceMesh / primitive → 该资产源 glTF 的 meshes[sourceMesh].primitives[primitive]。
    // 例：modelMeshSources[7] = {"model-1", 0, 1}，表示运行时网格 7 来自 model-1 的 mesh 0、primitive 1。
    // sourceMesh/primitive 定位源子资源，与这张 map 的运行时网格索引 key 不同。
    // Save：运行时索引 → 来源记录 → JSON 稳定引用。
    // Load：资产 ID 找 ModelAsset，再遍历其 meshIndices，查此表匹配源定位键，得到新的运行时索引。
    // 内置网格通过 builtin 名称保存，不登记在此表中。
    std::unordered_map<uint32_t, ModelMeshSource> modelMeshSources;

    // key（uint32_t）：materials 的材质数组索引，即 MeshRenderer.materialIndex，不是图片索引。
    // value（ModelMaterialSource）：记录 {assetId, sourceMaterial}。
    //   assetId → modelAssets 中 id 相同的模型资产；
    //   sourceMaterial >= 0 → 源 glTF 的 materials[sourceMaterial]；-1 → 该模型的 glTF 默认材质。
    // 例：modelMaterialSources[3] = {"model-1", 0}，表示运行时材质 3 来自 model-1 的显式材质 0。
    // Save：运行时索引 → 来源记录 → JSON 稳定引用。
    // Load：资产 ID 找 ModelAsset，再遍历其 materialIndices，查此表匹配源材质编号，得到新的运行时索引。
    // 引擎默认材质（运行时索引 0）通过 builtin=default 保存，不登记在此表中。
    std::unordered_map<uint32_t, ModelMaterialSource> modelMaterialSources;

    void DestroyEntitySubtree(Entity root);
    std::string AllocateModelAssetId();
    uint32_t FindModelAssetIndex(const std::string& id) const;

private:
    uint64_t m_nextModelAssetId = 1;

};

Scene BuildDemoScene();
Scene BuildDemo2DScene();

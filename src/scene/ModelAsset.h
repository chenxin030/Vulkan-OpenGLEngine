#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

// glTF 的 Node 引用 Mesh，Mesh 再包含多个 primitive。Node 不直接包含 primitives。
// 一个 Node 可以不引用 Mesh，例如只作为分组节点；也可以引用一个 Mesh。
// 一个 Mesh 包含一个或多个 primitive，每个 primitive 描述一个可绘制部分。
// 多个 Node 可以引用同一个 Mesh，用各自的变换把同一份几何放到不同位置

// primitives ：可独立绘制的网格部分，一组顶点属性、可选索引、材质引用和绘制拓扑，可以包含很多三角形、
// 椅子 Mesh
// ├── primitive 0：木质部分，800 个三角形，使用木头材质
// └── primitive 1：金属部分，200 个三角形，使用金属材质

// 一个源 glTF 节点的模板。多份实例共用模板，但每次都会创建新的节点实体。
//
// Node 表示模型中的一个对象或分组，负责位置、父子关系和变换继承；
// Mesh 则描述几何形状（顶点、索引等）。Node 可以引用 Mesh，也可以只是没有几何的分组。
// 例如“汽车”根节点下面有车身和四个车轮：移动汽车会带动所有子节点，
// 转动某个车轮则只影响该车轮及其子节点。多个 Node 也可以引用同一份 Mesh。
// 用数组存储树(ModelAsset.nodes 的 vector 和 parentIndex 表达关系)
//
// 与 glTF 的对应关系：
// 当前模板承接 glTF node，保留源模型的层级与局部变换；节点层级本身并非 glTF 独有。
// node 编号 -> sourceIndex，name -> name，matrix 或 TRS -> localTransform；
// glTF 用父节点的 children 数组表达关系，导入时整理成子节点的 parentIndex。
// node 引用的 mesh 中各个 primitive -> primitives，记录共享网格和材质的引用。
// glTF node -> ModelNodeTemplate（可复用模板）-> Entity + Transform + Parent（实例对象）；
// 每个 primitive 再创建带 MeshRenderer 的子实体，引用已加载的网格和材质。
// 模板不保存某份实例的 Entity 句柄；实例化会复制局部变换并创建新的实体层级。
// 引擎运行时用 ECS 表达这些关系，渲染器不需要直接认识 glTF node。

// 一个 Node 上的可绘制部分模板。实例化时，为每个 primitive 创建独立子实体和 MeshRenderer。
struct ModelPrimitiveTemplate
{
    // primitive 内 mesh 的编号，需要经过 ModelAsset 的映射，才能得到 Scene 网格库里的实际编号
    // 对应 ModelAsset 的 meshIndices 的下标
    uint32_t mesh = 0;

    // primitive 内 material 的编号，需要经过 ModelAsset 的映射，才能得到 Scene 材质库里的实际编号
    // 未指定材质的 primitive 会指向准备好的 glTF 默认材质，不能直接视为显式材质 0。
    uint32_t material = 0;

    // 该 primitive 在源 glTF 的某个 Mesh 的 primitives 数组中的序号，从 0 开始。每个 Mesh 都重新计数。
    // mesh 在整个资产内编号，ordinal 只在所属源 Mesh 内编号
    uint32_t ordinal = 0;
};

struct ModelNodeTemplate
{
    // glTF nodes 数组的编号，实例化时用它建立“节点编号 → 本次新建 Entity”的临时映射。
    uint32_t sourceIndex = 0;

    // 父节点在 glTF nodes 数组中的编号；-1 表示根节点。
    // 实例化一个模型时，引擎会额外创建一个根实体，把源模型的所有根节点都放到它下面。（glTF 场景里没有父节点的节点。一个模型可能有多个根节点）
    // 源 glTF：
    //     桌子（根节点）
    //     椅子（根节点）
    int32_t parentIndex = -1;

    // 节点名称；源文件未命名时由导入器生成“Node N”，实例创建时复制到 Name 组件。
    std::string name;

    // 节点相对父节点的矩阵，由 glTF matrix 或 TRS 转换而来，实例创建时复制到 Transform.local。
    glm::mat4 localTransform{ 1.0f };

    // 这个节点引用的全部 primitive 模板；没有 mesh 的分组节点可以是空数组。
    std::vector<ModelPrimitiveTemplate> primitives;
};

// Scene 内一份可复用模型资产，删除实例不会删除这份记录。
struct ModelAsset
{
    // 场景文件保存并恢复的持久 ID（例如 model-1）；mesh/material 引用用它定位资产。
    std::string id;

    // asset 的指纹
        // 导入 chair.gltf，当时是红色椅子，计算指纹 A。内存里的网格和材质也是这一版。
        // 在外部软件中把椅子改成蓝色，磁盘文件对应的指纹变成 B，但引擎内存里仍是红色椅子。
        // 点击 Save，场景文件继续保存指纹 A。
        // 下次 Load 重新读取模型，算出 B；发现与保存的 A 不同，就拒绝加载，提示模型内容已变化。
        // 为什么 Save 不直接更新成 B？ 因为保存场景并没有重新导入模型。
        // 如果更新成 B，就等于声称“这个场景对应蓝色椅子”，但保存时实际编辑、看到的仍是红色椅子。
        // 模型内部的网格、材质编号也可能改变，造成引用错位。
    std::string fingerprint; // 记录的是“当初加载进内存的那一版模型”，Save 不会因为磁盘文件后来改变，就把它当作当前场景已经使用的新版本。

    // 运行时定位源文件和查询路径缓存；Save 会按场景位置转换成相对路径或保留绝对路径。
    std::string sourcePath;

    // 数组下标是模板 mesh 局部编号；元素值是当前 Scene.meshes 中的运行时网格索引。
    std::vector<uint32_t> meshIndices;

    // 下标是 ModelPrimitiveTemplate 的 mesh；元素值是当前 Scene.materials 中的运行时材质索引。
    // 可能包含额外的 glTF 默认材质，所以局部编号不总等于源 glTF materials 编号。
    std::vector<uint32_t> materialIndices;

    // 下标是 ModelPrimitiveTemplate 的 material；元素值是 Scene.materials 图片数组中的索引。
    // 图片按导入器实际加载/复用情况排列，不直接等同于源 glTF textures 数组顺序。
    std::vector<uint32_t> textureIndices;

    // 保存所选场景的节点模板；先放父节点再放子节点，便于实例化时设置 Parent。
    std::vector<ModelNodeTemplate> nodes;
};

// Scene
// └── modelAssets：多份 ModelAsset
//      └── nodes：多份 ModelNodeTemplate
//          └── primitives：多份 ModelPrimitiveTemplate

// ---------------------------------------------------------------------------------------

// 内存中用编号快速找到网格，保存到文件时改用来源信息，加载后再转换成新的内存编号。
// 一份运行时网格的来源记录；Scene.modelMeshSources 用运行时网格索引作为 map 的键保存它。
// Save 把 meshIndex 转成 assetId + sourceMesh + primitive；Load 反向解析出当前资源索引。
//
// 例如 Scene 网格库中的第 7 份网格来自 model-1 的 meshes[2].primitives[0]：
// scene.modelMeshSources[7] = ModelMeshSource{ "model-1", 2, 0 };
// map 的键 7 是当前运行时网格索引，值则说明该网格属于哪个资产、源文件里的哪个部分。
// 因此拿着 MeshRenderer.meshIndex = 7，就能查出这份网格的来源。
//
// Save 不把运行时索引 7 写进场景文件，而是保存来源引用：
// "mesh": { "asset": "model-1", "sourceMesh": 2, "primitive": 0 }
// 下次 Load 时，资源加载顺序可能改变，这份网格在 Scene 中可能变成第 12 份。
// 加载器根据保存的来源信息找到它，再设置 MeshRenderer.meshIndex = 12。
// 运行时编号可以改变，但引用的仍然是同一份资产中的同一块网格。
//
// 这种引用以源内容版本不变为前提：外部重新导出后，meshes[2].primitives[0]
// 可能已指向另一块网格。Load 会核对 ModelAsset.fingerprint，内容不一致就拒绝加载。
// 指纹检查的是源内容是否变化，不是只检查编号；即使编号不变，其他内容修改也会拒绝。
// “锁定”表示只接受匹配导入基线的内容版本，并不阻止磁盘文件被修改。
struct ModelMeshSource
{
    // 网格所属 ModelAsset.id；不保存资产数组下标，避免资产表重排后引用错位。
    std::string assetId;

    // 该资产源 glTF 的 meshes 数组中的网格编号。
    uint32_t sourceMesh = 0;

    // 该源 mesh 的 primitives 数组中的编号，指定其中一个实际绘制部分。
    uint32_t primitive = 0;
};

// 一份运行时材质的来源记录；Scene.modelMaterialSources 用运行时材质索引作为 map 的键保存它。
struct ModelMaterialSource
{
    // 材质所属 ModelAsset.id；材质来源可以与某个实体的网格来源分开保存。
    std::string assetId;

    // -1 明确表示 glTF 默认材质，不等于引擎默认材质或显式材质 0。
    // 非负值表示 glTF materials 数组编号；不是 Scene 的材质索引或模板 material 局部编号。
    // Save 将 -1 写为 {asset, default:true}，非负值写为 {asset, sourceMaterial:N}。
    int32_t sourceMaterial = -1;
};

// Scene.modelMeshSources[运行时网格编号]
//      → ModelMeshSource{ assetId, sourceMesh, primitive }
//
// Scene.modelMaterialSources[运行时材质编号]
//      → ModelMaterialSource{ assetId, sourceMaterial }

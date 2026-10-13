#pragma once

#include "scene/Entity.h"

#include <filesystem>
#include <vector>
#include <string>

struct Scene;

class ModelImporter
{
public:
    // 返回值是 Scene 内运行时数组下标
    static uint32_t LoadAsset(Scene& scene, const std::filesystem::path& path,
        const std::string& savedId = {}, const std::string& expectedFingerprint = {});

    // 返回唯一的实例根节点（保持 vector 返回类型）；所有根节点挂在它下面。
    static std::vector<Entity> InstantiateAsset(Scene& scene, uint32_t assetIndex);
    static std::vector<Entity> Import(Scene& scene, const std::filesystem::path& path);
};

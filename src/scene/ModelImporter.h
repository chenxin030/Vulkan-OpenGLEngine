#pragma once

#include "scene/Entity.h"

#include <filesystem>
#include <vector>
#include <string>

struct Scene;

class ModelImporter
{
public:
    // 项目导入需要复制主文件及所有声明的外部 buffer/image，保持相对路径。
    static std::vector<std::filesystem::path> SourceFiles(const std::filesystem::path& path);
    // 返回值是 Scene 内运行时数组下标
    static uint32_t LoadAsset(Scene& scene, const std::filesystem::path& path,
        const std::string& savedId = {}, const std::string& expectedFingerprint = {});

    // 返回唯一的实例根节点（保持 vector 返回类型）；所有根节点挂在它下面。
    static std::vector<Entity> InstantiateAsset(Scene& scene, uint32_t assetIndex,
        std::vector<Entity>* completeInstance = nullptr);
    static std::vector<Entity> Import(Scene& scene, const std::filesystem::path& path);
};

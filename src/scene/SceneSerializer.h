#pragma once

#include <filesystem>

struct Scene;

class SceneSerializer
{
public:
    static void Save(const Scene& scene, const std::filesystem::path& path);
    static Scene Load(const std::filesystem::path& path);
};
